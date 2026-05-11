package linkmicmix

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"log"
	"net"
	"net/url"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"sync"
	"time"

	"vod-platform-server/internal/config"
)

const (
	StateReady   = "ready"
	StateRunning = "running"
	StateStopped = "stopped"
	StateError   = "error"

	defaultHTTPFLVPort = "18080"
	startProbeWindow   = 1200 * time.Millisecond
	stopWaitWindow     = 5 * time.Second
)

type RoomOutput struct {
	RoomKey        string `json:"roomKey"`
	MixedStreamKey string `json:"mixedStreamKey"`
	OutputURL      string `json:"outputUrl"`
	PlaybackURL    string `json:"playbackUrl"`
}

type SessionSpec struct {
	ProcessKey            string `json:"processKey"`
	SessionID             string `json:"sessionId"`
	RequestID             string `json:"requestId,omitempty"`
	RoomKey               string `json:"roomKey"`
	PeerRoomKey           string `json:"peerRoomKey"`
	PrimaryInputStreamKey string `json:"primaryInputStreamKey"`
	PeerInputStreamKey    string `json:"peerInputStreamKey"`
	MixedStreamKey        string `json:"mixedStreamKey"`
	PrimaryInputURL       string `json:"primaryInputUrl"`
	PeerInputURL          string `json:"peerInputUrl"`
	OutputURL             string `json:"outputUrl"`
	PlaybackURL           string `json:"playbackUrl"`
}

type Status struct {
	ProcessKey     string    `json:"processKey"`
	SessionID      string    `json:"sessionId,omitempty"`
	RequestID      string    `json:"requestId,omitempty"`
	RoomKey        string    `json:"roomKey"`
	PeerRoomKey    string    `json:"peerRoomKey,omitempty"`
	MixedStreamKey string    `json:"mixedStreamKey,omitempty"`
	State          string    `json:"state"`
	PID            int       `json:"pid,omitempty"`
	LogFile        string    `json:"logFile,omitempty"`
	StartedAt      time.Time `json:"startedAt,omitempty"`
	StoppedAt      time.Time `json:"stoppedAt,omitempty"`
	UpdatedAt      time.Time `json:"updatedAt,omitempty"`
	LastError      string    `json:"lastError,omitempty"`
	OutputURL      string    `json:"outputUrl,omitempty"`
	PlaybackURL    string    `json:"playbackUrl,omitempty"`
}

type persistedSession struct {
	Spec   SessionSpec `json:"spec"`
	Status Status      `json:"status"`
}

type Manager struct {
	enabled    bool
	ffmpegBin  string
	workDir    string
	logDir     string
	inputBase  string
	outputBase string

	mu        sync.Mutex
	processes map[string]*managedProcess
}

type managedProcess struct {
	spec     SessionSpec
	cmd      *exec.Cmd
	cancel   context.CancelFunc
	logFile  *os.File
	status   Status
	stopping bool
}

func NewManager(cfg config.Config) (*Manager, error) {
	manager := &Manager{
		enabled:    cfg.LinkMicMixEnable,
		ffmpegBin:  strings.TrimSpace(cfg.LinkMicMixFFmpegBin),
		workDir:    strings.TrimSpace(cfg.LinkMicMixWorkDir),
		logDir:     strings.TrimSpace(cfg.LinkMicMixLogDir),
		inputBase:  strings.TrimSpace(cfg.LinkMicMixInputBase),
		outputBase: strings.TrimSpace(cfg.LinkMicMixOutputBase),
		processes:  make(map[string]*managedProcess),
	}

	if manager.ffmpegBin == "" {
		manager.ffmpegBin = "ffmpeg"
	}
	if manager.workDir == "" {
		manager.workDir = filepath.Join("runtime", "linkmicmix", "work")
	}
	if manager.logDir == "" {
		manager.logDir = filepath.Join("runtime", "linkmicmix", "logs")
	}
	if !manager.enabled {
		return manager, nil
	}
	if manager.inputBase == "" {
		return nil, errors.New("LINKMIC_MIX_INPUT_BASE is required when LINKMIC_MIX_ENABLE=true")
	}
	if manager.outputBase == "" {
		return nil, errors.New("LINKMIC_MIX_OUTPUT_BASE is required when LINKMIC_MIX_ENABLE=true")
	}
	if err := os.MkdirAll(manager.workDir, 0o755); err != nil {
		return nil, fmt.Errorf("create linkmic mix workdir: %w", err)
	}
	if err := os.MkdirAll(manager.logDir, 0o755); err != nil {
		return nil, fmt.Errorf("create linkmic mix logdir: %w", err)
	}
	return manager, nil
}

func (m *Manager) Enabled() bool {
	return m != nil && m.enabled
}

func (m *Manager) BuildRoomOutput(roomKey string) (RoomOutput, error) {
	roomKey = normalizeRoomKey(roomKey)
	if roomKey == "" {
		return RoomOutput{}, errors.New("room key is required")
	}
	if strings.TrimSpace(m.outputBase) == "" {
		return RoomOutput{}, errors.New("linkmic mix output base is not configured")
	}

	mixedStreamKey := roomKey + "-mixed"
	outputURL := joinStreamURL(m.outputBase, mixedStreamKey)
	playbackURL, err := derivePlaybackURL(outputURL)
	if err != nil {
		return RoomOutput{}, err
	}

	return RoomOutput{
		RoomKey:        roomKey,
		MixedStreamKey: mixedStreamKey,
		OutputURL:      outputURL,
		PlaybackURL:    playbackURL,
	}, nil
}

func (m *Manager) BuildRoomSession(roomKey, peerRoomKey, sessionID, requestID string) (SessionSpec, error) {
	roomKey = normalizeRoomKey(roomKey)
	peerRoomKey = normalizeRoomKey(peerRoomKey)
	sessionID = strings.TrimSpace(sessionID)
	requestID = strings.TrimSpace(requestID)

	if roomKey == "" {
		return SessionSpec{}, errors.New("room key is required")
	}
	if peerRoomKey == "" {
		return SessionSpec{}, errors.New("peer room key is required")
	}
	if roomKey == peerRoomKey {
		return SessionSpec{}, errors.New("room key and peer room key must be different")
	}
	if sessionID == "" {
		return SessionSpec{}, errors.New("session id is required")
	}
	if strings.TrimSpace(m.inputBase) == "" {
		return SessionSpec{}, errors.New("linkmic mix input base is not configured")
	}

	output, err := m.BuildRoomOutput(roomKey)
	if err != nil {
		return SessionSpec{}, err
	}

	return SessionSpec{
		ProcessKey:            roomKey,
		SessionID:             sessionID,
		RequestID:             requestID,
		RoomKey:               roomKey,
		PeerRoomKey:           peerRoomKey,
		PrimaryInputStreamKey: roomKey,
		PeerInputStreamKey:    peerRoomKey,
		MixedStreamKey:        output.MixedStreamKey,
		PrimaryInputURL:       joinStreamURL(m.inputBase, roomKey),
		PeerInputURL:          joinStreamURL(m.inputBase, peerRoomKey),
		OutputURL:             output.OutputURL,
		PlaybackURL:           output.PlaybackURL,
	}, nil
}

func (m *Manager) Start(ctx context.Context, spec SessionSpec) error {
	if !m.Enabled() {
		return nil
	}

	spec.ProcessKey = normalizeRoomKey(firstNonEmpty(spec.ProcessKey, spec.RoomKey))
	if spec.ProcessKey == "" {
		return errors.New("process key is required")
	}

	m.mu.Lock()
	runtime := m.processes[spec.ProcessKey]
	m.mu.Unlock()
	if runtime != nil && runtime.status.PID > 0 && processAlive(runtime.status.PID) {
		if sameSession(runtime.spec, spec) {
			return nil
		}
		if err := m.Stop(ctx, spec.ProcessKey); err != nil {
			return err
		}
	}

	session, err := m.loadPersistedSession(spec.ProcessKey)
	if err != nil && !errors.Is(err, os.ErrNotExist) {
		return err
	}
	if err == nil && session.Status.PID > 0 && processAlive(session.Status.PID) {
		if sameSession(session.Spec, spec) {
			m.mu.Lock()
			m.processes[spec.ProcessKey] = &managedProcess{
				spec:   session.Spec,
				status: session.Status,
			}
			m.mu.Unlock()
			return nil
		}
		if err := m.Stop(ctx, spec.ProcessKey); err != nil {
			return err
		}
	}

	sessionDir := m.sessionDir(spec.ProcessKey)
	if err := os.MkdirAll(sessionDir, 0o755); err != nil {
		return fmt.Errorf("create linkmic mix session dir: %w", err)
	}

	logPath := m.logPath(spec.ProcessKey)
	logFile, err := os.OpenFile(logPath, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0o644)
	if err != nil {
		return fmt.Errorf("open linkmic mix log file: %w", err)
	}

	procCtx, cancel := context.WithCancel(context.Background())
	args := buildFFmpegArgs(spec)
	cmd := exec.CommandContext(procCtx, m.ffmpegBin, args...)
	cmd.Dir = sessionDir
	cmd.Stdout = logFile
	cmd.Stderr = logFile

	startedAt := time.Now().UTC()
	_, _ = fmt.Fprintf(logFile, "\n[%s] start process=%s room=%s peer=%s session=%s command=%s %s\n",
		startedAt.Format(time.RFC3339),
		spec.ProcessKey,
		spec.RoomKey,
		spec.PeerRoomKey,
		spec.SessionID,
		m.ffmpegBin,
		strings.Join(args, " "),
	)

	if err := cmd.Start(); err != nil {
		cancel()
		_ = logFile.Close()

		persisted := persistedSession{
			Spec: spec,
			Status: Status{
				ProcessKey:     spec.ProcessKey,
				SessionID:      spec.SessionID,
				RequestID:      spec.RequestID,
				RoomKey:        spec.RoomKey,
				PeerRoomKey:    spec.PeerRoomKey,
				MixedStreamKey: spec.MixedStreamKey,
				State:          StateError,
				LogFile:        logPath,
				UpdatedAt:      time.Now().UTC(),
				LastError:      fmt.Sprintf("start ffmpeg: %v", err),
				OutputURL:      spec.OutputURL,
				PlaybackURL:    spec.PlaybackURL,
			},
		}
		if persistErr := m.persistSession(persisted); persistErr != nil {
			log.Printf("persist failed linkmic mix start error: process_key=%s err=%v", spec.ProcessKey, persistErr)
		}
		return fmt.Errorf("start linkmic mix ffmpeg: %w", err)
	}

	runtime = &managedProcess{
		spec:    spec,
		cmd:     cmd,
		cancel:  cancel,
		logFile: logFile,
		status: Status{
			ProcessKey:     spec.ProcessKey,
			SessionID:      spec.SessionID,
			RequestID:      spec.RequestID,
			RoomKey:        spec.RoomKey,
			PeerRoomKey:    spec.PeerRoomKey,
			MixedStreamKey: spec.MixedStreamKey,
			State:          StateRunning,
			PID:            cmd.Process.Pid,
			LogFile:        logPath,
			StartedAt:      startedAt,
			UpdatedAt:      startedAt,
			OutputURL:      spec.OutputURL,
			PlaybackURL:    spec.PlaybackURL,
		},
	}

	if err := m.persistSession(persistedSession{Spec: spec, Status: runtime.status}); err != nil {
		cancel()
		_ = cmd.Process.Kill()
		_ = logFile.Close()
		return err
	}

	m.mu.Lock()
	m.processes[spec.ProcessKey] = runtime
	m.mu.Unlock()

	exitedEarly := make(chan error, 1)
	go m.waitProcess(runtime, exitedEarly)

	select {
	case err := <-exitedEarly:
		if err == nil {
			return errors.New("ffmpeg mix exited immediately after start")
		}
		return fmt.Errorf("ffmpeg mix exited immediately after start: %w", err)
	case <-time.After(startProbeWindow):
		return nil
	}
}

func (m *Manager) Stop(ctx context.Context, processKey string) error {
	if !m.Enabled() {
		return nil
	}

	processKey = normalizeRoomKey(processKey)
	if processKey == "" {
		return nil
	}

	var runtime *managedProcess
	m.mu.Lock()
	if current := m.processes[processKey]; current != nil {
		current.stopping = true
		runtime = current
	}
	m.mu.Unlock()

	if runtime != nil {
		if runtime.cancel != nil {
			runtime.cancel()
		}
		if runtime.status.PID > 0 && processAlive(runtime.status.PID) {
			if err := killProcess(runtime.status.PID); err != nil {
				log.Printf("kill running linkmic mix failed: process_key=%s pid=%d err=%v", processKey, runtime.status.PID, err)
			}
		}
		return m.waitUntilStopped(ctx, runtime.spec, runtime.status)
	}

	session, err := m.loadPersistedSession(processKey)
	if err != nil {
		if errors.Is(err, os.ErrNotExist) {
			return nil
		}
		return err
	}

	if session.Status.PID > 0 && processAlive(session.Status.PID) {
		if err := killProcess(session.Status.PID); err != nil {
			return fmt.Errorf("kill persisted linkmic mix process: %w", err)
		}
		if err := m.waitUntilStopped(ctx, session.Spec, session.Status); err != nil {
			return err
		}
	}

	session.Status.State = StateStopped
	session.Status.PID = 0
	session.Status.StoppedAt = time.Now().UTC()
	session.Status.UpdatedAt = session.Status.StoppedAt
	return m.persistSession(session)
}

func (m *Manager) StopRooms(ctx context.Context, roomKeys ...string) error {
	if !m.Enabled() {
		return nil
	}

	seen := make(map[string]struct{})
	errs := make([]string, 0)
	for _, roomKey := range roomKeys {
		processKey := normalizeRoomKey(roomKey)
		if processKey == "" {
			continue
		}
		if _, ok := seen[processKey]; ok {
			continue
		}
		seen[processKey] = struct{}{}
		if err := m.Stop(ctx, processKey); err != nil {
			errs = append(errs, fmt.Sprintf("%s: %v", processKey, err))
		}
	}
	if len(errs) > 0 {
		return errors.New(strings.Join(errs, "; "))
	}
	return nil
}

func (m *Manager) Query(processKey string) (Status, bool) {
	if !m.Enabled() {
		return Status{}, false
	}

	processKey = normalizeRoomKey(processKey)
	if processKey == "" {
		return Status{}, false
	}

	m.mu.Lock()
	if runtime := m.processes[processKey]; runtime != nil {
		status := runtime.status
		m.mu.Unlock()
		if status.PID > 0 && !processAlive(status.PID) {
			status.State = StateStopped
			status.PID = 0
			status.StoppedAt = time.Now().UTC()
			status.UpdatedAt = status.StoppedAt
		}
		return status, true
	}
	m.mu.Unlock()

	session, err := m.loadPersistedSession(processKey)
	if err != nil {
		return Status{}, false
	}
	if session.Status.PID > 0 && !processAlive(session.Status.PID) {
		session.Status.PID = 0
		if session.Status.State == StateRunning {
			session.Status.State = StateStopped
		}
		session.Status.StoppedAt = time.Now().UTC()
		session.Status.UpdatedAt = session.Status.StoppedAt
		if persistErr := m.persistSession(session); persistErr != nil {
			log.Printf("persist stale linkmic mix state failed: process_key=%s err=%v", processKey, persistErr)
		}
	}
	return session.Status, true
}

func (m *Manager) waitProcess(runtime *managedProcess, exitedEarly chan<- error) {
	err := runtime.cmd.Wait()
	_ = runtime.logFile.Close()

	m.mu.Lock()
	current := m.processes[runtime.spec.ProcessKey]
	if current == runtime {
		delete(m.processes, runtime.spec.ProcessKey)
	}
	stopping := runtime.stopping
	m.mu.Unlock()

	status := runtime.status
	status.PID = 0
	status.StoppedAt = time.Now().UTC()
	status.UpdatedAt = status.StoppedAt
	status.State = StateStopped
	status.LastError = ""
	if err != nil && !stopping {
		status.State = StateError
		status.LastError = err.Error()
	}

	if persistErr := m.persistSession(persistedSession{Spec: runtime.spec, Status: status}); persistErr != nil {
		log.Printf("persist linkmic mix exit state failed: process_key=%s err=%v", runtime.spec.ProcessKey, persistErr)
	}
	log.Printf("linkmic mix exited: process_key=%s room_key=%s peer_room_key=%s state=%s err=%v",
		runtime.spec.ProcessKey,
		runtime.spec.RoomKey,
		runtime.spec.PeerRoomKey,
		status.State,
		err,
	)

	select {
	case exitedEarly <- err:
	default:
	}
}

func (m *Manager) waitUntilStopped(ctx context.Context, spec SessionSpec, status Status) error {
	if status.PID <= 0 {
		return nil
	}

	waitCtx := ctx
	if waitCtx == nil {
		waitCtx = context.Background()
	}
	if _, hasDeadline := waitCtx.Deadline(); !hasDeadline {
		var cancel context.CancelFunc
		waitCtx, cancel = context.WithTimeout(waitCtx, stopWaitWindow)
		defer cancel()
	}

	ticker := time.NewTicker(200 * time.Millisecond)
	defer ticker.Stop()

	for {
		if !processAlive(status.PID) {
			status.State = StateStopped
			status.PID = 0
			status.StoppedAt = time.Now().UTC()
			status.UpdatedAt = status.StoppedAt
			return m.persistSession(persistedSession{Spec: spec, Status: status})
		}

		select {
		case <-waitCtx.Done():
			return fmt.Errorf("wait linkmic mix stop timeout: process_key=%s pid=%d", spec.ProcessKey, status.PID)
		case <-ticker.C:
		}
	}
}

func (m *Manager) persistSession(session persistedSession) error {
	processKey := normalizeRoomKey(firstNonEmpty(session.Spec.ProcessKey, session.Spec.RoomKey))
	if processKey == "" {
		return errors.New("persist linkmic mix session requires process key")
	}
	session.Spec.ProcessKey = processKey
	if err := os.MkdirAll(m.sessionDir(processKey), 0o755); err != nil {
		return fmt.Errorf("create linkmic mix session dir: %w", err)
	}

	session.Status.ProcessKey = processKey
	session.Status.SessionID = firstNonEmpty(session.Status.SessionID, session.Spec.SessionID)
	session.Status.RequestID = firstNonEmpty(session.Status.RequestID, session.Spec.RequestID)
	session.Status.RoomKey = firstNonEmpty(session.Status.RoomKey, session.Spec.RoomKey)
	session.Status.PeerRoomKey = firstNonEmpty(session.Status.PeerRoomKey, session.Spec.PeerRoomKey)
	session.Status.MixedStreamKey = firstNonEmpty(session.Status.MixedStreamKey, session.Spec.MixedStreamKey)
	if session.Status.OutputURL == "" {
		session.Status.OutputURL = session.Spec.OutputURL
	}
	if session.Status.PlaybackURL == "" {
		session.Status.PlaybackURL = session.Spec.PlaybackURL
	}

	payload, err := json.MarshalIndent(session, "", "  ")
	if err != nil {
		return fmt.Errorf("marshal linkmic mix session: %w", err)
	}

	tempPath := m.statePath(processKey) + ".tmp"
	if err := os.WriteFile(tempPath, payload, 0o644); err != nil {
		return fmt.Errorf("write linkmic mix temp state: %w", err)
	}
	if err := os.Rename(tempPath, m.statePath(processKey)); err != nil {
		return fmt.Errorf("rename linkmic mix state file: %w", err)
	}
	return nil
}

func (m *Manager) loadPersistedSession(processKey string) (persistedSession, error) {
	processKey = normalizeRoomKey(processKey)
	payload, err := os.ReadFile(m.statePath(processKey))
	if err != nil {
		return persistedSession{}, err
	}

	var session persistedSession
	if err := json.Unmarshal(payload, &session); err != nil {
		return persistedSession{}, fmt.Errorf("parse linkmic mix state file: %w", err)
	}
	return session, nil
}

func (m *Manager) sessionDir(processKey string) string {
	return filepath.Join(m.workDir, normalizeRoomKey(processKey))
}

func (m *Manager) statePath(processKey string) string {
	return filepath.Join(m.sessionDir(processKey), "session.json")
}

func (m *Manager) logPath(processKey string) string {
	return filepath.Join(m.logDir, normalizeRoomKey(processKey)+".log")
}

func buildFFmpegArgs(spec SessionSpec) []string {
	filterComplex := "[0:v]scale=640:360:force_original_aspect_ratio=decrease,pad=640:360:(ow-iw)/2:(oh-ih)/2:color=black,setsar=1[v0];" +
		"[1:v]scale=640:360:force_original_aspect_ratio=decrease,pad=640:360:(ow-iw)/2:(oh-ih)/2:color=black,setsar=1[v1];" +
		"[v0][v1]xstack=inputs=2:layout=0_0|w0_0[vout];" +
		"[0:a]aresample=async=1:first_pts=0[a0];" +
		"[1:a]aresample=async=1:first_pts=0[a1];" +
		"[a0][a1]amix=inputs=2:duration=longest:dropout_transition=2:normalize=0[aout]"

	return []string{
		"-nostdin",
		"-y",
		"-thread_queue_size", "1024",
		"-rtmp_live", "live",
		"-i", spec.PrimaryInputURL,
		"-thread_queue_size", "1024",
		"-rtmp_live", "live",
		"-i", spec.PeerInputURL,
		"-filter_complex", filterComplex,
		"-map", "[vout]",
		"-map", "[aout]",
		"-c:v", "libx264",
		"-preset", "veryfast",
		"-tune", "zerolatency",
		"-pix_fmt", "yuv420p",
		"-c:a", "aac",
		"-ar", "48000",
		"-ac", "2",
		"-f", "flv",
		spec.OutputURL,
	}
}

func derivePlaybackURL(outputURL string) (string, error) {
	parsed, err := url.Parse(strings.TrimSpace(outputURL))
	if err != nil {
		return "", fmt.Errorf("parse linkmic mix output url: %w", err)
	}
	if parsed.Host == "" {
		return "", fmt.Errorf("linkmic mix output url host is empty: %s", outputURL)
	}

	host := parsed.Hostname()
	if host == "" {
		return "", fmt.Errorf("linkmic mix output url hostname is empty: %s", outputURL)
	}
	appPath := strings.Trim(strings.TrimSpace(parsed.Path), "/")
	if appPath == "" {
		return "", fmt.Errorf("linkmic mix output url path is empty: %s", outputURL)
	}

	pathParts := strings.Split(appPath, "/")
	if len(pathParts) < 2 {
		return "", fmt.Errorf("linkmic mix output url missing app or stream key: %s", outputURL)
	}
	appName := pathParts[0]
	streamKey := pathParts[len(pathParts)-1]
	playbackHost := net.JoinHostPort(host, defaultHTTPFLVPort)

	scheme := "http"
	if parsed.Scheme == "rtmps" {
		scheme = "https"
	}
	return fmt.Sprintf("%s://%s/%s/%s.flv", scheme, playbackHost, appName, streamKey), nil
}

func joinStreamURL(base, streamKey string) string {
	base = strings.TrimRight(strings.TrimSpace(base), "/")
	streamKey = strings.TrimLeft(strings.TrimSpace(streamKey), "/")
	if base == "" {
		return streamKey
	}
	if streamKey == "" {
		return base
	}
	return base + "/" + streamKey
}

func normalizeRoomKey(roomKey string) string {
	return strings.ToLower(strings.TrimSpace(roomKey))
}

func firstNonEmpty(values ...string) string {
	for _, value := range values {
		value = strings.TrimSpace(value)
		if value != "" {
			return value
		}
	}
	return ""
}

func sameSession(left, right SessionSpec) bool {
	return normalizeRoomKey(left.ProcessKey) == normalizeRoomKey(right.ProcessKey) &&
		normalizeRoomKey(left.RoomKey) == normalizeRoomKey(right.RoomKey) &&
		normalizeRoomKey(left.PeerRoomKey) == normalizeRoomKey(right.PeerRoomKey) &&
		strings.TrimSpace(left.SessionID) == strings.TrimSpace(right.SessionID) &&
		strings.TrimSpace(left.RequestID) == strings.TrimSpace(right.RequestID)
}
