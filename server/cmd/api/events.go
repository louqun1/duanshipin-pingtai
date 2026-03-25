package main

import (
	"context"
	"database/sql"
	"encoding/json"
	"errors"
	"fmt"
	"net"
	"net/http"
	"sync"
	"time"
)

type eventBroker struct {
	mu      sync.RWMutex
	clients map[chan string]struct{}
}

type videoUpdatedNotification struct {
	VideoID int64 `json:"videoId"`
}

func newEventBroker() *eventBroker {
	return &eventBroker{
		clients: make(map[chan string]struct{}),
	}
}

func (b *eventBroker) subscribe() chan string {
	client := make(chan string, 8)

	b.mu.Lock()
	b.clients[client] = struct{}{}
	b.mu.Unlock()

	return client
}

func (b *eventBroker) unsubscribe(client chan string) {
	b.mu.Lock()
	if _, exists := b.clients[client]; exists {
		delete(b.clients, client)
		close(client)
	}
	b.mu.Unlock()
}

func (b *eventBroker) publish(eventName string, payload any) error {
	if b == nil {
		return nil
	}

	data, err := json.Marshal(payload)
	if err != nil {
		return err
	}

	message := fmt.Sprintf("event: %s\ndata: %s\n\n", eventName, data)

	b.mu.RLock()
	defer b.mu.RUnlock()

	for client := range b.clients {
		select {
		case client <- message:
		default:
		}
	}

	return nil
}

func (s *apiServer) handleEvents(writer http.ResponseWriter, request *http.Request) {
	if request.Method != http.MethodGet {
		writeMethodNotAllowed(writer)
		return
	}
	if request.URL.Path != "/api/events" {
		writeNotFound(writer)
		return
	}

	flusher, ok := writer.(http.Flusher)
	if !ok {
		writeServerError(writer, errors.New("streaming unsupported"))
		return
	}

	writer.Header().Set("Content-Type", "text/event-stream")
	writer.Header().Set("Cache-Control", "no-cache")
	writer.Header().Set("Connection", "keep-alive")
	writer.Header().Set("X-Accel-Buffering", "no")

	client := s.events.subscribe()
	defer s.events.unsubscribe(client)

	if _, err := fmt.Fprint(writer, ": connected\n\n"); err != nil {
		return
	}
	flusher.Flush()

	keepaliveTicker := time.NewTicker(20 * time.Second)
	defer keepaliveTicker.Stop()

	for {
		select {
		case <-request.Context().Done():
			return
		case message, ok := <-client:
			if !ok {
				return
			}
			if _, err := fmt.Fprint(writer, message); err != nil {
				return
			}
			flusher.Flush()
		case <-keepaliveTicker.C:
			if _, err := fmt.Fprint(writer, ": keepalive\n\n"); err != nil {
				return
			}
			flusher.Flush()
		}
	}
}

func (s *apiServer) handleVideoUpdatedNotification(writer http.ResponseWriter, request *http.Request) {
	if request.Method != http.MethodPost {
		writeMethodNotAllowed(writer)
		return
	}
	if request.URL.Path != "/internal/events/video-updated" {
		writeNotFound(writer)
		return
	}
	if !isLoopbackRequest(request.RemoteAddr) {
		writeJSON(writer, http.StatusForbidden, map[string]string{"error": "forbidden"})
		return
	}

	var notification videoUpdatedNotification
	if err := json.NewDecoder(request.Body).Decode(&notification); err != nil {
		writeJSON(writer, http.StatusBadRequest, map[string]string{"error": "invalid event payload"})
		return
	}
	if notification.VideoID <= 0 {
		writeJSON(writer, http.StatusBadRequest, map[string]string{"error": "invalid video id"})
		return
	}

	ctx, cancel := context.WithTimeout(request.Context(), 5*time.Second)
	defer cancel()

	if err := s.publishVideoUpdated(ctx, notification.VideoID); err != nil {
		if errors.Is(err, sql.ErrNoRows) {
			writeNotFound(writer)
			return
		}
		writeServerError(writer, fmt.Errorf("publish video updated event: %w", err))
		return
	}

	writeJSON(writer, http.StatusAccepted, map[string]string{"status": "accepted"})
}

func (s *apiServer) publishVideoUpdated(ctx context.Context, videoID int64) error {
	row, err := s.findVideoByID(ctx, videoID)
	if err != nil {
		return err
	}

	return s.events.publish("video.updated", s.toVideoResponse(row, s.cfg.PublicBaseURL))
}

func isLoopbackRequest(remoteAddr string) bool {
	host, _, err := net.SplitHostPort(remoteAddr)
	if err != nil {
		host = remoteAddr
	}

	ip := net.ParseIP(host)
	return ip != nil && ip.IsLoopback()
}
