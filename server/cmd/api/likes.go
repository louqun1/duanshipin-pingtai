package main

import (
	"context"
	"errors"
	"fmt"
	"net/http"
	"strconv"
	"strings"
	"time"
)

type likeStateResponse struct {
	VideoID    int64 `json:"videoId"`
	LikeCount  int64 `json:"likeCount"`
	LikedByMe  bool  `json:"likedByMe"`
}

func (s *apiServer) handleVideoLike(writer http.ResponseWriter, request *http.Request, idPart string) {
	if request.Method != http.MethodPost && request.Method != http.MethodDelete {
		writeMethodNotAllowed(writer)
		return
	}

	videoID, err := parseVideoID(idPart)
	if err != nil {
		writeJSON(writer, http.StatusBadRequest, map[string]string{"error": "invalid video id"})
		return
	}

	ctx, cancel := context.WithTimeout(request.Context(), 5*time.Second)
	defer cancel()

	user, err := s.requireAuthenticatedUser(ctx, request)
	if err != nil {
		writeJSON(writer, http.StatusUnauthorized, map[string]string{"error": err.Error()})
		return
	}

	exists, err := s.videoExists(ctx, videoID)
	if err != nil {
		writeServerError(writer, fmt.Errorf("check video exists: %w", err))
		return
	}
	if !exists {
		writeNotFound(writer)
		return
	}

	if request.Method == http.MethodPost {
		if err := s.addVideoLike(ctx, videoID, user.ID); err != nil {
			writeServerError(writer, fmt.Errorf("insert video like: %w", err))
			return
		}
	} else {
		if err := s.removeVideoLike(ctx, videoID, user.ID); err != nil {
			writeServerError(writer, fmt.Errorf("delete video like: %w", err))
			return
		}
	}

	likeState, err := s.findVideoLikeState(ctx, videoID, user.ID)
	if err != nil {
		writeServerError(writer, fmt.Errorf("query like state: %w", err))
		return
	}

	writeJSON(writer, http.StatusOK, likeState)
}

func parseVideoID(idPart string) (int64, error) {
	if strings.TrimSpace(idPart) == "" || strings.Contains(idPart, "/") {
		return 0, errors.New("invalid video id path")
	}

	videoID, err := strconv.ParseInt(idPart, 10, 64)
	if err != nil || videoID <= 0 {
		return 0, errors.New("invalid video id value")
	}

	return videoID, nil
}

func (s *apiServer) videoExists(ctx context.Context, videoID int64) (bool, error) {
	var count int
	if err := s.database.QueryRowContext(
		ctx,
		`SELECT COUNT(*) FROM videos WHERE id = ?`,
		videoID,
	).Scan(&count); err != nil {
		return false, err
	}

	return count > 0, nil
}

func (s *apiServer) addVideoLike(ctx context.Context, videoID, userID int64) error {
	_, err := s.database.ExecContext(
		ctx,
		`INSERT INTO video_likes (video_id, user_id)
		 VALUES (?, ?)
		 ON DUPLICATE KEY UPDATE created_at = created_at`,
		videoID,
		userID,
	)
	return err
}

func (s *apiServer) removeVideoLike(ctx context.Context, videoID, userID int64) error {
	_, err := s.database.ExecContext(
		ctx,
		`DELETE FROM video_likes
		 WHERE video_id = ?
		   AND user_id = ?`,
		videoID,
		userID,
	)
	return err
}

func (s *apiServer) findVideoLikeState(ctx context.Context, videoID, userID int64) (likeStateResponse, error) {
	row := s.database.QueryRowContext(
		ctx,
		`SELECT
		     (SELECT COUNT(*) FROM video_likes WHERE video_id = ?) AS like_count,
		     (SELECT COUNT(*) FROM video_likes WHERE video_id = ? AND user_id = ?) AS liked_by_me`,
		videoID,
		videoID,
		userID,
	)

	var likeCount int64
	var likedByMeCount int64
	if err := row.Scan(&likeCount, &likedByMeCount); err != nil {
		return likeStateResponse{}, err
	}

	return likeStateResponse{
		VideoID:   videoID,
		LikeCount: likeCount,
		LikedByMe: likedByMeCount > 0,
	}, nil
}
