package main

import (
	"bytes"
	"context"
	"database/sql"
	"encoding/json"
	"errors"
	"fmt"
	"log"
	"mime"
	"net"
	"net/http"
	"os"
	"os/signal"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"syscall"
	"time"

	"vod-platform-server/internal/config"
	"vod-platform-server/internal/db"
	"vod-platform-server/internal/storage"
)

type transcodeJob struct {
	ID          int64
	VideoID     int64
	PayloadJSON string
}

type jobPayload struct {
	VideoID         int64  `json:"videoId"`
	SourceBucket    string `json:"sourceBucket"`
	SourceObjectKey string `json:"sourceObjectKey"`
}

type ffprobeResult struct {
	Streams []struct {
		CodecType string `json:"codec_type"`
		Width     int    `json:"width"`
		Height    int    `json:"height"`
	} `json:"streams"`
	Format struct {
		Duration string `json:"duration"`
	} `json:"format"`
}

type videoUpdatedNotification struct {
	VideoID int64 `json:"videoId"`
}

const (
	idlePollInterval   = 3 * time.Second
	errorRetryInterval = 5 * time.Second
)

func main() {
	cfg := config.Load()
	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()

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
	if err := minioStorage.EnsureVODBucket(ctx); err != nil {
		log.Fatalf("check vod bucket: %v", err)
	}
	if err := minioStorage.EnsureImageBucket(ctx); err != nil {
		log.Fatalf("check image bucket: %v", err)
	}

	log.Printf("worker started: polling for pending jobs every %s", idlePollInterval)

	idleLogged := false
	for {
		select {
		case <-ctx.Done():
			log.Println("worker shutting down")
			return
		default:
		}

		job, err := claimPendingJob(ctx, database)//查询并锁定一个待处理的转码任务
		if err != nil {
			log.Printf("claim pending job: %v", err)
			if !waitForNextIteration(ctx, errorRetryInterval) {
				log.Println("worker shutting down")
				return
			}
			continue
		}

		if job == nil {//没有待处理的转码任务
			if !idleLogged {
				log.Println("no pending transcode job found; waiting for new uploads")
				idleLogged = true
			}
			if !waitForNextIteration(ctx, idlePollInterval) {
				log.Println("worker shutting down")
				return
			}
			continue
		}

		idleLogged = false
		log.Printf("worker picked job: job_id=%d video_id=%d", job.ID, job.VideoID)
		if err := notifyVideoUpdated(ctx, cfg, job.VideoID); err != nil {//如果通知视频正在处理事件失败，记录日志但继续处理任务
			log.Printf("notify video processing event: %v", err)
		}

		if err := processJob(ctx, database, minioStorage, *job); err != nil {//执行下载、探测、转码、封面生成、产物上传、状态落库
			_ = markJobFailed(ctx, database, job.ID, err.Error())
			_ = markVideoFailed(ctx, database, job.VideoID, err.Error())
			if notifyErr := notifyVideoUpdated(ctx, cfg, job.VideoID); notifyErr != nil {//如果通知视频失败事件失败，记录日志但继续处理任务，通知 API 发 SSE
				log.Printf("notify video failure event: %v", notifyErr)
			}
			log.Printf("process job %d failed: %v", job.ID, err)
			continue
		}

		if err := notifyVideoUpdated(ctx, cfg, job.VideoID); err != nil {//如果通知视频准备就绪事件失败，记录日志但继续处理任务，通知 API 发 SSE
			log.Printf("notify video ready event: %v", err)
		}
		log.Printf("worker finished: job_id=%d video_id=%d", job.ID, job.VideoID)//记录转码任务完成日志
	}
}

func processJob(ctx context.Context, database *sql.DB, minioStorage *storage.MinIOStorage, job transcodeJob) error {
	//解析任务负载，准备工作目录，从 MinIO 下载原始视频文件，使用 ffprobe 获取视频元信息，使用 ffmpeg 转码为 HLS 格式并生成封面图，上传转码后的视频和封面图到 MinIO，更新数据库中的视频状态和相关信息，最后标记转码任务成功。
	var payload jobPayload
	if err := json.Unmarshal([]byte(job.PayloadJSON), &payload); err != nil {
		return fmt.Errorf("parse payload json: %w", err)
	}

	tmpDir, err := os.MkdirTemp("", fmt.Sprintf("vod-worker-%d-", job.ID))//创建临时目录用于存储处理过程中的文件
	if err != nil {
		return fmt.Errorf("create temp dir: %w", err)
	}
	defer os.RemoveAll(tmpDir)//处理完成后删除临时目录

	inputPath := filepath.Join(tmpDir, "input"+filepath.Ext(payload.SourceObjectKey))//赋值输入文件路径为临时目录下的 input 文件，保持原始扩展名
	hlsDir := filepath.Join(tmpDir, "hls")
	coverPath := filepath.Join(tmpDir, "cover.jpg")

	if err := os.MkdirAll(hlsDir, 0o755); err != nil {//
		return fmt.Errorf("create hls dir: %w", err)
	}

	if err := minioStorage.DownloadFile(ctx, payload.SourceBucket, payload.SourceObjectKey, inputPath); err != nil {
		//如果下载原始视频文件失败，返回错误并在外层标记转码任务和视频状态为失败
		return err
	}

	meta, err := probeVideo(ctx, inputPath)//使用 ffprobe 获取视频元信息
	if err != nil {
		return err
	}

	if err := transcodeToHLS(ctx, inputPath, hlsDir); err != nil {
		return err
	}

	if err := generateCover(ctx, inputPath, coverPath); err != nil {
		return err
	}

	playObjectKey := fmt.Sprintf("video/%d/hls/index.m3u8", job.VideoID)
	coverObjectKey := fmt.Sprintf("video/%d/cover.jpg", job.VideoID)

	if err := uploadHLSDir(ctx, minioStorage, job.VideoID, hlsDir); err != nil {
		return err
	}
	if err := minioStorage.UploadImageFile(ctx, coverObjectKey, coverPath, "image/jpeg"); err != nil {
		return err
	}

	if err := markVideoReady(ctx, database, job.VideoID, playObjectKey, coverObjectKey, meta); err != nil {
		return err
	}
	if err := markJobSuccess(ctx, database, job.ID); err != nil {
		return err
	}

	return nil
}

// 查询并锁定一个待处理的转码任务
func claimPendingJob(ctx context.Context, database *sql.DB) (*transcodeJob, error) {
	tx, err := database.BeginTx(ctx, nil)
	if err != nil {
		return nil, err
	}

	var job transcodeJob
	row := tx.QueryRowContext(
		ctx,
		`SELECT id, video_id, payload_json
		 FROM transcode_jobs
		 WHERE status = 'pending'
		 ORDER BY id ASC
		 LIMIT 1
		 FOR UPDATE`,
	)

	if err := row.Scan(&job.ID, &job.VideoID, &job.PayloadJSON); err != nil {
		_ = tx.Rollback()
		if errors.Is(err, sql.ErrNoRows) {
			return nil, nil
		}
		return nil, err
	}

	if _, err := tx.ExecContext(
		ctx,
		`UPDATE transcode_jobs
		 SET status = 'running',
		     started_at = NOW(),
		     finished_at = NULL,
		     error_message = NULL
		 WHERE id = ?`,
		job.ID,
	); err != nil {
		_ = tx.Rollback()
		return nil, err
	}

	if _, err := tx.ExecContext(
		ctx,
		`UPDATE videos
		 SET status = 'processing',
		     transcode_error = NULL
		 WHERE id = ?`,
		job.VideoID,
	); err != nil {
		_ = tx.Rollback()
		return nil, err
	}

	if err := tx.Commit(); err != nil {
		_ = tx.Rollback()
		return nil, err
	}

	return &job, nil
}

func markJobSuccess(ctx context.Context, database *sql.DB, jobID int64) error {
	_, err := database.ExecContext(
		ctx,
		`UPDATE transcode_jobs
		 SET status = 'success', finished_at = NOW(), error_message = NULL
		 WHERE id = ?`,
		jobID,
	)
	return err
}

func markJobFailed(ctx context.Context, database *sql.DB, jobID int64, message string) error {
	_, err := database.ExecContext(
		ctx,
		`UPDATE transcode_jobs
		 SET status = 'failed', finished_at = NOW(), error_message = ?
		 WHERE id = ?`,
		message,
		jobID,
	)
	return err
}

func markVideoReady(ctx context.Context, database *sql.DB, videoID int64, playObjectKey, coverObjectKey string, meta videoMeta) error {
	_, err := database.ExecContext(
		ctx,
		`UPDATE videos
		 SET status = 'ready',
		     play_object_key = ?,
		     cover_object_key = ?,
		     duration_ms = ?,
		     width = ?,
		     height = ?,
		     published_at = NOW(),
		     transcode_error = NULL
		 WHERE id = ?`,
		playObjectKey,
		coverObjectKey,
		meta.DurationMs,
		meta.Width,
		meta.Height,
		videoID,
	)
	return err
}

func markVideoFailed(ctx context.Context, database *sql.DB, videoID int64, message string) error {
	_, err := database.ExecContext(
		ctx,
		`UPDATE videos
		 SET status = 'failed', transcode_error = ?
		 WHERE id = ?`,
		message,
		videoID,
	)
	return err
}

type videoMeta struct {
	DurationMs int64
	Width      int
	Height     int
}

func probeVideo(ctx context.Context, inputPath string) (videoMeta, error) {
	cmd := exec.CommandContext(
		ctx,
		"ffprobe",
		"-v", "error",
		"-print_format", "json",
		"-show_format",
		"-show_streams",
		inputPath,
	)

	output, err := cmd.Output()
	if err != nil {
		return videoMeta{}, fmt.Errorf("run ffprobe: %w", err)
	}

	var result ffprobeResult
	if err := json.Unmarshal(output, &result); err != nil {
		return videoMeta{}, fmt.Errorf("parse ffprobe output: %w", err)
	}

	meta := videoMeta{}
	for _, stream := range result.Streams {
		if stream.CodecType == "video" {
			meta.Width = stream.Width
			meta.Height = stream.Height
			break
		}
	}

	durationSeconds, err := strconv.ParseFloat(result.Format.Duration, 64)
	if err == nil {
		meta.DurationMs = int64(durationSeconds * 1000)
	}

	return meta, nil
}

func transcodeToHLS(ctx context.Context, inputPath, hlsDir string) error {
	indexPath := filepath.Join(hlsDir, "index.m3u8")
	segmentPattern := filepath.Join(hlsDir, "seg_%03d.ts")

	cmd := exec.CommandContext(
		ctx,
		"ffmpeg",
		"-y",
		"-i", inputPath,
		"-vf", "scale='min(1280,iw)':'min(720,ih)':force_original_aspect_ratio=decrease",
		"-c:v", "libx264",
		"-preset", "veryfast",
		"-crf", "23",
		"-c:a", "aac",
		"-b:a", "128k",
		"-ac", "2",
		"-f", "hls",
		"-hls_time", "6",
		"-hls_playlist_type", "vod",
		"-hls_segment_filename", segmentPattern,
		indexPath,
	)

	if output, err := cmd.CombinedOutput(); err != nil {
		return fmt.Errorf("run ffmpeg hls: %w: %s", err, strings.TrimSpace(string(output)))
	}
	return nil
}

func generateCover(ctx context.Context, inputPath, coverPath string) error {
	cmd := exec.CommandContext(
		ctx,
		"ffmpeg",
		"-y",
		"-i", inputPath,
		"-ss", "00:00:01.000",
		"-vframes", "1",
		coverPath,
	)

	if output, err := cmd.CombinedOutput(); err != nil {
		return fmt.Errorf("run ffmpeg cover: %w: %s", err, strings.TrimSpace(string(output)))
	}
	return nil
}

func uploadHLSDir(ctx context.Context, minioStorage *storage.MinIOStorage, videoID int64, hlsDir string) error {
	entries, err := os.ReadDir(hlsDir)
	if err != nil {
		return fmt.Errorf("read hls dir: %w", err)
	}

	for _, entry := range entries {
		if entry.IsDir() {
			continue
		}

		localPath := filepath.Join(hlsDir, entry.Name())
		objectKey := fmt.Sprintf("video/%d/hls/%s", videoID, entry.Name())
		contentType := detectContentType(localPath)
		if entry.Name() == "index.m3u8" {
			contentType = "application/vnd.apple.mpegurl"
		}
		if strings.HasSuffix(entry.Name(), ".ts") {
			contentType = "video/mp2t"
		}

		if err := minioStorage.UploadVODFile(ctx, objectKey, localPath, contentType); err != nil {
			return err
		}
	}

	return nil
}

func detectContentType(filePath string) string {
	contentType := mime.TypeByExtension(strings.ToLower(filepath.Ext(filePath)))
	if contentType == "" {
		return "application/octet-stream"
	}
	return contentType
}

func waitForNextIteration(ctx context.Context, interval time.Duration) bool {
	timer := time.NewTimer(interval)
	defer timer.Stop()

	select {
	case <-ctx.Done():
		return false
	case <-timer.C:
		return true
	}
}

func notifyVideoUpdated(ctx context.Context, cfg config.Config, videoID int64) error {
	if videoID <= 0 {
		return fmt.Errorf("invalid video id %d", videoID)
	}

	payload, err := json.Marshal(videoUpdatedNotification{VideoID: videoID})
	if err != nil {
		return fmt.Errorf("marshal video notification: %w", err)
	}

	requestCtx, cancel := context.WithTimeout(ctx, 5*time.Second)
	defer cancel()

	request, err := http.NewRequestWithContext(
		requestCtx,
		http.MethodPost,
		internalVideoUpdatedNotificationURL(cfg.HTTPAddr),
		bytes.NewReader(payload),
	)
	if err != nil {
		return fmt.Errorf("create notify request: %w", err)
	}
	request.Header.Set("Content-Type", "application/json")

	response, err := http.DefaultClient.Do(request)
	if err != nil {
		return fmt.Errorf("post notify request: %w", err)
	}
	defer response.Body.Close()

	if response.StatusCode < http.StatusOK || response.StatusCode >= http.StatusMultipleChoices {
		return fmt.Errorf("notify api returned %s", response.Status)
	}

	return nil
}

func internalVideoUpdatedNotificationURL(httpAddr string) string {
	return apiBaseURLFromHTTPAddr(httpAddr) + "/internal/events/video-updated"
}

func apiBaseURLFromHTTPAddr(httpAddr string) string {
	trimmed := strings.TrimSpace(httpAddr)
	if trimmed == "" {
		return "http://127.0.0.1:8080"
	}
	if strings.HasPrefix(trimmed, ":") {
		return "http://127.0.0.1" + trimmed
	}

	host, port, err := net.SplitHostPort(trimmed)
	if err == nil {
		if host == "" || host == "0.0.0.0" || host == "::" {
			host = "127.0.0.1"
		}
		return "http://" + net.JoinHostPort(host, port)
	}

	return "http://" + trimmed
}
