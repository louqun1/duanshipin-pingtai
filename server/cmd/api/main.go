package main

import (
	"context"
	"database/sql"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"net/http"
	"net/url"
	"os"
	"path"
	"strconv"
	"strings"
	"time"

	"vod-platform-server/internal/config"
	"vod-platform-server/internal/db"
	"vod-platform-server/internal/ingest"
	"vod-platform-server/internal/storage"
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
	cfg          config.Config
	database     *sql.DB
	mediaStorage *storage.MinIOStorage
	events       *eventBroker
}

func main() {
	cfg := config.Load()

	database, err := db.OpenMySQL(cfg.MySQLDSN)
	if err != nil {
		log.Fatalf("connect mysql: %v", err)
	}
	defer database.Close()

	mediaStorage, err := storage.NewMinIOStorage(cfg)
	if err != nil {
		log.Fatalf("connect minio: %v", err)
	}
	ctx := context.Background()
	if err := mediaStorage.EnsureRawBucket(ctx); err != nil {
		log.Fatalf("check raw bucket: %v", err)
	}
	if err := mediaStorage.EnsureVODBucket(ctx); err != nil {
		log.Fatalf("check vod bucket: %v", err)
	}
	if err := mediaStorage.EnsureImageBucket(ctx); err != nil {
		log.Fatalf("check image bucket: %v", err)
	}

	server := &apiServer{
		cfg:          cfg,
		database:     database,
		mediaStorage: mediaStorage,
		events:       newEventBroker(),
	}

	mux := http.NewServeMux()
	mux.HandleFunc("/api/events", server.handleEvents)
	mux.HandleFunc("/api/videos", server.handleVideos)
	mux.HandleFunc("/api/videos/upload", server.handleVideoUpload)
	mux.HandleFunc("/api/videos/", server.handleVideoDetail)
	mux.HandleFunc("/internal/events/video-updated", server.handleVideoUpdatedNotification)
	mux.HandleFunc("/vod/", server.handleVODObject)
	mux.HandleFunc("/image/", server.handleImageObject)
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

		responses = append(responses, s.toVideoResponse(row, s.cfg.PublicBaseURL))
	}

	if err := rows.Err(); err != nil {
		writeServerError(writer, fmt.Errorf("iterate videos: %w", err))
		return
	}

	writeJSON(writer, http.StatusOK, map[string]any{
		"items": responses,
	})
}

func (s *apiServer) handleVideoUpload(writer http.ResponseWriter, request *http.Request) {
	const (
		uploadFormMemory = 32 << 20
		maxUploadSize    = int64(4) << 30
	)

	if request.Method != http.MethodPost {
		writeMethodNotAllowed(writer)
		return
	}
	if request.URL.Path != "/api/videos/upload" {
		writeNotFound(writer)
		return
	}

	request.Body = http.MaxBytesReader(writer, request.Body, maxUploadSize)
	if err := request.ParseMultipartForm(uploadFormMemory); err != nil {
		writeJSON(writer, http.StatusBadRequest, map[string]string{"error": "invalid multipart upload"})
		return
	}
	if request.MultipartForm != nil {
		defer request.MultipartForm.RemoveAll()
	}

	file, header, err := request.FormFile("file")
	if err != nil {
		writeJSON(writer, http.StatusBadRequest, map[string]string{"error": "missing file field"})
		return
	}
	defer file.Close()

	tempFile, err := os.CreateTemp("", "vod-upload-*")
	if err != nil {
		writeServerError(writer, fmt.Errorf("create temp upload file: %w", err))
		return
	}
	tempPath := tempFile.Name()
	defer os.Remove(tempPath)

	if _, err := io.Copy(tempFile, file); err != nil {
		_ = tempFile.Close()
		writeServerError(writer, fmt.Errorf("write temp upload file: %w", err))
		return
	}
	if err := tempFile.Close(); err != nil {
		writeServerError(writer, fmt.Errorf("close temp upload file: %w", err))
		return
	}

	description := strings.TrimSpace(request.FormValue("desc"))
	if description == "" {
		description = strings.TrimSpace(request.FormValue("description"))
	}

	ctx, cancel := context.WithTimeout(request.Context(), 15*time.Minute)
	defer cancel()

	result, err := ingest.IngestLocalFile(
		ctx,
		s.database,
		s.mediaStorage,
		tempPath,
		header.Filename,
		strings.TrimSpace(request.FormValue("title")),
		description,
	)
	if err != nil {
		writeServerError(writer, fmt.Errorf("ingest uploaded file: %w", err))
		return
	}
	if err := s.publishVideoUpdated(ctx, result.VideoID); err != nil && !errors.Is(err, sql.ErrNoRows) {
		log.Printf("publish upload event: %v", err)
	}

	writeJSON(writer, http.StatusCreated, map[string]any{
		"id":        result.VideoID,
		"jobId":     result.JobID,
		"title":     result.Title,
		"status":    result.Status,
		"message":   "upload accepted and queued for transcoding",
		"createdAt": time.Now().Format(time.RFC3339),
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

	writeJSON(writer, http.StatusOK, s.toVideoResponse(row, s.cfg.PublicBaseURL))
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

func (s *apiServer) toVideoResponse(row videoRow, baseURL string) videoResponse {
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
		response.CoverURL = buildMediaURL(baseURL, "/image/", row.CoverObjectKey.String)
	}
	if row.PlayObjectKey.Valid {
		response.PlayURL = buildMediaURL(baseURL, "/vod/", row.PlayObjectKey.String)
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

func (s *apiServer) handleVODObject(writer http.ResponseWriter, request *http.Request) {
	s.handleBucketObject(writer, request, "/vod/", func(ctx context.Context, objectKey string) (io.ReadSeekCloser, objectInfo, error) {
		stream, info, err := s.mediaStorage.OpenVODObject(ctx, objectKey)
		return stream, objectInfo{
			ContentType:  info.ContentType,
			ETag:         info.ETag,
			LastModified: info.LastModified,
		}, err
	})
}

func (s *apiServer) handleImageObject(writer http.ResponseWriter, request *http.Request) {
	s.handleBucketObject(writer, request, "/image/", func(ctx context.Context, objectKey string) (io.ReadSeekCloser, objectInfo, error) {
		stream, info, err := s.mediaStorage.OpenImageObject(ctx, objectKey)
		return stream, objectInfo{
			ContentType:  info.ContentType,
			ETag:         info.ETag,
			LastModified: info.LastModified,
		}, err
	})
}

type objectInfo struct {
	ContentType  string
	ETag         string
	LastModified time.Time
}

func (s *apiServer) handleBucketObject(
	writer http.ResponseWriter,
	request *http.Request,
	routePrefix string,
	openObject func(ctx context.Context, objectKey string) (io.ReadSeekCloser, objectInfo, error),
) {
	if request.Method != http.MethodGet && request.Method != http.MethodHead {
		writeMethodNotAllowed(writer)
		return
	}

	objectKey := normalizeObjectKey(request.URL.Path, routePrefix)
	if objectKey == "" {
		writeNotFound(writer)
		return
	}

	ctx, cancel := context.WithTimeout(request.Context(), 30*time.Second)
	defer cancel()

	stream, info, err := openObject(ctx, objectKey)
	if err != nil {
		if storage.IsObjectNotFound(err) {
			writeNotFound(writer)
			return
		}
		writeServerError(writer, fmt.Errorf("open object %s: %w", objectKey, err))
		return
	}
	defer stream.Close()

	if info.ContentType != "" {
		writer.Header().Set("Content-Type", info.ContentType)
	}
	if info.ETag != "" {
		writer.Header().Set("ETag", fmt.Sprintf(`"%s"`, strings.Trim(info.ETag, `"`)))
	}

	http.ServeContent(writer, request, path.Base(objectKey), info.LastModified, stream)
}

func buildMediaURL(baseURL, routePrefix, objectKey string) string {
	trimmedBase := strings.TrimRight(baseURL, "/")
	trimmedPrefix := strings.Trim(routePrefix, "/")
	segments := []string{trimmedBase}
	if trimmedPrefix != "" {
		segments = append(segments, url.PathEscape(trimmedPrefix))
	}

	for _, segment := range strings.Split(strings.TrimLeft(objectKey, "/"), "/") {
		if segment == "" {
			continue
		}
		segments = append(segments, url.PathEscape(segment))
	}

	return strings.Join(segments, "/")
}

func normalizeObjectKey(requestPath, routePrefix string) string {
	rawPath := strings.TrimPrefix(requestPath, routePrefix)
	if rawPath == "" {
		return ""
	}

	cleaned := path.Clean("/" + rawPath)
	if cleaned == "/" || cleaned == "." {
		return ""
	}

	return strings.TrimPrefix(cleaned, "/")
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
