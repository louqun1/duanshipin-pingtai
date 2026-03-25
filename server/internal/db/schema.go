package db

import (
	"context"
	"database/sql"
	"fmt"
)

func EnsureSchema(ctx context.Context, database *sql.DB) error {
	statements := []string{
		`CREATE TABLE IF NOT EXISTS users (
			id BIGINT PRIMARY KEY AUTO_INCREMENT,
			username VARCHAR(64) NOT NULL UNIQUE,
			email VARCHAR(255) NOT NULL UNIQUE,
			nickname VARCHAR(64) NULL,
			avatar_url VARCHAR(512) NULL,
			password_hash VARCHAR(128) NOT NULL,
			password_salt VARCHAR(128) NOT NULL,
			status VARCHAR(32) NOT NULL DEFAULT 'active',
			created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
			updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
		)`,
		`CREATE TABLE IF NOT EXISTS user_sessions (
			id BIGINT PRIMARY KEY AUTO_INCREMENT,
			user_id BIGINT NOT NULL,
			token_hash VARCHAR(128) NOT NULL UNIQUE,
			created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
			expires_at TIMESTAMP NOT NULL,
			last_seen_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
			revoked_at TIMESTAMP NULL DEFAULT NULL,
			INDEX idx_user_sessions_user_id (user_id),
			INDEX idx_user_sessions_active_lookup (token_hash, revoked_at, expires_at),
			CONSTRAINT fk_user_sessions_user_id FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
		)`,
		`CREATE TABLE IF NOT EXISTS video_likes (
			video_id BIGINT NOT NULL,
			user_id BIGINT NOT NULL,
			created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
			PRIMARY KEY (video_id, user_id),
			INDEX idx_video_likes_user_id (user_id),
			CONSTRAINT fk_video_likes_video_id FOREIGN KEY (video_id) REFERENCES videos(id) ON DELETE CASCADE,
			CONSTRAINT fk_video_likes_user_id FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
		)`,
	}

	for _, statement := range statements {
		if _, err := database.ExecContext(ctx, statement); err != nil {
			return fmt.Errorf("apply schema statement: %w", err)
		}
	}

	if err := ensureVideosUserIDColumn(ctx, database); err != nil {
		return err
	}

	return nil
}

func ensureVideosUserIDColumn(ctx context.Context, database *sql.DB) error {
	exists, err := hasColumn(ctx, database, "videos", "user_id")
	if err != nil {
		return fmt.Errorf("check videos.user_id column: %w", err)
	}
	if exists {
		return nil
	}

	if _, err := database.ExecContext(
		ctx,
		`ALTER TABLE videos ADD COLUMN user_id BIGINT NULL AFTER id`,
	); err != nil {
		return fmt.Errorf("add videos.user_id column: %w", err)
	}

	if _, err := database.ExecContext(
		ctx,
		`CREATE INDEX idx_videos_user_id ON videos(user_id)`,
	); err != nil {
		return fmt.Errorf("create idx_videos_user_id: %w", err)
	}

	return nil
}

func hasColumn(ctx context.Context, database *sql.DB, tableName, columnName string) (bool, error) {
	var count int
	err := database.QueryRowContext(
		ctx,
		`SELECT COUNT(*)
		 FROM information_schema.COLUMNS
		 WHERE TABLE_SCHEMA = DATABASE()
		   AND TABLE_NAME = ?
		   AND COLUMN_NAME = ?`,
		tableName,
		columnName,
	).Scan(&count)
	if err != nil {
		return false, err
	}

	return count > 0, nil
}
