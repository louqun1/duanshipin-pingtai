package main

import (
	"context"
	"database/sql"
	"encoding/json"
	"errors"
	"fmt"
	"log"
	"net/http"
	"net/url"
	"strconv"
	"strings"
	"time"

	"vod-platform-server/internal/config"
	"vod-platform-server/internal/db"
)

type videoRow struct {
	ID             int64
	Title          string
	Description    sql.NullString
	Status         string
	CoverObjectKey sql.NullString
	PlayObjectKey  sql.NullString
	DurationMs     sql.NullInt64
	Width          sql.NullInt64
	Height         sql.NullInt64
	CreatedAt      time.Time
}

type videoResponse struct {
	ID          int64   `json:"id"`
	Title       string  `json:"title"`
	Description string  `json:"description,omitempty"`
	Status      string  `json:"status"`
	CoverURL    string  `json:"coverUrl,omitempty"`
	PlayURL     string  `json:"playUrl,omitempty"`
	DurationMs  *int64  `json:"durationMs,omitempty"`
	Width       *int64  `json:"width,omitempty"`
	Height      *int64  `json:"height,omitempty"`
	CreatedAt   string  `json:"createdAt"`
}

type apiServer struct {
	cfg      config.Config
	database *sql.DB
}

func main() {
	cfg := config.Load()

	database, err := db.OpenMySQL(cfg.MySQLDSN)
	if err != nil {
		log.Fatalf("connect mysql: %v", err)
	}
	defer database.Close()

	server := &apiServer{
		cfg:      cfg,
		database: database,
	}

	mux := http.NewServeMux()
	mux.HandleFunc("/api/videos", server.handleVideos)
	mux.HandleFunc("/api/videos/", server.handleVideoDetail)
	mux.HandleFunc("/healthz", handleHealthz)

	log.Printf("api listening on %s", cfg.HTTPAddr)
	if err := http.ListenAndServe(cfg.HTTPAddr, mux); err != nil {
		log.Fatalf("listen and serve: %v", err)
	}
}

func handleHealthz(writer http.ResponseWriter, _ *http.Request) {
	writeJSON(writer, http.StatusOK, map[string]string{"status": "ok"})
}

func (s *apiServer) handleVideos(writer http.ResponseWriter, request *http.Request) {
	if request.Method != http.MethodGet {
		writeMethodNotAllowed(writer)
		return
	}
	if request.URL.Path != "/api/videos" {
		writeNotFound(writer)
		return
	}

	ctx, cancel := context.WithTimeout(request.Context(), 5*time.Second)
	defer cancel()

	rows, err := s.database.QueryContext(
		ctx,
		`SELECT id, title, description, status, cover_object_key, play_object_key, duration_ms, width, height, created_at
		 FROM videos
		 ORDER BY id DESC`,
	)
	if err != nil {
		writeServerError(writer, fmt.Errorf("query videos: %w", err))
		return
	}
	defer rows.Close()

	responses := make([]videoResponse, 0)
	for rows.Next() {
		var row videoRow
		if err := rows.Scan(
			&row.ID,
			&row.Title,
			&row.Description,
			&row.Status,
			&row.CoverObjectKey,
			&row.PlayObjectKey,
			&row.DurationMs,
			&row.Width,
			&row.Height,
			&row.CreatedAt,
		); err != nil {
			writeServerError(writer, fmt.Errorf("scan video row: %w", err))
			return
		}

		responses = append(responses, s.toVideoResponse(row))
	}

	if err := rows.Err(); err != nil {
		writeServerError(writer, fmt.Errorf("iterate videos: %w", err))
		return
	}

	writeJSON(writer, http.StatusOK, map[string]any{
		"items": responses,
	})
}

func (s *apiServer) handleVideoDetail(writer http.ResponseWriter, request *http.Request) {
	if request.Method != http.MethodGet {
		writeMethodNotAllowed(writer)
		return
	}

	idPart := strings.TrimPrefix(request.URL.Path, "/api/videos/")
	if idPart == "" || strings.Contains(idPart, "/") {
		writeNotFound(writer)
		return
	}

	videoID, err := strconv.ParseInt(idPart, 10, 64)
	if err != nil || videoID <= 0 {
		writeJSON(writer, http.StatusBadRequest, map[string]string{"error": "invalid video id"})
		return
	}

	ctx, cancel := context.WithTimeout(request.Context(), 5*time.Second)
	defer cancel()

	row, err := s.findVideoByID(ctx, videoID)
	if err != nil {
		if errors.Is(err, sql.ErrNoRows) {
			writeNotFound(writer)
			return
		}
		writeServerError(writer, fmt.Errorf("find video by id: %w", err))
		return
	}

	writeJSON(writer, http.StatusOK, s.toVideoResponse(row))
}

func (s *apiServer) findVideoByID(ctx context.Context, videoID int64) (videoRow, error) {
	row := s.database.QueryRowContext(
		ctx,
		`SELECT id, title, description, status, cover_object_key, play_object_key, duration_ms, width, height, created_at
		 FROM videos
		 WHERE id = ?`,
		videoID,
	)

	var result videoRow
	err := row.Scan(
		&result.ID,
		&result.Title,
		&result.Description,
		&result.Status,
		&result.CoverObjectKey,
		&result.PlayObjectKey,
		&result.DurationMs,
		&result.Width,
		&result.Height,
		&result.CreatedAt,
	)
	return result, err
}

func (s *apiServer) toVideoResponse(row videoRow) videoResponse {
	response := videoResponse{
		ID:        row.ID,
		Title:     row.Title,
		Status:    row.Status,
		CreatedAt: row.CreatedAt.Format(time.RFC3339),
	}

	if row.Description.Valid {
		response.Description = row.Description.String
	}
	if row.CoverObjectKey.Valid {
		response.CoverURL = buildObjectURL(s.cfg.PublicBaseURL, s.cfg.ImageBucket, row.CoverObjectKey.String)
	}
	if row.PlayObjectKey.Valid {
		response.PlayURL = buildObjectURL(s.cfg.PublicBaseURL, s.cfg.VODBucket, row.PlayObjectKey.String)
	}
	if row.DurationMs.Valid {
		value := row.DurationMs.Int64
		response.DurationMs = &value
	}
	if row.Width.Valid {
		value := row.Width.Int64
		response.Width = &value
	}
	if row.Height.Valid {
		value := row.Height.Int64
		response.Height = &value
	}

	return response
}

func buildObjectURL(baseURL, bucket, objectKey string) string {
	trimmedBase := strings.TrimRight(baseURL, "/")
	segments := []string{trimmedBase, url.PathEscape(bucket)}

	for _, segment := range strings.Split(strings.TrimLeft(objectKey, "/"), "/") {
		segments = append(segments, url.PathEscape(segment))
	}

	return strings.Join(segments, "/")
}

func writeJSON(writer http.ResponseWriter, statusCode int, payload any) {
	writer.Header().Set("Content-Type", "application/json; charset=utf-8")
	writer.WriteHeader(statusCode)

	encoder := json.NewEncoder(writer)
	encoder.SetIndent("", "  ")
	if err := encoder.Encode(payload); err != nil {
		log.Printf("write json response: %v", err)
	}
}

func writeMethodNotAllowed(writer http.ResponseWriter) {
	writeJSON(writer, http.StatusMethodNotAllowed, map[string]string{"error": "method not allowed"})
}

func writeNotFound(writer http.ResponseWriter) {
	writeJSON(writer, http.StatusNotFound, map[string]string{"error": "not found"})
}

func writeServerError(writer http.ResponseWriter, err error) {
	log.Printf("api error: %v", err)
	writeJSON(writer, http.StatusInternalServerError, map[string]string{"error": "internal server error"})
}
