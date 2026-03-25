package main

import (
	"context"
	"flag"
	"log"
	"strings"
	"time"

	"vod-platform-server/internal/config"
	"vod-platform-server/internal/db"
	"vod-platform-server/internal/ingest"
	"vod-platform-server/internal/storage"
)

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

	cfg := config.Load()
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Minute)
	defer cancel()

	database, err := db.OpenMySQL(cfg.MySQLDSN)
	if err != nil {
		log.Fatalf("connect mysql: %v", err)
	}
	defer database.Close()
	if err := db.EnsureSchema(ctx, database); err != nil {
		log.Fatalf("ensure mysql schema: %v", err)
	}

	minioStorage, err := storage.NewMinIOStorage(cfg)
	if err != nil {
		log.Fatalf("connect minio: %v", err)
	}
	if err := minioStorage.EnsureRawBucket(ctx); err != nil {
		log.Fatalf("check raw bucket: %v", err)
	}

	result, err := ingest.IngestLocalFile(
		ctx,
		database,
		minioStorage,
		*filePath,
		*filePath,
		strings.TrimSpace(*title),
		strings.TrimSpace(*description),
		nil,
	)
	if err != nil {
		log.Fatalf("import video: %v", err)
	}

	log.Printf(
		"import complete: video_id=%d job_id=%d object_key=%s",
		result.VideoID,
		result.JobID,
		result.ObjectKey,
	)
}
