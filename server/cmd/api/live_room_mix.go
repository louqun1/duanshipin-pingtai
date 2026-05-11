package main

import (
	"context"
	"fmt"
	"log"
	"strings"
	"time"

	"vod-platform-server/internal/linkmicmix"
)

type liveRoomMixedStreamResponse struct {
	Active      bool   `json:"active"`
	State       string `json:"state"`
	SessionID   string `json:"sessionId,omitempty"`
	RequestID   string `json:"requestId,omitempty"`
	RoomKey     string `json:"roomKey,omitempty"`
	PeerRoomKey string `json:"peerRoomKey,omitempty"`
	StreamKey   string `json:"streamKey,omitempty"`
	PublishURL  string `json:"publishUrl,omitempty"`
	PlayURL     string `json:"playUrl,omitempty"`
	LastError   string `json:"lastError,omitempty"`
}

func (s *apiServer) buildLiveRoomMixedStreamResponse(roomKey string) *liveRoomMixedStreamResponse {
	roomKey = normalizeLiveRoomKey(roomKey)
	if roomKey == "" {
		return nil
	}

	response := &liveRoomMixedStreamResponse{
		Active:  false,
		State:   linkmicmix.StateStopped,
		RoomKey: roomKey,
	}

	if s.mixManager != nil {
		if output, err := s.mixManager.BuildRoomOutput(roomKey); err == nil {
			response.StreamKey = output.MixedStreamKey
			response.PublishURL = output.OutputURL
			response.PlayURL = output.PlaybackURL
		} else {
			response.State = linkmicmix.StateError
			response.LastError = err.Error()
		}

		if status, ok := s.mixManager.Query(roomKey); ok {
			applyLiveRoomMixedStreamStatus(response, status)
		}
	}

	if s.liveSignals == nil {
		return response
	}

	session, peerRoomKey, ok := s.liveSignals.findRelayReadyCrossRoomSession(roomKey)
	if !ok {
		return response
	}

	response.SessionID = session.SessionID
	response.RequestID = session.RequestID
	response.PeerRoomKey = peerRoomKey
	if response.State == "" || response.State == linkmicmix.StateStopped {
		response.State = linkmicmix.StateReady
	}
	response.Active = response.State == linkmicmix.StateRunning
	return response
}

func applyLiveRoomMixedStreamStatus(response *liveRoomMixedStreamResponse, status linkmicmix.Status) {
	if response == nil {
		return
	}

	if state := strings.TrimSpace(status.State); state != "" {
		response.State = state
	}
	if sessionID := strings.TrimSpace(status.SessionID); sessionID != "" {
		response.SessionID = sessionID
	}
	if requestID := strings.TrimSpace(status.RequestID); requestID != "" {
		response.RequestID = requestID
	}
	if roomKey := normalizeLiveRoomKey(status.RoomKey); roomKey != "" {
		response.RoomKey = roomKey
	}
	if peerRoomKey := normalizeLiveRoomKey(status.PeerRoomKey); peerRoomKey != "" {
		response.PeerRoomKey = peerRoomKey
	}
	if streamKey := strings.TrimSpace(status.MixedStreamKey); streamKey != "" {
		response.StreamKey = streamKey
	}
	if publishURL := strings.TrimSpace(status.OutputURL); publishURL != "" {
		response.PublishURL = publishURL
	}
	if playURL := strings.TrimSpace(status.PlaybackURL); playURL != "" {
		response.PlayURL = playURL
	}
	response.LastError = strings.TrimSpace(status.LastError)
	response.Active = response.State == linkmicmix.StateRunning
}

func (s *apiServer) startCrossRoomLinkMicMix(ctx context.Context, session CrossRoomLinkMicSession) error {
	if s.mixManager == nil || !s.mixManager.Enabled() {
		return nil
	}

	roomASpec, err := s.mixManager.BuildRoomSession(session.RoomAKey, session.RoomBKey, session.SessionID, session.RequestID)
	if err != nil {
		return err
	}
	roomBSpec, err := s.mixManager.BuildRoomSession(session.RoomBKey, session.RoomAKey, session.SessionID, session.RequestID)
	if err != nil {
		return err
	}

	if err := s.mixManager.Start(ctx, roomASpec); err != nil {
		return fmt.Errorf("start roomA mixed stream: %w", err)
	}
	if err := s.mixManager.Start(ctx, roomBSpec); err != nil {
		_ = s.mixManager.Stop(ctx, roomASpec.ProcessKey)
		return fmt.Errorf("start roomB mixed stream: %w", err)
	}
	return nil
}

func (s *apiServer) stopCrossRoomLinkMicMix(ctx context.Context, session CrossRoomLinkMicSession) error {
	if s.mixManager == nil || !s.mixManager.Enabled() {
		return nil
	}
	return s.mixManager.StopRooms(ctx, session.RoomAKey, session.RoomBKey)
}

func (s *apiServer) stopRoomMixedStream(ctx context.Context, roomKey string) error {
	if s.mixManager == nil || !s.mixManager.Enabled() {
		return nil
	}
	return s.mixManager.Stop(ctx, roomKey)
}

func (s *apiServer) closeCrossRoomMixForAnchor(actorUserID int64, roomKey, reason string) {
	if s == nil || s.liveSignals == nil {
		return
	}

	outbound, endedSessions := s.liveSignals.closeCrossRoomSessionsForAnchor(roomKey, actorUserID, reason)
	for _, message := range outbound {
		message.client.sendRaw(message.message)
	}
	for _, session := range endedSessions {
		s.syncCrossRoomMixOnSignal(linkMicTypeHangup, actorUserID, session)
	}
}

func (s *apiServer) publishCrossRoomLiveRoomEvent(action string, actorUserID int64, session CrossRoomLinkMicSession) {
	if s == nil {
		return
	}

	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()

	roomKeys := []string{
		normalizeLiveRoomKey(session.RoomAKey),
		normalizeLiveRoomKey(session.RoomBKey),
	}
	seen := make(map[string]struct{})
	for _, roomKey := range roomKeys {
		if roomKey == "" {
			continue
		}
		if _, ok := seen[roomKey]; ok {
			continue
		}
		seen[roomKey] = struct{}{}
		s.publishLiveRoomEvent(ctx, action, actorUserID, roomKey, session.RequestID)
	}
}

func (s *apiServer) syncCrossRoomMixOnSignal(action string, actorUserID int64, session CrossRoomLinkMicSession) {
	if s == nil {
		return
	}

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	switch action {
	case linkMicTypeAccept:
		if err := s.startCrossRoomLinkMicMix(ctx, session); err != nil {
			log.Printf("start cross-room linkmic mix failed: session_id=%s request_id=%s err=%v", session.SessionID, session.RequestID, err)
		}
	case linkMicTypeReject, linkMicTypeCancel, linkMicTypeHangup, linkMicTypeKick:
		if err := s.stopCrossRoomLinkMicMix(ctx, session); err != nil {
			log.Printf("stop cross-room linkmic mix failed: session_id=%s request_id=%s err=%v", session.SessionID, session.RequestID, err)
		}
	}

	s.publishCrossRoomLiveRoomEvent(action, actorUserID, session)
}
