package main

import (
	"context"
	"crypto/rand"
	"crypto/sha256"
	"database/sql"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"net/http"
	"strings"
	"time"
)

const sessionLifetime = 30 * 24 * time.Hour

type authUserRow struct {
	ID           int64
	Username     string
	Email        string
	Nickname     sql.NullString
	AvatarURL    sql.NullString
	PasswordHash string
	PasswordSalt string
}

type authUserResponse struct {
	ID         int64  `json:"id"`
	Username   string `json:"username"`
	Email      string `json:"email"`
	Nickname   string `json:"nickname,omitempty"`
	AvatarURL  string `json:"avatarUrl,omitempty"`
}

type authMutationRequest struct {
	Username string `json:"username"`
	Password string `json:"password"`
	Email    string `json:"email"`
}

func (s *apiServer) handleRegister(writer http.ResponseWriter, request *http.Request) {
	if request.Method != http.MethodPost {
		writeMethodNotAllowed(writer)
		return
	}
	if request.URL.Path != "/api/auth/register" {
		writeNotFound(writer)
		return
	}

	var payload authMutationRequest
	if err := json.NewDecoder(request.Body).Decode(&payload); err != nil {
		writeJSON(writer, http.StatusBadRequest, map[string]string{"error": "invalid register payload"})
		return
	}

	payload.Username = strings.TrimSpace(payload.Username)
	payload.Email = strings.TrimSpace(payload.Email)
	if payload.Username == "" || payload.Password == "" || payload.Email == "" {
		writeJSON(writer, http.StatusBadRequest, map[string]string{"error": "username, password, and email are required"})
		return
	}

	ctx, cancel := context.WithTimeout(request.Context(), 5*time.Second)
	defer cancel()

	existingUser, err := s.findUserByUsername(ctx, payload.Username)
	if err != nil && !errors.Is(err, sql.ErrNoRows) {
		writeServerError(writer, fmt.Errorf("find existing user: %w", err))
		return
	}
	if err == nil && existingUser.ID > 0 {
		writeJSON(writer, http.StatusConflict, map[string]string{"error": "username already exists"})
		return
	}

	salt, err := generateSecretHex(16)
	if err != nil {
		writeServerError(writer, fmt.Errorf("generate password salt: %w", err))
		return
	}

	result, err := s.database.ExecContext(
		ctx,
		`INSERT INTO users (username, email, nickname, password_hash, password_salt)
		 VALUES (?, ?, ?, ?, ?)`,
		payload.Username,
		payload.Email,
		payload.Username,
		hashSecret(payload.Password, salt),
		salt,
	)
	if err != nil {
		if isDuplicateEntryError(err) {
			writeJSON(writer, http.StatusConflict, map[string]string{"error": "username or email already exists"})
			return
		}
		writeServerError(writer, fmt.Errorf("insert user: %w", err))
		return
	}

	userID, err := result.LastInsertId()
	if err != nil {
		writeServerError(writer, fmt.Errorf("read inserted user id: %w", err))
		return
	}

	user, err := s.findUserByID(ctx, userID)
	if err != nil {
		writeServerError(writer, fmt.Errorf("find registered user: %w", err))
		return
	}

	token, expiresAt, err := s.createSession(ctx, user.ID)
	if err != nil {
		writeServerError(writer, fmt.Errorf("create register session: %w", err))
		return
	}

	writeJSON(writer, http.StatusCreated, map[string]any{
		"user":        toAuthUserResponse(user),
		"accessToken": token,
		"expiresAt":   expiresAt.Format(time.RFC3339),
	})
}

func (s *apiServer) handleLogin(writer http.ResponseWriter, request *http.Request) {
	if request.Method != http.MethodPost {
		writeMethodNotAllowed(writer)
		return
	}
	if request.URL.Path != "/api/auth/login" {
		writeNotFound(writer)
		return
	}

	var payload authMutationRequest
	if err := json.NewDecoder(request.Body).Decode(&payload); err != nil {
		writeJSON(writer, http.StatusBadRequest, map[string]string{"error": "invalid login payload"})
		return
	}

	payload.Username = strings.TrimSpace(payload.Username)
	if payload.Username == "" || payload.Password == "" {
		writeJSON(writer, http.StatusBadRequest, map[string]string{"error": "username and password are required"})
		return
	}

	ctx, cancel := context.WithTimeout(request.Context(), 5*time.Second)
	defer cancel()

	user, err := s.findUserByUsername(ctx, payload.Username)
	if err != nil {
		if errors.Is(err, sql.ErrNoRows) {
			writeJSON(writer, http.StatusUnauthorized, map[string]string{"error": "invalid username or password"})
			return
		}
		writeServerError(writer, fmt.Errorf("find user for login: %w", err))
		return
	}

	if user.PasswordHash != hashSecret(payload.Password, user.PasswordSalt) {
		writeJSON(writer, http.StatusUnauthorized, map[string]string{"error": "invalid username or password"})
		return
	}

	token, expiresAt, err := s.createSession(ctx, user.ID)
	if err != nil {
		writeServerError(writer, fmt.Errorf("create login session: %w", err))
		return
	}

	writeJSON(writer, http.StatusOK, map[string]any{
		"user":        toAuthUserResponse(user),
		"accessToken": token,
		"expiresAt":   expiresAt.Format(time.RFC3339),
	})
}

func (s *apiServer) handleLogout(writer http.ResponseWriter, request *http.Request) {
	if request.Method != http.MethodPost {
		writeMethodNotAllowed(writer)
		return
	}
	if request.URL.Path != "/api/auth/logout" {
		writeNotFound(writer)
		return
	}

	token, ok := bearerTokenFromRequest(request)
	if !ok {
		writeJSON(writer, http.StatusUnauthorized, map[string]string{"error": "missing bearer token"})
		return
	}

	ctx, cancel := context.WithTimeout(request.Context(), 5*time.Second)
	defer cancel()

	if err := s.revokeSession(ctx, token); err != nil {
		writeServerError(writer, fmt.Errorf("revoke session: %w", err))
		return
	}

	writeJSON(writer, http.StatusOK, map[string]string{"message": "logout succeeded"})
}

func (s *apiServer) handleMe(writer http.ResponseWriter, request *http.Request) {
	if request.Method != http.MethodGet {
		writeMethodNotAllowed(writer)
		return
	}
	if request.URL.Path != "/api/me" {
		writeNotFound(writer)
		return
	}

	ctx, cancel := context.WithTimeout(request.Context(), 5*time.Second)
	defer cancel()

	user, err := s.requireAuthenticatedUser(ctx, request)
	if err != nil {
		writeJSON(writer, http.StatusUnauthorized, map[string]string{"error": err.Error()})
		return
	}

	writeJSON(writer, http.StatusOK, map[string]any{
		"user": toAuthUserResponse(user),
	})
}

func (s *apiServer) requireAuthenticatedUser(ctx context.Context, request *http.Request) (authUserRow, error) {
	token, ok := bearerTokenFromRequest(request)
	if !ok {
		return authUserRow{}, errors.New("missing bearer token")
	}

	user, err := s.findUserByAccessToken(ctx, token)
	if err != nil {
		if errors.Is(err, sql.ErrNoRows) {
			return authUserRow{}, errors.New("invalid or expired session")
		}
		return authUserRow{}, err
	}

	if err := s.touchSession(ctx, token); err != nil {
		return authUserRow{}, err
	}

	return user, nil
}

func (s *apiServer) optionalAuthenticatedUser(ctx context.Context, request *http.Request) (*authUserRow, error) {
	token, ok := bearerTokenFromRequest(request)
	if !ok {
		return nil, nil
	}

	user, err := s.findUserByAccessToken(ctx, token)
	if err != nil {
		if errors.Is(err, sql.ErrNoRows) {
			return nil, nil
		}
		return nil, err
	}

	if err := s.touchSession(ctx, token); err != nil {
		return nil, err
	}

	return &user, nil
}

func (s *apiServer) findUserByUsername(ctx context.Context, username string) (authUserRow, error) {
	row := s.database.QueryRowContext(
		ctx,
		`SELECT id, username, email, nickname, avatar_url, password_hash, password_salt
		 FROM users
		 WHERE username = ?`,
		username,
	)

	var user authUserRow
	err := row.Scan(
		&user.ID,
		&user.Username,
		&user.Email,
		&user.Nickname,
		&user.AvatarURL,
		&user.PasswordHash,
		&user.PasswordSalt,
	)
	return user, err
}

func (s *apiServer) findUserByID(ctx context.Context, userID int64) (authUserRow, error) {
	row := s.database.QueryRowContext(
		ctx,
		`SELECT id, username, email, nickname, avatar_url, password_hash, password_salt
		 FROM users
		 WHERE id = ?`,
		userID,
	)

	var user authUserRow
	err := row.Scan(
		&user.ID,
		&user.Username,
		&user.Email,
		&user.Nickname,
		&user.AvatarURL,
		&user.PasswordHash,
		&user.PasswordSalt,
	)
	return user, err
}

func (s *apiServer) findUserByAccessToken(ctx context.Context, token string) (authUserRow, error) {
	row := s.database.QueryRowContext(
		ctx,
		`SELECT u.id, u.username, u.email, u.nickname, u.avatar_url, u.password_hash, u.password_salt
		 FROM user_sessions session
		 INNER JOIN users u ON u.id = session.user_id
		 WHERE session.token_hash = ?
		   AND session.revoked_at IS NULL
		   AND session.expires_at > NOW()
		 LIMIT 1`,
		hashSecret(token, ""),
	)

	var user authUserRow
	err := row.Scan(
		&user.ID,
		&user.Username,
		&user.Email,
		&user.Nickname,
		&user.AvatarURL,
		&user.PasswordHash,
		&user.PasswordSalt,
	)
	return user, err
}

func (s *apiServer) createSession(ctx context.Context, userID int64) (string, time.Time, error) {
	token, err := generateSecretHex(32)
	if err != nil {
		return "", time.Time{}, err
	}

	expiresAt := time.Now().Add(sessionLifetime)
	_, err = s.database.ExecContext(
		ctx,
		`INSERT INTO user_sessions (user_id, token_hash, expires_at, last_seen_at)
		 VALUES (?, ?, ?, NOW())`,
		userID,
		hashSecret(token, ""),
		expiresAt,
	)
	if err != nil {
		return "", time.Time{}, err
	}

	return token, expiresAt, nil
}

func (s *apiServer) revokeSession(ctx context.Context, token string) error {
	_, err := s.database.ExecContext(
		ctx,
		`UPDATE user_sessions
		 SET revoked_at = NOW(),
		     last_seen_at = NOW()
		 WHERE token_hash = ?
		   AND revoked_at IS NULL`,
		hashSecret(token, ""),
	)
	return err
}

func (s *apiServer) touchSession(ctx context.Context, token string) error {
	_, err := s.database.ExecContext(
		ctx,
		`UPDATE user_sessions
		 SET last_seen_at = NOW()
		 WHERE token_hash = ?
		   AND revoked_at IS NULL`,
		hashSecret(token, ""),
	)
	return err
}

func toAuthUserResponse(user authUserRow) authUserResponse {
	response := authUserResponse{
		ID:       user.ID,
		Username: user.Username,
		Email:    user.Email,
	}
	if user.Nickname.Valid {
		response.Nickname = user.Nickname.String
	}
	if user.AvatarURL.Valid {
		response.AvatarURL = user.AvatarURL.String
	}
	return response
}

func bearerTokenFromRequest(request *http.Request) (string, bool) {
	header := strings.TrimSpace(request.Header.Get("Authorization"))
	if header == "" {
		return "", false
	}

	parts := strings.SplitN(header, " ", 2)
	if len(parts) != 2 || !strings.EqualFold(parts[0], "Bearer") {
		return "", false
	}

	token := strings.TrimSpace(parts[1])
	return token, token != ""
}

func generateSecretHex(byteLength int) (string, error) {
	buffer := make([]byte, byteLength)
	if _, err := rand.Read(buffer); err != nil {
		return "", err
	}
	return hex.EncodeToString(buffer), nil
}

func hashSecret(value, salt string) string {
	digest := sha256.Sum256([]byte(value + ":" + salt))
	return hex.EncodeToString(digest[:])
}

func isDuplicateEntryError(err error) bool {
	return err != nil && strings.Contains(strings.ToLower(err.Error()), "duplicate entry")
}
