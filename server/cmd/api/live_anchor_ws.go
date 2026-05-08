package main

import (
	"context"
	"database/sql"
	"encoding/json"
	"errors"
	"log"
	"strconv"
	"strings"
	"time"
)

const liveAnchorRole = "anchor"
const liveAudienceRole = "audience"

type liveAnchorJoinMessage struct {
	Type    string `json:"type"`
	Token   string `json:"token"`
	RoomKey string `json:"roomKey"`
	Role    string `json:"role"`
}

type liveAnchorHeartbeatMessage struct {
	Type    string `json:"type"`
	RoomKey string `json:"roomKey"`
}

type liveAnchorLeaveMessage struct {
	Type    string `json:"type"`
	RoomKey string `json:"roomKey"`
}

type liveAnchorJoinedMessage struct {
	Type    string `json:"type"`
	RoomKey string `json:"roomKey"`
	UserID  int64  `json:"userId"`
	Role    string `json:"role"`
	TsMs    int64  `json:"tsMs"`
}

type liveAnchorLeftMessage struct {
	Type    string `json:"type"`
	RoomKey string `json:"roomKey"`
	TsMs    int64  `json:"tsMs"`
}

type liveAnchorErrorMessage struct {
	Type    string `json:"type"`
	RoomKey string `json:"roomKey,omitempty"`
	Message string `json:"message"`
	TsMs    int64  `json:"tsMs"`
}

type businessRoomMember struct {
	UserID   int64  `json:"userId"`
	Username string `json:"username"`
	Role     string `json:"role"`
	Online   bool   `json:"online"`
}

type businessRoomMemberListMessage struct {
	Type    string               `json:"type"`
	RoomKey string               `json:"roomKey"`
	Members []businessRoomMember `json:"members"`
	TsMs    int64                `json:"tsMs"`
}

type businessRoomMemberEventMessage struct {
	Type    string             `json:"type"`
	RoomKey string             `json:"roomKey"`
	Member  businessRoomMember `json:"member"`
	TsMs    int64              `json:"tsMs"`
}

func (c *probeSignalClient) handleLiveAnchorJoinMessage(data []byte) error {
	var message liveAnchorJoinMessage
	if err := json.Unmarshal(data, &message); err != nil {
		c.sendLiveAnchorError("", "invalid live.anchor.join payload")
		return nil
	}

	roomKey := normalizeLiveRoomKey(message.RoomKey)
	if roomKey == "" {
		c.sendLiveAnchorError("", "live.anchor.join requires roomKey")
		return nil
	}

	if c.server == nil || c.server.database == nil {
		c.sendLiveAnchorError(roomKey, "server is not ready for live anchor signaling")
		return nil
	}

	requestedRole := strings.TrimSpace(message.Role)
	if requestedRole == "" {
		requestedRole = liveAudienceRole
	}

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	user, err := c.server.authenticateAccessToken(ctx, strings.TrimSpace(message.Token))
	if err != nil {
		c.sendLiveAnchorError(roomKey, err.Error())
		return nil
	}

	room, err := findLiveRoomByKeyQuerier(ctx, c.server.database, roomKey)
	if err != nil {
		if errors.Is(err, sql.ErrNoRows) {
			c.sendLiveAnchorError(roomKey, "live room not found")
			return nil
		}
		log.Printf("anchor join load room failed: roomKey=%s err=%v", roomKey, err)
		c.sendLiveAnchorError(roomKey, "failed to load live room")
		return nil
	}
	if room.Status != liveRoomStatusLive {
		c.sendLiveAnchorError(roomKey, "live room is not active")
		return nil
	}

	presenceRole := liveRoomRoleParticipant
	joinedRole := requestedRole
	if room.OwnerUserID == user.ID {
		presenceRole = liveRoomRoleController
		joinedRole = liveAnchorRole
	} else if joinedRole == liveAnchorRole {
		joinedRole = liveAudienceRole
	}

	if err := upsertLiveRoomPresenceQuerier(ctx, c.server.database, room.ID, user.ID, presenceRole); err != nil {
		log.Printf("anchor join upsert presence failed: roomKey=%s userId=%d err=%v", roomKey, user.ID, err)
		c.sendLiveAnchorError(roomKey, "failed to update anchor presence")
		return nil
	}

	previousRoomKey := c.anchorRoomKey
	previousUserID := c.anchorUserID
	previousUsername := c.anchorUsername
	previousRole := c.anchorRole
	replaced := c.hub.bindAnchorClient(c, roomKey, user.ID, user.Username, presenceRole)
	if previousRoomKey != "" && (previousRoomKey != roomKey || previousUserID != user.ID) {
		if err := markAnchorOfflineByRoomKey(ctx, c.server.database, previousRoomKey, previousUserID); err != nil {
			log.Printf("anchor rebind cleanup failed: roomKey=%s userId=%d err=%v", previousRoomKey, previousUserID, err)
		}
		c.hub.buildAndSendBusinessDisconnectSessionMessages(previousRoomKey, previousUserID)
		c.hub.broadcastBusinessRoomMemberEvent(previousRoomKey, "room.member-leave", businessRoomMember{
			UserID:   previousUserID,
			Username: previousUsername,
			Role:     previousRole,
			Online:   false,
		})
		c.hub.broadcastAnchorMemberList(previousRoomKey)
	}
	if replaced != nil && replaced != c {
		replaced.sendLiveAnchorError(roomKey, "anchor session replaced by a newer websocket")
		_ = replaced.conn.Close()
	}

	c.sendJSON(liveAnchorJoinedMessage{
		Type:    "live.anchor.joined",
		RoomKey: roomKey,
		UserID:  user.ID,
		Role:    joinedRole,
		TsMs:    nowUnixMilli(),
	})
	c.hub.broadcastBusinessRoomMemberEvent(roomKey, "room.member-join", businessRoomMember{
		UserID:   user.ID,
		Username: user.Username,
		Role:     presenceRole,
		Online:   true,
	})
	c.hub.broadcastAnchorMemberList(roomKey)
	log.Printf("[presence] business websocket joined roomKey=%s userId=%d role=%s", roomKey, user.ID, presenceRole)
	return nil
}

func (c *probeSignalClient) handleLiveAnchorHeartbeatMessage(data []byte) error {
	var message liveAnchorHeartbeatMessage
	if err := json.Unmarshal(data, &message); err != nil {
		c.sendLiveAnchorError("", "invalid live.anchor.heartbeat payload")
		return nil
	}

	roomKey := normalizeLiveRoomKey(message.RoomKey)
	if roomKey == "" {
		c.sendLiveAnchorError("", "live.anchor.heartbeat requires roomKey")
		return nil
	}
	if !c.isBoundAnchor(roomKey) {
		c.sendLiveAnchorError(roomKey, "anchor websocket is not joined to the requested room")
		return nil
	}

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	becameOnline, err := touchAnchorPresenceByRoomKey(ctx, c.hub.database, roomKey, c.anchorUserID)
	if err != nil {
		log.Printf("anchor heartbeat failed: roomKey=%s userId=%d err=%v", roomKey, c.anchorUserID, err)
		c.sendLiveAnchorError(roomKey, "failed to refresh anchor presence")
		return nil
	}

	log.Printf("[presence] anchor heartbeat roomKey=%s userId=%d", roomKey, c.anchorUserID)
	if becameOnline {
		c.hub.broadcastAnchorMemberList(roomKey)
	}
	return nil
}

func (c *probeSignalClient) handleLiveAnchorLeaveMessage(data []byte) error {
	var message liveAnchorLeaveMessage
	if err := json.Unmarshal(data, &message); err != nil {
		c.sendLiveAnchorError("", "invalid live.anchor.leave payload")
		return nil
	}

	roomKey := normalizeLiveRoomKey(message.RoomKey)
	if roomKey == "" {
		c.sendLiveAnchorError("", "live.anchor.leave requires roomKey")
		return nil
	}
	if !c.isBoundAnchor(roomKey) {
		c.sendLiveAnchorError(roomKey, "anchor websocket is not joined to the requested room")
		return nil
	}

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	if err := markAnchorOfflineByRoomKey(ctx, c.hub.database, roomKey, c.anchorUserID); err != nil {
		log.Printf("anchor leave failed: roomKey=%s userId=%d err=%v", roomKey, c.anchorUserID, err)
		c.sendLiveAnchorError(roomKey, "failed to mark anchor offline")
		return nil
	}

	userID := c.anchorUserID
	username := c.anchorUsername
	role := c.anchorRole
	c.hub.unbindAnchorClient(c)
	c.hub.buildAndSendBusinessDisconnectSessionMessages(roomKey, userID)
	c.sendJSON(liveAnchorLeftMessage{
		Type:    "live.anchor.left",
		RoomKey: roomKey,
		TsMs:    nowUnixMilli(),
	})
	c.hub.broadcastBusinessRoomMemberEvent(roomKey, "room.member-leave", businessRoomMember{
		UserID:   userID,
		Username: username,
		Role:     role,
		Online:   false,
	})
	c.hub.broadcastAnchorMemberList(roomKey)
	log.Printf("[presence] anchor left roomKey=%s userId=%d", roomKey, userID)
	return nil
}

func (h *probeSignalHub) bindAnchorClient(client *probeSignalClient, roomKey string, userID int64, username, role string) *probeSignalClient {
	h.mu.Lock()
	defer h.mu.Unlock()

	if client.anchorRoomKey != "" {
		if existingRoom := h.businessRooms[client.anchorRoomKey]; existingRoom != nil && existingRoom.peers[strconv.FormatInt(client.anchorUserID, 10)] == client {
			delete(existingRoom.peers, strconv.FormatInt(client.anchorUserID, 10))
			if len(existingRoom.peers) == 0 {
				delete(h.businessRooms, client.anchorRoomKey)
			}
		}
	}

	room := h.businessRooms[roomKey]
	if room == nil {
		room = newProbeSignalRoom()
		h.businessRooms[roomKey] = room
	}

	peerID := strconv.FormatInt(userID, 10)
	replaced := room.peers[peerID]
	room.peers[peerID] = client
	client.anchorRoomKey = roomKey
	client.anchorUserID = userID
	client.anchorUsername = username
	client.anchorRole = role
	return replaced
}

func (h *probeSignalHub) unbindAnchorClient(client *probeSignalClient) (string, int64, bool) {
	h.mu.Lock()
	defer h.mu.Unlock()

	if client.anchorRoomKey == "" || client.anchorUserID <= 0 {
		return "", 0, false
	}

	roomKey := client.anchorRoomKey
	userID := client.anchorUserID
	room := h.businessRooms[roomKey]
	peerID := strconv.FormatInt(userID, 10)
	if room != nil && room.peers[peerID] == client {
		delete(room.peers, peerID)
		if len(room.peers) == 0 {
			delete(h.businessRooms, roomKey)
		}
		client.anchorRoomKey = ""
		client.anchorUserID = 0
		client.anchorUsername = ""
		client.anchorRole = ""
		return roomKey, userID, true
	}

	client.anchorRoomKey = ""
	client.anchorUserID = 0
	client.anchorUsername = ""
	client.anchorRole = ""
	return roomKey, userID, false
}

func (h *probeSignalHub) broadcastAnchorMemberList(roomKey string) {
	roomKey = normalizeLiveRoomKey(roomKey)
	if roomKey == "" || h.database == nil {
		return
	}

	recipients := h.snapshotAnchorRoom(roomKey)
	if len(recipients) == 0 {
		return
	}

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	members, err := loadBusinessRoomMembers(ctx, h.database, roomKey)
	if err != nil {
		log.Printf("broadcast anchor member-list failed: roomKey=%s err=%v", roomKey, err)
		return
	}

	encoded, err := json.Marshal(businessRoomMemberListMessage{
		Type:    "room.member-list",
		RoomKey: roomKey,
		Members: members,
		TsMs:    nowUnixMilli(),
	})
	if err != nil {
		log.Printf("marshal business room.member-list failed: roomKey=%s err=%v", roomKey, err)
		return
	}

	for _, client := range recipients {
		client.sendRaw(encoded)
	}
}

func (h *probeSignalHub) snapshotAnchorRoom(roomKey string) []*probeSignalClient {
	h.mu.Lock()
	defer h.mu.Unlock()

	room := h.businessRooms[roomKey]
	if room == nil {
		return nil
	}

	recipients := make([]*probeSignalClient, 0, len(room.peers))
	for _, client := range room.peers {
		recipients = append(recipients, client)
	}
	return recipients
}

func (h *probeSignalHub) broadcastBusinessRoomMemberEvent(roomKey, eventType string, member businessRoomMember) {
	roomKey = normalizeLiveRoomKey(roomKey)
	if roomKey == "" {
		return
	}

	recipients := h.snapshotAnchorRoom(roomKey)
	if len(recipients) == 0 {
		return
	}

	encoded, err := json.Marshal(businessRoomMemberEventMessage{
		Type:    eventType,
		RoomKey: roomKey,
		Member:  member,
		TsMs:    nowUnixMilli(),
	})
	if err != nil {
		log.Printf("marshal business %s failed: roomKey=%s err=%v", eventType, roomKey, err)
		return
	}

	for _, client := range recipients {
		client.sendRaw(encoded)
	}
}

func (h *probeSignalHub) buildAndSendBusinessDisconnectSessionMessages(roomKey string, userID int64) {
	for _, message := range h.buildBusinessDisconnectSessionMessages(roomKey, strconv.FormatInt(userID, 10)) {
		message.client.sendRaw(message.message)
	}
}

func (c *probeSignalClient) isBoundAnchor(roomKey string) bool {
	return c.anchorUserID > 0 && normalizeLiveRoomKey(c.anchorRoomKey) == normalizeLiveRoomKey(roomKey)
}

func (c *probeSignalClient) sendLiveAnchorError(roomKey, message string) {
	c.sendJSON(liveAnchorErrorMessage{
		Type:    "live.anchor.error",
		RoomKey: normalizeLiveRoomKey(roomKey),
		Message: strings.TrimSpace(message),
		TsMs:    nowUnixMilli(),
	})
}

func markAnchorOfflineByRoomKey(ctx context.Context, q dbQuerier, roomKey string, userID int64) error {
	_, err := q.ExecContext(
		ctx,
		`UPDATE live_room_presences presence
		 INNER JOIN live_rooms room ON room.id = presence.room_id
		 SET presence.is_online = 0,
		     presence.last_seen_at = NOW(),
		     presence.left_at = NOW()
		 WHERE room.room_key = ?
		   AND presence.user_id = ?`,
		roomKey,
		userID,
	)
	return err
}

func touchAnchorPresenceByRoomKey(ctx context.Context, q dbQuerier, roomKey string, userID int64) (bool, error) {
	var previousOnline int
	err := q.QueryRowContext(
		ctx,
		`SELECT presence.is_online
		 FROM live_room_presences presence
		 INNER JOIN live_rooms room ON room.id = presence.room_id
		 WHERE room.room_key = ?
		   AND presence.user_id = ?
		 LIMIT 1`,
		roomKey,
		userID,
	).Scan(&previousOnline)
	if err != nil {
		return false, err
	}

	if _, err := q.ExecContext(
		ctx,
		`UPDATE live_room_presences presence
		 INNER JOIN live_rooms room ON room.id = presence.room_id
		 SET presence.is_online = 1,
		     presence.last_seen_at = NOW(),
		     presence.left_at = NULL
		 WHERE room.room_key = ?
		   AND presence.user_id = ?`,
		roomKey,
		userID,
	); err != nil {
		return false, err
	}

	return previousOnline == 0, nil
}

func loadBusinessRoomMembers(ctx context.Context, q dbQuerier, roomKey string) ([]businessRoomMember, error) {
	room, err := findLiveRoomByKeyQuerier(ctx, q, roomKey)
	if err != nil {
		return nil, err
	}

	rows, err := q.QueryContext(
		ctx,
		`SELECT u.id, u.username, presence.role, presence.is_online
		 FROM live_room_presences presence
		 INNER JOIN users u ON u.id = presence.user_id
		 WHERE presence.room_id = ?
		   AND presence.is_online = 1
		   AND presence.last_seen_at >= DATE_SUB(NOW(), INTERVAL 2 MINUTE)
		 ORDER BY presence.last_seen_at DESC`,
		room.ID,
	)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	members := make([]businessRoomMember, 0)
	for rows.Next() {
		var member businessRoomMember
		var role string
		var isOnline int
		if err := rows.Scan(&member.UserID, &member.Username, &role, &isOnline); err != nil {
			return nil, err
		}
		member.Online = isOnline > 0
		member.Role = role
		if member.UserID == room.OwnerUserID {
			member.Role = liveAnchorRole
		}
		members = append(members, member)
	}
	if err := rows.Err(); err != nil {
		return nil, err
	}
	return members, nil
}
