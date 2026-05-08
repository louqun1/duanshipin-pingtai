package main

import (
	"context"
	"database/sql"
	"encoding/json"
	"errors"
	"fmt"
	"log"
	"net/http"
	"strings"
	"time"
)

const (
	liveRoomStatusLive   = "live"
	liveRoomStatusClosed = "closed"

	liveRoomRoleController  = "controller"
	liveRoomRoleParticipant = "participant"

	linkMicRequestStatePending   = "pending"
	linkMicRequestStateAccepted  = "accepted"
	linkMicRequestStateRejected  = "rejected"
	linkMicRequestStateCancelled = "cancelled"
	linkMicRequestStateEnded     = "ended"

	liveRoomPresenceFreshWindow = 2 * time.Minute
)

type dbQuerier interface {
	ExecContext(ctx context.Context, query string, args ...any) (sql.Result, error)
	QueryContext(ctx context.Context, query string, args ...any) (*sql.Rows, error)
	QueryRowContext(ctx context.Context, query string, args ...any) *sql.Row
}

type createLiveRoomRequest struct {
	RoomKey string `json:"roomKey"`
	Title   string `json:"title"`
}

type liveRoomPresenceRequest struct {
	Action string `json:"action"`
}

type liveRoomApplyRequest struct {
	Note string `json:"note"`
}

type liveRoomInviteRequest struct {
	TargetUserID int64 `json:"targetUserId"`
}

type liveRoomRespondRequest struct {
	RequestID string `json:"requestId"`
	Action    string `json:"action"`
}

type liveRoomCancelRequest struct {
	RequestID string `json:"requestId"`
}

type liveRoomHangupRequest struct {
	RequestID string `json:"requestId"`
}

type roomUserRow struct {
	ID        int64
	Username  string
	Nickname  sql.NullString
	AvatarURL sql.NullString
}

type liveRoomRow struct {
	ID            int64
	RoomKey       string
	OwnerUserID   int64
	OwnerUsername string
	OwnerNickname sql.NullString
	OwnerAvatar   sql.NullString
	Title         sql.NullString
	Status        string
	CreatedAt     time.Time
	UpdatedAt     time.Time
}

type liveRoomMemberRow struct {
	User       roomUserRow
	Role       string
	IsOnline   bool
	LastSeenAt time.Time
}

type linkMicRequestRow struct {
	ID             int64
	RequestID      string
	RoomID         int64
	RequestType    string
	State          string
	Initiator      roomUserRow
	Target         roomUserRow
	AcceptedByUser sql.NullInt64
	EndedByUser    sql.NullInt64
	MetadataJSON   sql.NullString
	CreatedAt      time.Time
	UpdatedAt      time.Time
	RespondedAt    sql.NullTime
	EndedAt        sql.NullTime
}

type roomUserResponse struct {
	ID        int64  `json:"id"`
	Username  string `json:"username"`
	Nickname  string `json:"nickname,omitempty"`
	AvatarURL string `json:"avatarUrl,omitempty"`
}

type liveRoomMemberResponse struct {
	User           roomUserResponse `json:"user"`
	Role           string           `json:"role"`
	IsOnline       bool             `json:"isOnline"`
	IsController   bool             `json:"isController"`
	IsLinkMicActive bool            `json:"isLinkMicActive"`
	LastSeenAt     string           `json:"lastSeenAt"`
}

type linkMicRequestResponse struct {
	RequestID   string           `json:"requestId"`
	RequestType string           `json:"requestType"`
	State       string           `json:"state"`
	Initiator   roomUserResponse `json:"initiator"`
	Target      roomUserResponse `json:"target"`
	Metadata    any              `json:"metadata,omitempty"`
	CreatedAt   string           `json:"createdAt"`
	UpdatedAt   string           `json:"updatedAt"`
	RespondedAt string           `json:"respondedAt,omitempty"`
	EndedAt     string           `json:"endedAt,omitempty"`
}

type rtcJoinParamsResponse struct {
	Transport         string `json:"transport"`
	RoomName          string `json:"roomName"`
	ParticipantRole   string `json:"participantRole"`
	SignalingURL      string `json:"signalingUrl"`
	RequestID         string `json:"requestId"`
	RequestType       string `json:"requestType"`
	ControllerUserID  int64  `json:"controllerUserId"`
	ParticipantUserID int64  `json:"participantUserId"`
}

type liveRoomResponse struct {
	ID                int64                    `json:"id"`
	RoomKey           string                   `json:"roomKey"`
	Title             string                   `json:"title,omitempty"`
	Status            string                   `json:"status"`
	SignalingURL      string                   `json:"signalingUrl,omitempty"`
	Owner             roomUserResponse         `json:"owner"`
	OnlineMemberCount int                      `json:"onlineMemberCount"`
	ActiveRequest     *linkMicRequestResponse  `json:"activeRequest,omitempty"`
	CreatedAt         string                   `json:"createdAt"`
	UpdatedAt         string                   `json:"updatedAt"`
}

type liveRoomUpdatedEvent struct {
	Action        string                  `json:"action"`
	RoomKey       string                  `json:"roomKey"`
	ActorUserID   int64                   `json:"actorUserId,omitempty"`
	RequestID     string                  `json:"requestId,omitempty"`
	Room          *liveRoomResponse       `json:"room,omitempty"`
	Request       *linkMicRequestResponse `json:"request,omitempty"`
	RtcJoinParams *rtcJoinParamsResponse  `json:"rtcJoinParams,omitempty"`
}

type liveRoomAPIError struct {
	StatusCode int
	Message    string
}

func (e *liveRoomAPIError) Error() string {
	return e.Message
}

func newLiveRoomAPIError(statusCode int, format string, args ...any) error {
	return &liveRoomAPIError{
		StatusCode: statusCode,
		Message:    fmt.Sprintf(format, args...),
	}
}

func (s *apiServer) handleLiveRooms(writer http.ResponseWriter, request *http.Request) {
	if request.URL.Path != "/api/live-rooms" {
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

	switch request.Method {
	case http.MethodGet:
		rooms, err := s.listLiveRoomResponses(ctx)
		if err != nil {
			writeServerError(writer, fmt.Errorf("list live rooms: %w", err))
			return
		}
		writeJSON(writer, http.StatusOK, map[string]any{
			"items": rooms,
		})
	case http.MethodPost:
		var payload createLiveRoomRequest
		if err := decodeOptionalJSON(request, &payload); err != nil {
			writeJSON(writer, http.StatusBadRequest, map[string]string{"error": "invalid create room payload"})
			return
		}

		room, err := s.createLiveRoom(ctx, user, payload)
		if err != nil {
			writeLiveRoomMutationError(writer, err)
			return
		}

		s.publishLiveRoomEvent(ctx, "room.created", user.ID, room.RoomKey, "")
		writeJSON(writer, http.StatusCreated, map[string]any{
			"room": room,
		})
	default:
		writeMethodNotAllowed(writer)
	}
}

func (s *apiServer) handleLiveRoomRoute(writer http.ResponseWriter, request *http.Request) {
	trimmedPath := strings.TrimPrefix(request.URL.Path, "/api/live-rooms/")
	if trimmedPath == request.URL.Path || strings.TrimSpace(trimmedPath) == "" {
		writeNotFound(writer)
		return
	}

	parts := strings.Split(strings.Trim(trimmedPath, "/"), "/")
	if len(parts) == 0 || strings.TrimSpace(parts[0]) == "" {
		writeNotFound(writer)
		return
	}
	roomKey := normalizeLiveRoomKey(parts[0])
	if roomKey == "" {
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

	if len(parts) == 1 {
		if request.Method != http.MethodGet {
			writeMethodNotAllowed(writer)
			return
		}
		room, err := s.getLiveRoomResponseByKey(ctx, roomKey)
		if err != nil {
			if errors.Is(err, sql.ErrNoRows) {
				writeNotFound(writer)
				return
			}
			writeServerError(writer, fmt.Errorf("get live room: %w", err))
			return
		}
		writeJSON(writer, http.StatusOK, map[string]any{"room": room})
		return
	}

	if len(parts) == 2 && parts[1] == "members" {
		if request.Method != http.MethodGet {
			writeMethodNotAllowed(writer)
			return
		}
		room, members, err := s.getLiveRoomMembers(ctx, roomKey)
		if err != nil {
			if errors.Is(err, sql.ErrNoRows) {
				writeNotFound(writer)
				return
			}
			writeServerError(writer, fmt.Errorf("get live room members: %w", err))
			return
		}
		writeJSON(writer, http.StatusOK, map[string]any{
			"room":    room,
			"members": members,
		})
		return
	}

	if len(parts) == 2 && parts[1] == "presence" {
		if request.Method != http.MethodPost {
			writeMethodNotAllowed(writer)
			return
		}
		s.handleLiveRoomPresence(writer, request, ctx, roomKey, user)
		return
	}

	if len(parts) == 2 && parts[1] == "close" {
		if request.Method != http.MethodPost {
			writeMethodNotAllowed(writer)
			return
		}
		s.handleCloseLiveRoom(writer, request, ctx, roomKey, user)
		return
	}

	if len(parts) == 3 && parts[1] == "linkmic" {
		switch parts[2] {
		case "apply":
			if request.Method != http.MethodPost {
				writeMethodNotAllowed(writer)
				return
			}
			s.handleLiveRoomApply(writer, request, ctx, roomKey, user)
			return
		case "invite":
			if request.Method != http.MethodPost {
				writeMethodNotAllowed(writer)
				return
			}
			s.handleLiveRoomInvite(writer, request, ctx, roomKey, user)
			return
		case "respond":
			if request.Method != http.MethodPost {
				writeMethodNotAllowed(writer)
				return
			}
			s.handleLiveRoomRespond(writer, request, ctx, roomKey, user)
			return
		case "cancel":
			if request.Method != http.MethodPost {
				writeMethodNotAllowed(writer)
				return
			}
			s.handleLiveRoomCancel(writer, request, ctx, roomKey, user)
			return
		case "hangup":
			if request.Method != http.MethodPost {
				writeMethodNotAllowed(writer)
				return
			}
			s.handleLiveRoomHangup(writer, request, ctx, roomKey, user)
			return
		case "state":
			if request.Method != http.MethodGet {
				writeMethodNotAllowed(writer)
				return
			}
			s.handleLiveRoomLinkMicState(writer, request, ctx, roomKey, user)
			return
		default:
			writeNotFound(writer)
			return
		}
	}

	writeNotFound(writer)
}

func (s *apiServer) handleLiveRoomPresence(writer http.ResponseWriter, request *http.Request, ctx context.Context, roomKey string, user authUserRow) {
	var payload liveRoomPresenceRequest
	if err := decodeOptionalJSON(request, &payload); err != nil {
		writeJSON(writer, http.StatusBadRequest, map[string]string{"error": "invalid presence payload"})
		return
	}

	action := strings.ToLower(strings.TrimSpace(payload.Action))
	if action == "" {
		action = "join"
	}

	room, err := s.findLiveRoomByKey(ctx, roomKey)
	if err != nil {
		if errors.Is(err, sql.ErrNoRows) {
			writeNotFound(writer)
			return
		}
		writeServerError(writer, fmt.Errorf("find live room for presence: %w", err))
		return
	}

	role := liveRoomRoleParticipant
	if room.OwnerUserID == user.ID {
		role = liveRoomRoleController
	}

	var eventAction string
	switch action {
	case "join", "heartbeat":
		if room.Status != liveRoomStatusLive {
			writeJSON(writer, http.StatusConflict, map[string]string{"error": "live room is not active"})
			return
		}
		if err := s.upsertLiveRoomPresence(ctx, room.ID, user.ID, role); err != nil {
			writeServerError(writer, fmt.Errorf("upsert room presence: %w", err))
			return
		}
		eventAction = "presence." + action
	case "leave":
		if err := s.leaveLiveRoomPresence(ctx, room.ID, user.ID); err != nil {
			writeServerError(writer, fmt.Errorf("leave room presence: %w", err))
			return
		}
		if err := s.endLiveRoomRequestForLeavingUser(ctx, room.ID, user.ID); err != nil {
			writeServerError(writer, fmt.Errorf("end live room request on leave: %w", err))
			return
		}
		eventAction = "presence.leave"
	default:
		writeJSON(writer, http.StatusBadRequest, map[string]string{"error": "presence action must be join, heartbeat, or leave"})
		return
	}

	roomResponse, members, err := s.getLiveRoomMembers(ctx, roomKey)
	if err != nil {
		writeServerError(writer, fmt.Errorf("reload live room members: %w", err))
		return
	}

	log.Printf("[presence] room presence action=%s roomKey=%s userId=%d role=%s members=%d",
		action,
		roomKey,
		user.ID,
		role,
		len(members),
	)
	if s.probeSignals != nil {
		s.probeSignals.broadcastAnchorMemberList(roomKey)
	}
	s.publishLiveRoomEvent(ctx, eventAction, user.ID, roomKey, "")
	writeJSON(writer, http.StatusOK, map[string]any{
		"room":    roomResponse,
		"members": members,
	})
}

func (s *apiServer) handleLiveRoomApply(writer http.ResponseWriter, request *http.Request, ctx context.Context, roomKey string, user authUserRow) {
	var payload liveRoomApplyRequest
	if err := decodeOptionalJSON(request, &payload); err != nil {
		writeJSON(writer, http.StatusBadRequest, map[string]string{"error": "invalid apply payload"})
		return
	}

	room, requestResponse, err := s.createLiveRoomLinkMicRequest(ctx, roomKey, user, linkMicTypeApply, 0, payload.Note)
	if err != nil {
		writeLiveRoomMutationError(writer, err)
		return
	}

	s.publishLiveRoomEvent(ctx, "linkmic.apply", user.ID, room.RoomKey, requestResponse.RequestID)
	writeJSON(writer, http.StatusCreated, map[string]any{
		"room":    room,
		"request": requestResponse,
	})
}

func (s *apiServer) handleLiveRoomInvite(writer http.ResponseWriter, request *http.Request, ctx context.Context, roomKey string, user authUserRow) {
	var payload liveRoomInviteRequest
	if err := decodeOptionalJSON(request, &payload); err != nil {
		writeJSON(writer, http.StatusBadRequest, map[string]string{"error": "invalid invite payload"})
		return
	}
	if payload.TargetUserID <= 0 {
		writeJSON(writer, http.StatusBadRequest, map[string]string{"error": "targetUserId is required"})
		return
	}

	room, requestResponse, err := s.createLiveRoomLinkMicRequest(ctx, roomKey, user, linkMicTypeInvite, payload.TargetUserID, "")
	if err != nil {
		writeLiveRoomMutationError(writer, err)
		return
	}

	s.publishLiveRoomEvent(ctx, "linkmic.invite", user.ID, room.RoomKey, requestResponse.RequestID)
	writeJSON(writer, http.StatusCreated, map[string]any{
		"room":    room,
		"request": requestResponse,
	})
}

func (s *apiServer) handleLiveRoomRespond(writer http.ResponseWriter, request *http.Request, ctx context.Context, roomKey string, user authUserRow) {
	var payload liveRoomRespondRequest
	if err := decodeOptionalJSON(request, &payload); err != nil {
		writeJSON(writer, http.StatusBadRequest, map[string]string{"error": "invalid respond payload"})
		return
	}
	if strings.TrimSpace(payload.RequestID) == "" {
		writeJSON(writer, http.StatusBadRequest, map[string]string{"error": "requestId is required"})
		return
	}

	action := strings.ToLower(strings.TrimSpace(payload.Action))
	if action != "accept" && action != "reject" {
		writeJSON(writer, http.StatusBadRequest, map[string]string{"error": "action must be accept or reject"})
		return
	}

	room, requestResponse, err := s.respondLiveRoomLinkMicRequest(ctx, roomKey, user, payload.RequestID, action)
	if err != nil {
		writeLiveRoomMutationError(writer, err)
		return
	}

	response := map[string]any{
		"room":    room,
		"request": requestResponse,
	}
	if action == "accept" {
		if rtcJoinParams := s.buildRTCJoinParams(*room, *requestResponse, user.ID); rtcJoinParams != nil {
			response["rtcJoinParams"] = rtcJoinParams
		}
	}

	s.publishLiveRoomEvent(ctx, "linkmic."+action, user.ID, room.RoomKey, requestResponse.RequestID)
	writeJSON(writer, http.StatusOK, response)
}

func (s *apiServer) handleLiveRoomCancel(writer http.ResponseWriter, request *http.Request, ctx context.Context, roomKey string, user authUserRow) {
	var payload liveRoomCancelRequest
	if err := decodeOptionalJSON(request, &payload); err != nil {
		writeJSON(writer, http.StatusBadRequest, map[string]string{"error": "invalid cancel payload"})
		return
	}
	if strings.TrimSpace(payload.RequestID) == "" {
		writeJSON(writer, http.StatusBadRequest, map[string]string{"error": "requestId is required"})
		return
	}

	room, requestResponse, err := s.transitionLiveRoomLinkMicRequest(ctx, roomKey, user, payload.RequestID, linkMicRequestStateCancelled)
	if err != nil {
		writeLiveRoomMutationError(writer, err)
		return
	}

	s.publishLiveRoomEvent(ctx, "linkmic.cancel", user.ID, room.RoomKey, requestResponse.RequestID)
	writeJSON(writer, http.StatusOK, map[string]any{
		"room":    room,
		"request": requestResponse,
	})
}

func (s *apiServer) handleLiveRoomHangup(writer http.ResponseWriter, request *http.Request, ctx context.Context, roomKey string, user authUserRow) {
	var payload liveRoomHangupRequest
	if err := decodeOptionalJSON(request, &payload); err != nil {
		writeJSON(writer, http.StatusBadRequest, map[string]string{"error": "invalid hangup payload"})
		return
	}
	if strings.TrimSpace(payload.RequestID) == "" {
		writeJSON(writer, http.StatusBadRequest, map[string]string{"error": "requestId is required"})
		return
	}

	room, requestResponse, err := s.transitionLiveRoomLinkMicRequest(ctx, roomKey, user, payload.RequestID, linkMicRequestStateEnded)
	if err != nil {
		writeLiveRoomMutationError(writer, err)
		return
	}

	s.publishLiveRoomEvent(ctx, "linkmic.hangup", user.ID, room.RoomKey, requestResponse.RequestID)
	writeJSON(writer, http.StatusOK, map[string]any{
		"room":    room,
		"request": requestResponse,
	})
}

func (s *apiServer) handleLiveRoomLinkMicState(writer http.ResponseWriter, _ *http.Request, ctx context.Context, roomKey string, user authUserRow) {
	room, err := s.getLiveRoomResponseByKey(ctx, roomKey)
	if err != nil {
		if errors.Is(err, sql.ErrNoRows) {
			writeNotFound(writer)
			return
		}
		writeServerError(writer, fmt.Errorf("get live room state: %w", err))
		return
	}

	response := map[string]any{
		"room": room,
	}
	if room.ActiveRequest != nil {
		response["request"] = room.ActiveRequest
		if rtcJoinParams := s.buildRTCJoinParams(*room, *room.ActiveRequest, user.ID); rtcJoinParams != nil {
			response["rtcJoinParams"] = rtcJoinParams
		}
	}

	writeJSON(writer, http.StatusOK, response)
}

func (s *apiServer) handleCloseLiveRoom(writer http.ResponseWriter, _ *http.Request, ctx context.Context, roomKey string, user authUserRow) {
	room, err := s.closeLiveRoom(ctx, roomKey, user)
	if err != nil {
		writeLiveRoomMutationError(writer, err)
		return
	}

	s.publishLiveRoomEvent(ctx, "room.closed", user.ID, room.RoomKey, "")
	writeJSON(writer, http.StatusOK, map[string]any{
		"room": room,
	})
}

func (s *apiServer) createLiveRoom(ctx context.Context, owner authUserRow, payload createLiveRoomRequest) (*liveRoomResponse, error) {
	title := strings.TrimSpace(payload.Title)
	roomKey := normalizeLiveRoomKey(payload.RoomKey)
	var err error
	if roomKey == "" {
		roomKey, err = s.generateLiveRoomKey(ctx, owner.Username)
		if err != nil {
			return nil, err
		}
	}

	tx, err := s.database.BeginTx(ctx, nil)
	if err != nil {
		return nil, err
	}
	defer tx.Rollback()

	if err := tx.QueryRowContext(
		ctx,
		`SELECT id FROM users WHERE id = ? FOR UPDATE`,
		owner.ID,
	).Scan(new(int64)); err != nil {
		return nil, err
	}

	existingRoom, err := findLiveRoomByOwnerAndStatusQuerier(ctx, tx, owner.ID, liveRoomStatusLive)
	if err == nil {
		return nil, newLiveRoomAPIError(http.StatusConflict, "user already has a live room: %s", existingRoom.RoomKey)
	}
	if err != nil && !errors.Is(err, sql.ErrNoRows) {
		return nil, err
	}

	result, err := tx.ExecContext(
		ctx,
		`INSERT INTO live_rooms (room_key, owner_user_id, title, status)
		 VALUES (?, ?, ?, ?)`,
		roomKey,
		owner.ID,
		nullIfEmpty(title),
		liveRoomStatusLive,
	)
	if err != nil {
		if isDuplicateEntryError(err) {
			return nil, newLiveRoomAPIError(http.StatusConflict, "room key already exists")
		}
		return nil, err
	}

	roomID, err := result.LastInsertId()
	if err != nil {
		return nil, err
	}

	if err := upsertLiveRoomPresenceQuerier(ctx, tx, roomID, owner.ID, liveRoomRoleController); err != nil {
		return nil, err
	}

	if err := tx.Commit(); err != nil {
		return nil, err
	}

	return s.getLiveRoomResponseByKey(ctx, roomKey)
}

func (s *apiServer) listLiveRoomResponses(ctx context.Context) ([]liveRoomResponse, error) {
	rows, err := s.database.QueryContext(
		ctx,
		`SELECT room.id, room.room_key, room.owner_user_id, owner.username, owner.nickname, owner.avatar_url, room.title, room.status, room.created_at, room.updated_at
		 FROM live_rooms room
		 INNER JOIN users owner ON owner.id = room.owner_user_id
		 WHERE room.status = ?
		 ORDER BY room.created_at DESC
		 LIMIT 50`,
		liveRoomStatusLive,
	)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	rooms := make([]liveRoomResponse, 0)
	for rows.Next() {
		var row liveRoomRow
		if err := rows.Scan(
			&row.ID,
			&row.RoomKey,
			&row.OwnerUserID,
			&row.OwnerUsername,
			&row.OwnerNickname,
			&row.OwnerAvatar,
			&row.Title,
			&row.Status,
			&row.CreatedAt,
			&row.UpdatedAt,
		); err != nil {
			return nil, err
		}

		response, err := s.buildLiveRoomResponse(ctx, row)
		if err != nil {
			return nil, err
		}
		rooms = append(rooms, *response)
	}
	if err := rows.Err(); err != nil {
		return nil, err
	}

	return rooms, nil
}

func (s *apiServer) getLiveRoomResponseByKey(ctx context.Context, roomKey string) (*liveRoomResponse, error) {
	room, err := s.findLiveRoomByKey(ctx, roomKey)
	if err != nil {
		return nil, err
	}
	return s.buildLiveRoomResponse(ctx, room)
}

func (s *apiServer) getLiveRoomMembers(ctx context.Context, roomKey string) (*liveRoomResponse, []liveRoomMemberResponse, error) {
	room, err := s.findLiveRoomByKey(ctx, roomKey)
	if err != nil {
		return nil, nil, err
	}

	members, err := s.findLiveRoomMembers(ctx, room.ID)
	if err != nil {
		return nil, nil, err
	}

	roomResponse, err := s.buildLiveRoomResponse(ctx, room)
	if err != nil {
		return nil, nil, err
	}

	activeMemberIDs := make(map[int64]struct{}, 2)
	if roomResponse.ActiveRequest != nil && roomResponse.ActiveRequest.State == linkMicRequestStateAccepted {
		activeMemberIDs[roomResponse.ActiveRequest.Initiator.ID] = struct{}{}
		activeMemberIDs[roomResponse.ActiveRequest.Target.ID] = struct{}{}
	}

	responses := make([]liveRoomMemberResponse, 0, len(members))
	for _, member := range members {
		responses = append(responses, liveRoomMemberResponse{
			User:            toRoomUserResponse(member.User),
			Role:            member.Role,
			IsOnline:        member.IsOnline,
			IsController:    member.Role == liveRoomRoleController,
			IsLinkMicActive: memberIsLinkMicActive(activeMemberIDs, member.User.ID),
			LastSeenAt:      member.LastSeenAt.Format(time.RFC3339),
		})
	}

	return roomResponse, responses, nil
}

func (s *apiServer) createLiveRoomLinkMicRequest(ctx context.Context, roomKey string, user authUserRow, requestType string, targetUserID int64, note string) (*liveRoomResponse, *linkMicRequestResponse, error) {
	tx, err := s.database.BeginTx(ctx, nil)
	if err != nil {
		return nil, nil, err
	}
	defer tx.Rollback()

	room, err := findLiveRoomByKeyForUpdateQuerier(ctx, tx, roomKey)
	if err != nil {
		return nil, nil, err
	}
	if room.Status != liveRoomStatusLive {
		return nil, nil, newLiveRoomAPIError(http.StatusConflict, "live room is not active")
	}

	initiatorRole := liveRoomRoleParticipant
	if room.OwnerUserID == user.ID {
		initiatorRole = liveRoomRoleController
	}
	if err := upsertLiveRoomPresenceQuerier(ctx, tx, room.ID, user.ID, initiatorRole); err != nil {
		return nil, nil, err
	}
	if err := ensureNoActiveLinkMicRequestQuerier(ctx, tx, room.ID); err != nil {
		return nil, nil, err
	}

	switch requestType {
	case linkMicTypeApply:
		if room.OwnerUserID == user.ID {
			return nil, nil, newLiveRoomAPIError(http.StatusForbidden, "controller cannot apply to their own room")
		}
		targetUserID = room.OwnerUserID
	case linkMicTypeInvite:
		if room.OwnerUserID != user.ID {
			return nil, nil, newLiveRoomAPIError(http.StatusForbidden, "only room owner can invite participants")
		}
		if targetUserID <= 0 || targetUserID == user.ID {
			return nil, nil, newLiveRoomAPIError(http.StatusBadRequest, "targetUserId must be another room member")
		}
	default:
		return nil, nil, newLiveRoomAPIError(http.StatusBadRequest, "unsupported request type")
	}

	targetPresence, err := findLiveRoomPresenceQuerier(ctx, tx, room.ID, targetUserID)
	if err != nil {
		if errors.Is(err, sql.ErrNoRows) {
			return nil, nil, newLiveRoomAPIError(http.StatusConflict, "target user is not online in this room")
		}
		return nil, nil, err
	}
	if !targetPresence.IsOnline {
		return nil, nil, newLiveRoomAPIError(http.StatusConflict, "target user is not online in this room")
	}

	requestID, err := generateSecretHex(16)
	if err != nil {
		return nil, nil, err
	}
	requestID = "lm-" + requestID

	metadataRaw, err := json.Marshal(map[string]any{
		"note": note,
	})
	if err != nil {
		return nil, nil, err
	}

	if _, err := tx.ExecContext(
		ctx,
		`INSERT INTO linkmic_requests (request_id, room_id, request_type, state, initiator_user_id, target_user_id, metadata_json)
		 VALUES (?, ?, ?, ?, ?, ?, ?)`,
		requestID,
		room.ID,
		requestType,
		linkMicRequestStatePending,
		user.ID,
		targetUserID,
		string(metadataRaw),
	); err != nil {
		return nil, nil, err
	}

	if err := tx.Commit(); err != nil {
		return nil, nil, err
	}

	roomResponse, err := s.getLiveRoomResponseByKey(ctx, roomKey)
	if err != nil {
		return nil, nil, err
	}
	if roomResponse.ActiveRequest == nil {
		return roomResponse, nil, fmt.Errorf("created request but failed to reload active request")
	}
	return roomResponse, roomResponse.ActiveRequest, nil
}

func (s *apiServer) respondLiveRoomLinkMicRequest(ctx context.Context, roomKey string, user authUserRow, requestID, action string) (*liveRoomResponse, *linkMicRequestResponse, error) {
	tx, err := s.database.BeginTx(ctx, nil)
	if err != nil {
		return nil, nil, err
	}
	defer tx.Rollback()

	room, err := findLiveRoomByKeyForUpdateQuerier(ctx, tx, roomKey)
	if err != nil {
		return nil, nil, err
	}
	if room.Status != liveRoomStatusLive {
		return nil, nil, newLiveRoomAPIError(http.StatusConflict, "live room is not active")
	}

	role := liveRoomRoleParticipant
	if room.OwnerUserID == user.ID {
		role = liveRoomRoleController
	}
	if err := upsertLiveRoomPresenceQuerier(ctx, tx, room.ID, user.ID, role); err != nil {
		return nil, nil, err
	}

	requestRow, err := findLinkMicRequestByRequestIDQuerier(ctx, tx, room.ID, requestID)
	if err != nil {
		return nil, nil, err
	}
	if requestRow.State != linkMicRequestStatePending {
		return nil, nil, newLiveRoomAPIError(http.StatusConflict, "request is no longer pending")
	}
	if requestRow.Target.ID != user.ID {
		return nil, nil, newLiveRoomAPIError(http.StatusForbidden, "only the target user can %s this request", action)
	}

	nextState := linkMicRequestStateAccepted
	if action == "reject" {
		nextState = linkMicRequestStateRejected
	}

	result, err := tx.ExecContext(
		ctx,
		`UPDATE linkmic_requests
		 SET state = ?, accepted_by_user_id = ?, responded_at = NOW(), updated_at = NOW()
		 WHERE room_id = ? AND request_id = ? AND state = ?`,
		nextState,
		nullableInt64(user.ID, action == "accept"),
		room.ID,
		requestID,
		linkMicRequestStatePending,
	)
	if err != nil {
		return nil, nil, err
	}
	rowsAffected, err := result.RowsAffected()
	if err != nil {
		return nil, nil, err
	}
	if rowsAffected == 0 {
		return nil, nil, newLiveRoomAPIError(http.StatusConflict, "request is no longer pending")
	}

	if err := tx.Commit(); err != nil {
		return nil, nil, err
	}

	roomResponse, err := s.getLiveRoomResponseByKey(ctx, roomKey)
	if err != nil {
		return nil, nil, err
	}
	requestResponse, err := s.findLatestRequestResponseByID(ctx, room.ID, requestID)
	if err != nil {
		return nil, nil, err
	}
	return roomResponse, requestResponse, nil
}

func (s *apiServer) transitionLiveRoomLinkMicRequest(ctx context.Context, roomKey string, user authUserRow, requestID, nextState string) (*liveRoomResponse, *linkMicRequestResponse, error) {
	tx, err := s.database.BeginTx(ctx, nil)
	if err != nil {
		return nil, nil, err
	}
	defer tx.Rollback()

	room, err := findLiveRoomByKeyForUpdateQuerier(ctx, tx, roomKey)
	if err != nil {
		return nil, nil, err
	}
	if room.Status != liveRoomStatusLive {
		return nil, nil, newLiveRoomAPIError(http.StatusConflict, "live room is not active")
	}

	role := liveRoomRoleParticipant
	if room.OwnerUserID == user.ID {
		role = liveRoomRoleController
	}
	if err := upsertLiveRoomPresenceQuerier(ctx, tx, room.ID, user.ID, role); err != nil {
		return nil, nil, err
	}

	requestRow, err := findLinkMicRequestByRequestIDQuerier(ctx, tx, room.ID, requestID)
	if err != nil {
		return nil, nil, err
	}

	switch nextState {
	case linkMicRequestStateCancelled:
		if requestRow.State != linkMicRequestStatePending {
			return nil, nil, newLiveRoomAPIError(http.StatusConflict, "only pending requests can be cancelled")
		}
		if requestRow.Initiator.ID != user.ID {
			return nil, nil, newLiveRoomAPIError(http.StatusForbidden, "only the initiator can cancel this request")
		}
	case linkMicRequestStateEnded:
		if requestRow.State != linkMicRequestStateAccepted {
			return nil, nil, newLiveRoomAPIError(http.StatusConflict, "only accepted requests can be ended")
		}
		if requestRow.Initiator.ID != user.ID && requestRow.Target.ID != user.ID {
			return nil, nil, newLiveRoomAPIError(http.StatusForbidden, "only participants in the active session can hang up")
		}
	default:
		return nil, nil, newLiveRoomAPIError(http.StatusBadRequest, "unsupported transition")
	}

	expectedState := requestRow.State
	result, err := tx.ExecContext(
		ctx,
		`UPDATE linkmic_requests
		 SET state = ?, ended_by_user_id = ?, ended_at = NOW(), updated_at = NOW()
		 WHERE room_id = ? AND request_id = ? AND state = ?`,
		nextState,
		user.ID,
		room.ID,
		requestID,
		expectedState,
	)
	if err != nil {
		return nil, nil, err
	}
	rowsAffected, err := result.RowsAffected()
	if err != nil {
		return nil, nil, err
	}
	if rowsAffected == 0 {
		return nil, nil, newLiveRoomAPIError(http.StatusConflict, "request state changed before the transition completed")
	}

	if err := tx.Commit(); err != nil {
		return nil, nil, err
	}

	roomResponse, err := s.getLiveRoomResponseByKey(ctx, roomKey)
	if err != nil {
		return nil, nil, err
	}
	requestResponse, err := s.findLatestRequestResponseByID(ctx, room.ID, requestID)
	if err != nil {
		return nil, nil, err
	}
	return roomResponse, requestResponse, nil
}

func (s *apiServer) closeLiveRoom(ctx context.Context, roomKey string, user authUserRow) (*liveRoomResponse, error) {
	tx, err := s.database.BeginTx(ctx, nil)
	if err != nil {
		return nil, err
	}
	defer tx.Rollback()

	room, err := findLiveRoomByKeyForUpdateQuerier(ctx, tx, roomKey)
	if err != nil {
		return nil, err
	}
	if room.OwnerUserID != user.ID {
		return nil, newLiveRoomAPIError(http.StatusForbidden, "only room owner can close the live room")
	}
	if room.Status == liveRoomStatusClosed {
		return s.getLiveRoomResponseByKey(ctx, roomKey)
	}

	if _, err := tx.ExecContext(
		ctx,
		`UPDATE live_rooms
		 SET status = ?, updated_at = NOW()
		 WHERE id = ? AND status = ?`,
		liveRoomStatusClosed,
		room.ID,
		liveRoomStatusLive,
	); err != nil {
		return nil, err
	}

	if _, err := tx.ExecContext(
		ctx,
		`UPDATE live_room_presences
		 SET is_online = 0, last_seen_at = NOW(), left_at = COALESCE(left_at, NOW())
		 WHERE room_id = ? AND is_online = 1`,
		room.ID,
	); err != nil {
		return nil, err
	}

	if _, err := tx.ExecContext(
		ctx,
		`UPDATE linkmic_requests
		 SET state = CASE
		         WHEN state = ? THEN ?
		         WHEN state = ? THEN ?
		         ELSE state
		     END,
		     ended_by_user_id = ?,
		     ended_at = CASE
		         WHEN state IN (?, ?) THEN NOW()
		         ELSE ended_at
		     END,
		     updated_at = NOW()
		 WHERE room_id = ?
		   AND state IN (?, ?)`,
		linkMicRequestStatePending,
		linkMicRequestStateCancelled,
		linkMicRequestStateAccepted,
		linkMicRequestStateEnded,
		user.ID,
		linkMicRequestStatePending,
		linkMicRequestStateAccepted,
		room.ID,
		linkMicRequestStatePending,
		linkMicRequestStateAccepted,
	); err != nil {
		return nil, err
	}

	if err := tx.Commit(); err != nil {
		return nil, err
	}

	return s.getLiveRoomResponseByKey(ctx, roomKey)
}

func (s *apiServer) endLiveRoomRequestForLeavingUser(ctx context.Context, roomID, userID int64) error {
	requestRow, err := findCurrentLinkMicRequestQuerier(ctx, s.database, roomID)
	if err != nil {
		if errors.Is(err, sql.ErrNoRows) {
			return nil
		}
		return err
	}
	if requestRow.Initiator.ID != userID && requestRow.Target.ID != userID {
		return nil
	}

	nextState := linkMicRequestStateCancelled
	if requestRow.State == linkMicRequestStateAccepted {
		nextState = linkMicRequestStateEnded
	}

	_, err = s.database.ExecContext(
		ctx,
		`UPDATE linkmic_requests
		 SET state = ?, ended_by_user_id = ?, ended_at = NOW(), updated_at = NOW()
		 WHERE room_id = ? AND request_id = ?`,
		nextState,
		userID,
		roomID,
		requestRow.RequestID,
	)
	return err
}

func (s *apiServer) upsertLiveRoomPresence(ctx context.Context, roomID, userID int64, role string) error {
	return upsertLiveRoomPresenceQuerier(ctx, s.database, roomID, userID, role)
}

func (s *apiServer) leaveLiveRoomPresence(ctx context.Context, roomID, userID int64) error {
	_, err := s.database.ExecContext(
		ctx,
		`UPDATE live_room_presences
		 SET is_online = 0, last_seen_at = NOW(), left_at = NOW()
		 WHERE room_id = ? AND user_id = ?`,
		roomID,
		userID,
	)
	return err
}

func (s *apiServer) findLiveRoomByKey(ctx context.Context, roomKey string) (liveRoomRow, error) {
	return findLiveRoomByKeyQuerier(ctx, s.database, roomKey)
}

func (s *apiServer) findLiveRoomMembers(ctx context.Context, roomID int64) ([]liveRoomMemberRow, error) {
	rows, err := s.database.QueryContext(
		ctx,
		`SELECT u.id, u.username, u.nickname, u.avatar_url, presence.role, presence.is_online, presence.last_seen_at
		 FROM live_room_presences presence
		 INNER JOIN users u ON u.id = presence.user_id
		 WHERE presence.room_id = ?
		   AND presence.is_online = 1
		   AND presence.last_seen_at >= DATE_SUB(NOW(), INTERVAL 2 MINUTE)
		 ORDER BY CASE WHEN presence.role = 'controller' THEN 0 ELSE 1 END, presence.last_seen_at DESC`,
		roomID,
	)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	members := make([]liveRoomMemberRow, 0)
	for rows.Next() {
		var row liveRoomMemberRow
		var isOnline int
		if err := rows.Scan(
			&row.User.ID,
			&row.User.Username,
			&row.User.Nickname,
			&row.User.AvatarURL,
			&row.Role,
			&isOnline,
			&row.LastSeenAt,
		); err != nil {
			return nil, err
		}
		row.IsOnline = isOnline > 0
		members = append(members, row)
	}
	if err := rows.Err(); err != nil {
		return nil, err
	}

	return members, nil
}

func (s *apiServer) buildLiveRoomResponse(ctx context.Context, room liveRoomRow) (*liveRoomResponse, error) {
	var onlineMemberCount int
	if err := s.database.QueryRowContext(
		ctx,
		`SELECT COUNT(*)
		 FROM live_room_presences
		 WHERE room_id = ?
		   AND is_online = 1
		   AND last_seen_at >= DATE_SUB(NOW(), INTERVAL 2 MINUTE)`,
		room.ID,
	).Scan(&onlineMemberCount); err != nil {
		return nil, err
	}

	var activeRequestResponse *linkMicRequestResponse
	activeRequestRow, err := findCurrentLinkMicRequestQuerier(ctx, s.database, room.ID)
	if err != nil && !errors.Is(err, sql.ErrNoRows) {
		return nil, err
	}
	if err == nil {
		response, err := toLinkMicRequestResponse(*activeRequestRow)
		if err != nil {
			return nil, err
		}
		activeRequestResponse = response
	}

	response := &liveRoomResponse{
		ID:                room.ID,
		RoomKey:           room.RoomKey,
		Status:            room.Status,
		SignalingURL:      s.cfg.PublicSignalingURL,
		Owner:             toRoomUserResponse(roomUserRow{ID: room.OwnerUserID, Username: room.OwnerUsername, Nickname: room.OwnerNickname, AvatarURL: room.OwnerAvatar}),
		OnlineMemberCount: onlineMemberCount,
		ActiveRequest:     activeRequestResponse,
		CreatedAt:         room.CreatedAt.Format(time.RFC3339),
		UpdatedAt:         room.UpdatedAt.Format(time.RFC3339),
	}
	if room.Title.Valid {
		response.Title = room.Title.String
	}
	return response, nil
}

func (s *apiServer) findLatestRequestResponseByID(ctx context.Context, roomID int64, requestID string) (*linkMicRequestResponse, error) {
	row, err := findLinkMicRequestByRequestIDQuerier(ctx, s.database, roomID, requestID)
	if err != nil {
		return nil, err
	}
	return toLinkMicRequestResponse(*row)
}

func (s *apiServer) buildRTCJoinParams(room liveRoomResponse, request linkMicRequestResponse, userID int64) *rtcJoinParamsResponse {
	if request.State != linkMicRequestStateAccepted {
		return nil
	}
	if request.Initiator.ID != userID && request.Target.ID != userID {
		return nil
	}

	participantRole := liveRoomRoleParticipant
	if room.Owner.ID == userID {
		participantRole = liveRoomRoleController
	}

	controllerUserID := room.Owner.ID
	participantUserID := request.Initiator.ID
	if request.Initiator.ID == controllerUserID {
		participantUserID = request.Target.ID
	} else if request.Target.ID == controllerUserID {
		participantUserID = request.Initiator.ID
	}

	return &rtcJoinParamsResponse{
		Transport:         "libdatachannel",
		RoomName:          room.RoomKey,
		ParticipantRole:   participantRole,
		SignalingURL:      s.cfg.PublicSignalingURL,
		RequestID:         request.RequestID,
		RequestType:       request.RequestType,
		ControllerUserID:  controllerUserID,
		ParticipantUserID: participantUserID,
	}
}

func (s *apiServer) publishLiveRoomEvent(ctx context.Context, action string, actorUserID int64, roomKey, requestID string) {
	room, err := s.getLiveRoomResponseByKey(ctx, roomKey)
	if err != nil {
		log.Printf("publish live room event load room failed: room_key=%s err=%v", roomKey, err)
		return
	}

	event := liveRoomUpdatedEvent{
		Action:      action,
		RoomKey:     roomKey,
		ActorUserID: actorUserID,
		RequestID:   requestID,
		Room:        room,
	}
	if room.ActiveRequest != nil {
		event.Request = room.ActiveRequest
	}

	if err := s.events.publish("linkmic.room.updated", event); err != nil {
		log.Printf("publish live room event failed: room_key=%s action=%s err=%v", roomKey, action, err)
	}
}

func findLiveRoomByKeyQuerier(ctx context.Context, q dbQuerier, roomKey string) (liveRoomRow, error) {
	row := q.QueryRowContext(
		ctx,
		`SELECT room.id, room.room_key, room.owner_user_id, owner.username, owner.nickname, owner.avatar_url, room.title, room.status, room.created_at, room.updated_at
		 FROM live_rooms room
		 INNER JOIN users owner ON owner.id = room.owner_user_id
		 WHERE room.room_key = ?`,
		roomKey,
	)

	var result liveRoomRow
	err := row.Scan(
		&result.ID,
		&result.RoomKey,
		&result.OwnerUserID,
		&result.OwnerUsername,
		&result.OwnerNickname,
		&result.OwnerAvatar,
		&result.Title,
		&result.Status,
		&result.CreatedAt,
		&result.UpdatedAt,
	)
	return result, err
}

func findLiveRoomByKeyForUpdateQuerier(ctx context.Context, q dbQuerier, roomKey string) (liveRoomRow, error) {
	row := q.QueryRowContext(
		ctx,
		`SELECT room.id, room.room_key, room.owner_user_id, owner.username, owner.nickname, owner.avatar_url, room.title, room.status, room.created_at, room.updated_at
		 FROM live_rooms room
		 INNER JOIN users owner ON owner.id = room.owner_user_id
		 WHERE room.room_key = ?
		 FOR UPDATE`,
		roomKey,
	)

	var result liveRoomRow
	err := row.Scan(
		&result.ID,
		&result.RoomKey,
		&result.OwnerUserID,
		&result.OwnerUsername,
		&result.OwnerNickname,
		&result.OwnerAvatar,
		&result.Title,
		&result.Status,
		&result.CreatedAt,
		&result.UpdatedAt,
	)
	return result, err
}

func findLiveRoomByOwnerAndStatusQuerier(ctx context.Context, q dbQuerier, ownerUserID int64, status string) (liveRoomRow, error) {
	row := q.QueryRowContext(
		ctx,
		`SELECT room.id, room.room_key, room.owner_user_id, owner.username, owner.nickname, owner.avatar_url, room.title, room.status, room.created_at, room.updated_at
		 FROM live_rooms room
		 INNER JOIN users owner ON owner.id = room.owner_user_id
		 WHERE room.owner_user_id = ?
		   AND room.status = ?
		 ORDER BY room.created_at DESC
		 LIMIT 1`,
		ownerUserID,
		status,
	)

	var result liveRoomRow
	err := row.Scan(
		&result.ID,
		&result.RoomKey,
		&result.OwnerUserID,
		&result.OwnerUsername,
		&result.OwnerNickname,
		&result.OwnerAvatar,
		&result.Title,
		&result.Status,
		&result.CreatedAt,
		&result.UpdatedAt,
	)
	return result, err
}

func findLiveRoomPresenceQuerier(ctx context.Context, q dbQuerier, roomID, userID int64) (liveRoomMemberRow, error) {
	row := q.QueryRowContext(
		ctx,
		`SELECT u.id, u.username, u.nickname, u.avatar_url, presence.role, presence.is_online, presence.last_seen_at
		 FROM live_room_presences presence
		 INNER JOIN users u ON u.id = presence.user_id
		 WHERE presence.room_id = ?
		   AND presence.user_id = ?
		   AND presence.is_online = 1
		   AND presence.last_seen_at >= DATE_SUB(NOW(), INTERVAL 2 MINUTE)`,
		roomID,
		userID,
	)

	var result liveRoomMemberRow
	var isOnline int
	err := row.Scan(
		&result.User.ID,
		&result.User.Username,
		&result.User.Nickname,
		&result.User.AvatarURL,
		&result.Role,
		&isOnline,
		&result.LastSeenAt,
	)
	result.IsOnline = isOnline > 0
	return result, err
}

func upsertLiveRoomPresenceQuerier(ctx context.Context, q dbQuerier, roomID, userID int64, role string) error {
	_, err := q.ExecContext(
		ctx,
		`INSERT INTO live_room_presences (room_id, user_id, role, is_online, joined_at, last_seen_at, left_at)
		 VALUES (?, ?, ?, 1, NOW(), NOW(), NULL)
		 ON DUPLICATE KEY UPDATE
		     role = VALUES(role),
		     is_online = 1,
		     last_seen_at = NOW(),
		     left_at = NULL`,
		roomID,
		userID,
		role,
	)
	return err
}

func ensureNoActiveLinkMicRequestQuerier(ctx context.Context, q dbQuerier, roomID int64) error {
	var count int
	if err := q.QueryRowContext(
		ctx,
		`SELECT COUNT(*)
		 FROM linkmic_requests
		 WHERE room_id = ?
		   AND state IN (?, ?)`,
		roomID,
		linkMicRequestStatePending,
		linkMicRequestStateAccepted,
	).Scan(&count); err != nil {
		return err
	}
	if count > 0 {
		return fmt.Errorf("room already has a pending or active linkmic request")
	}
	return nil
}

func findCurrentLinkMicRequestQuerier(ctx context.Context, q dbQuerier, roomID int64) (*linkMicRequestRow, error) {
	rows, err := q.QueryContext(
		ctx,
		`SELECT
		     req.id, req.request_id, req.room_id, req.request_type, req.state,
		     initiator.id, initiator.username, initiator.nickname, initiator.avatar_url,
		     target.id, target.username, target.nickname, target.avatar_url,
		     req.accepted_by_user_id, req.ended_by_user_id, req.metadata_json,
		     req.created_at, req.updated_at, req.responded_at, req.ended_at
		 FROM linkmic_requests req
		 INNER JOIN users initiator ON initiator.id = req.initiator_user_id
		 INNER JOIN users target ON target.id = req.target_user_id
		 WHERE req.room_id = ?
		   AND req.state IN (?, ?)
		 ORDER BY CASE WHEN req.state = ? THEN 0 ELSE 1 END, req.updated_at DESC
		 LIMIT 1`,
		roomID,
		linkMicRequestStateAccepted,
		linkMicRequestStatePending,
		linkMicRequestStateAccepted,
	)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	if !rows.Next() {
		return nil, sql.ErrNoRows
	}

	requestRow, err := scanLinkMicRequestRow(rows)
	if err != nil {
		return nil, err
	}
	return &requestRow, nil
}

func findLinkMicRequestByRequestIDQuerier(ctx context.Context, q dbQuerier, roomID int64, requestID string) (*linkMicRequestRow, error) {
	rows, err := q.QueryContext(
		ctx,
		`SELECT
		     req.id, req.request_id, req.room_id, req.request_type, req.state,
		     initiator.id, initiator.username, initiator.nickname, initiator.avatar_url,
		     target.id, target.username, target.nickname, target.avatar_url,
		     req.accepted_by_user_id, req.ended_by_user_id, req.metadata_json,
		     req.created_at, req.updated_at, req.responded_at, req.ended_at
		 FROM linkmic_requests req
		 INNER JOIN users initiator ON initiator.id = req.initiator_user_id
		 INNER JOIN users target ON target.id = req.target_user_id
		 WHERE req.room_id = ?
		   AND req.request_id = ?
		 LIMIT 1`,
		roomID,
		requestID,
	)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	if !rows.Next() {
		return nil, sql.ErrNoRows
	}

	requestRow, err := scanLinkMicRequestRow(rows)
	if err != nil {
		return nil, err
	}
	return &requestRow, nil
}

func scanLinkMicRequestRow(scanner interface {
	Scan(dest ...any) error
}) (linkMicRequestRow, error) {
	var row linkMicRequestRow
	err := scanner.Scan(
		&row.ID,
		&row.RequestID,
		&row.RoomID,
		&row.RequestType,
		&row.State,
		&row.Initiator.ID,
		&row.Initiator.Username,
		&row.Initiator.Nickname,
		&row.Initiator.AvatarURL,
		&row.Target.ID,
		&row.Target.Username,
		&row.Target.Nickname,
		&row.Target.AvatarURL,
		&row.AcceptedByUser,
		&row.EndedByUser,
		&row.MetadataJSON,
		&row.CreatedAt,
		&row.UpdatedAt,
		&row.RespondedAt,
		&row.EndedAt,
	)
	return row, err
}

func toLinkMicRequestResponse(row linkMicRequestRow) (*linkMicRequestResponse, error) {
	response := &linkMicRequestResponse{
		RequestID:   row.RequestID,
		RequestType: row.RequestType,
		State:       row.State,
		Initiator:   toRoomUserResponse(row.Initiator),
		Target:      toRoomUserResponse(row.Target),
		CreatedAt:   row.CreatedAt.Format(time.RFC3339),
		UpdatedAt:   row.UpdatedAt.Format(time.RFC3339),
	}
	if row.RespondedAt.Valid {
		response.RespondedAt = row.RespondedAt.Time.Format(time.RFC3339)
	}
	if row.EndedAt.Valid {
		response.EndedAt = row.EndedAt.Time.Format(time.RFC3339)
	}
	if row.MetadataJSON.Valid && strings.TrimSpace(row.MetadataJSON.String) != "" {
		var metadata any
		if err := json.Unmarshal([]byte(row.MetadataJSON.String), &metadata); err != nil {
			return nil, err
		}
		response.Metadata = metadata
	}
	return response, nil
}

func toRoomUserResponse(user roomUserRow) roomUserResponse {
	response := roomUserResponse{
		ID:       user.ID,
		Username: user.Username,
	}
	if user.Nickname.Valid {
		response.Nickname = user.Nickname.String
	}
	if user.AvatarURL.Valid {
		response.AvatarURL = user.AvatarURL.String
	}
	return response
}

func memberIsLinkMicActive(activeMemberIDs map[int64]struct{}, userID int64) bool {
	_, ok := activeMemberIDs[userID]
	return ok
}

func normalizeLiveRoomKey(roomKey string) string {
	roomKey = strings.ToLower(strings.TrimSpace(roomKey))
	if roomKey == "" {
		return ""
	}

	var builder strings.Builder
	lastWasDash := false
	for _, ch := range roomKey {
		switch {
		case ch >= 'a' && ch <= 'z', ch >= '0' && ch <= '9':
			builder.WriteRune(ch)
			lastWasDash = false
		case ch == '-' || ch == '_':
			builder.WriteRune(ch)
			lastWasDash = false
		default:
			if !lastWasDash {
				builder.WriteRune('-')
				lastWasDash = true
			}
		}
	}

	normalized := strings.Trim(builder.String(), "-_")
	if len(normalized) > 64 {
		normalized = normalized[:64]
	}
	return normalized
}

func (s *apiServer) generateLiveRoomKey(ctx context.Context, username string) (string, error) {
	base := normalizeLiveRoomKey(username)
	if base == "" {
		base = "room"
	}
	if len(base) > 32 {
		base = base[:32]
	}

	for attempt := 0; attempt < 5; attempt++ {
		suffix, err := generateSecretHex(4)
		if err != nil {
			return "", err
		}
		roomKey := normalizeLiveRoomKey(base + "-" + suffix)

		var count int
		if err := s.database.QueryRowContext(
			ctx,
			`SELECT COUNT(*) FROM live_rooms WHERE room_key = ?`,
			roomKey,
		).Scan(&count); err != nil {
			return "", err
		}
		if count == 0 {
			return roomKey, nil
		}
	}

	return "", fmt.Errorf("failed to generate a unique room key")
}

func decodeOptionalJSON(request *http.Request, target any) error {
	if request.Body == nil {
		return nil
	}
	defer request.Body.Close()

	decoder := json.NewDecoder(request.Body)
	if err := decoder.Decode(target); err != nil {
		if errors.Is(err, http.ErrBodyReadAfterClose) {
			return err
		}
		if strings.Contains(strings.ToLower(err.Error()), "eof") {
			return nil
		}
		return err
	}
	return nil
}

func writeLiveRoomMutationError(writer http.ResponseWriter, err error) {
	if err == nil {
		return
	}

	var apiErr *liveRoomAPIError
	switch {
	case errors.As(err, &apiErr):
		writeJSON(writer, apiErr.StatusCode, map[string]string{"error": apiErr.Message})
	case errors.Is(err, sql.ErrNoRows):
		writeNotFound(writer)
	case isConflictError(err):
		writeJSON(writer, http.StatusConflict, map[string]string{"error": err.Error()})
	default:
		writeServerError(writer, err)
	}
}

func isConflictError(err error) bool {
	if err == nil {
		return false
	}
	text := strings.ToLower(err.Error())
	return strings.Contains(text, "already") ||
		strings.Contains(text, "pending") ||
		strings.Contains(text, "active") ||
		strings.Contains(text, "must be") ||
		strings.Contains(text, "cannot") ||
		strings.Contains(text, "not online") ||
		strings.Contains(text, "no longer pending")
}

func nullIfEmpty(value string) any {
	if strings.TrimSpace(value) == "" {
		return nil
	}
	return value
}

func nullableInt64(value int64, enabled bool) any {
	if !enabled {
		return nil
	}
	return value
}
