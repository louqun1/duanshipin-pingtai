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

	cfg := config.Load()//加载配置文件
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Minute)
	defer cancel()

	database, err := db.OpenMySQL(cfg.MySQLDSN)//连接 MySQL 数据库，如果连接失败，记录错误并退出程序
	if err != nil {
		log.Fatalf("connect mysql: %v", err)
	}
	defer database.Close()
	if err := db.EnsureSchema(ctx, database); err != nil {//确保 MySQL 数据库模式，如果失败，记录错误并退出程序
		log.Fatalf("ensure mysql schema: %v", err)
	}

	minioStorage, err := storage.NewMinIOStorage(cfg)//连接 MinIO 存储，如果连接失败，记录错误并退出程序
	if err != nil {
		log.Fatalf("connect minio: %v", err)
	}
	if err := minioStorage.EnsureRawBucket(ctx); err != nil {
		log.Fatalf("check raw bucket: %v", err)
	}

	result, err := ingest.IngestLocalFile(//调用 ingest 包的 IngestLocalFile 函数执行视频导入，传入上下文、数据库连接、MinIO 存储实例、本地文件路径、视频标题和描述，如果导入失败，记录错误并退出程序
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
