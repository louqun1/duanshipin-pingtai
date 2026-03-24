package config

import (
	"os"
	"strconv"
)

type Config struct {
	MySQLDSN      string
	HTTPAddr      string
	PublicBaseURL string
	MinIOEndpoint string
	MinIOAccess   string
	MinIOSecret   string
	MinIOUseSSL   bool
	RawBucket     string
	VODBucket     string
	ImageBucket   string
}

func Load() Config {
	return Config{
		MySQLDSN:      getEnv("MYSQL_DSN", "vod_user:vod_pass_123@tcp(127.0.0.1:3307)/vod_platform?charset=utf8mb4&parseTime=True&loc=Local"),
		HTTPAddr:      getEnv("HTTP_ADDR", ":8080"),
		PublicBaseURL: getEnv("PUBLIC_BASE_URL", "http://192.168.99.128:8080"),
		MinIOEndpoint: getEnv("MINIO_ENDPOINT", "127.0.0.1:9000"),
		MinIOAccess:   getEnv("MINIO_ACCESS_KEY", "minioadmin"),
		MinIOSecret:   getEnv("MINIO_SECRET_KEY", "minioadmin123"),
		MinIOUseSSL:   getEnvBool("MINIO_USE_SSL", false),
		RawBucket:     getEnv("MINIO_RAW_BUCKET", "raw-media"),
		VODBucket:     getEnv("MINIO_VOD_BUCKET", "vod-media"),
		ImageBucket:   getEnv("MINIO_IMAGE_BUCKET", "image-assets"),
	}
}

func getEnv(key, fallback string) string {
	value := os.Getenv(key)
	if value == "" {
		return fallback
	}
	return value
}

func getEnvBool(key string, fallback bool) bool {
	value := os.Getenv(key)
	if value == "" {
		return fallback
	}

	parsed, err := strconv.ParseBool(value)
	if err != nil {
		return fallback
	}
	return parsed
}
