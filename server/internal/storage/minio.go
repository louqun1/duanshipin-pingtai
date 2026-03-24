package storage

import (
	"context"
	"fmt"
	"io"
	"net/http"

	"vod-platform-server/internal/config"

	"github.com/minio/minio-go/v7"
	"github.com/minio/minio-go/v7/pkg/credentials"
)

type MinIOStorage struct {
	client      *minio.Client
	rawBucket   string
	vodBucket   string
	imageBucket string
}

func NewMinIOStorage(cfg config.Config) (*MinIOStorage, error) {
	client, err := minio.New(cfg.MinIOEndpoint, &minio.Options{
		Creds:  credentials.NewStaticV4(cfg.MinIOAccess, cfg.MinIOSecret, ""),
		Secure: cfg.MinIOUseSSL,
	})
	if err != nil {
		return nil, fmt.Errorf("create minio client: %w", err)
	}

	return &MinIOStorage{
		client:      client,
		rawBucket:   cfg.RawBucket,
		vodBucket:   cfg.VODBucket,
		imageBucket: cfg.ImageBucket,
	}, nil
}

func (s *MinIOStorage) EnsureRawBucket(ctx context.Context) error {
	return s.ensureBucket(ctx, s.rawBucket)
}

func (s *MinIOStorage) EnsureVODBucket(ctx context.Context) error {
	return s.ensureBucket(ctx, s.vodBucket)
}

func (s *MinIOStorage) EnsureImageBucket(ctx context.Context) error {
	return s.ensureBucket(ctx, s.imageBucket)
}

func (s *MinIOStorage) ensureBucket(ctx context.Context, bucket string) error {
	exists, err := s.client.BucketExists(ctx, bucket)
	if err != nil {
		return fmt.Errorf("check bucket %s: %w", bucket, err)
	}
	if !exists {
		return fmt.Errorf("bucket %s does not exist", bucket)
	}
	return nil
}

func (s *MinIOStorage) UploadRawFile(ctx context.Context, objectKey, localPath, contentType string) error {
	return s.UploadFile(ctx, s.rawBucket, objectKey, localPath, contentType)
}

func (s *MinIOStorage) UploadVODFile(ctx context.Context, objectKey, localPath, contentType string) error {
	return s.UploadFile(ctx, s.vodBucket, objectKey, localPath, contentType)
}

func (s *MinIOStorage) UploadImageFile(ctx context.Context, objectKey, localPath, contentType string) error {
	return s.UploadFile(ctx, s.imageBucket, objectKey, localPath, contentType)
}

func (s *MinIOStorage) UploadFile(ctx context.Context, bucket, objectKey, localPath, contentType string) error {
	_, err := s.client.FPutObject(ctx, bucket, objectKey, localPath, minio.PutObjectOptions{
		ContentType: contentType,
	})
	if err != nil {
		return fmt.Errorf("upload %s to minio bucket %s: %w", localPath, bucket, err)
	}
	return nil
}

func (s *MinIOStorage) DownloadFile(ctx context.Context, bucket, objectKey, localPath string) error {
	if err := s.client.FGetObject(ctx, bucket, objectKey, localPath, minio.GetObjectOptions{}); err != nil {
		return fmt.Errorf("download %s from minio bucket %s: %w", objectKey, bucket, err)
	}
	return nil
}

func (s *MinIOStorage) OpenObject(ctx context.Context, bucket, objectKey string) (io.ReadSeekCloser, minio.ObjectInfo, error) {
	object, err := s.client.GetObject(ctx, bucket, objectKey, minio.GetObjectOptions{})
	if err != nil {
		return nil, minio.ObjectInfo{}, fmt.Errorf("open %s from minio bucket %s: %w", objectKey, bucket, err)
	}

	info, err := object.Stat()
	if err != nil {
		_ = object.Close()
		return nil, minio.ObjectInfo{}, fmt.Errorf("stat %s from minio bucket %s: %w", objectKey, bucket, err)
	}

	return object, info, nil
}

func (s *MinIOStorage) OpenVODObject(ctx context.Context, objectKey string) (io.ReadSeekCloser, minio.ObjectInfo, error) {
	return s.OpenObject(ctx, s.vodBucket, objectKey)
}

func (s *MinIOStorage) OpenImageObject(ctx context.Context, objectKey string) (io.ReadSeekCloser, minio.ObjectInfo, error) {
	return s.OpenObject(ctx, s.imageBucket, objectKey)
}

func (s *MinIOStorage) RawBucket() string {
	return s.rawBucket
}

func (s *MinIOStorage) VODBucket() string {
	return s.vodBucket
}

func (s *MinIOStorage) ImageBucket() string {
	return s.imageBucket
}

func IsObjectNotFound(err error) bool {
	if err == nil {
		return false
	}

	response := minio.ToErrorResponse(err)
	return response.StatusCode == http.StatusNotFound ||
		response.Code == "NoSuchKey" ||
		response.Code == "NoSuchBucket" ||
		response.Code == "NoSuchVersion"
}
