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
	liveSignalWriteWait      = 10 * time.Second
	liveSignalPongWait       = 60 * time.Second
	liveSignalPingPeriod     = liveSignalPongWait * 9 / 10
	liveSignalMaxMessageSize = 1 << 20

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

var liveSignalUpgrader = websocket.Upgrader{
	ReadBufferSize:  4096,
	WriteBufferSize: 4096,
	CheckOrigin: func(_ *http.Request) bool {
		return true
	},
}

type liveSignalEnvelope struct {
	Type             string          `json:"type"`
	RoomID           string          `json:"room_id,omitempty"`
	PeerID           string          `json:"peer_id,omitempty"`
	Role             string          `json:"role,omitempty"`
	Mode             string          `json:"mode,omitempty"`
	SessionID        string          `json:"sessionId,omitempty"`
	SessionIDLegacy  string          `json:"session_id,omitempty"`
	FromRoomKey      string          `json:"fromRoomKey,omitempty"`
	FromRoomKeySnake string          `json:"from_room_key,omitempty"`
	ToRoomKey        string          `json:"toRoomKey,omitempty"`
	ToRoomKeySnake   string          `json:"to_room_key,omitempty"`
	FromUserID       string          `json:"from_user_id,omitempty"`
	FromUserIDCamel  string          `json:"fromUserId,omitempty"`
	ToUserID         string          `json:"to_user_id,omitempty"`
	ToUserIDCamel    string          `json:"toUserId,omitempty"`
	RequestID        string          `json:"request_id,omitempty"`
	RequestIDCamel   string          `json:"requestId,omitempty"`
	Payload          json.RawMessage `json:"payload,omitempty"`
	TsMs             int64           `json:"ts_ms,omitempty"`
	TsMsCamel        int64           `json:"tsMs,omitempty"`
}

type liveSignalPeer struct {
	PeerID string `json:"peer_id"`
	Role   string `json:"role,omitempty"`
}

type liveSignalRegisteredMessage struct {
	Type   string `json:"type"`
	RoomID string `json:"room_id"`
	PeerID string `json:"peer_id"`
	Role   string `json:"role"`
	TsMs   int64  `json:"ts_ms"`
}

type roomMemberListMessage struct {
	Type   string          `json:"type"`
	RoomID string          `json:"room_id"`
	Peers  []liveSignalPeer `json:"peers"`
	TsMs   int64           `json:"ts_ms"`
}

type signalErrorMessage struct {
	Type    string `json:"type"`
	RoomID  string `json:"room_id,omitempty"`
	Message string `json:"message"`
	TsMs    int64  `json:"ts_ms"`
}

type liveSignalHub struct {
	mu       sync.Mutex
	database *sql.DB
	publicSignalingURL string
	rooms              map[string]*liveSignalRoom
	anchorRooms        map[string]map[int64]*liveSignalClient
	crossRoomSessions map[string]*crossRoomSessionRuntime
}

type liveSignalRoom struct {
	peers   map[string]*liveSignalClient
	session *liveSignalLinkMicSession
}

type liveSignalLinkMicSession struct {
	RequestID     string
	RequestType   string
	ControllerID  string
	ParticipantID string
	State         string
	CreatedAtMs   int64
	UpdatedAtMs   int64
}

type liveSignalClient struct {
	hub    *liveSignalHub
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

type crossRoomSessionRuntime struct {
	session            CrossRoomLinkMicSession
	connectedAnchorKey map[string]bool
}

type outboundMessage struct {
	client  *liveSignalClient
	message []byte
}

func newLiveSignalHub(database *sql.DB, publicSignalingURL string) *liveSignalHub {
	return &liveSignalHub{
		database:           database,
		publicSignalingURL: strings.TrimSpace(publicSignalingURL),
		rooms:              make(map[string]*liveSignalRoom),
		anchorRooms:        make(map[string]map[int64]*liveSignalClient),
		crossRoomSessions:  make(map[string]*crossRoomSessionRuntime),
	}
}

func firstNonEmptyString(values ...string) string {
	for _, value := range values {
		trimmed := strings.TrimSpace(value)
		if trimmed != "" {
			return trimmed
		}
	}
	return ""
}

func (e liveSignalEnvelope) sessionID() string {
	return firstNonEmptyString(e.SessionID, e.SessionIDLegacy)
}

func (e liveSignalEnvelope) fromRoomKey() string {
	return normalizeLiveRoomKey(firstNonEmptyString(e.FromRoomKey, e.FromRoomKeySnake))
}

func (e liveSignalEnvelope) toRoomKey() string {
	return normalizeLiveRoomKey(firstNonEmptyString(e.ToRoomKey, e.ToRoomKeySnake))
}

func (e liveSignalEnvelope) fromUserID() string {
	return firstNonEmptyString(e.FromUserIDCamel, e.FromUserID)
}

func (e liveSignalEnvelope) toUserID() string {
	return firstNonEmptyString(e.ToUserIDCamel, e.ToUserID)
}

func (e liveSignalEnvelope) requestID() string {
	return firstNonEmptyString(e.RequestIDCamel, e.RequestID)
}

func (e liveSignalEnvelope) timestampMs() int64 {
	if e.TsMsCamel > 0 {
		return e.TsMsCamel
	}
	return e.TsMs
}

func (e liveSignalEnvelope) isCrossRoomMessage() bool {
	return e.sessionID() != "" || e.fromRoomKey() != "" || e.toRoomKey() != ""
}

func (c *liveSignalClient) isAnchorBound() bool {
	return normalizeLiveRoomKey(c.anchorRoomKey) != "" && c.anchorUserID > 0
}

func crossRoomAnchorKey(roomKey string, userID int64) string {
	return normalizeLiveRoomKey(roomKey) + "#" + strconv.FormatInt(userID, 10)
}

func isCrossRoomSessionActiveStatus(status string) bool {
	switch strings.TrimSpace(status) {
	case crossRoomLinkMicSessionStatusPending,
		crossRoomLinkMicSessionStatusAccepted,
		crossRoomLinkMicSessionStatusConnected:
		return true
	default:
		return false
	}
}

func isCrossRoomSessionRelayReady(status string) bool {
	switch strings.TrimSpace(status) {
	case crossRoomLinkMicSessionStatusAccepted,
		crossRoomLinkMicSessionStatusConnected:
		return true
	default:
		return false
	}
}

func (s *apiServer) handleLiveSignalingWebSocket(writer http.ResponseWriter, request *http.Request) {
	if request.Method != http.MethodGet {
		writeMethodNotAllowed(writer)
		return
	}
	if request.URL.Path != "/ws" {
		writeNotFound(writer)
		return
	}

	conn, err := liveSignalUpgrader.Upgrade(writer, request, nil)
	if err != nil {
		log.Printf("live signaling upgrade failed: %v", err)
		return
	}

	client := &liveSignalClient{
		hub:    s.liveSignals,
		server: s,
		conn:   conn,
		send:   make(chan []byte, 32),
	}

	log.Printf("live signaling connected: remote=%s", request.RemoteAddr)
	go client.writePump()
	client.readPump()
}

func (c *liveSignalClient) readPump() {
	defer func() {
		c.hub.unregister(c)
		_ = c.conn.Close()
	}()

	c.conn.SetReadLimit(liveSignalMaxMessageSize)
	_ = c.conn.SetReadDeadline(time.Now().Add(liveSignalPongWait))
	c.conn.SetPongHandler(func(string) error {
		return c.conn.SetReadDeadline(time.Now().Add(liveSignalPongWait))
	})

	for {
		messageType, data, err := c.conn.ReadMessage()
		if err != nil {
			if websocket.IsUnexpectedCloseError(err, websocket.CloseGoingAway, websocket.CloseAbnormalClosure) {
				log.Printf("live signaling read failed: peer_id=%s room_id=%s err=%v", c.peerID, c.roomID, err)
			}
			return
		}
		if messageType != websocket.TextMessage {
			c.sendError("", "only text websocket messages are supported")
			continue
		}

		if err := c.handleMessage(data); err != nil {
			log.Printf("live signaling message rejected: peer_id=%s room_id=%s err=%v", c.peerID, c.roomID, err)
			c.sendError(c.roomID, err.Error())
		}
	}
}

func (c *liveSignalClient) writePump() {
	ticker := time.NewTicker(liveSignalPingPeriod)
	defer func() {
		ticker.Stop()
		_ = c.conn.Close()
	}()

	for {
		select {
		case message, ok := <-c.send:
			_ = c.conn.SetWriteDeadline(time.Now().Add(liveSignalWriteWait))
			if !ok {
				_ = c.conn.WriteMessage(websocket.CloseMessage, nil)
				return
			}

			if err := c.conn.WriteMessage(websocket.TextMessage, message); err != nil {
				return
			}
		case <-ticker.C:
			_ = c.conn.SetWriteDeadline(time.Now().Add(liveSignalWriteWait))
			if err := c.conn.WriteMessage(websocket.PingMessage, nil); err != nil {
				return
			}
		}
	}
}

func (c *liveSignalClient) handleMessage(data []byte) error {
	var envelope liveSignalEnvelope
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
		if c.isAnchorBound() || envelope.isCrossRoomMessage() {
			return c.handleCrossRoomRTCRelay(envelope, data)
		}
		return c.handleRTCRelay(envelope, data)
	case linkMicTypeApply, linkMicTypeInvite, linkMicTypeAccept, linkMicTypeReject, linkMicTypeCancel, linkMicTypeHangup, linkMicTypeKick:
		if envelope.Type != linkMicTypeApply && (c.isAnchorBound() || envelope.isCrossRoomMessage()) {
			return c.handleCrossRoomLinkMicSignal(envelope, data)
		}
		return c.handleLinkMicSignal(envelope, data)
	case linkMicTypeConnected:
		if c.isAnchorBound() || envelope.isCrossRoomMessage() {
			return c.handleCrossRoomLinkMicSignal(envelope, data)
		}
		return fmt.Errorf("linkmic.connected is only supported for cross-room anchor sessions")
	default:
		return fmt.Errorf("unsupported message type: %s", envelope.Type)
	}
}

func (c *liveSignalClient) handleRegister(envelope liveSignalEnvelope) error {
	roomID := strings.TrimSpace(envelope.RoomID)
	peerID := strings.TrimSpace(envelope.PeerID)
	role := strings.TrimSpace(envelope.Role)
	mode := strings.TrimSpace(envelope.Mode)
	requestID := strings.TrimSpace(envelope.requestID())

	if roomID == "" {
		return fmt.Errorf("signaling register requires room_id")
	}
	if peerID == "" {
		return fmt.Errorf("signaling register requires peer_id")
	}
	if role == "" {
		return fmt.Errorf("signaling register requires role")
	}
	if c.peerID != "" {
		return fmt.Errorf("signaling client already registered")
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
		replacedPeer.sendError(roomID, "peer_id replaced by a newer session")
		_ = replacedPeer.conn.Close()
	}

	c.sendJSON(liveSignalRegisteredMessage{
		Type:   "probe.registered",
		RoomID: roomID,
		PeerID: peerID,
		Role:   role,
		TsMs:   nowUnixMilli(),
	})

	c.hub.broadcastRoomMemberEvent(roomID, "room.member-join", liveSignalPeer{
		PeerID: peerID,
		Role:   role,
	})
	c.hub.broadcastRoomMemberList(roomID)

	log.Printf("live signaling registered: room_id=%s peer_id=%s role=%s mode=%s request_id=%s", roomID, peerID, role, mode, requestID)
	return nil
}

func (c *liveSignalClient) handleRTCRelay(envelope liveSignalEnvelope, rawMessage []byte) error {
	if c.peerID == "" || c.roomID == "" {
		return fmt.Errorf("signaling client must register before rtc signaling")
	}
	if strings.TrimSpace(envelope.RoomID) != c.roomID {
		return fmt.Errorf("%s room_id mismatch", envelope.Type)
	}
	if strings.TrimSpace(envelope.fromUserID()) != c.peerID {
		return fmt.Errorf("%s from_user_id mismatch", envelope.Type)
	}

	targetID := strings.TrimSpace(envelope.toUserID())
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

func (c *liveSignalClient) handleLinkMicSignal(envelope liveSignalEnvelope, rawMessage []byte) error {
	if c.peerID == "" || c.roomID == "" {
		return fmt.Errorf("signaling client must register before linkmic signaling")
	}
	if strings.TrimSpace(envelope.RoomID) != c.roomID {
		return fmt.Errorf("%s room_id mismatch", envelope.Type)
	}
	if strings.TrimSpace(envelope.fromUserID()) != c.peerID {
		return fmt.Errorf("%s from_user_id mismatch", envelope.Type)
	}
	if strings.TrimSpace(envelope.requestID()) == "" {
		return fmt.Errorf("%s requires request_id", envelope.Type)
	}

	return c.hub.handleLinkMicSignal(c, envelope, rawMessage)
}

func parseCrossRoomUserID(fieldName, rawValue string) (int64, error) {
	value := strings.TrimSpace(rawValue)
	if value == "" {
		return 0, fmt.Errorf("%s is required", fieldName)
	}

	userID, err := strconv.ParseInt(value, 10, 64)
	if err != nil || userID <= 0 {
		return 0, fmt.Errorf("%s must be a positive integer", fieldName)
	}
	return userID, nil
}

func (c *liveSignalClient) validateCrossRoomEnvelopeDirection(envelope liveSignalEnvelope) (string, int64, string, int64, error) {
	if !c.isAnchorBound() {
		return "", 0, "", 0, fmt.Errorf("live.anchor.join is required before cross-room signaling")
	}

	sessionID := envelope.sessionID()
	if sessionID == "" {
		return "", 0, "", 0, fmt.Errorf("%s requires sessionId", envelope.Type)
	}

	requestID := envelope.requestID()
	if requestID == "" {
		return "", 0, "", 0, fmt.Errorf("%s requires requestId", envelope.Type)
	}

	fromRoomKey := envelope.fromRoomKey()
	if fromRoomKey == "" {
		return "", 0, "", 0, fmt.Errorf("%s requires fromRoomKey", envelope.Type)
	}
	if fromRoomKey != normalizeLiveRoomKey(c.anchorRoomKey) {
		return "", 0, "", 0, fmt.Errorf("%s fromRoomKey mismatch", envelope.Type)
	}

	fromUserID, err := parseCrossRoomUserID("fromUserId", envelope.fromUserID())
	if err != nil {
		return "", 0, "", 0, fmt.Errorf("%s %w", envelope.Type, err)
	}
	if fromUserID != c.anchorUserID {
		return "", 0, "", 0, fmt.Errorf("%s fromUserId mismatch", envelope.Type)
	}

	toRoomKey := envelope.toRoomKey()
	if toRoomKey == "" {
		return "", 0, "", 0, fmt.Errorf("%s requires toRoomKey", envelope.Type)
	}

	toUserID, err := parseCrossRoomUserID("toUserId", envelope.toUserID())
	if err != nil {
		return "", 0, "", 0, fmt.Errorf("%s %w", envelope.Type, err)
	}
	if toRoomKey == fromRoomKey && toUserID == fromUserID {
		return "", 0, "", 0, fmt.Errorf("%s target must be another anchor", envelope.Type)
	}

	return fromRoomKey, fromUserID, toRoomKey, toUserID, nil
}

func (c *liveSignalClient) handleCrossRoomRTCRelay(envelope liveSignalEnvelope, rawMessage []byte) error {
	fromRoomKey, fromUserID, toRoomKey, toUserID, err := c.validateCrossRoomEnvelopeDirection(envelope)
	if err != nil {
		return err
	}

	return c.hub.handleCrossRoomRTCSignal(c, envelope, rawMessage, fromRoomKey, fromUserID, toRoomKey, toUserID)
}

func (c *liveSignalClient) handleCrossRoomLinkMicSignal(envelope liveSignalEnvelope, rawMessage []byte) error {
	if envelope.Type == linkMicTypeApply {
		return fmt.Errorf("cross-room anchor signaling does not support linkmic.apply")
	}

	fromRoomKey, fromUserID, toRoomKey, toUserID, err := c.validateCrossRoomEnvelopeDirection(envelope)
	if err != nil {
		return err
	}

	return c.hub.handleCrossRoomLinkMicSignal(c, envelope, rawMessage, fromRoomKey, fromUserID, toRoomKey, toUserID)
}

func (h *liveSignalHub) register(client *liveSignalClient, roomID, peerID, role, mode, requestID string) *liveSignalClient {
	h.mu.Lock()
	defer h.mu.Unlock()

	room := h.rooms[roomID]
	if room == nil {
		room = &liveSignalRoom{
			peers: make(map[string]*liveSignalClient),
		}
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

func (h *liveSignalHub) unregister(client *liveSignalClient) {
	if client.roomID != "" && client.peerID != "" {
		roomID := client.roomID
		leavingPeer := liveSignalPeer{
			PeerID: client.peerID,
			Role:   client.role,
		}
		var recipients []*liveSignalClient
		var peers []liveSignalPeer
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
			log.Printf("live signaling disconnected: room_id=%s peer_id=%s", roomID, client.peerID)
		}
	}

	anchorRoomKey, anchorUserID, anchorRemoved := h.unbindAnchorClient(client)
	if anchorRemoved {
		outbound := h.closeCrossRoomSessionsForAnchor(anchorRoomKey, anchorUserID, "anchor-disconnected")
		for _, message := range outbound {
			message.client.sendRaw(message.message)
		}

		ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
		defer cancel()

		if err := markAnchorOfflineByRoomKey(ctx, h.database, anchorRoomKey, anchorUserID); err != nil {
			log.Printf("anchor disconnect cleanup failed: roomKey=%s userId=%d err=%v", anchorRoomKey, anchorUserID, err)
		}
		h.broadcastAnchorMemberList(anchorRoomKey)
		log.Printf("[presence] anchor disconnected roomKey=%s userId=%d", anchorRoomKey, anchorUserID)
	}
}

func (runtime *crossRoomSessionRuntime) hasAnchor(roomKey string, userID int64) bool {
	roomKey = normalizeLiveRoomKey(roomKey)
	if roomKey == normalizeLiveRoomKey(runtime.session.RoomAKey) && userID == runtime.session.AnchorAUserID {
		return true
	}
	if roomKey == normalizeLiveRoomKey(runtime.session.RoomBKey) && userID == runtime.session.AnchorBUserID {
		return true
	}
	return false
}

func (runtime *crossRoomSessionRuntime) peerOf(roomKey string, userID int64) (string, int64, bool) {
	roomKey = normalizeLiveRoomKey(roomKey)
	if roomKey == normalizeLiveRoomKey(runtime.session.RoomAKey) && userID == runtime.session.AnchorAUserID {
		return runtime.session.RoomBKey, runtime.session.AnchorBUserID, true
	}
	if roomKey == normalizeLiveRoomKey(runtime.session.RoomBKey) && userID == runtime.session.AnchorBUserID {
		return runtime.session.RoomAKey, runtime.session.AnchorAUserID, true
	}
	return "", 0, false
}

func (runtime *crossRoomSessionRuntime) businessRoleFor(userID int64) string {
	if userID == runtime.session.InviterUserID {
		return "inviter"
	}
	return "invitee"
}

func (runtime *crossRoomSessionRuntime) rtcRoleFor(userID int64) string {
	if userID == runtime.session.InviterUserID {
		return "offerer"
	}
	return "answerer"
}

func (runtime *crossRoomSessionRuntime) setStatus(status string) {
	runtime.session.Status = status
	runtime.session.UpdatedAt = time.Now().UTC()
}

func (h *liveSignalHub) findAnchorClientLocked(roomKey string, userID int64) *liveSignalClient {
	room := h.anchorRooms[normalizeLiveRoomKey(roomKey)]
	if room == nil {
		return nil
	}
	return room[userID]
}

func (h *liveSignalHub) findActiveCrossRoomSessionForAnchorLocked(roomKey string, userID int64, excludeSessionID string) *crossRoomSessionRuntime {
	for sessionID, runtime := range h.crossRoomSessions {
		if sessionID == excludeSessionID {
			continue
		}
		if !isCrossRoomSessionActiveStatus(runtime.session.Status) {
			continue
		}
		if runtime.hasAnchor(roomKey, userID) {
			return runtime
		}
	}
	return nil
}

func (h *liveSignalHub) requireCrossRoomSessionLocked(envelope liveSignalEnvelope) (*crossRoomSessionRuntime, error) {
	sessionID := envelope.sessionID()
	if sessionID == "" {
		return nil, fmt.Errorf("%s requires sessionId", envelope.Type)
	}

	requestID := envelope.requestID()
	if requestID == "" {
		return nil, fmt.Errorf("%s requires requestId", envelope.Type)
	}

	runtime := h.crossRoomSessions[sessionID]
	if runtime == nil {
		return nil, fmt.Errorf("cross-room session not found: %s", sessionID)
	}
	if strings.TrimSpace(runtime.session.RequestID) != "" && runtime.session.RequestID != requestID {
		return nil, fmt.Errorf("%s requestId mismatch", envelope.Type)
	}
	return runtime, nil
}

func buildCrossRoomEnvelopePayload(messageType string,
	session CrossRoomLinkMicSession,
	fromRoomKey, toRoomKey string,
	fromUserID, toUserID int64,
	payload any) ([]byte, error) {
	encodedPayload, err := json.Marshal(payload)
	if err != nil {
		return nil, err
	}

	tsMs := nowUnixMilli()
	envelope := map[string]any{
		"type":          messageType,
		"sessionId":     session.SessionID,
		"session_id":    session.SessionID,
		"fromRoomKey":   normalizeLiveRoomKey(fromRoomKey),
		"from_room_key": normalizeLiveRoomKey(fromRoomKey),
		"toRoomKey":     normalizeLiveRoomKey(toRoomKey),
		"to_room_key":   normalizeLiveRoomKey(toRoomKey),
		"fromUserId":    strconv.FormatInt(fromUserID, 10),
		"from_user_id":  strconv.FormatInt(fromUserID, 10),
		"toUserId":      strconv.FormatInt(toUserID, 10),
		"to_user_id":    strconv.FormatInt(toUserID, 10),
		"requestId":     session.RequestID,
		"request_id":    session.RequestID,
		"payload":       json.RawMessage(encodedPayload),
		"tsMs":          tsMs,
		"ts_ms":         tsMs,
	}
	return json.Marshal(envelope)
}

func (h *liveSignalHub) buildCrossRoomJoinParamsMessagesLocked(runtime *crossRoomSessionRuntime) []outboundMessage {
	session := runtime.session
	recipients := []struct {
		roomKey string
		userID  int64
		peerKey string
		peerID  int64
	}{
		{roomKey: session.RoomAKey, userID: session.AnchorAUserID, peerKey: session.RoomBKey, peerID: session.AnchorBUserID},
		{roomKey: session.RoomBKey, userID: session.AnchorBUserID, peerKey: session.RoomAKey, peerID: session.AnchorAUserID},
	}

	messages := make([]outboundMessage, 0, len(recipients))
	for _, recipient := range recipients {
		client := h.findAnchorClientLocked(recipient.roomKey, recipient.userID)
		if client == nil {
			continue
		}

		joinParams := buildCrossRoomRTCJoinParams(session, recipient.roomKey, recipient.userID, h.publicSignalingURL)
		if joinParams == nil {
			continue
		}

		encoded, err := buildCrossRoomEnvelopePayload(
			"rtc.join-params",
			session,
			recipient.peerKey,
			recipient.roomKey,
			recipient.peerID,
			recipient.userID,
			joinParams,
		)
		if err != nil {
			log.Printf("marshal cross-room rtc.join-params failed: session_id=%s user_id=%d err=%v", session.SessionID, recipient.userID, err)
			continue
		}
		messages = append(messages, outboundMessage{client: client, message: encoded})
	}
	return messages
}

func (h *liveSignalHub) closeCrossRoomSessionsForAnchor(roomKey string, userID int64, reason string) []outboundMessage {
	roomKey = normalizeLiveRoomKey(roomKey)
	if roomKey == "" || userID <= 0 {
		return nil
	}

	h.mu.Lock()
	defer h.mu.Unlock()

	messages := make([]outboundMessage, 0)
	for _, runtime := range h.crossRoomSessions {
		if !isCrossRoomSessionActiveStatus(runtime.session.Status) || !runtime.hasAnchor(roomKey, userID) {
			continue
		}

		runtime.setStatus(crossRoomLinkMicSessionStatusEnded)
		peerRoomKey, peerUserID, ok := runtime.peerOf(roomKey, userID)
		if !ok {
			continue
		}

		peerClient := h.findAnchorClientLocked(peerRoomKey, peerUserID)
		if peerClient == nil {
			continue
		}

		encoded, err := buildCrossRoomEnvelopePayload(
			linkMicTypeHangup,
			runtime.session,
			roomKey,
			peerRoomKey,
			userID,
			peerUserID,
			map[string]any{
				"reason": reason,
				"status": crossRoomLinkMicSessionStatusEnded,
			},
		)
		if err != nil {
			log.Printf("marshal cross-room disconnect hangup failed: session_id=%s room_key=%s user_id=%d err=%v", runtime.session.SessionID, roomKey, userID, err)
			continue
		}
		messages = append(messages, outboundMessage{client: peerClient, message: encoded})
	}
	return messages
}

func (h *liveSignalHub) handleCrossRoomRTCSignal(_ *liveSignalClient,
	envelope liveSignalEnvelope,
	rawMessage []byte,
	fromRoomKey string,
	fromUserID int64,
	toRoomKey string,
	toUserID int64) error {
	h.mu.Lock()
	runtime, err := h.requireCrossRoomSessionLocked(envelope)
	if err != nil {
		h.mu.Unlock()
		return err
	}
	if !isCrossRoomSessionRelayReady(runtime.session.Status) {
		h.mu.Unlock()
		return fmt.Errorf("%s requires an accepted cross-room session", envelope.Type)
	}

	expectedRoomKey, expectedUserID, ok := runtime.peerOf(fromRoomKey, fromUserID)
	if !ok {
		h.mu.Unlock()
		return fmt.Errorf("%s sender does not belong to session %s", envelope.Type, runtime.session.SessionID)
	}
	if normalizeLiveRoomKey(expectedRoomKey) != normalizeLiveRoomKey(toRoomKey) || expectedUserID != toUserID {
		h.mu.Unlock()
		return fmt.Errorf("%s target does not match cross-room session peer", envelope.Type)
	}

	target := h.findAnchorClientLocked(expectedRoomKey, expectedUserID)
	if target == nil {
		h.mu.Unlock()
		return fmt.Errorf("target anchor is offline: roomKey=%s userId=%d", expectedRoomKey, expectedUserID)
	}
	h.mu.Unlock()

	target.sendRaw(cloneBytes(rawMessage))
	log.Printf("cross-room rtc relayed: type=%s session_id=%s from=%s/%d to=%s/%d",
		envelope.Type,
		runtime.session.SessionID,
		fromRoomKey,
		fromUserID,
		toRoomKey,
		toUserID,
	)
	return nil
}

func (h *liveSignalHub) handleCrossRoomLinkMicSignal(_ *liveSignalClient,
	envelope liveSignalEnvelope,
	rawMessage []byte,
	fromRoomKey string,
	fromUserID int64,
	toRoomKey string,
	toUserID int64) error {
	h.mu.Lock()
	defer h.mu.Unlock()

	switch envelope.Type {
	case linkMicTypeInvite:
		target := h.findAnchorClientLocked(toRoomKey, toUserID)
		if target == nil {
			return fmt.Errorf("target anchor is offline: roomKey=%s userId=%d", toRoomKey, toUserID)
		}
		if active := h.findActiveCrossRoomSessionForAnchorLocked(fromRoomKey, fromUserID, ""); active != nil {
			return fmt.Errorf("inviter anchor already has an active cross-room session: %s", active.session.SessionID)
		}
		if active := h.findActiveCrossRoomSessionForAnchorLocked(toRoomKey, toUserID, ""); active != nil {
			return fmt.Errorf("invitee anchor already has an active cross-room session: %s", active.session.SessionID)
		}
		if existing := h.crossRoomSessions[envelope.sessionID()]; existing != nil && isCrossRoomSessionActiveStatus(existing.session.Status) {
			return fmt.Errorf("cross-room session already exists: %s", envelope.sessionID())
		}

		now := time.Now().UTC()
		h.crossRoomSessions[envelope.sessionID()] = &crossRoomSessionRuntime{
			session: CrossRoomLinkMicSession{
				SessionID:     envelope.sessionID(),
				RequestID:     envelope.requestID(),
				RoomAKey:      fromRoomKey,
				RoomBKey:      toRoomKey,
				AnchorAUserID: fromUserID,
				AnchorBUserID: toUserID,
				InviterUserID: fromUserID,
				InviteeUserID: toUserID,
				Status:        crossRoomLinkMicSessionStatusPending,
				CreatedAt:     now,
				UpdatedAt:     now,
			},
			connectedAnchorKey: make(map[string]bool),
		}

		target.sendRaw(cloneBytes(rawMessage))
		log.Printf("cross-room invite created: session_id=%s request_id=%s inviter=%s/%d invitee=%s/%d",
			envelope.sessionID(),
			envelope.requestID(),
			fromRoomKey,
			fromUserID,
			toRoomKey,
			toUserID,
		)
		return nil
	}

	runtime, err := h.requireCrossRoomSessionLocked(envelope)
	if err != nil {
		return err
	}

	expectedRoomKey, expectedUserID, ok := runtime.peerOf(fromRoomKey, fromUserID)
	if !ok {
		return fmt.Errorf("%s sender does not belong to session %s", envelope.Type, runtime.session.SessionID)
	}
	if normalizeLiveRoomKey(expectedRoomKey) != normalizeLiveRoomKey(toRoomKey) || expectedUserID != toUserID {
		return fmt.Errorf("%s target does not match cross-room session peer", envelope.Type)
	}

	target := h.findAnchorClientLocked(expectedRoomKey, expectedUserID)
	outbound := make([]outboundMessage, 0, 3)
	sendRawToTarget := func() {
		if target != nil {
			outbound = append(outbound, outboundMessage{client: target, message: cloneBytes(rawMessage)})
		}
	}

	switch envelope.Type {
	case linkMicTypeAccept:
		if runtime.session.Status != crossRoomLinkMicSessionStatusPending {
			return fmt.Errorf("linkmic.accept requires a pending cross-room session")
		}
		if fromUserID != runtime.session.InviteeUserID || toUserID != runtime.session.InviterUserID {
			return fmt.Errorf("linkmic.accept direction does not match the pending invite")
		}
		if target == nil {
			return fmt.Errorf("inviter anchor is offline: roomKey=%s userId=%d", expectedRoomKey, expectedUserID)
		}
		runtime.setStatus(crossRoomLinkMicSessionStatusAccepted)
		sendRawToTarget()
		outbound = append(outbound, h.buildCrossRoomJoinParamsMessagesLocked(runtime)...)
	case linkMicTypeReject:
		if runtime.session.Status != crossRoomLinkMicSessionStatusPending {
			return fmt.Errorf("linkmic.reject requires a pending cross-room session")
		}
		if fromUserID != runtime.session.InviteeUserID || toUserID != runtime.session.InviterUserID {
			return fmt.Errorf("linkmic.reject direction does not match the pending invite")
		}
		runtime.setStatus(crossRoomLinkMicSessionStatusRejected)
		sendRawToTarget()
	case linkMicTypeCancel:
		if runtime.session.Status != crossRoomLinkMicSessionStatusPending {
			return fmt.Errorf("linkmic.cancel requires a pending cross-room session")
		}
		if fromUserID != runtime.session.InviterUserID || toUserID != runtime.session.InviteeUserID {
			return fmt.Errorf("linkmic.cancel must be sent by the inviter")
		}
		runtime.setStatus(crossRoomLinkMicSessionStatusCancelled)
		sendRawToTarget()
	case linkMicTypeHangup:
		if !isCrossRoomSessionRelayReady(runtime.session.Status) {
			return fmt.Errorf("linkmic.hangup requires an accepted or connected cross-room session")
		}
		runtime.setStatus(crossRoomLinkMicSessionStatusEnded)
		sendRawToTarget()
	case linkMicTypeKick:
		if !isCrossRoomSessionRelayReady(runtime.session.Status) {
			return fmt.Errorf("linkmic.kick requires an accepted or connected cross-room session")
		}
		if fromUserID != runtime.session.InviterUserID {
			return fmt.Errorf("linkmic.kick must be sent by the inviter anchor")
		}
		runtime.setStatus(crossRoomLinkMicSessionStatusEnded)
		sendRawToTarget()
	case linkMicTypeConnected:
		if !isCrossRoomSessionRelayReady(runtime.session.Status) {
			return fmt.Errorf("linkmic.connected requires an accepted cross-room session")
		}
		runtime.connectedAnchorKey[crossRoomAnchorKey(fromRoomKey, fromUserID)] = true
		runtime.setStatus(crossRoomLinkMicSessionStatusConnected)
		sendRawToTarget()
	default:
		return fmt.Errorf("unsupported cross-room linkmic signal: %s", envelope.Type)
	}

	for _, message := range outbound {
		message.client.sendRaw(message.message)
	}

	log.Printf("cross-room signal handled: type=%s session_id=%s request_id=%s from=%s/%d to=%s/%d status=%s",
		envelope.Type,
		runtime.session.SessionID,
		runtime.session.RequestID,
		fromRoomKey,
		fromUserID,
		toRoomKey,
		toUserID,
		runtime.session.Status,
	)
	return nil
}

func (h *liveSignalHub) handleLinkMicSignal(sender *liveSignalClient, envelope liveSignalEnvelope, rawMessage []byte) error {
	targetID := strings.TrimSpace(envelope.toUserID())
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

	var outbound []outboundMessage
	var err error

	switch envelope.Type {
	case linkMicTypeApply:
		outbound, err = h.handleLinkMicApplyLocked(room, sender, target, envelope, rawMessage)
	case linkMicTypeInvite:
		outbound, err = h.handleLinkMicInviteLocked(room, sender, target, envelope, rawMessage)
	case linkMicTypeAccept:
		outbound, err = h.handleLinkMicAcceptLocked(room, sender, target, envelope, rawMessage)
	case linkMicTypeReject:
		outbound, err = h.handleLinkMicRejectLocked(room, sender, target, envelope, rawMessage)
	case linkMicTypeCancel:
		outbound, err = h.handleLinkMicCancelLocked(room, sender, target, envelope, rawMessage)
	case linkMicTypeHangup:
		outbound, err = h.handleLinkMicHangupLocked(room, sender, target, envelope, rawMessage)
	case linkMicTypeKick:
		outbound, err = h.handleLinkMicKickLocked(room, sender, target, envelope, rawMessage)
	default:
		err = fmt.Errorf("unsupported message type: %s", envelope.Type)
	}
	h.mu.Unlock()

	if err != nil {
		return err
	}

	for _, message := range outbound {
		message.client.sendRaw(message.message)
	}

	log.Printf("live signaling handled: room_id=%s type=%s request_id=%s from=%s to=%s",
		sender.roomID, envelope.Type, envelope.requestID(), sender.peerID, targetID)
	return nil
}

func (h *liveSignalHub) handleLinkMicApplyLocked(room *liveSignalRoom, sender, target *liveSignalClient, envelope liveSignalEnvelope, rawMessage []byte) ([]outboundMessage, error) {
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
	room.session = &liveSignalLinkMicSession{
		RequestID:     envelope.requestID(),
		RequestType:   envelope.Type,
		ControllerID:  target.peerID,
		ParticipantID: sender.peerID,
		State:         linkMicStateApplying,
		CreatedAtMs:   now,
		UpdatedAtMs:   now,
	}

	outbound := []outboundMessage{
		{client: target, message: cloneBytes(rawMessage)},
	}
	outbound = append(outbound, buildStateSyncMessages(room, sender.roomID, *room.session, "pending-request-created")...)
	return outbound, nil
}

func (h *liveSignalHub) handleLinkMicInviteLocked(room *liveSignalRoom, sender, target *liveSignalClient, envelope liveSignalEnvelope, rawMessage []byte) ([]outboundMessage, error) {
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
	room.session = &liveSignalLinkMicSession{
		RequestID:     envelope.requestID(),
		RequestType:   envelope.Type,
		ControllerID:  sender.peerID,
		ParticipantID: target.peerID,
		State:         linkMicStateInviting,
		CreatedAtMs:   now,
		UpdatedAtMs:   now,
	}

	outbound := []outboundMessage{
		{client: target, message: cloneBytes(rawMessage)},
	}
	outbound = append(outbound, buildStateSyncMessages(room, sender.roomID, *room.session, "pending-request-created")...)
	return outbound, nil
}

func (h *liveSignalHub) handleLinkMicAcceptLocked(room *liveSignalRoom, sender, target *liveSignalClient, envelope liveSignalEnvelope, rawMessage []byte) ([]outboundMessage, error) {
	session, err := requireMatchingSession(room.session, envelope.requestID())
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
	session.UpdatedAtMs = nowUnixMilli()

	outbound := []outboundMessage{
		{client: target, message: cloneBytes(rawMessage)},
	}
	outbound = append(outbound, buildRTCJoinParamsMessages(room, sender.roomID, *session)...)
	outbound = append(outbound, buildLinkMicConnectedMessages(room, sender.roomID, *session)...)
	outbound = append(outbound, buildStateSyncMessages(room, sender.roomID, *session, "linkmic-accepted")...)
	return outbound, nil
}

func (h *liveSignalHub) handleLinkMicRejectLocked(room *liveSignalRoom, sender, target *liveSignalClient, envelope liveSignalEnvelope, rawMessage []byte) ([]outboundMessage, error) {
	session, err := requireMatchingSession(room.session, envelope.requestID())
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
	outbound = append(outbound, buildResetStateMessages(room, sender.roomID, *session, "linkmic-rejected")...)
	room.session = nil
	return outbound, nil
}

func (h *liveSignalHub) handleLinkMicCancelLocked(room *liveSignalRoom, sender, target *liveSignalClient, envelope liveSignalEnvelope, rawMessage []byte) ([]outboundMessage, error) {
	session, err := requireMatchingSession(room.session, envelope.requestID())
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
	outbound = append(outbound, buildResetStateMessages(room, sender.roomID, *session, "linkmic-cancelled")...)
	room.session = nil
	return outbound, nil
}

func (h *liveSignalHub) handleLinkMicHangupLocked(room *liveSignalRoom, sender, target *liveSignalClient, envelope liveSignalEnvelope, rawMessage []byte) ([]outboundMessage, error) {
	session, err := requireMatchingSession(room.session, envelope.requestID())
	if err != nil {
		return nil, err
	}
	if !sessionHasPeer(*session, sender.peerID) || !sessionHasPeer(*session, target.peerID) {
		return nil, fmt.Errorf("linkmic.hangup peers do not match the active session")
	}

	outbound := []outboundMessage{
		{client: target, message: cloneBytes(rawMessage)},
	}
	outbound = append(outbound, buildResetStateMessages(room, sender.roomID, *session, "linkmic-hangup")...)
	room.session = nil
	return outbound, nil
}

func (h *liveSignalHub) handleLinkMicKickLocked(room *liveSignalRoom, sender, target *liveSignalClient, envelope liveSignalEnvelope, rawMessage []byte) ([]outboundMessage, error) {
	session, err := requireMatchingSession(room.session, envelope.requestID())
	if err != nil {
		return nil, err
	}
	if sender.peerID != session.ControllerID || target.peerID != session.ParticipantID {
		return nil, fmt.Errorf("linkmic.kick must be sent by controller to the participant")
	}

	outbound := []outboundMessage{
		{client: target, message: cloneBytes(rawMessage)},
	}
	outbound = append(outbound, buildResetStateMessages(room, sender.roomID, *session, "linkmic-kicked")...)
	room.session = nil
	return outbound, nil
}

func (h *liveSignalHub) buildDisconnectSessionMessagesLocked(roomID string, room *liveSignalRoom, leavingPeerID string) []outboundMessage {
	session := room.session
	if session == nil || !sessionHasPeer(*session, leavingPeerID) {
		return nil
	}

	otherPeerID := session.ControllerID
	if otherPeerID == leavingPeerID {
		otherPeerID = session.ParticipantID
	}
	otherClient := room.peers[otherPeerID]
	room.session = nil
	if otherClient == nil {
		return nil
	}

	hangupMessage, err := marshalEnvelope(liveSignalEnvelope{
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
		log.Printf("live signaling marshal disconnect hangup failed: room_id=%s peer_id=%s err=%v", roomID, leavingPeerID, err)
		return nil
	}

	outbound := []outboundMessage{
		{client: otherClient, message: hangupMessage},
	}
	outbound = append(outbound, buildResetStateMessages(room, roomID, *session, "peer-disconnected")...)
	return outbound
}

func (h *liveSignalHub) findPeer(roomID, peerID string) *liveSignalClient {
	h.mu.Lock()
	defer h.mu.Unlock()

	room := h.rooms[roomID]
	if room == nil {
		return nil
	}
	return room.peers[peerID]
}

func (h *liveSignalHub) broadcastRoomMemberEvent(roomID, eventType string, peer liveSignalPeer) {
	recipients, _, ok := h.snapshotRoom(roomID)
	if !ok {
		return
	}
	sendRoomMemberEvent(recipients, roomID, eventType, peer)
}

func (h *liveSignalHub) broadcastRoomMemberList(roomID string) {
	recipients, peers, _ := h.snapshotRoom(roomID)
	sendRoomMemberList(recipients, roomID, peers)
}

func (h *liveSignalHub) snapshotRoom(roomID string) ([]*liveSignalClient, []liveSignalPeer, bool) {
	h.mu.Lock()
	defer h.mu.Unlock()

	room := h.rooms[roomID]
	if room == nil {
		return nil, []liveSignalPeer{}, false
	}

	recipients, peers := snapshotRoom(room)
	return recipients, peers, true
}

func snapshotRoom(room *liveSignalRoom) ([]*liveSignalClient, []liveSignalPeer) {
	recipients := make([]*liveSignalClient, 0, len(room.peers))
	peers := make([]liveSignalPeer, 0, len(room.peers))
	for _, client := range room.peers {
		recipients = append(recipients, client)
		peers = append(peers, liveSignalPeer{
			PeerID: client.peerID,
			Role:   client.role,
		})
	}

	sort.Slice(peers, func(i, j int) bool {
		return peers[i].PeerID < peers[j].PeerID
	})

	return recipients, peers
}

func sendRoomMemberEvent(recipients []*liveSignalClient, roomID, eventType string, peer liveSignalPeer) {
	if len(recipients) == 0 {
		return
	}

	encoded, err := marshalEnvelope(liveSignalEnvelope{
		Type:   eventType,
		RoomID: roomID,
		Payload: mustMarshalRaw(map[string]any{
			"member": peer,
		}),
		TsMs: nowUnixMilli(),
	})
	if err != nil {
		log.Printf("live signaling marshal %s failed: room_id=%s err=%v", eventType, roomID, err)
		return
	}

	for _, client := range recipients {
		client.sendRaw(encoded)
	}
}

func sendRoomMemberList(recipients []*liveSignalClient, roomID string, peers []liveSignalPeer) {
	message := roomMemberListMessage{
		Type:   "room.member-list",
		RoomID: roomID,
		Peers:  peers,
		TsMs:   nowUnixMilli(),
	}

	encoded, err := json.Marshal(message)
	if err != nil {
		log.Printf("live signaling marshal room.member-list failed: room_id=%s err=%v", roomID, err)
		return
	}

	for _, client := range recipients {
		client.sendRaw(encoded)
	}
}

func requireMatchingSession(session *liveSignalLinkMicSession, requestID string) (*liveSignalLinkMicSession, error) {
	if session == nil {
		return nil, fmt.Errorf("no pending or active linkmic session")
	}
	if session.RequestID != requestID {
		return nil, fmt.Errorf("request_id mismatch")
	}
	return session, nil
}

func sessionEndpoints(session *liveSignalLinkMicSession) (initiatorID string, responderID string) {
	if session.RequestType == linkMicTypeApply {
		return session.ParticipantID, session.ControllerID
	}
	return session.ControllerID, session.ParticipantID
}

func sessionHasPeer(session liveSignalLinkMicSession, peerID string) bool {
	return session.ControllerID == peerID || session.ParticipantID == peerID
}

func buildStateSyncMessages(room *liveSignalRoom, roomID string, session liveSignalLinkMicSession, reason string) []outboundMessage {
	controller := room.peers[session.ControllerID]
	participant := room.peers[session.ParticipantID]
	if controller == nil && participant == nil {
		return nil
	}

	messages := make([]outboundMessage, 0, 2)
	if controller != nil {
		encoded, err := marshalEnvelope(liveSignalEnvelope{
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
			log.Printf("live signaling marshal state sync failed: room_id=%s peer_id=%s err=%v", roomID, session.ControllerID, err)
		}
	}
	if participant != nil {
		encoded, err := marshalEnvelope(liveSignalEnvelope{
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
			log.Printf("live signaling marshal state sync failed: room_id=%s peer_id=%s err=%v", roomID, session.ParticipantID, err)
		}
	}

	return messages
}

func buildResetStateMessages(room *liveSignalRoom, roomID string, session liveSignalLinkMicSession, reason string) []outboundMessage {
	controller := room.peers[session.ControllerID]
	participant := room.peers[session.ParticipantID]
	if controller == nil && participant == nil {
		return nil
	}

	messages := make([]outboundMessage, 0, 2)
	if controller != nil {
		encoded, err := marshalEnvelope(liveSignalEnvelope{
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
			log.Printf("live signaling marshal reset state failed: room_id=%s peer_id=%s err=%v", roomID, session.ControllerID, err)
		}
	}
	if participant != nil {
		encoded, err := marshalEnvelope(liveSignalEnvelope{
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
			log.Printf("live signaling marshal reset state failed: room_id=%s peer_id=%s err=%v", roomID, session.ParticipantID, err)
		}
	}

	return messages
}

func buildRTCJoinParamsMessages(room *liveSignalRoom, roomID string, session liveSignalLinkMicSession) []outboundMessage {
	controller := room.peers[session.ControllerID]
	participant := room.peers[session.ParticipantID]
	if controller == nil && participant == nil {
		return nil
	}

	messages := make([]outboundMessage, 0, 2)
	if controller != nil {
		encoded, err := marshalEnvelope(liveSignalEnvelope{
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
			log.Printf("live signaling marshal rtc.join-params failed: room_id=%s peer_id=%s err=%v", roomID, session.ControllerID, err)
		}
	}
	if participant != nil {
		encoded, err := marshalEnvelope(liveSignalEnvelope{
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
			log.Printf("live signaling marshal rtc.join-params failed: room_id=%s peer_id=%s err=%v", roomID, session.ParticipantID, err)
		}
	}

	return messages
}

func buildLinkMicConnectedMessages(room *liveSignalRoom, roomID string, session liveSignalLinkMicSession) []outboundMessage {
	controller := room.peers[session.ControllerID]
	participant := room.peers[session.ParticipantID]
	if controller == nil && participant == nil {
		return nil
	}

	messages := make([]outboundMessage, 0, 2)
	if controller != nil {
		encoded, err := marshalEnvelope(liveSignalEnvelope{
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
			log.Printf("live signaling marshal linkmic.connected failed: room_id=%s peer_id=%s err=%v", roomID, session.ControllerID, err)
		}
	}
	if participant != nil {
		encoded, err := marshalEnvelope(liveSignalEnvelope{
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
			log.Printf("live signaling marshal linkmic.connected failed: room_id=%s peer_id=%s err=%v", roomID, session.ParticipantID, err)
		}
	}

	return messages
}

func stateForPeer(session liveSignalLinkMicSession, peerID string) string {
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

func marshalEnvelope(envelope liveSignalEnvelope) ([]byte, error) {
	if envelope.TsMs == 0 {
		envelope.TsMs = nowUnixMilli()
	}
	return json.Marshal(envelope)
}

func mustMarshalRaw(payload any) json.RawMessage {
	encoded, err := json.Marshal(payload)
	if err != nil {
		log.Printf("live signaling marshal payload failed: %v", err)
		return nil
	}
	return encoded
}

func cloneBytes(source []byte) []byte {
	return append([]byte(nil), source...)
}

func validateBoundRTCRequest(sender, target *liveSignalClient, envelope liveSignalEnvelope) error {
	senderRequestID := strings.TrimSpace(sender.requestID)
	targetRequestID := strings.TrimSpace(target.requestID)
	if senderRequestID == "" && targetRequestID == "" {
		return nil
	}

	if senderRequestID == "" || targetRequestID == "" {
		return fmt.Errorf("%s requires both peers to bind the same request_id before RTC relay", envelope.Type)
	}

	messageRequestID := strings.TrimSpace(envelope.requestID())
	if messageRequestID == "" {
		return fmt.Errorf("%s requires request_id", envelope.Type)
	}
	if messageRequestID != senderRequestID || messageRequestID != targetRequestID {
		return fmt.Errorf("%s request_id mismatch", envelope.Type)
	}
	return nil
}

func (h *liveSignalHub) validateAcceptedRTCRegistration(ctx context.Context, roomID, peerID, role, requestID string) error {
	if h.database == nil {
		return fmt.Errorf("live signaling database is not configured")
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
		return fmt.Errorf("unsupported signaling role for request binding: %s", role)
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

func (c *liveSignalClient) sendError(roomID, message string) {
	c.sendJSON(signalErrorMessage{
		Type:    "signal.error",
		RoomID:  roomID,
		Message: message,
		TsMs:    nowUnixMilli(),
	})
}

func (c *liveSignalClient) sendJSON(payload any) {
	encoded, err := json.Marshal(payload)
	if err != nil {
		log.Printf("live signaling marshal failed: peer_id=%s room_id=%s err=%v", c.peerID, c.roomID, err)
		return
	}
	c.sendRaw(encoded)
}

func (c *liveSignalClient) sendRaw(message []byte) {
	select {
	case c.send <- message:
	default:
		log.Printf("live signaling send queue full: peer_id=%s room_id=%s", c.peerID, c.roomID)
	}
}

func nowUnixMilli() int64 {
	return time.Now().UnixMilli()
}
