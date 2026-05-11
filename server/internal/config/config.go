package config

import (
	"os"
	"strconv"
	"strings"
)

type Config struct {
	MySQLDSN            string
	HTTPAddr            string
	PublicBaseURL       string
	PublicSignalingURL  string
	LinkMicMixEnable    bool
	LinkMicMixFFmpegBin string
	LinkMicMixWorkDir   string
	LinkMicMixLogDir    string
	LinkMicMixInputBase  string
	LinkMicMixOutputBase string
	MinIOEndpoint       string
	MinIOAccess         string
	MinIOSecret         string
	MinIOUseSSL         bool
	RawBucket           string
	VODBucket           string
	ImageBucket         string
}

func Load() Config {
	publicBaseURL := getEnv("PUBLIC_BASE_URL", "http://192.168.3.28:8080")
	return Config{
		MySQLDSN:            getEnv("MYSQL_DSN", "vod_user:vod_pass_123@tcp(127.0.0.1:3307)/vod_platform?charset=utf8mb4&parseTime=True&loc=Local"),
		HTTPAddr:            getEnv("HTTP_ADDR", ":8080"),
		PublicBaseURL:       publicBaseURL,
		PublicSignalingURL:  getEnv("PUBLIC_SIGNALING_URL", derivePublicSignalingURL(publicBaseURL)),
		LinkMicMixEnable:    getEnvBool("LINKMIC_MIX_ENABLE", false),
		LinkMicMixFFmpegBin: getEnv("LINKMIC_MIX_FFMPEG_BIN", "ffmpeg"),
		LinkMicMixWorkDir:   getEnv("LINKMIC_MIX_WORKDIR", "./runtime/linkmicmix/work"),
		LinkMicMixLogDir:    getEnv("LINKMIC_MIX_LOGDIR", "./runtime/linkmicmix/logs"),
		LinkMicMixInputBase:  getEnv("LINKMIC_MIX_INPUT_BASE", "rtmp://127.0.0.1/live"),
		LinkMicMixOutputBase: getEnv("LINKMIC_MIX_OUTPUT_BASE", "rtmp://127.0.0.1/live"),
		MinIOEndpoint:       getEnv("MINIO_ENDPOINT", "127.0.0.1:9000"),
		MinIOAccess:         getEnv("MINIO_ACCESS_KEY", "minioadmin"),
		MinIOSecret:         getEnv("MINIO_SECRET_KEY", "minioadmin123"),
		MinIOUseSSL:         getEnvBool("MINIO_USE_SSL", false),
		RawBucket:           getEnv("MINIO_RAW_BUCKET", "raw-media"),
		VODBucket:           getEnv("MINIO_VOD_BUCKET", "vod-media"),
		ImageBucket:         getEnv("MINIO_IMAGE_BUCKET", "image-assets"),
	}
}

func derivePublicSignalingURL(publicBaseURL string) string {
	trimmed := strings.TrimRight(strings.TrimSpace(publicBaseURL), "/")
	switch {
	case trimmed == "":
		return "ws://192.168.3.28:8080/ws"
	case strings.HasPrefix(trimmed, "http://"):
		return "ws://" + strings.TrimPrefix(trimmed, "http://") + "/ws"
	case strings.HasPrefix(trimmed, "https://"):
		return "wss://" + strings.TrimPrefix(trimmed, "https://") + "/ws"
	case strings.HasPrefix(trimmed, "ws://"), strings.HasPrefix(trimmed, "wss://"):
		if strings.HasSuffix(trimmed, "/ws") {
			return trimmed
		}
		return trimmed + "/ws"
	default:
		return "ws://" + trimmed + "/ws"
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
