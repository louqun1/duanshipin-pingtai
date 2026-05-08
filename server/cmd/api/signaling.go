package main

import (
	"context"
	"database/sql"
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"sort"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/gorilla/websocket"
)

const (
	probeSignalWriteWait      = 10 * time.Second
	probeSignalPongWait       = 60 * time.Second
	probeSignalPingPeriod     = probeSignalPongWait * 9 / 10
	probeSignalMaxMessageSize = 1 << 20

	linkMicTypeApply      = "linkmic.apply"
	linkMicTypeInvite     = "linkmic.invite"
	linkMicTypeAccept     = "linkmic.accept"
	linkMicTypeReject     = "linkmic.reject"
	linkMicTypeCancel     = "linkmic.cancel"
	linkMicTypeHangup     = "linkmic.hangup"
	linkMicTypeKick       = "linkmic.kick"
	linkMicTypeStateSync  = "linkmic.state-sync"
	linkMicTypeConnected  = "linkmic.connected"
	linkMicStateApplying  = "guest-applying"
	linkMicStateInviting  = "host-inviting"
	linkMicStateActive    = "linkmic-active"
	linkMicStateLiveOnly  = "live-only"
	linkMicStateIdle      = "idle"
	linkMicStateRtcActive = "rtc-active"
)

var probeSignalUpgrader = websocket.Upgrader{
	ReadBufferSize:  4096,
	WriteBufferSize: 4096,
	CheckOrigin: func(_ *http.Request) bool {
		return true
	},
}

type probeSignalEnvelope struct {
	Type       string          `json:"type"`
	RoomID     string          `json:"room_id,omitempty"`
	RoomKey    string          `json:"roomKey,omitempty"`
	RoomKeyAlt string          `json:"room_key,omitempty"`
	PeerID     string          `json:"peer_id,omitempty"`
	Role       string          `json:"role,omitempty"`
	Mode       string          `json:"mode,omitempty"`
	FromUserID string          `json:"from_user_id,omitempty"`
	ToUserID   string          `json:"to_user_id,omitempty"`
	RequestID  string          `json:"request_id,omitempty"`
	Payload    json.RawMessage `json:"payload,omitempty"`
	TsMs       int64           `json:"ts_ms,omitempty"`
}

type probeRoomPeer struct {
	PeerID string `json:"peer_id"`
	Role   string `json:"role,omitempty"`
}

type probeRegisteredMessage struct {
	Type   string `json:"type"`
	RoomID string `json:"room_id"`
	PeerID string `json:"peer_id"`
	Role   string `json:"role"`
	TsMs   int64  `json:"ts_ms"`
}

type roomMemberListMessage struct {
	Type   string          `json:"type"`
	RoomID string          `json:"room_id"`
	Peers  []probeRoomPeer `json:"peers"`
	TsMs   int64           `json:"ts_ms"`
}

type signalErrorMessage struct {
	Type       string `json:"type"`
	RoomID     string `json:"room_id,omitempty"`
	RoomKey    string `json:"roomKey,omitempty"`
	RoomKeyAlt string `json:"room_key,omitempty"`
	RequestID  string `json:"request_id,omitempty"`
	Reason     string `json:"reason,omitempty"`
	Message    string `json:"message"`
	TsMs       int64  `json:"ts_ms"`
}

type probeSignalHub struct {
	mu            sync.Mutex
	database      *sql.DB
	rooms         map[string]*probeSignalRoom
	businessRooms map[string]*probeSignalRoom
}

type probeSignalRoom struct {
	peers    map[string]*probeSignalClient
	session  *probeLinkMicSession
	requests map[string]*probeLinkMicSession
}

type probeLinkMicSession struct {
	RequestID     string
	RequestType   string
	RequestState  string
	ControllerID  string
	ParticipantID string
	State         string
	CreatedAtMs   int64
	UpdatedAtMs   int64
}

type probeSignalClient struct {
	hub    *probeSignalHub
	server *apiServer
	conn   *websocket.Conn
	send   chan []byte
	roomID string
	peerID string
	role   string
	mode   string
	requestID string
	anchorRoomKey string
	anchorUserID  int64
	anchorUsername string
	anchorRole    string
}

type outboundMessage struct {
	client  *probeSignalClient
	message []byte
}

func newProbeSignalHub(database *sql.DB) *probeSignalHub {
	return &probeSignalHub{
		database:      database,
		rooms:         make(map[string]*probeSignalRoom),
		businessRooms: make(map[string]*probeSignalRoom),
	}
}

type signalRoutingError struct {
	roomID    string
	roomKey   string
	requestID string
	reason    string
	message   string
}

func (e *signalRoutingError) Error() string {
	if e == nil {
		return ""
	}
	return e.message
}

func newSignalRoutingError(roomID, roomKey, requestID, reason, message string) error {
	return &signalRoutingError{
		roomID:    strings.TrimSpace(roomID),
		roomKey:   normalizeLiveRoomKey(roomKey),
		requestID: strings.TrimSpace(requestID),
		reason:    strings.TrimSpace(reason),
		message:   strings.TrimSpace(message),
	}
}

func newProbeSignalRoom() *probeSignalRoom {
	return &probeSignalRoom{
		peers:    make(map[string]*probeSignalClient),
		requests: make(map[string]*probeLinkMicSession),
	}
}

func (e probeSignalEnvelope) normalizedBusinessRoomKey() string {
	if roomKey := normalizeLiveRoomKey(e.RoomKey); roomKey != "" {
		return roomKey
	}
	if roomKey := normalizeLiveRoomKey(e.RoomKeyAlt); roomKey != "" {
		return roomKey
	}
	return ""
}

func (s *apiServer) handleProbeSignalingWebSocket(writer http.ResponseWriter, request *http.Request) {
	if request.Method != http.MethodGet {
		writeMethodNotAllowed(writer)
		return
	}
	if request.URL.Path != "/ws" {
		writeNotFound(writer)
		return
	}

	conn, err := probeSignalUpgrader.Upgrade(writer, request, nil)
	if err != nil {
		log.Printf("probe signaling upgrade failed: %v", err)
		return
	}

	client := &probeSignalClient{
		hub:    s.probeSignals,
		server: s,
		conn:   conn,
		send:   make(chan []byte, 32),
	}

	log.Printf("probe signaling connected: remote=%s", request.RemoteAddr)
	go client.writePump()
	client.readPump()
}

func (c *probeSignalClient) readPump() {
	defer func() {
		c.hub.unregister(c)
		_ = c.conn.Close()
	}()

	c.conn.SetReadLimit(probeSignalMaxMessageSize)
	_ = c.conn.SetReadDeadline(time.Now().Add(probeSignalPongWait))
	c.conn.SetPongHandler(func(string) error {
		return c.conn.SetReadDeadline(time.Now().Add(probeSignalPongWait))
	})

	for {
		messageType, data, err := c.conn.ReadMessage()
		if err != nil {
			if websocket.IsUnexpectedCloseError(err, websocket.CloseGoingAway, websocket.CloseAbnormalClosure) {
				log.Printf("probe signaling read failed: peer_id=%s room_id=%s err=%v", c.peerID, c.roomID, err)
			}
			return
		}
		if messageType != websocket.TextMessage {
			c.sendError("", "", "", "", "only text websocket messages are supported")
			continue
		}

		if err := c.handleMessage(data); err != nil {
			log.Printf("probe signaling message rejected: peer_id=%s room_id=%s err=%v", c.peerID, c.roomID, err)
			c.sendSignalError(err)
		}
	}
}

func (c *probeSignalClient) writePump() {
	ticker := time.NewTicker(probeSignalPingPeriod)
	defer func() {
		ticker.Stop()
		_ = c.conn.Close()
	}()

	for {
		select {
		case message, ok := <-c.send:
			_ = c.conn.SetWriteDeadline(time.Now().Add(probeSignalWriteWait))
			if !ok {
				_ = c.conn.WriteMessage(websocket.CloseMessage, nil)
				return
			}

			if err := c.conn.WriteMessage(websocket.TextMessage, message); err != nil {
				return
			}
		case <-ticker.C:
			_ = c.conn.SetWriteDeadline(time.Now().Add(probeSignalWriteWait))
			if err := c.conn.WriteMessage(websocket.PingMessage, nil); err != nil {
				return
			}
		}
	}
}

func (c *probeSignalClient) handleMessage(data []byte) error {
	var envelope probeSignalEnvelope
	if err := json.Unmarshal(data, &envelope); err != nil {
		return fmt.Errorf("invalid json message")
	}

	switch envelope.Type {
	case "probe.register":
		return c.handleRegister(envelope)
	case "live.anchor.join":
		return c.handleLiveAnchorJoinMessage(data)
	case "live.anchor.heartbeat":
		return c.handleLiveAnchorHeartbeatMessage(data)
	case "live.anchor.leave":
		return c.handleLiveAnchorLeaveMessage(data)
	case "rtc.offer", "rtc.answer", "rtc.ice-candidate":
		return c.handleRTCRelay(envelope, data)
	case linkMicTypeApply, linkMicTypeInvite, linkMicTypeAccept, linkMicTypeReject, linkMicTypeCancel, linkMicTypeHangup, linkMicTypeKick:
		return c.handleLinkMicSignal(envelope, data)
	default:
		return fmt.Errorf("unsupported message type: %s", envelope.Type)
	}
}

func (c *probeSignalClient) handleRegister(envelope probeSignalEnvelope) error {
	roomID := strings.TrimSpace(envelope.RoomID)
	peerID := strings.TrimSpace(envelope.PeerID)
	role := strings.TrimSpace(envelope.Role)
	mode := strings.TrimSpace(envelope.Mode)
	requestID := strings.TrimSpace(envelope.RequestID)

	if roomID == "" {
		return fmt.Errorf("probe.register requires room_id")
	}
	if peerID == "" {
		return fmt.Errorf("probe.register requires peer_id")
	}
	if role == "" {
		return fmt.Errorf("probe.register requires role")
	}
	if c.peerID != "" {
		return fmt.Errorf("probe already registered")
	}
	if requestID != "" {
		ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
		defer cancel()

		if err := c.hub.validateAcceptedRTCRegistration(ctx, roomID, peerID, role, requestID); err != nil {
			return err
		}
	}

	replacedPeer := c.hub.register(c, roomID, peerID, role, mode, requestID)
	if replacedPeer != nil {
		replacedPeer.sendError(roomID, "", requestID, "", "peer_id replaced by a newer session")
		_ = replacedPeer.conn.Close()
	}

	c.sendJSON(probeRegisteredMessage{
		Type:   "probe.registered",
		RoomID: roomID,
		PeerID: peerID,
		Role:   role,
		TsMs:   nowUnixMilli(),
	})

	c.hub.broadcastRoomMemberEvent(roomID, "room.member-join", probeRoomPeer{
		PeerID: peerID,
		Role:   role,
	})
	c.hub.broadcastRoomMemberList(roomID)

	log.Printf("probe signaling registered: room_id=%s peer_id=%s role=%s mode=%s request_id=%s", roomID, peerID, role, mode, requestID)
	return nil
}

func (c *probeSignalClient) handleRTCRelay(envelope probeSignalEnvelope, rawMessage []byte) error {
	if c.anchorUserID > 0 || envelope.normalizedBusinessRoomKey() != "" {
		return c.handleBusinessRTCRelay(envelope, rawMessage)
	}

	if c.peerID == "" || c.roomID == "" {
		return fmt.Errorf("probe must register before rtc signaling")
	}
	if strings.TrimSpace(envelope.RoomID) != c.roomID {
		return fmt.Errorf("%s room_id mismatch", envelope.Type)
	}
	if strings.TrimSpace(envelope.FromUserID) != c.peerID {
		return fmt.Errorf("%s from_user_id mismatch", envelope.Type)
	}

	targetID := strings.TrimSpace(envelope.ToUserID)
	if targetID == "" {
		return fmt.Errorf("%s requires to_user_id", envelope.Type)
	}

	target := c.hub.findPeer(c.roomID, targetID)
	if target == nil {
		return fmt.Errorf("target peer not found: %s", targetID)
	}
	if err := validateBoundRTCRequest(c, target, envelope); err != nil {
		return err
	}

	target.sendRaw(cloneBytes(rawMessage))
	return nil
}

func (c *probeSignalClient) handleLinkMicSignal(envelope probeSignalEnvelope, rawMessage []byte) error {
	if c.anchorUserID > 0 || envelope.normalizedBusinessRoomKey() != "" {
		return c.handleBusinessLinkMicSignal(envelope, rawMessage)
	}

	if c.peerID == "" || c.roomID == "" {
		return fmt.Errorf("probe must register before linkmic signaling")
	}
	if strings.TrimSpace(envelope.RoomID) != c.roomID {
		return fmt.Errorf("%s room_id mismatch", envelope.Type)
	}
	if strings.TrimSpace(envelope.FromUserID) != c.peerID {
		return fmt.Errorf("%s from_user_id mismatch", envelope.Type)
	}
	if strings.TrimSpace(envelope.RequestID) == "" {
		return fmt.Errorf("%s requires request_id", envelope.Type)
	}

	return c.hub.handleLinkMicSignal(c, envelope, rawMessage)
}

func (h *probeSignalHub) register(client *probeSignalClient, roomID, peerID, role, mode, requestID string) *probeSignalClient {
	h.mu.Lock()
	defer h.mu.Unlock()

	room := h.rooms[roomID]
	if room == nil {
		room = newProbeSignalRoom()
		h.rooms[roomID] = room
	}

	replacedPeer := room.peers[peerID]
	room.peers[peerID] = client
	client.roomID = roomID
	client.peerID = peerID
	client.role = role
	client.mode = mode
	client.requestID = requestID
	return replacedPeer
}

func (h *probeSignalHub) unregister(client *probeSignalClient) {
	if client.roomID != "" && client.peerID != "" {
		roomID := client.roomID
		leavingPeer := probeRoomPeer{
			PeerID: client.peerID,
			Role:   client.role,
		}
		var recipients []*probeSignalClient
		var peers []probeRoomPeer
		var outbound []outboundMessage
		removed := false

		h.mu.Lock()
		room := h.rooms[roomID]
		if room != nil && room.peers[client.peerID] == client {
			delete(room.peers, client.peerID)
			removed = true
			outbound = append(outbound, h.buildDisconnectSessionMessagesLocked(roomID, room, client.peerID)...)
			if len(room.peers) == 0 {
				delete(h.rooms, roomID)
			} else {
				recipients, peers = snapshotRoom(room)
			}
		}
		h.mu.Unlock()

		for _, message := range outbound {
			message.client.sendRaw(message.message)
		}

		if removed {
			sendRoomMemberEvent(recipients, roomID, "room.member-leave", leavingPeer)
			sendRoomMemberList(recipients, roomID, peers)
			log.Printf("probe signaling disconnected: room_id=%s peer_id=%s", roomID, client.peerID)
		}
	}

	anchorRoomKey, anchorUserID, anchorRemoved := h.unbindAnchorClient(client)
	if anchorRemoved {
		leavingMember := businessRoomMember{
			UserID:   anchorUserID,
			Username: client.anchorUsername,
			Role:     client.anchorRole,
			Online:   false,
		}

		var outbound []outboundMessage
		ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
		defer cancel()

		if err := markAnchorOfflineByRoomKey(ctx, h.database, anchorRoomKey, anchorUserID); err != nil {
			log.Printf("anchor disconnect cleanup failed: roomKey=%s userId=%d err=%v", anchorRoomKey, anchorUserID, err)
		}
		outbound = h.buildBusinessDisconnectSessionMessages(anchorRoomKey, strconv.FormatInt(anchorUserID, 10))
		for _, message := range outbound {
			message.client.sendRaw(message.message)
		}
		h.broadcastBusinessRoomMemberEvent(anchorRoomKey, "room.member-leave", leavingMember)
		h.broadcastAnchorMemberList(anchorRoomKey)
		log.Printf("[presence] anchor disconnected roomKey=%s userId=%d", anchorRoomKey, anchorUserID)
	}
}

func (h *probeSignalHub) handleLinkMicSignal(sender *probeSignalClient, envelope probeSignalEnvelope, rawMessage []byte) error {
	targetID := strings.TrimSpace(envelope.ToUserID)
	if targetID == "" {
		return fmt.Errorf("%s requires to_user_id", envelope.Type)
	}
	if targetID == sender.peerID {
		return fmt.Errorf("%s target must be another peer", envelope.Type)
	}

	h.mu.Lock()
	room := h.rooms[sender.roomID]
	if room == nil {
		h.mu.Unlock()
		return fmt.Errorf("room not found: %s", sender.roomID)
	}

	target := room.peers[targetID]
	if target == nil {
		h.mu.Unlock()
		return fmt.Errorf("target peer not found: %s", targetID)
	}

	outbound, err := h.handleLinkMicSignalLocked(room, sender, target, envelope, rawMessage)
	h.mu.Unlock()

	if err != nil {
		return err
	}

	for _, message := range outbound {
		message.client.sendRaw(message.message)
	}

	log.Printf("probe signaling handled: room_id=%s type=%s request_id=%s from=%s to=%s",
		sender.roomID, envelope.Type, envelope.RequestID, sender.peerID, targetID)
	return nil
}

func (h *probeSignalHub) handleLinkMicSignalLocked(room *probeSignalRoom, sender, target *probeSignalClient, envelope probeSignalEnvelope, rawMessage []byte) ([]outboundMessage, error) {
	switch envelope.Type {
	case linkMicTypeApply:
		return h.handleLinkMicApplyLocked(room, sender, target, envelope, rawMessage)
	case linkMicTypeInvite:
		return h.handleLinkMicInviteLocked(room, sender, target, envelope, rawMessage)
	case linkMicTypeAccept:
		return h.handleLinkMicAcceptLocked(room, sender, target, envelope, rawMessage)
	case linkMicTypeReject:
		return h.handleLinkMicRejectLocked(room, sender, target, envelope, rawMessage)
	case linkMicTypeCancel:
		return h.handleLinkMicCancelLocked(room, sender, target, envelope, rawMessage)
	case linkMicTypeHangup:
		return h.handleLinkMicHangupLocked(room, sender, target, envelope, rawMessage)
	case linkMicTypeKick:
		return h.handleLinkMicKickLocked(room, sender, target, envelope, rawMessage)
	default:
		return nil, fmt.Errorf("unsupported message type: %s", envelope.Type)
	}
}

func (c *probeSignalClient) handleBusinessRTCRelay(envelope probeSignalEnvelope, rawMessage []byte) error {
	roomKey := envelope.normalizedBusinessRoomKey()
	if roomKey == "" {
		roomKey = normalizeLiveRoomKey(c.anchorRoomKey)
	}
	if roomKey == "" {
		return newSignalRoutingError("", "", envelope.RequestID, "", fmt.Sprintf("%s requires roomKey", envelope.Type))
	}
	if !c.isBoundAnchor(roomKey) {
		return newSignalRoutingError("", roomKey, envelope.RequestID, "", "business websocket is not joined to the requested room")
	}

	localUserID := strconv.FormatInt(c.anchorUserID, 10)
	if strings.TrimSpace(envelope.FromUserID) != localUserID {
		return newSignalRoutingError("", roomKey, envelope.RequestID, "", fmt.Sprintf("%s from_user_id mismatch", envelope.Type))
	}

	targetUserID := strings.TrimSpace(envelope.ToUserID)
	if targetUserID == "" {
		return newSignalRoutingError("", roomKey, envelope.RequestID, "", fmt.Sprintf("%s requires to_user_id", envelope.Type))
	}
	if targetUserID == localUserID {
		return newSignalRoutingError("", roomKey, envelope.RequestID, "", fmt.Sprintf("%s target must be another user", envelope.Type))
	}

	var target *probeSignalClient

	c.hub.mu.Lock()
	room := c.hub.businessRooms[roomKey]
	if room != nil {
		target = room.peers[targetUserID]
	}
	if target == nil {
		c.hub.mu.Unlock()
		return newSignalRoutingError("", roomKey, envelope.RequestID, "target offline", "target offline")
	}
	if err := validateAcceptedBusinessRTCRelayLocked(room, localUserID, targetUserID, strings.TrimSpace(envelope.RequestID)); err != nil {
		c.hub.mu.Unlock()
		return enrichSignalRoutingError(err, roomKey, envelope.RequestID)
	}
	c.hub.mu.Unlock()

	target.sendRaw(cloneBytes(rawMessage))
	log.Printf("business signaling relayed rtc: room_key=%s type=%s request_id=%s from=%s to=%s",
		roomKey, envelope.Type, strings.TrimSpace(envelope.RequestID), localUserID, targetUserID)
	return nil
}

func (c *probeSignalClient) handleBusinessLinkMicSignal(envelope probeSignalEnvelope, rawMessage []byte) error {
	roomKey := envelope.normalizedBusinessRoomKey()
	if roomKey == "" {
		roomKey = normalizeLiveRoomKey(c.anchorRoomKey)
	}
	if roomKey == "" {
		return newSignalRoutingError("", "", envelope.RequestID, "", fmt.Sprintf("%s requires roomKey", envelope.Type))
	}
	if !c.isBoundAnchor(roomKey) {
		return newSignalRoutingError("", roomKey, envelope.RequestID, "", "business websocket is not joined to the requested room")
	}

	localUserID := strconv.FormatInt(c.anchorUserID, 10)
	if strings.TrimSpace(envelope.FromUserID) != localUserID {
		return newSignalRoutingError("", roomKey, envelope.RequestID, "", fmt.Sprintf("%s from_user_id mismatch", envelope.Type))
	}
	if strings.TrimSpace(envelope.RequestID) == "" {
		return newSignalRoutingError("", roomKey, "", "", fmt.Sprintf("%s requires request_id", envelope.Type))
	}

	targetUserID := strings.TrimSpace(envelope.ToUserID)
	if targetUserID == "" {
		return newSignalRoutingError("", roomKey, envelope.RequestID, "", fmt.Sprintf("%s requires to_user_id", envelope.Type))
	}
	if targetUserID == localUserID {
		return newSignalRoutingError("", roomKey, envelope.RequestID, "", fmt.Sprintf("%s target must be another user", envelope.Type))
	}

	c.hub.mu.Lock()
	room := c.hub.businessRooms[roomKey]
	if room == nil {
		c.hub.mu.Unlock()
		return newSignalRoutingError("", roomKey, envelope.RequestID, "", "room not found")
	}

	target := room.peers[targetUserID]
	if target == nil {
		c.hub.mu.Unlock()
		return newSignalRoutingError("", roomKey, envelope.RequestID, "target offline", "target offline")
	}

	outbound, err := c.hub.handleLinkMicSignalLocked(room, c.withBusinessRole(), target.withBusinessRole(), envelope, rawMessage)
	c.hub.mu.Unlock()
	if err != nil {
		return enrichSignalRoutingError(err, roomKey, envelope.RequestID)
	}

	for _, message := range outbound {
		message.client.sendRaw(message.message)
	}

	log.Printf("business signaling handled: room_key=%s type=%s request_id=%s from=%s to=%s",
		roomKey, envelope.Type, envelope.RequestID, localUserID, targetUserID)
	return nil
}

func (c *probeSignalClient) businessPeerID() string {
	if c.anchorUserID <= 0 {
		return ""
	}
	return strconv.FormatInt(c.anchorUserID, 10)
}

func (c *probeSignalClient) withBusinessRole() *probeSignalClient {
	clone := *c
	clone.roomID = normalizeLiveRoomKey(c.anchorRoomKey)
	clone.peerID = c.businessPeerID()
	clone.role = strings.TrimSpace(c.anchorRole)
	return &clone
}

func validateAcceptedBusinessRTCRelayLocked(room *probeSignalRoom, fromUserID, toUserID, requestID string) error {
	requestID = strings.TrimSpace(requestID)
	if requestID == "" {
		return newSignalRoutingError("", "", "", "request not found", "request not found")
	}
	if room == nil || room.requests == nil {
		return newSignalRoutingError("", "", requestID, "request not found", "request not found")
	}

	request := room.requests[requestID]
	if request == nil {
		return newSignalRoutingError("", "", requestID, "request not found", "request not found")
	}
	if request.RequestState != linkMicRequestStateAccepted {
		return newSignalRoutingError("", "", requestID, "request expired", "request expired")
	}
	if !sessionHasPeer(*request, fromUserID) || !sessionHasPeer(*request, toUserID) {
		return fmt.Errorf("rtc signaling peers do not match the accepted request")
	}
	return nil
}

func enrichSignalRoutingError(err error, roomKey, requestID string) error {
	if err == nil {
		return nil
	}

	routingErr, ok := err.(*signalRoutingError)
	if !ok {
		return newSignalRoutingError("", roomKey, requestID, "", err.Error())
	}
	if routingErr.roomKey == "" {
		routingErr.roomKey = normalizeLiveRoomKey(roomKey)
	}
	if routingErr.requestID == "" {
		routingErr.requestID = strings.TrimSpace(requestID)
	}
	return routingErr
}

func (h *probeSignalHub) handleLinkMicApplyLocked(room *probeSignalRoom, sender, target *probeSignalClient, envelope probeSignalEnvelope, rawMessage []byte) ([]outboundMessage, error) {
	if sender.role == "controller" {
		return nil, fmt.Errorf("linkmic.apply must be sent by a non-controller peer")
	}
	if target.role != "controller" {
		return nil, fmt.Errorf("linkmic.apply target must be controller")
	}
	if room.session != nil {
		return nil, fmt.Errorf("room already has a pending or active linkmic session")
	}

	now := nowUnixMilli()
	room.session = &probeLinkMicSession{
		RequestID:     envelope.RequestID,
		RequestType:   envelope.Type,
		RequestState:  linkMicRequestStatePending,
		ControllerID:  target.peerID,
		ParticipantID: sender.peerID,
		State:         linkMicStateApplying,
		CreatedAtMs:   now,
		UpdatedAtMs:   now,
	}
	recordRoomRequestLocked(room, room.session)

	outbound := []outboundMessage{
		{client: target, message: cloneBytes(rawMessage)},
	}
	outbound = append(outbound, buildStateSyncMessages(room, sender.roomID, *room.session, "pending-request-created")...)
	return outbound, nil
}

func (h *probeSignalHub) handleLinkMicInviteLocked(room *probeSignalRoom, sender, target *probeSignalClient, envelope probeSignalEnvelope, rawMessage []byte) ([]outboundMessage, error) {
	if sender.role != "controller" {
		return nil, fmt.Errorf("linkmic.invite must be sent by controller")
	}
	if target.role == "controller" {
		return nil, fmt.Errorf("linkmic.invite target must be non-controller")
	}
	if room.session != nil {
		return nil, fmt.Errorf("room already has a pending or active linkmic session")
	}

	now := nowUnixMilli()
	room.session = &probeLinkMicSession{
		RequestID:     envelope.RequestID,
		RequestType:   envelope.Type,
		RequestState:  linkMicRequestStatePending,
		ControllerID:  sender.peerID,
		ParticipantID: target.peerID,
		State:         linkMicStateInviting,
		CreatedAtMs:   now,
		UpdatedAtMs:   now,
	}
	recordRoomRequestLocked(room, room.session)

	outbound := []outboundMessage{
		{client: target, message: cloneBytes(rawMessage)},
	}
	outbound = append(outbound, buildStateSyncMessages(room, sender.roomID, *room.session, "pending-request-created")...)
	return outbound, nil
}

func (h *probeSignalHub) handleLinkMicAcceptLocked(room *probeSignalRoom, sender, target *probeSignalClient, envelope probeSignalEnvelope, rawMessage []byte) ([]outboundMessage, error) {
	session, err := requireActiveRoomSession(room, envelope.RequestID)
	if err != nil {
		return nil, err
	}
	if session.RequestType == linkMicTypeApply {
		if sender.peerID != session.ControllerID || target.peerID != session.ParticipantID {
			return nil, fmt.Errorf("linkmic.accept direction does not match linkmic.apply request")
		}
	} else if session.RequestType == linkMicTypeInvite {
		if sender.peerID != session.ParticipantID || target.peerID != session.ControllerID {
			return nil, fmt.Errorf("linkmic.accept direction does not match linkmic.invite request")
		}
	} else {
		return nil, fmt.Errorf("linkmic.accept requires a pending apply or invite request")
	}

	session.State = linkMicStateActive
	session.RequestState = linkMicRequestStateAccepted
	session.UpdatedAtMs = nowUnixMilli()

	outbound := []outboundMessage{
		{client: target, message: cloneBytes(rawMessage)},
	}
	outbound = append(outbound, buildRTCJoinParamsMessages(room, sender.roomID, *session)...)
	outbound = append(outbound, buildLinkMicConnectedMessages(room, sender.roomID, *session)...)
	outbound = append(outbound, buildStateSyncMessages(room, sender.roomID, *session, "linkmic-accepted")...)
	return outbound, nil
}

func (h *probeSignalHub) handleLinkMicRejectLocked(room *probeSignalRoom, sender, target *probeSignalClient, envelope probeSignalEnvelope, rawMessage []byte) ([]outboundMessage, error) {
	session, err := requireActiveRoomSession(room, envelope.RequestID)
	if err != nil {
		return nil, err
	}
	if session.RequestType == linkMicTypeApply {
		if sender.peerID != session.ControllerID || target.peerID != session.ParticipantID {
			return nil, fmt.Errorf("linkmic.reject direction does not match linkmic.apply request")
		}
	} else if session.RequestType == linkMicTypeInvite {
		if sender.peerID != session.ParticipantID || target.peerID != session.ControllerID {
			return nil, fmt.Errorf("linkmic.reject direction does not match linkmic.invite request")
		}
	} else {
		return nil, fmt.Errorf("linkmic.reject requires a pending apply or invite request")
	}

	outbound := []outboundMessage{
		{client: target, message: cloneBytes(rawMessage)},
	}
	session.RequestState = linkMicRequestStateRejected
	session.UpdatedAtMs = nowUnixMilli()
	outbound = append(outbound, buildResetStateMessages(room, sender.roomID, *session, "linkmic-rejected")...)
	room.session = nil
	return outbound, nil
}

func (h *probeSignalHub) handleLinkMicCancelLocked(room *probeSignalRoom, sender, target *probeSignalClient, envelope probeSignalEnvelope, rawMessage []byte) ([]outboundMessage, error) {
	session, err := requireActiveRoomSession(room, envelope.RequestID)
	if err != nil {
		return nil, err
	}

	initiatorID, responderID := sessionEndpoints(session)
	if sender.peerID != initiatorID || target.peerID != responderID {
		return nil, fmt.Errorf("linkmic.cancel must be sent by the request initiator")
	}

	outbound := []outboundMessage{
		{client: target, message: cloneBytes(rawMessage)},
	}
	session.RequestState = linkMicRequestStateCancelled
	session.UpdatedAtMs = nowUnixMilli()
	outbound = append(outbound, buildResetStateMessages(room, sender.roomID, *session, "linkmic-cancelled")...)
	room.session = nil
	return outbound, nil
}

func (h *probeSignalHub) handleLinkMicHangupLocked(room *probeSignalRoom, sender, target *probeSignalClient, envelope probeSignalEnvelope, rawMessage []byte) ([]outboundMessage, error) {
	session, err := requireActiveRoomSession(room, envelope.RequestID)
	if err != nil {
		return nil, err
	}
	if !sessionHasPeer(*session, sender.peerID) || !sessionHasPeer(*session, target.peerID) {
		return nil, fmt.Errorf("linkmic.hangup peers do not match the active session")
	}

	outbound := []outboundMessage{
		{client: target, message: cloneBytes(rawMessage)},
	}
	session.RequestState = linkMicRequestStateEnded
	session.UpdatedAtMs = nowUnixMilli()
	outbound = append(outbound, buildResetStateMessages(room, sender.roomID, *session, "linkmic-hangup")...)
	room.session = nil
	return outbound, nil
}

func (h *probeSignalHub) handleLinkMicKickLocked(room *probeSignalRoom, sender, target *probeSignalClient, envelope probeSignalEnvelope, rawMessage []byte) ([]outboundMessage, error) {
	session, err := requireActiveRoomSession(room, envelope.RequestID)
	if err != nil {
		return nil, err
	}
	if sender.peerID != session.ControllerID || target.peerID != session.ParticipantID {
		return nil, fmt.Errorf("linkmic.kick must be sent by controller to the participant")
	}

	outbound := []outboundMessage{
		{client: target, message: cloneBytes(rawMessage)},
	}
	session.RequestState = linkMicRequestStateEnded
	session.UpdatedAtMs = nowUnixMilli()
	outbound = append(outbound, buildResetStateMessages(room, sender.roomID, *session, "linkmic-kicked")...)
	room.session = nil
	return outbound, nil
}

func (h *probeSignalHub) buildDisconnectSessionMessagesLocked(roomID string, room *probeSignalRoom, leavingPeerID string) []outboundMessage {
	session := room.session
	if session == nil || !sessionHasPeer(*session, leavingPeerID) {
		return nil
	}

	otherPeerID := session.ControllerID
	if otherPeerID == leavingPeerID {
		otherPeerID = session.ParticipantID
	}
	otherClient := room.peers[otherPeerID]
	if session.State == linkMicStateApplying || session.State == linkMicStateInviting {
		session.RequestState = linkMicRequestStateCancelled
	} else {
		session.RequestState = linkMicRequestStateEnded
	}
	session.UpdatedAtMs = nowUnixMilli()
	room.session = nil
	if otherClient == nil {
		return nil
	}

	hangupMessage, err := marshalEnvelope(probeSignalEnvelope{
		Type:       linkMicTypeHangup,
		RoomID:     roomID,
		FromUserID: leavingPeerID,
		ToUserID:   otherPeerID,
		RequestID:  session.RequestID,
		Payload: mustMarshalRaw(map[string]any{
			"reason": "peer-disconnected",
		}),
		TsMs: nowUnixMilli(),
	})
	if err != nil {
		log.Printf("probe signaling marshal disconnect hangup failed: room_id=%s peer_id=%s err=%v", roomID, leavingPeerID, err)
		return nil
	}

	outbound := []outboundMessage{
		{client: otherClient, message: hangupMessage},
	}
	outbound = append(outbound, buildResetStateMessages(room, roomID, *session, "peer-disconnected")...)
	return outbound
}

func (h *probeSignalHub) findPeer(roomID, peerID string) *probeSignalClient {
	h.mu.Lock()
	defer h.mu.Unlock()

	room := h.rooms[roomID]
	if room == nil {
		return nil
	}
	return room.peers[peerID]
}

func (h *probeSignalHub) buildBusinessDisconnectSessionMessages(roomKey, leavingUserID string) []outboundMessage {
	h.mu.Lock()
	defer h.mu.Unlock()

	room := h.businessRooms[normalizeLiveRoomKey(roomKey)]
	if room == nil {
		return nil
	}
	return h.buildDisconnectSessionMessagesLocked(normalizeLiveRoomKey(roomKey), room, strings.TrimSpace(leavingUserID))
}

func (h *probeSignalHub) broadcastRoomMemberEvent(roomID, eventType string, peer probeRoomPeer) {
	recipients, _, ok := h.snapshotRoom(roomID)
	if !ok {
		return
	}
	sendRoomMemberEvent(recipients, roomID, eventType, peer)
}

func (h *probeSignalHub) broadcastRoomMemberList(roomID string) {
	recipients, peers, _ := h.snapshotRoom(roomID)
	sendRoomMemberList(recipients, roomID, peers)
}

func (h *probeSignalHub) snapshotRoom(roomID string) ([]*probeSignalClient, []probeRoomPeer, bool) {
	h.mu.Lock()
	defer h.mu.Unlock()

	room := h.rooms[roomID]
	if room == nil {
		return nil, []probeRoomPeer{}, false
	}

	recipients, peers := snapshotRoom(room)
	return recipients, peers, true
}

func snapshotRoom(room *probeSignalRoom) ([]*probeSignalClient, []probeRoomPeer) {
	recipients := make([]*probeSignalClient, 0, len(room.peers))
	peers := make([]probeRoomPeer, 0, len(room.peers))
	for _, client := range room.peers {
		recipients = append(recipients, client)
		peers = append(peers, probeRoomPeer{
			PeerID: client.peerID,
			Role:   client.role,
		})
	}

	sort.Slice(peers, func(i, j int) bool {
		return peers[i].PeerID < peers[j].PeerID
	})

	return recipients, peers
}

func sendRoomMemberEvent(recipients []*probeSignalClient, roomID, eventType string, peer probeRoomPeer) {
	if len(recipients) == 0 {
		return
	}

	encoded, err := marshalEnvelope(probeSignalEnvelope{
		Type:   eventType,
		RoomID: roomID,
		Payload: mustMarshalRaw(map[string]any{
			"member": peer,
		}),
		TsMs: nowUnixMilli(),
	})
	if err != nil {
		log.Printf("probe signaling marshal %s failed: room_id=%s err=%v", eventType, roomID, err)
		return
	}

	for _, client := range recipients {
		client.sendRaw(encoded)
	}
}

func sendRoomMemberList(recipients []*probeSignalClient, roomID string, peers []probeRoomPeer) {
	message := roomMemberListMessage{
		Type:   "room.member-list",
		RoomID: roomID,
		Peers:  peers,
		TsMs:   nowUnixMilli(),
	}

	encoded, err := json.Marshal(message)
	if err != nil {
		log.Printf("probe signaling marshal room.member-list failed: room_id=%s err=%v", roomID, err)
		return
	}

	for _, client := range recipients {
		client.sendRaw(encoded)
	}
}

func recordRoomRequestLocked(room *probeSignalRoom, session *probeLinkMicSession) {
	if room == nil || session == nil {
		return
	}
	if room.requests == nil {
		room.requests = make(map[string]*probeLinkMicSession)
	}
	room.requests[session.RequestID] = session
}

func requireActiveRoomSession(room *probeSignalRoom, requestID string) (*probeLinkMicSession, error) {
	requestID = strings.TrimSpace(requestID)
	if requestID == "" {
		return nil, newSignalRoutingError("", "", "", "request not found", "request not found")
	}
	if room == nil {
		return nil, newSignalRoutingError("", "", requestID, "request not found", "request not found")
	}

	request := room.requests[requestID]
	if request == nil {
		return nil, newSignalRoutingError("", "", requestID, "request not found", "request not found")
	}
	if room.session == nil || room.session.RequestID != requestID {
		return nil, newSignalRoutingError("", "", requestID, "request expired", "request expired")
	}
	return request, nil
}

func sessionEndpoints(session *probeLinkMicSession) (initiatorID string, responderID string) {
	if session.RequestType == linkMicTypeApply {
		return session.ParticipantID, session.ControllerID
	}
	return session.ControllerID, session.ParticipantID
}

func sessionHasPeer(session probeLinkMicSession, peerID string) bool {
	return session.ControllerID == peerID || session.ParticipantID == peerID
}

func buildStateSyncMessages(room *probeSignalRoom, roomID string, session probeLinkMicSession, reason string) []outboundMessage {
	controller := room.peers[session.ControllerID]
	participant := room.peers[session.ParticipantID]
	if controller == nil && participant == nil {
		return nil
	}

	messages := make([]outboundMessage, 0, 2)
	if controller != nil {
		encoded, err := marshalEnvelope(probeSignalEnvelope{
			Type:       linkMicTypeStateSync,
			RoomID:     roomID,
			FromUserID: session.ParticipantID,
			ToUserID:   session.ControllerID,
			RequestID:  session.RequestID,
			Payload: mustMarshalRaw(map[string]any{
				"state":               stateForPeer(session, session.ControllerID),
				"reason":              reason,
				"request_type":        session.RequestType,
				"controller_user_id":  session.ControllerID,
				"participant_user_id": session.ParticipantID,
			}),
			TsMs: nowUnixMilli(),
		})
		if err == nil {
			messages = append(messages, outboundMessage{client: controller, message: encoded})
		} else {
			log.Printf("probe signaling marshal state sync failed: room_id=%s peer_id=%s err=%v", roomID, session.ControllerID, err)
		}
	}
	if participant != nil {
		encoded, err := marshalEnvelope(probeSignalEnvelope{
			Type:       linkMicTypeStateSync,
			RoomID:     roomID,
			FromUserID: session.ControllerID,
			ToUserID:   session.ParticipantID,
			RequestID:  session.RequestID,
			Payload: mustMarshalRaw(map[string]any{
				"state":               stateForPeer(session, session.ParticipantID),
				"reason":              reason,
				"request_type":        session.RequestType,
				"controller_user_id":  session.ControllerID,
				"participant_user_id": session.ParticipantID,
			}),
			TsMs: nowUnixMilli(),
		})
		if err == nil {
			messages = append(messages, outboundMessage{client: participant, message: encoded})
		} else {
			log.Printf("probe signaling marshal state sync failed: room_id=%s peer_id=%s err=%v", roomID, session.ParticipantID, err)
		}
	}

	return messages
}

func buildResetStateMessages(room *probeSignalRoom, roomID string, session probeLinkMicSession, reason string) []outboundMessage {
	controller := room.peers[session.ControllerID]
	participant := room.peers[session.ParticipantID]
	if controller == nil && participant == nil {
		return nil
	}

	messages := make([]outboundMessage, 0, 2)
	if controller != nil {
		encoded, err := marshalEnvelope(probeSignalEnvelope{
			Type:       linkMicTypeStateSync,
			RoomID:     roomID,
			FromUserID: session.ParticipantID,
			ToUserID:   session.ControllerID,
			RequestID:  session.RequestID,
			Payload: mustMarshalRaw(map[string]any{
				"state":               linkMicStateLiveOnly,
				"reason":              reason,
				"request_type":        session.RequestType,
				"controller_user_id":  session.ControllerID,
				"participant_user_id": session.ParticipantID,
			}),
			TsMs: nowUnixMilli(),
		})
		if err == nil {
			messages = append(messages, outboundMessage{client: controller, message: encoded})
		} else {
			log.Printf("probe signaling marshal reset state failed: room_id=%s peer_id=%s err=%v", roomID, session.ControllerID, err)
		}
	}
	if participant != nil {
		encoded, err := marshalEnvelope(probeSignalEnvelope{
			Type:       linkMicTypeStateSync,
			RoomID:     roomID,
			FromUserID: session.ControllerID,
			ToUserID:   session.ParticipantID,
			RequestID:  session.RequestID,
			Payload: mustMarshalRaw(map[string]any{
				"state":               linkMicStateIdle,
				"reason":              reason,
				"request_type":        session.RequestType,
				"controller_user_id":  session.ControllerID,
				"participant_user_id": session.ParticipantID,
			}),
			TsMs: nowUnixMilli(),
		})
		if err == nil {
			messages = append(messages, outboundMessage{client: participant, message: encoded})
		} else {
			log.Printf("probe signaling marshal reset state failed: room_id=%s peer_id=%s err=%v", roomID, session.ParticipantID, err)
		}
	}

	return messages
}

func buildRTCJoinParamsMessages(room *probeSignalRoom, roomID string, session probeLinkMicSession) []outboundMessage {
	controller := room.peers[session.ControllerID]
	participant := room.peers[session.ParticipantID]
	if controller == nil && participant == nil {
		return nil
	}

	messages := make([]outboundMessage, 0, 2)
	if controller != nil {
		encoded, err := marshalEnvelope(probeSignalEnvelope{
			Type:       "rtc.join-params",
			RoomID:     roomID,
			FromUserID: session.ParticipantID,
			ToUserID:   session.ControllerID,
			RequestID:  session.RequestID,
			Payload: mustMarshalRaw(map[string]any{
				"transport":           "libdatachannel",
				"room_name":           roomID,
				"participant_role":    "controller",
				"controller_user_id":  session.ControllerID,
				"participant_user_id": session.ParticipantID,
				"request_type":        session.RequestType,
			}),
			TsMs: nowUnixMilli(),
		})
		if err == nil {
			messages = append(messages, outboundMessage{client: controller, message: encoded})
		} else {
			log.Printf("probe signaling marshal rtc.join-params failed: room_id=%s peer_id=%s err=%v", roomID, session.ControllerID, err)
		}
	}
	if participant != nil {
		encoded, err := marshalEnvelope(probeSignalEnvelope{
			Type:       "rtc.join-params",
			RoomID:     roomID,
			FromUserID: session.ControllerID,
			ToUserID:   session.ParticipantID,
			RequestID:  session.RequestID,
			Payload: mustMarshalRaw(map[string]any{
				"transport":           "libdatachannel",
				"room_name":           roomID,
				"participant_role":    "participant",
				"controller_user_id":  session.ControllerID,
				"participant_user_id": session.ParticipantID,
				"request_type":        session.RequestType,
			}),
			TsMs: nowUnixMilli(),
		})
		if err == nil {
			messages = append(messages, outboundMessage{client: participant, message: encoded})
		} else {
			log.Printf("probe signaling marshal rtc.join-params failed: room_id=%s peer_id=%s err=%v", roomID, session.ParticipantID, err)
		}
	}

	return messages
}

func buildLinkMicConnectedMessages(room *probeSignalRoom, roomID string, session probeLinkMicSession) []outboundMessage {
	controller := room.peers[session.ControllerID]
	participant := room.peers[session.ParticipantID]
	if controller == nil && participant == nil {
		return nil
	}

	messages := make([]outboundMessage, 0, 2)
	if controller != nil {
		encoded, err := marshalEnvelope(probeSignalEnvelope{
			Type:       linkMicTypeConnected,
			RoomID:     roomID,
			FromUserID: session.ParticipantID,
			ToUserID:   session.ControllerID,
			RequestID:  session.RequestID,
			Payload: mustMarshalRaw(map[string]any{
				"state":               linkMicStateActive,
				"controller_user_id":  session.ControllerID,
				"participant_user_id": session.ParticipantID,
			}),
			TsMs: nowUnixMilli(),
		})
		if err == nil {
			messages = append(messages, outboundMessage{client: controller, message: encoded})
		} else {
			log.Printf("probe signaling marshal linkmic.connected failed: room_id=%s peer_id=%s err=%v", roomID, session.ControllerID, err)
		}
	}
	if participant != nil {
		encoded, err := marshalEnvelope(probeSignalEnvelope{
			Type:       linkMicTypeConnected,
			RoomID:     roomID,
			FromUserID: session.ControllerID,
			ToUserID:   session.ParticipantID,
			RequestID:  session.RequestID,
			Payload: mustMarshalRaw(map[string]any{
				"state":               linkMicStateRtcActive,
				"controller_user_id":  session.ControllerID,
				"participant_user_id": session.ParticipantID,
			}),
			TsMs: nowUnixMilli(),
		})
		if err == nil {
			messages = append(messages, outboundMessage{client: participant, message: encoded})
		} else {
			log.Printf("probe signaling marshal linkmic.connected failed: room_id=%s peer_id=%s err=%v", roomID, session.ParticipantID, err)
		}
	}

	return messages
}

func stateForPeer(session probeLinkMicSession, peerID string) string {
	switch session.State {
	case linkMicStateApplying:
		if peerID == session.ControllerID {
			return linkMicStateApplying
		}
		return "waiting-host"
	case linkMicStateInviting:
		if peerID == session.ControllerID {
			return linkMicStateInviting
		}
		return "guest-invited"
	case linkMicStateActive:
		if peerID == session.ControllerID {
			return linkMicStateActive
		}
		return linkMicStateRtcActive
	default:
		if peerID == session.ControllerID {
			return linkMicStateLiveOnly
		}
		return linkMicStateIdle
	}
}

func marshalEnvelope(envelope probeSignalEnvelope) ([]byte, error) {
	if envelope.TsMs == 0 {
		envelope.TsMs = nowUnixMilli()
	}
	return json.Marshal(envelope)
}

func mustMarshalRaw(payload any) json.RawMessage {
	encoded, err := json.Marshal(payload)
	if err != nil {
		log.Printf("probe signaling marshal payload failed: %v", err)
		return nil
	}
	return encoded
}

func cloneBytes(source []byte) []byte {
	return append([]byte(nil), source...)
}

func validateBoundRTCRequest(sender, target *probeSignalClient, envelope probeSignalEnvelope) error {
	senderRequestID := strings.TrimSpace(sender.requestID)
	targetRequestID := strings.TrimSpace(target.requestID)
	if senderRequestID == "" && targetRequestID == "" {
		return nil
	}

	if senderRequestID == "" || targetRequestID == "" {
		return fmt.Errorf("%s requires both peers to bind the same request_id before RTC relay", envelope.Type)
	}

	messageRequestID := strings.TrimSpace(envelope.RequestID)
	if messageRequestID == "" {
		return fmt.Errorf("%s requires request_id", envelope.Type)
	}
	if messageRequestID != senderRequestID || messageRequestID != targetRequestID {
		return fmt.Errorf("%s request_id mismatch", envelope.Type)
	}
	return nil
}

func (h *probeSignalHub) validateAcceptedRTCRegistration(ctx context.Context, roomID, peerID, role, requestID string) error {
	if h.database == nil {
		return fmt.Errorf("probe signaling database is not configured")
	}

	room, err := findLiveRoomByKeyQuerier(ctx, h.database, roomID)
	if err != nil {
		if err == sql.ErrNoRows {
			return fmt.Errorf("room not found for request binding: %s", roomID)
		}
		return fmt.Errorf("load room for request binding: %w", err)
	}

	requestRow, err := findLinkMicRequestByRequestIDQuerier(ctx, h.database, room.ID, requestID)
	if err != nil {
		if err == sql.ErrNoRows {
			return fmt.Errorf("request_id not found in room: %s", requestID)
		}
		return fmt.Errorf("load request for request binding: %w", err)
	}
	if requestRow.State != linkMicRequestStateAccepted {
		return fmt.Errorf("request_id is not accepted: %s", requestID)
	}

	controllerUserID, participantUserID, err := resolveAcceptedRTCUserIDs(room, *requestRow)
	if err != nil {
		return err
	}

	expectedPeerID := ""
	switch role {
	case liveRoomRoleController:
		expectedPeerID = strconv.FormatInt(controllerUserID, 10)
	case liveRoomRoleParticipant:
		expectedPeerID = strconv.FormatInt(participantUserID, 10)
	default:
		return fmt.Errorf("unsupported probe role for request binding: %s", role)
	}

	if peerID != expectedPeerID {
		return fmt.Errorf("peer_id does not match accepted request binding: expected %s for role %s", expectedPeerID, role)
	}
	return nil
}

func resolveAcceptedRTCUserIDs(room liveRoomRow, requestRow linkMicRequestRow) (int64, int64, error) {
	controllerUserID := room.OwnerUserID
	if requestRow.Initiator.ID != controllerUserID && requestRow.Target.ID != controllerUserID {
		return 0, 0, fmt.Errorf("accepted request does not belong to the room owner")
	}

	participantUserID := requestRow.Initiator.ID
	if participantUserID == controllerUserID {
		participantUserID = requestRow.Target.ID
	}
	if participantUserID == controllerUserID {
		return 0, 0, fmt.Errorf("accepted request is missing a non-controller participant")
	}

	return controllerUserID, participantUserID, nil
}

func (c *probeSignalClient) sendError(roomID, roomKey, requestID, reason, message string) {
	c.sendJSON(signalErrorMessage{
		Type:       "signal.error",
		RoomID:     strings.TrimSpace(roomID),
		RoomKey:    normalizeLiveRoomKey(roomKey),
		RoomKeyAlt: normalizeLiveRoomKey(roomKey),
		RequestID:  strings.TrimSpace(requestID),
		Reason:     strings.TrimSpace(reason),
		Message:    strings.TrimSpace(message),
		TsMs:       nowUnixMilli(),
	})
}

func (c *probeSignalClient) sendSignalError(err error) {
	if err == nil {
		return
	}

	routingErr, ok := err.(*signalRoutingError)
	if !ok {
		c.sendError(c.roomID, c.anchorRoomKey, "", "", err.Error())
		return
	}

	roomID := routingErr.roomID
	if roomID == "" {
		roomID = c.roomID
	}
	roomKey := routingErr.roomKey
	if roomKey == "" {
		roomKey = c.anchorRoomKey
	}
	c.sendError(roomID, roomKey, routingErr.requestID, routingErr.reason, routingErr.message)
}

func (c *probeSignalClient) sendJSON(payload any) {
	encoded, err := json.Marshal(payload)
	if err != nil {
		log.Printf("probe signaling marshal failed: peer_id=%s room_id=%s err=%v", c.peerID, c.roomID, err)
		return
	}
	c.sendRaw(encoded)
}

func (c *probeSignalClient) sendRaw(message []byte) {
	select {
	case c.send <- message:
	default:
		log.Printf("probe signaling send queue full: peer_id=%s room_id=%s", c.peerID, c.roomID)
	}
}

func nowUnixMilli() int64 {
	return time.Now().UnixMilli()
}
