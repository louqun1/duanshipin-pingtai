package main

import (
	"context"
	"database/sql"
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"mime"
	"os"
	"path/filepath"
	"strings"
	"time"

	"vod-platform-server/internal/config"
	"vod-platform-server/internal/db"
	"vod-platform-server/internal/storage"
)

type jobPayload struct {
	VideoID         int64  `json:"videoId"`
	SourceBucket    string `json:"sourceBucket"`
	SourceObjectKey string `json:"sourceObjectKey"`
}

func main() {
	var (
		filePath    = flag.String("file", "", "local video file path")
		title       = flag.String("title", "", "video title")
		description = flag.String("desc", "", "video description")
	)
	flag.Parse()

	if *filePath == "" {
		log.Fatal("missing required -file argument")
	}

	absolutePath, err := filepath.Abs(*filePath)
	if err != nil {
		log.Fatalf("resolve file path: %v", err)
	}

	fileInfo, err := os.Stat(absolutePath)
	if err != nil {
		log.Fatalf("stat file: %v", err)
	}
	if fileInfo.IsDir() {
		log.Fatal("file path points to a directory")
	}

	cfg := config.Load()
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Minute)
	defer cancel()

	database, err := db.OpenMySQL(cfg.MySQLDSN)
	if err != nil {
		log.Fatalf("connect mysql: %v", err)
	}
	defer database.Close()

	minioStorage, err := storage.NewMinIOStorage(cfg)
	if err != nil {
		log.Fatalf("connect minio: %v", err)
	}
	if err := minioStorage.EnsureRawBucket(ctx); err != nil {
		log.Fatalf("check raw bucket: %v", err)
	}

	resolvedTitle := strings.TrimSpace(*title)
	if resolvedTitle == "" {
		resolvedTitle = strings.TrimSuffix(filepath.Base(absolutePath), filepath.Ext(absolutePath))
	}

	contentType := detectContentType(absolutePath)

	videoID, err := insertVideo(ctx, database, resolvedTitle, strings.TrimSpace(*description), fileInfo.Size(), contentType)
	if err != nil {
		log.Fatalf("insert video row: %v", err)
	}

	objectKey := buildSourceObjectKey(videoID, absolutePath)
	if err := minioStorage.UploadRawFile(ctx, objectKey, absolutePath, contentType); err != nil {
		_ = markVideoFailed(ctx, database, videoID, err.Error())
		log.Fatalf("upload raw file: %v", err)
	}

	if err := updateVideoSourceObjectKey(ctx, database, videoID, objectKey); err != nil {
		_ = markVideoFailed(ctx, database, videoID, err.Error())
		log.Fatalf("update video source object key: %v", err)
	}

	payloadBytes, err := json.Marshal(jobPayload{
		VideoID:         videoID,
		SourceBucket:    minioStorage.RawBucket(),
		SourceObjectKey: objectKey,
	})
	if err != nil {
		_ = markVideoFailed(ctx, database, videoID, err.Error())
		log.Fatalf("marshal job payload: %v", err)
	}

	jobID, err := insertTranscodeJob(ctx, database, videoID, string(payloadBytes))
	if err != nil {
		_ = markVideoFailed(ctx, database, videoID, err.Error())
		log.Fatalf("insert transcode job: %v", err)
	}

	if err := markVideoQueued(ctx, database, videoID); err != nil {
		_ = markVideoFailed(ctx, database, videoID, err.Error())
		log.Fatalf("update video status to queued: %v", err)
	}

	log.Printf("import complete: video_id=%d job_id=%d object_key=%s", videoID, jobID, objectKey)
}

func detectContentType(filePath string) string {
	contentType := mime.TypeByExtension(strings.ToLower(filepath.Ext(filePath)))
	if contentType == "" {
		return "application/octet-stream"
	}
	return contentType
}

func buildSourceObjectKey(videoID int64, filePath string) string {
	filename := filepath.Base(filePath)
	return fmt.Sprintf("video/%d/source/%s", videoID, filename)
}

func insertVideo(ctx context.Context, database *sql.DB, title, description string, fileSize int64, mimeType string) (int64, error) {
	result, err := database.ExecContext(
		ctx,
		`INSERT INTO videos (title, description, source_object_key, status, file_size, mime_type)
		 VALUES (?, ?, ?, 'uploaded', ?, ?)`,
		title,
		nullIfEmpty(description),
		"pending",
		fileSize,
		mimeType,
	)
	if err != nil {
		return 0, err
	}

	return result.LastInsertId()
}

func updateVideoSourceObjectKey(ctx context.Context, database *sql.DB, videoID int64, objectKey string) error {
	_, err := database.ExecContext(
		ctx,
		`UPDATE videos SET source_object_key = ? WHERE id = ?`,
		objectKey,
		videoID,
	)
	return err
}

func insertTranscodeJob(ctx context.Context, database *sql.DB, videoID int64, payloadJSON string) (int64, error) {
	result, err := database.ExecContext(
		ctx,
		`INSERT INTO transcode_jobs (video_id, job_type, status, payload_json)
		 VALUES (?, 'hls', 'pending', ?)`,
		videoID,
		payloadJSON,
	)
	if err != nil {
		return 0, err
	}

	return result.LastInsertId()
}

func markVideoQueued(ctx context.Context, database *sql.DB, videoID int64) error {
	_, err := database.ExecContext(
		ctx,
		`UPDATE videos SET status = 'queued', transcode_error = NULL WHERE id = ?`,
		videoID,
	)
	return err
}

func markVideoFailed(ctx context.Context, database *sql.DB, videoID int64, message string) error {
	_, err := database.ExecContext(
		ctx,
		`UPDATE videos SET status = 'failed', transcode_error = ? WHERE id = ?`,
		message,
		videoID,
	)
	return err
}

func nullIfEmpty(value string) any {
	if strings.TrimSpace(value) == "" {
		return nil
	}
	return value
}
