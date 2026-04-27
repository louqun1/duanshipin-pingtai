package main

import (
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"sort"
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
	PeerID     string          `json:"peer_id,omitempty"`
	Role       string          `json:"role,omitempty"`
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
	Type    string `json:"type"`
	RoomID  string `json:"room_id,omitempty"`
	Message string `json:"message"`
	TsMs    int64  `json:"ts_ms"`
}

type probeSignalHub struct {
	mu    sync.Mutex
	rooms map[string]map[string]*probeSignalClient
}

type probeSignalClient struct {
	hub    *probeSignalHub
	conn   *websocket.Conn
	send   chan []byte
	roomID string
	peerID string
	role   string
}

func newProbeSignalHub() *probeSignalHub {
	return &probeSignalHub{
		rooms: make(map[string]map[string]*probeSignalClient),
	}
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
		hub:  s.probeSignals,
		conn: conn,
		send: make(chan []byte, 32),
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
			c.sendError("", "only text websocket messages are supported")
			continue
		}

		if err := c.handleMessage(data); err != nil {
			log.Printf("probe signaling message rejected: peer_id=%s room_id=%s err=%v", c.peerID, c.roomID, err)
			c.sendError(c.roomID, err.Error())
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
	case "rtc.offer", "rtc.answer", "rtc.ice-candidate":
		return c.handleRTCRelay(envelope, data)
	default:
		return fmt.Errorf("unsupported message type: %s", envelope.Type)
	}
}

func (c *probeSignalClient) handleRegister(envelope probeSignalEnvelope) error {
	roomID := strings.TrimSpace(envelope.RoomID)
	peerID := strings.TrimSpace(envelope.PeerID)
	role := strings.TrimSpace(envelope.Role)

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

	replacedPeer := c.hub.register(c, roomID, peerID, role)
	if replacedPeer != nil {
		replacedPeer.sendError(roomID, "peer_id replaced by a newer session")
		_ = replacedPeer.conn.Close()
	}

	c.sendJSON(probeRegisteredMessage{
		Type:   "probe.registered",
		RoomID: roomID,
		PeerID: peerID,
		Role:   role,
		TsMs:   nowUnixMilli(),
	})

	c.hub.broadcastRoomMemberList(roomID)
	log.Printf("probe signaling registered: room_id=%s peer_id=%s role=%s", roomID, peerID, role)
	return nil
}

func (c *probeSignalClient) handleRTCRelay(envelope probeSignalEnvelope, rawMessage []byte) error {
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

	target.sendRaw(rawMessage)
	return nil
}

func (h *probeSignalHub) register(client *probeSignalClient, roomID, peerID, role string) *probeSignalClient {
	h.mu.Lock()
	defer h.mu.Unlock()

	room := h.rooms[roomID]
	if room == nil {
		room = make(map[string]*probeSignalClient)
		h.rooms[roomID] = room
	}

	replacedPeer := room[peerID]
	room[peerID] = client
	client.roomID = roomID
	client.peerID = peerID
	client.role = role
	return replacedPeer
}

func (h *probeSignalHub) unregister(client *probeSignalClient) {
	if client.roomID == "" || client.peerID == "" {
		return
	}

	roomID := client.roomID
	removed := false

	h.mu.Lock()
	room := h.rooms[roomID]
	if room != nil && room[client.peerID] == client {
		delete(room, client.peerID)
		removed = true
		if len(room) == 0 {
			delete(h.rooms, roomID)
		}
	}
	h.mu.Unlock()

	if removed {
		h.broadcastRoomMemberList(roomID)
		log.Printf("probe signaling disconnected: room_id=%s peer_id=%s", roomID, client.peerID)
	}
}

func (h *probeSignalHub) findPeer(roomID, peerID string) *probeSignalClient {
	h.mu.Lock()
	defer h.mu.Unlock()

	room := h.rooms[roomID]
	if room == nil {
		return nil
	}
	return room[peerID]
}

func (h *probeSignalHub) broadcastRoomMemberList(roomID string) {
	recipients, peers := h.snapshotRoom(roomID)
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

func (h *probeSignalHub) snapshotRoom(roomID string) ([]*probeSignalClient, []probeRoomPeer) {
	h.mu.Lock()
	defer h.mu.Unlock()

	room := h.rooms[roomID]
	if room == nil {
		return nil, []probeRoomPeer{}
	}

	recipients := make([]*probeSignalClient, 0, len(room))
	peers := make([]probeRoomPeer, 0, len(room))
	for _, client := range room {
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

func (c *probeSignalClient) sendError(roomID, message string) {
	c.sendJSON(signalErrorMessage{
		Type:    "signal.error",
		RoomID:  roomID,
		Message: message,
		TsMs:    nowUnixMilli(),
	})
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
