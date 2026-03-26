package ingest

import (
	"context"
	"database/sql"
	"encoding/json"
	"fmt"
	"mime"
	"os"
	"path"
	"path/filepath"
	"strings"

	"vod-platform-server/internal/storage"
)

type Result struct {
	VideoID   int64
	JobID     int64
	Title     string
	ObjectKey string
	Status    string
}

type jobPayload struct {
	VideoID         int64  `json:"videoId"`
	SourceBucket    string `json:"sourceBucket"`
	SourceObjectKey string `json:"sourceObjectKey"`
}

func IngestLocalFile(
	ctx context.Context,
	database *sql.DB,
	minioStorage *storage.MinIOStorage,
	localPath string,
	originalFilename string,
	title string,
	description string,
	uploaderUserID *int64,
) (Result, error) {
	absolutePath, err := filepath.Abs(localPath)
	if err != nil {
		return Result{}, fmt.Errorf("resolve file path: %w", err)
	}

	fileInfo, err := os.Stat(absolutePath)//获取文件信息，如果失败，返回错误
	if err != nil {
		return Result{}, fmt.Errorf("stat file: %w", err)
	}
	if fileInfo.IsDir() {//如果文件路径指向一个目录，返回错误
		return Result{}, fmt.Errorf("file path points to a directory")
	}

	safeFilename := normalizeFilename(originalFilename, absolutePath)
	resolvedTitle := strings.TrimSpace(title)
	if resolvedTitle == "" {
		resolvedTitle = strings.TrimSuffix(safeFilename, filepath.Ext(safeFilename))
	}

	contentType := detectContentType(safeFilename, absolutePath)

	videoID, err := insertVideo(//mysql插入视频记录，获取视频ID，如果失败，返回错误
		ctx,
		database,
		uploaderUserID,
		resolvedTitle,
		strings.TrimSpace(description),
		fileInfo.Size(),
		contentType,
	)
	if err != nil {
		return Result{}, fmt.Errorf("insert video row: %w", err)
	}

	objectKey := buildSourceObjectKey(videoID, safeFilename)
	//上传文件到MinIO，如果失败，更新视频状态为失败并返回错误
	if err := minioStorage.UploadRawFile(ctx, objectKey, absolutePath, contentType); err != nil {
		_ = markVideoFailed(ctx, database, videoID, err.Error())
		return Result{}, fmt.Errorf("upload raw file: %w", err)
	}

	//上传成功后，更新视频记录的source_object_key，id字段，如果失败，更新视频状态为失败并返回错误
	if err := updateVideoSourceObjectKey(ctx, database, videoID, objectKey); err != nil {
		_ = markVideoFailed(ctx, database, videoID, err.Error())
		return Result{}, fmt.Errorf("update video source object key: %w", err)
	}

	payloadBytes, err := json.Marshal(jobPayload{
		VideoID:         videoID,
		SourceBucket:    minioStorage.RawBucket(),
		SourceObjectKey: objectKey,
	})
	if err != nil {
		_ = markVideoFailed(ctx, database, videoID, err.Error())
		return Result{}, fmt.Errorf("marshal job payload: %w", err)
	}

	jobID, err := insertTranscodeJob(ctx, database, videoID, string(payloadBytes))
	if err != nil {
		_ = markVideoFailed(ctx, database, videoID, err.Error())
		return Result{}, fmt.Errorf("insert transcode job: %w", err)
	}

	if err := markVideoQueued(ctx, database, videoID); err != nil {
		_ = markVideoFailed(ctx, database, videoID, err.Error())
		return Result{}, fmt.Errorf("update video status to queued: %w", err)
	}

	return Result{
		VideoID:   videoID,
		JobID:     jobID,
		Title:     resolvedTitle,
		ObjectKey: objectKey,
		Status:    "queued",
	}, nil
}

func normalizeFilename(originalFilename string, localPath string) string {
	candidate := strings.TrimSpace(originalFilename)
	if candidate == "" {
		candidate = filepath.Base(localPath)
	}

	candidate = strings.ReplaceAll(candidate, "\\", "/")
	candidate = path.Base(candidate)
	if candidate == "" || candidate == "." || candidate == "/" {
		return filepath.Base(localPath)
	}

	return candidate
}

func detectContentType(filename string, localPath string) string {
	contentType := mime.TypeByExtension(strings.ToLower(filepath.Ext(filename)))
	if contentType == "" {
		contentType = mime.TypeByExtension(strings.ToLower(filepath.Ext(localPath)))
	}
	if contentType == "" {
		return "application/octet-stream"
	}
	return contentType
}

func buildSourceObjectKey(videoID int64, filename string) string {
	return fmt.Sprintf("video/%d/source/%s", videoID, filename)
}

func insertVideo(
	ctx context.Context,
	database *sql.DB,
	uploaderUserID *int64,
	title, description string,
	fileSize int64,
	mimeType string,
) (int64, error) {
	var userIDValue any
	if uploaderUserID != nil {
		userIDValue = *uploaderUserID
	}

	result, err := database.ExecContext(
		ctx,
		`INSERT INTO videos (user_id, title, description, source_object_key, status, file_size, mime_type)
		 VALUES (?, ?, ?, ?, 'uploaded', ?, ?)`,
		userIDValue,
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
