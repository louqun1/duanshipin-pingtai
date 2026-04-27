#include "liveplayer/session/LivePlayerSession.hpp"

#include "liveplayer/audio/SdlAudioOutput.hpp"
#include "liveplayer/logging/LiveWatchLogger.hpp"
#include "liveplayer/session/LiveAudioDecodeWorker.hpp"
#include "liveplayer/session/LiveVideoDecodeWorker.hpp"
#include "liveplayer/protocol/FlvTypes.hpp"
#include "liveplayer/protocol/HttpFlvStreamReader.hpp"

#include <QByteArray>
#include <QMetaObject>
#include <QTimer>
#include <QUrl>
#include <QWidget>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstring>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

namespace backend::liveplayer::session {

namespace {

enum class VideoColorMatrix
{
    Bt601 = 0,
    Bt709 = 1
};

const char *sessionStateName(LivePlayerSession::SessionState state)
{
    switch (state) {
    case LivePlayerSession::SessionState::Idle:
        return "Idle";
    case LivePlayerSession::SessionState::Connecting:
        return "Connecting";
    case LivePlayerSession::SessionState::Reading:
        return "Reading";
    case LivePlayerSession::SessionState::Playing:
        return "Playing";
    case LivePlayerSession::SessionState::Stopped:
        return "Stopped";
    case LivePlayerSession::SessionState::Error:
        return "Error";
    }

    return "Unknown";
}

QString videoCodecName(quint8 codecId)
{
    switch (codecId) {
    case 2:
        return "Sorenson H.263";
    case 4:
        return "On2 VP6";
    case 7:
        return "AVC/H.264";
    case 12:
        return "HEVC";
    default:
        return QString("codec=%1").arg(codecId);
    }
}

QString audioFormatName(quint8 soundFormat)
{
    switch (soundFormat) {
    case 2:
        return "MP3";
    case 10:
        return "AAC";
    case 11:
        return "Speex";
    default:
        return QString("format=%1").arg(soundFormat);
    }
}

QByteArray copyPlane(
    const uint8_t *sourceData,
    int sourceLineSize,
    int copyWidth,
    int copyHeight)
{
    if (!sourceData || sourceLineSize == 0 || copyWidth <= 0 || copyHeight <= 0) {
        return {};
    }

    if ((sourceLineSize > 0 && sourceLineSize < copyWidth) ||
        (sourceLineSize < 0 && -sourceLineSize < copyWidth)) {
        return {};
    }

    QByteArray plane(copyWidth * copyHeight, Qt::Uninitialized);
    auto *destinationData = reinterpret_cast<uint8_t *>(plane.data());

    if (sourceLineSize > 0) {
        for (int row = 0; row < copyHeight; ++row) {
            std::memcpy(destinationData + (row * copyWidth),
                        sourceData + (row * sourceLineSize),
                        copyWidth);
        }
        return plane;
    }

    const int absoluteLineSize = -sourceLineSize;
    const uint8_t *rowData = sourceData + ((copyHeight - 1) * absoluteLineSize);
    for (int row = 0; row < copyHeight; ++row) {
        std::memcpy(destinationData + (row * copyWidth),
                    rowData - (row * absoluteLineSize),
                    copyWidth);
    }

    return plane;
}

VideoColorMatrix resolveColorMatrix(const AVFrame *frame)
{
    if (!frame) {
        return VideoColorMatrix::Bt601;
    }

    switch (frame->colorspace) {
    case AVCOL_SPC_BT709:
        return VideoColorMatrix::Bt709;
    case AVCOL_SPC_FCC:
    case AVCOL_SPC_BT470BG:
    case AVCOL_SPC_SMPTE170M:
    case AVCOL_SPC_SMPTE240M:
        return VideoColorMatrix::Bt601;
    default:
        break;
    }

    if (frame->width >= 1280 || frame->height > 576) {
        return VideoColorMatrix::Bt709;
    }

    return VideoColorMatrix::Bt601;
}

bool isFullRangeSource(AVPixelFormat sourceFormat, AVColorRange colorRange)
{
    if (colorRange == AVCOL_RANGE_JPEG) {
        return true;
    }

    switch (sourceFormat) {
    case AV_PIX_FMT_YUVJ420P:
    case AV_PIX_FMT_YUVJ422P:
    case AV_PIX_FMT_YUVJ444P:
    case AV_PIX_FMT_YUVJ440P:
    case AV_PIX_FMT_YUVJ411P:
        return true;
    default:
        return false;
    }
}

constexpr int kRenderTickIntervalMs = 10;
constexpr qint64 kLateVideoFrameDropThresholdMs = 80;
constexpr int kStartupMinBufferedFrameDepth = 5;
constexpr qint64 kStartupMinBufferedDurationMs = 300;
constexpr qint64 kStartupMinBufferedDurationWithMinDepthMs = 200;
constexpr qint64 kFutureQueueRetentionWindowMs = 320;
constexpr qint64 kFutureQueueEmergencyTrimWindowMs = 900;
constexpr qint64 kFutureQueueEmergencyTrimBufferedMarginMs = 250;
constexpr qint64 kFutureQueueLeadThresholdMs = 160;
constexpr qint64 kReanchorFutureLeadMs = 80;
constexpr int kReanchorWaitThreshold = 8;
constexpr int kReanchorOverflowThreshold = 3;
constexpr qint64 kReanchorFastPathFrontLeadMs = 300;
constexpr qint64 kReanchorFastPathBackLeadMs = 300;
constexpr int kReanchorFastPathWaitThreshold = 2;
constexpr qint64 kReanchorCooldownMs = 400;
constexpr qint64 kPlaybackSnapshotLogIntervalMs = 200;
constexpr int kRenderWaitLogEvery = 5;
constexpr int kVideoFrameQueueMaxDepthForAudioMaster = 28;
constexpr qint64 kAudioBackpressureOnMs = 180;
constexpr qint64 kAudioBackpressureOffMs = 90;
constexpr qint64 kAudioHungryDeviceQueueMs = 40;
constexpr qint64 kAudioDrainPreferMs = 140;
constexpr qint64 kAudioDrainTargetDeviceQueueMs = 60;
constexpr int kAudioDrainMinQuota = 3;
constexpr int kAudioTagQueueHighWater = 6;
constexpr int kAudioTagQueueLowWater = 2;
constexpr int kVideoTagQueueHighWater = 10;
constexpr int kVideoTagQueueLowWater = 4;
constexpr qint64 kReaderThrottleBufferBytes = 24 * 1024;
constexpr qint64 kVideoDrainDeferBufferedMs = 120;
constexpr qint64 kVideoDecodeBackpressureHighWaterMs = 420;
constexpr qint64 kVideoDecodeBackpressureLowWaterMs = 220;
constexpr int kVideoDecodeBackpressureHighDepth = 18;
constexpr int kVideoDecodeBackpressureLowDepth = 10;

struct AudioBacklogSnapshot
{
    audio::SdlAudioOutput::ClockSnapshot clock;
    qint64 pendingPcmMs = 0;
    int pendingPcmCallbacks = 0;
    qint64 totalAudioMs = 0;
    bool audioNeedsRefill = false;
    bool audioHungry = false;
};

AudioBacklogSnapshot captureAudioBacklogSnapshot(
    const audio::SdlAudioOutput *audioOutput,
    qint64 pendingPcmMs,
    int pendingPcmCallbacks)
{
    AudioBacklogSnapshot snapshot;
    snapshot.clock = audioOutput
        ? audioOutput->clockSnapshot()
        : audio::SdlAudioOutput::ClockSnapshot{};
    snapshot.pendingPcmMs = std::max<qint64>(0, pendingPcmMs);
    snapshot.pendingPcmCallbacks = std::max(0, pendingPcmCallbacks);
    snapshot.totalAudioMs = snapshot.clock.queuedDurationMs + snapshot.pendingPcmMs;
    snapshot.audioNeedsRefill = snapshot.totalAudioMs <= kAudioBackpressureOffMs;
    snapshot.audioHungry =
        snapshot.audioNeedsRefill ||
        snapshot.clock.queuedDurationMs <= kAudioHungryDeviceQueueMs;
    return snapshot;
}

qint64 steadyNowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}  // namespace

LivePlayerSession::LivePlayerSession(QObject *parent)
    : QObject(parent)
    , reader_(new protocol::HttpFlvStreamReader(this))
    , audioOutput_(std::make_unique<audio::SdlAudioOutput>())
    , videoFrameQueue_(kVideoFrameQueueMaxDepthForAudioMaster)
    , renderTimer_(new QTimer(this))
{
    renderTimer_->setTimerType(Qt::PreciseTimer);
    renderTimer_->setInterval(kRenderTickIntervalMs);
    connect(renderTimer_, &QTimer::timeout, this, &LivePlayerSession::onRenderTick);
    connect(reader_, &protocol::HttpFlvStreamReader::connected,
            this, &LivePlayerSession::handleReaderConnected);
    connect(reader_, &protocol::HttpFlvStreamReader::dataChunkReceived,
            this, &LivePlayerSession::handleReaderDataChunk);
    connect(reader_, &protocol::HttpFlvStreamReader::errorOccurred,
            this, &LivePlayerSession::handleReaderError);
    connect(reader_, &protocol::HttpFlvStreamReader::finished,
            this, &LivePlayerSession::handleReaderFinished);
    connect(reader_, &protocol::HttpFlvStreamReader::logMessage,
            this, [this](const QString &message) {
                appendInfoLog(message);
            });
    audioOutput_->setLogCallback([this](const QString &message, bool warning) {
        QMetaObject::invokeMethod(this,
                                  [this, message, warning]() {
                                      if (warning) {
                                          appendWarnLog(message);
                                      } else {
                                          appendInfoLog(message);
                                      }
                                  },
                                  Qt::QueuedConnection);
    });
}

LivePlayerSession::~LivePlayerSession()
{
    resetPlaybackResources(true);
}

void LivePlayerSession::attachVideoSurface(QWidget *surface)
{
    videoSurface_ = surface;
    if (videoSurface_) {
        appendInfoLog("Live render surface attached. Decoded AVFrames from your manual HTTP-FLV path will be copied here.");
    }
}

void LivePlayerSession::open(const QString &url)
{
    const QString trimmedUrl = url.trimmed();
    if (trimmedUrl.isEmpty()) {
        setState(SessionState::Error, "Live URL is empty.");
        appendErrorLog("Refusing to open an empty live URL.");
        return;
    }

    const QString previousUrl = currentUrl_;
    const SessionState previousState = state_;
    resetPlaybackResources(false);
    resetCounters();
    currentUrl_ = trimmedUrl;
    sessionOpenStartedAt_ = std::chrono::steady_clock::now();
    const QByteArray currentUrlUtf8 = currentUrl_.toUtf8();
    const QByteArray previousUrlUtf8 = previousUrl.toUtf8();
    logging::info("[session] [gen={}] open begin url={} previous_state={} previous_url={}",
                  videoDecodeGeneration_,
                  currentUrlUtf8.constData(),
                  sessionStateName(previousState),
                  previousUrlUtf8.constData());
    startAudioDecodeWorker();
    startVideoDecodeWorker();

    if (videoSurface_) {
        QMetaObject::invokeMethod(videoSurface_.data(), "clearFrame", Qt::QueuedConnection);
    }

    setState(SessionState::Connecting, QString("Opening %1").arg(currentUrl_));
    appendInfoLog("Data flow 1/4: QNetworkReply::readyRead -> QByteArray chunk.");
    appendInfoLog("Data flow 2/4: FlvDemuxer::pushBytes -> FLV tag header + payload.");
    appendInfoLog("Data flow 3/4: audio tag -> AudioTagQueue -> LiveAudioDecodeWorker -> AAC decode -> PCM queue -> SDL audio callback.");
    appendInfoLog("Data flow 4/4: video tag -> VideoTagQueue -> LiveVideoDecodeWorker -> VideoFrameQueue -> render tick -> VideoOpenGLWidget.");
    appendInfoLog("Current milestone keeps the existing render tick and video frame queue. Audio now decodes and plays independently first, and video can follow audio clock when it becomes valid.");
    startRenderTimer();
    reader_->open(QUrl(currentUrl_));
}

void LivePlayerSession::stop()
{
    const QByteArray currentUrlUtf8 = currentUrl_.toUtf8();
    logging::info("[session] [gen={}] stop requested current_url={}", videoDecodeGeneration_, currentUrlUtf8.constData());
    resetPlaybackResources(false);
    resetCounters();
    currentUrl_.clear();
    setState(SessionState::Stopped, "Live stream stopped.");
}

void LivePlayerSession::handleReaderConnected(const QString &contentType, int statusCode)
{
    if (!contentType.isEmpty()) {
        appendInfoLog(QString("HTTP response received. Content-Type: %1").arg(contentType));
    }

    if (statusCode >= 400) {
        appendWarnLog(QString("HTTP status code is %1. The client will still try to parse the stream body.").arg(statusCode));
    }

    if (!contentType.isEmpty() && !contentType.contains("flv", Qt::CaseInsensitive)) {
        appendWarnLog("Content-Type does not explicitly contain flv. Continue parsing, but verify your HTTP-FLV mapping if demux fails.");
    }

    setState(SessionState::Reading,
             QString("HTTP connected (%1, status %2). Waiting for FLV header and media tags.")
                 .arg(contentType.isEmpty() ? "unknown content type" : contentType)
                 .arg(statusCode));
}

void LivePlayerSession::handleReaderDataChunk(const QByteArray &chunk)
{
    if (chunk.isEmpty()) {
        return;
    }

    bytesReceived_ += chunk.size();

    protocol::FlvFeedReport report;
    if (!demuxer_.pushBytes(chunk, report)) {
        const QString errorMessage = demuxer_.lastError().isEmpty()
            ? QString("FLV demux failed after receiving %1 bytes.").arg(bytesReceived_)
            : demuxer_.lastError();
        setState(SessionState::Error, errorMessage);
        appendErrorLog("Demuxer reported a fatal parse error. Check FLV tag boundaries and previous tag size.");
        logging::error("[session] [gen={}] demux fatal error after bytes_received={} message={}",
                       videoDecodeGeneration_,
                       bytesReceived_,
                       errorMessage.toUtf8().constData());
        if (reader_->isActive()) {
            reader_->close();
        }
        clearQueuedVideoFramesAndClock();
        invalidateVideoDecodeGeneration("demux fatal error");
        resetAudioPlaybackChain();
        stopVideoDecodeWorker();
        return;
    }

    if (!firstPayloadObserved_) {
        firstPayloadObserved_ = true;
        appendInfoLog(QString("Received first payload chunk: %1 bytes.").arg(chunk.size()));
    }

    if (report.headerValidated) {
        appendInfoLog("FLV header validated. The session is now consuming tag headers and payloads incrementally.");
    }

    updateDecodeBackpressure();
    if (report.parsedTagCount > 0 && !drainParsedTags()) {
        if (reader_->isActive()) {
            reader_->close();
        }
        logging::error("[session] [gen={}] drainParsedTags failed, begin fatal cleanup", videoDecodeGeneration_);
        clearQueuedVideoFramesAndClock();
        invalidateVideoDecodeGeneration("drainParsedTags fatal cleanup");
        resetAudioPlaybackChain();
        stopVideoDecodeWorker();
        return;
    }
    updateDecodeBackpressure();

    emit statsChanged(bytesReceived_, audioTagCount_, videoTagCount_, scriptTagCount_);
}

void LivePlayerSession::handleReaderError(const QString &message)
{
    setState(SessionState::Error, QString("HTTP-FLV read failed: %1").arg(message));
    appendErrorLog(QString("Network layer reported an error: %1").arg(message));
    logging::error("[session] [gen={}] reader error, begin cleanup message={}",
                   videoDecodeGeneration_,
                   message.toUtf8().constData());
    if (reader_ && reader_->isActive()) {
        reader_->close();
    }
    clearQueuedVideoFramesAndClock();
    invalidateVideoDecodeGeneration("reader error");
    resetAudioPlaybackChain();
    stopVideoDecodeWorker();
}

void LivePlayerSession::handleReaderFinished()
{
    if (state_ == SessionState::Error) {
        return;
    }

    setState(SessionState::Stopped, "Live stream finished.");
    appendInfoLog("HTTP-FLV reader finished.");
    logging::info("[session] [gen={}] reader finished, begin cleanup", videoDecodeGeneration_);
    clearQueuedVideoFramesAndClock();
    invalidateVideoDecodeGeneration("reader finished");
    resetAudioPlaybackChain();
    stopVideoDecodeWorker();
}

void LivePlayerSession::startRenderTimer()
{
    if (!renderTimer_ || renderTimer_->isActive()) {
        return;
    }

    renderTimer_->start();
    logging::info("[session] [gen={}] render timer started interval={}ms",
                  videoDecodeGeneration_,
                  kRenderTickIntervalMs);
}

void LivePlayerSession::stopRenderTimer()
{
    if (!renderTimer_ || !renderTimer_->isActive()) {
        return;
    }

    renderTimer_->stop();
    logging::info("[session] [gen={}] render timer stopped", videoDecodeGeneration_);
}

void LivePlayerSession::clearQueuedVideoFramesAndClock()
{
    stopRenderTimer();
    videoFrameQueue_.clear();
    firstVideoFrameQueued_ = false;
    firstVideoFrameSubmittedToRender_ = false;
    playbackStarted_ = false;
    firstObservedVideoPtsMs_ = -1;
    firstQueuedVideoPtsMs_ = -1;
    lastRenderedVideoPtsMs_ = -1;
    consecutiveRenderWaitCount_ = 0;
    consecutiveQueueOverflowCount_ = 0;
    lastPlaybackSnapshotLogWallClockMs_ = 0;
    lastClockPendingLogWallClockMs_ = 0;
    lastPlaybackReanchorWallClockMs_ = 0;
    lastAvDriftLogWallClockMs_ = 0;
    lastIngressBackpressureLogWallClockMs_ = 0;
    audioClockMasterActiveLogged_ = false;
    audioDecodeBackpressureActive_ = false;
    videoDecodeBackpressureActive_ = false;
    ingressBackpressureActive_ = false;
    playbackStartWallClock_ = std::chrono::steady_clock::time_point{};
    pendingAudioPcmDurationMs_.store(0);
    pendingAudioPcmCallbackCount_.store(0);
    if (audioDecodeWorker_) {
        audioDecodeWorker_->setDecodeBackpressure(false, 0, 0, 0, 0);
    }
    if (videoDecodeWorker_) {
        videoDecodeWorker_->setDecodeBackpressure(false, -1, 0, 0, 0);
    }
    if (reader_) {
        reader_->setReadThrottled(false, 0);
    }
}

void LivePlayerSession::resetAudioPlaybackChain()
{
    stopAudioDecodeWorker();
    if (audioOutput_) {
        audioOutput_->stop();
    }
}

void LivePlayerSession::updateDecodeBackpressure()
{
    const auto audioBacklog = captureAudioBacklogSnapshot(audioOutput_.get(),
                                                          pendingAudioPcmDurationMs_.load(),
                                                          pendingAudioPcmCallbackCount_.load());
    const int audioTagDepth = audioDecodeWorker_ ? audioDecodeWorker_->queuedTagCount() : 0;
    const int videoTagDepth = videoDecodeWorker_ ? videoDecodeWorker_->queuedTagCount() : 0;
    const int demuxParsedTags = demuxer_.parsedTagCount();
    const int demuxBufferedBytes = demuxer_.bufferedByteCount();

    const bool previousAudioBackpressure = audioDecodeBackpressureActive_;
    const char *audioBackpressureReason = "audio_worker_missing";
    bool nextAudioBackpressure = false;
    if (audioDecodeWorker_) {
        if (audioBacklog.audioHungry) {
            nextAudioBackpressure = false;
            audioBackpressureReason = "audio_hungry";
        } else if (audioDecodeBackpressureActive_) {
            nextAudioBackpressure = audioBacklog.totalAudioMs >= kAudioBackpressureOffMs;
            audioBackpressureReason = nextAudioBackpressure ? "hold_above_off" : "below_off";
        } else {
            nextAudioBackpressure = audioBacklog.totalAudioMs >= kAudioBackpressureOnMs;
            audioBackpressureReason = nextAudioBackpressure ? "above_on" : "below_on";
        }
    }
    audioDecodeBackpressureActive_ = nextAudioBackpressure;
    if (audioDecodeWorker_) {
        audioDecodeWorker_->setDecodeBackpressure(nextAudioBackpressure,
                                                  audioBacklog.totalAudioMs,
                                                  audioBacklog.clock.queuedDurationMs,
                                                  audioBacklog.pendingPcmMs,
                                                  audioTagDepth);
    }
    if (previousAudioBackpressure != nextAudioBackpressure) {
        logging::info("[audio_backpressure] [gen={}] {} reason={} total_audio_ms={} audio_device_queue_ms={} pending_pcm_ms={} pending_pcm_callbacks={} audio_needs_refill={} audio_hungry={} audio_tag_depth={}",
                      videoDecodeGeneration_,
                      nextAudioBackpressure ? "on" : "off",
                      audioBackpressureReason,
                      audioBacklog.totalAudioMs,
                      audioBacklog.clock.queuedDurationMs,
                      audioBacklog.pendingPcmMs,
                      audioBacklog.pendingPcmCallbacks,
                      audioBacklog.audioNeedsRefill,
                      audioBacklog.audioHungry,
                      audioTagDepth);
    }

    qint64 backLeadMs = 0;
    int queueDepth = 0;
    qint64 bufferedDurationMs = 0;
    bool nextVideoBackpressure = false;
    if (videoDecodeWorker_ &&
        playbackStarted_ &&
        audioBacklog.clock.valid &&
        !videoFrameQueue_.empty()) {
        backLeadMs = videoFrameQueue_.back().ptsMs - audioBacklog.clock.ptsMs;
        queueDepth = videoFrameQueue_.size();
        bufferedDurationMs = videoFrameQueue_.bufferedDurationMs();
        if (videoDecodeBackpressureActive_) {
            nextVideoBackpressure =
                backLeadMs >= kVideoDecodeBackpressureLowWaterMs ||
                queueDepth >= kVideoDecodeBackpressureLowDepth ||
                bufferedDurationMs >= kVideoDecodeBackpressureLowWaterMs;
        } else {
            nextVideoBackpressure =
                backLeadMs >= kVideoDecodeBackpressureHighWaterMs ||
                queueDepth >= kVideoDecodeBackpressureHighDepth ||
                bufferedDurationMs >= kVideoDecodeBackpressureHighWaterMs;
        }
    }
    videoDecodeBackpressureActive_ = nextVideoBackpressure;
    if (videoDecodeWorker_) {
        videoDecodeWorker_->setDecodeBackpressure(nextVideoBackpressure,
                                                  audioBacklog.clock.valid ? audioBacklog.clock.ptsMs : -1,
                                                  backLeadMs,
                                                  queueDepth,
                                                  bufferedDurationMs);
    }

    const bool nextIngressBackpressure = audioBacklog.audioHungry
        ? false
        : (ingressBackpressureActive_
            ? (audioDecodeBackpressureActive_ ||
               videoDecodeBackpressureActive_ ||
               audioBacklog.totalAudioMs >= kAudioBackpressureOffMs ||
               audioTagDepth >= kAudioTagQueueLowWater ||
               videoTagDepth >= kVideoTagQueueLowWater)
            : (audioDecodeBackpressureActive_ ||
               videoDecodeBackpressureActive_ ||
               audioBacklog.totalAudioMs >= kAudioBackpressureOnMs ||
               audioTagDepth >= kAudioTagQueueHighWater ||
               videoTagDepth >= kVideoTagQueueHighWater));
    if (ingressBackpressureActive_ != nextIngressBackpressure) {
        ingressBackpressureActive_ = nextIngressBackpressure;
        if (reader_) {
            reader_->setReadThrottled(ingressBackpressureActive_, kReaderThrottleBufferBytes);
        }
        logging::info("[ingress_backpressure] [gen={}] {} total_audio_ms={} audio_device_queue_ms={} pending_pcm_ms={} pending_pcm_callbacks={} audio_needs_refill={} audio_hungry={} audio_tag_depth={} video_tag_depth={} demux_parsed_tags={} demux_buffer_bytes={} video_queue_depth={} video_buffered_ms={} video_back_lead_ms={}",
                      videoDecodeGeneration_,
                      ingressBackpressureActive_ ? "on" : "off",
                      audioBacklog.totalAudioMs,
                      audioBacklog.clock.queuedDurationMs,
                      audioBacklog.pendingPcmMs,
                      audioBacklog.pendingPcmCallbacks,
                      audioBacklog.audioNeedsRefill,
                      audioBacklog.audioHungry,
                      audioTagDepth,
                      videoTagDepth,
                      demuxParsedTags,
                      demuxBufferedBytes,
                      queueDepth,
                      bufferedDurationMs,
                      backLeadMs);
    } else if (ingressBackpressureActive_) {
        const qint64 nowMs = steadyNowMs();
        if (nowMs - lastIngressBackpressureLogWallClockMs_ >= kPlaybackSnapshotLogIntervalMs) {
            lastIngressBackpressureLogWallClockMs_ = nowMs;
            logging::debug("[ingress_backpressure] [gen={}] hold total_audio_ms={} audio_device_queue_ms={} pending_pcm_ms={} pending_pcm_callbacks={} audio_needs_refill={} audio_hungry={} audio_tag_depth={} video_tag_depth={} demux_parsed_tags={} demux_buffer_bytes={} video_queue_depth={} video_buffered_ms={} video_back_lead_ms={}",
                           videoDecodeGeneration_,
                           audioBacklog.totalAudioMs,
                           audioBacklog.clock.queuedDurationMs,
                           audioBacklog.pendingPcmMs,
                           audioBacklog.pendingPcmCallbacks,
                           audioBacklog.audioNeedsRefill,
                           audioBacklog.audioHungry,
                           audioTagDepth,
                           videoTagDepth,
                           demuxParsedTags,
                           demuxBufferedBytes,
                           queueDepth,
                           bufferedDurationMs,
                           backLeadMs);
        }
    }
}

bool LivePlayerSession::shouldThrottleIngress() const
{
    return ingressBackpressureActive_;
}

qint64 LivePlayerSession::sessionElapsedMs() const
{
    if (sessionOpenStartedAt_.time_since_epoch().count() == 0) {
        return 0;
    }

    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - sessionOpenStartedAt_)
        .count();
}

bool LivePlayerSession::playbackClockStarted() const
{
    return playbackStarted_ &&
        firstQueuedVideoPtsMs_ >= 0 &&
        playbackStartWallClock_.time_since_epoch().count() != 0;
}

qint64 LivePlayerSession::currentAudioClockPtsMs() const
{
    if (!audioOutput_) {
        return -1;
    }

    const auto snapshot = audioOutput_->clockSnapshot();
    return snapshot.valid ? snapshot.ptsMs : -1;
}

qint64 LivePlayerSession::currentTargetVideoPtsMs(qint64 audioClockPtsMs)
{
    if (playbackStarted_ && audioClockPtsMs >= 0) {
        return audioClockPtsMs;
    }

    if (!playbackClockStarted()) {
        const qint64 nowMs = steadyNowMs();
        if (!videoFrameQueue_.empty() &&
            nowMs - lastClockPendingLogWallClockMs_ >= kPlaybackSnapshotLogIntervalMs) {
            lastClockPendingLogWallClockMs_ = nowMs;
            logging::debug("[playback_clock] [gen={}] target_unavailable playbackStarted={} anchorPts={} depth={} bufferedMs={}",
                           videoDecodeGeneration_,
                           playbackStarted_,
                           firstQueuedVideoPtsMs_,
                           videoFrameQueue_.size(),
                           videoFrameQueue_.bufferedDurationMs());
        }
        return -1;
    }

    const qint64 elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - playbackStartWallClock_)
                                 .count();
    return firstQueuedVideoPtsMs_ + elapsedMs;
}

void LivePlayerSession::logPlaybackSnapshot(const char *reason, qint64 targetPtsMs, bool force)
{
    const qint64 nowMs = steadyNowMs();
    if (!force && nowMs - lastPlaybackSnapshotLogWallClockMs_ < kPlaybackSnapshotLogIntervalMs) {
        return;
    }

    lastPlaybackSnapshotLogWallClockMs_ = nowMs;
    const qint64 frontPtsMs = videoFrameQueue_.empty() ? -1 : videoFrameQueue_.front().ptsMs;
    const qint64 backPtsMs = videoFrameQueue_.empty() ? -1 : videoFrameQueue_.back().ptsMs;
    const qint64 bufferedDurationMs = videoFrameQueue_.empty() ? 0 : videoFrameQueue_.bufferedDurationMs();

    logging::debug("[playback] [gen={}] snapshot reason={} targetPts={} frontPts={} backPts={} depth={} bufferedMs={} waitStreak={} overflowStreak={} playbackStarted={}",
                   videoDecodeGeneration_,
                   reason ? reason : "unknown",
                   targetPtsMs,
                   frontPtsMs,
                   backPtsMs,
                   videoFrameQueue_.size(),
                   bufferedDurationMs,
                   consecutiveRenderWaitCount_,
                   consecutiveQueueOverflowCount_,
                   playbackStarted_);
}

void LivePlayerSession::maybeStartBufferedPlayback()
{
    if (playbackStarted_ || videoFrameQueue_.empty()) {
        return;
    }

    const int depth = videoFrameQueue_.size();
    const qint64 bufferedDurationMs = videoFrameQueue_.bufferedDurationMs();
    const bool readyByDuration = bufferedDurationMs >= kStartupMinBufferedDurationMs;
    const bool readyByDepthWindow = depth >= kStartupMinBufferedFrameDepth &&
        bufferedDurationMs >= kStartupMinBufferedDurationWithMinDepthMs;
    if (!readyByDuration && !readyByDepthWindow) {
        logging::debug("[playback] [gen={}] startup_buffering depth={} bufferedMs={} minDepth={} minDepthBufferedMs={} minBufferedMs={} firstObservedPts={}",
                       videoDecodeGeneration_,
                       depth,
                       bufferedDurationMs,
                       kStartupMinBufferedFrameDepth,
                       kStartupMinBufferedDurationWithMinDepthMs,
                       kStartupMinBufferedDurationMs,
                       firstObservedVideoPtsMs_);
        logPlaybackSnapshot("startup_buffering", -1, false);
        return;
    }

    firstQueuedVideoPtsMs_ = videoFrameQueue_.front().ptsMs;
    playbackStartWallClock_ = std::chrono::steady_clock::now();
    playbackStarted_ = true;

    const qint64 anchorWallClockMs = steadyNowMs();
    appendInfoLog(
        QString("Startup buffering finished. Live video playback started with anchorPts=%1, depth=%2, buffered=%3 ms.")
            .arg(firstQueuedVideoPtsMs_)
            .arg(depth)
            .arg(bufferedDurationMs));
    setState(SessionState::Playing,
             QString("Startup buffer ready (depth=%1, buffered=%2 ms). Render tick keeps driving video; target pts will follow audio clock when audio starts playing.")
                 .arg(depth)
                 .arg(bufferedDurationMs));
    logging::info("[playback] [gen={}] startup_playback_started anchorPts={} anchorWallClockMs={} depth={} bufferedMs={} readyByDuration={} readyByDepthWindow={} firstObservedPts={} frontPts={} backPts={}",
                  videoDecodeGeneration_,
                  firstQueuedVideoPtsMs_,
                  anchorWallClockMs,
                  depth,
                  bufferedDurationMs,
                  readyByDuration,
                  readyByDepthWindow,
                  firstObservedVideoPtsMs_,
                  videoFrameQueue_.front().ptsMs,
                  videoFrameQueue_.back().ptsMs);
    logPlaybackSnapshot("startup_playback_started", firstQueuedVideoPtsMs_, true);
}

void LivePlayerSession::trimQueuedVideoFramesForPlaybackWindow(qint64 referenceTargetPtsMs, const char *reason)
{//根据当前播放时间点（referenceTargetPtsMs）和预设的时间窗口，丢弃过早或过晚的帧，以保持视频播放的流畅性和同步性。
    if (videoFrameQueue_.empty()) {
        return;
    }

    if (referenceTargetPtsMs < 0) {
        referenceTargetPtsMs = videoFrameQueue_.front().ptsMs;
    }

    const auto audioClockSnapshot = audioOutput_
        ? audioOutput_->clockSnapshot()
        : audio::SdlAudioOutput::ClockSnapshot{};
    const qint64 frontPtsBeforeTrimMs = videoFrameQueue_.front().ptsMs;
    const qint64 backPtsBeforeTrimMs = videoFrameQueue_.back().ptsMs;
    const int depthBeforeTrim = videoFrameQueue_.size();
    const qint64 bufferedBeforeTrimMs = videoFrameQueue_.bufferedDurationMs();
    const qint64 minRetainPtsMs = referenceTargetPtsMs - kLateVideoFrameDropThresholdMs;
    const qint64 maxRetainPtsMs = referenceTargetPtsMs + kFutureQueueRetentionWindowMs;
    const qint64 nonNegativeMinRetainPtsMs = minRetainPtsMs < 0 ? 0 : minRetainPtsMs;
    int droppedTooOldCount = 0;
    int droppedTooFutureCount = 0;
    qint64 firstDroppedPtsMs = -1;
    qint64 lastDroppedPtsMs = -1;
    bool trimmedByDepthLimit = false;
    bool trimmedByFutureWindow = false;

    auto noteDropped = [&](qint64 ptsMs) {
        if (firstDroppedPtsMs < 0) {
            firstDroppedPtsMs = ptsMs;
        }
        lastDroppedPtsMs = ptsMs;
    };

    while (videoFrameQueue_.size() > 1) {
        const QueuedVideoFrame &queuedFrame = videoFrameQueue_.front();
        if (queuedFrame.ptsMs >= minRetainPtsMs) {
            break;
        }

        noteDropped(queuedFrame.ptsMs);
        ++droppedTooOldCount;
        videoFrameQueue_.popFront();
    }

    while (videoFrameQueue_.size() > videoFrameQueue_.maxDepth()) {
        if (videoFrameQueue_.size() <= 1) {
            break;
        }

        const QueuedVideoFrame &futureFrame = videoFrameQueue_.back();
        noteDropped(futureFrame.ptsMs);
        ++droppedTooFutureCount;
        trimmedByDepthLimit = true;
        videoFrameQueue_.popBack();
    }

    const bool usingAudioMaster = playbackStarted_ && audioClockSnapshot.valid;
    const qint64 futureTrimWindowMs = usingAudioMaster
        ? kFutureQueueEmergencyTrimWindowMs
        : kFutureQueueRetentionWindowMs;
    const qint64 effectiveMaxRetainPtsMs = referenceTargetPtsMs + futureTrimWindowMs;
    if (playbackStarted_) {
        while (videoFrameQueue_.size() > 1 &&
               videoFrameQueue_.back().ptsMs > effectiveMaxRetainPtsMs &&
               videoFrameQueue_.back().ptsMs - referenceTargetPtsMs > futureTrimWindowMs &&
               videoFrameQueue_.bufferedDurationMs() > futureTrimWindowMs + kFutureQueueEmergencyTrimBufferedMarginMs) {
            const QueuedVideoFrame &futureFrame = videoFrameQueue_.back();
            noteDropped(futureFrame.ptsMs);
            ++droppedTooFutureCount;
            trimmedByFutureWindow = true;
            videoFrameQueue_.popBack();
        }
    }

    if (droppedTooOldCount == 0 && droppedTooFutureCount == 0) {
        consecutiveQueueOverflowCount_ = 0;
        return;
    }

    ++consecutiveQueueOverflowCount_;
    const qint64 frontPtsAfterTrimMs = videoFrameQueue_.empty() ? -1 : videoFrameQueue_.front().ptsMs;
    const qint64 backPtsAfterTrimMs = videoFrameQueue_.empty() ? -1 : videoFrameQueue_.back().ptsMs;
    const int depthAfterTrim = videoFrameQueue_.size();
    const qint64 bufferedAfterTrimMs = videoFrameQueue_.empty() ? 0 : videoFrameQueue_.bufferedDurationMs();
    const qint64 firstDroppedLeadVsTargetMs = firstDroppedPtsMs >= 0
        ? firstDroppedPtsMs - referenceTargetPtsMs
        : -1;
    const qint64 lastDroppedLeadVsTargetMs = lastDroppedPtsMs >= 0
        ? lastDroppedPtsMs - referenceTargetPtsMs
        : -1;
    const qint64 firstDroppedLeadVsAudioMs = (firstDroppedPtsMs >= 0 && audioClockSnapshot.valid)
        ? firstDroppedPtsMs - audioClockSnapshot.ptsMs
        : -1;
    const qint64 lastDroppedLeadVsAudioMs = (lastDroppedPtsMs >= 0 && audioClockSnapshot.valid)
        ? lastDroppedPtsMs - audioClockSnapshot.ptsMs
        : -1;
    logging::debug("[frame_queue] [gen={}] trim_detail trigger={} cause=too_old:{} depth_limit:{} future_window:{} usingAudioMaster={} refTargetPts={} audioClockPts={} frontPtsBefore={} backPtsBefore={} frontPtsAfter={} backPtsAfter={} frontLeadVsTargetMs={} backLeadVsTargetMs={} frontLeadVsAudioMs={} backLeadVsAudioMs={} keepWindowRaw=[{},{}] keepWindowEffective=[{},{}] keepWindowNonNegative=[{},{}] droppedTooOld={} droppedTooFuture={} firstDroppedPts={} lastDroppedPts={} firstDroppedLeadVsTargetMs={} lastDroppedLeadVsTargetMs={} firstDroppedLeadVsAudioMs={} lastDroppedLeadVsAudioMs={} depthBefore={} depthAfter={} bufferedBeforeMs={} bufferedAfterMs={} overflowStreak={}",
                   videoDecodeGeneration_,
                   reason ? reason : "unknown",
                   droppedTooOldCount > 0,
                   trimmedByDepthLimit,
                   trimmedByFutureWindow,
                   usingAudioMaster,
                   referenceTargetPtsMs,
                   audioClockSnapshot.valid ? audioClockSnapshot.ptsMs : -1,
                   frontPtsBeforeTrimMs,
                   backPtsBeforeTrimMs,
                   frontPtsAfterTrimMs,
                   backPtsAfterTrimMs,
                   frontPtsBeforeTrimMs - referenceTargetPtsMs,
                   backPtsBeforeTrimMs - referenceTargetPtsMs,
                   audioClockSnapshot.valid ? frontPtsBeforeTrimMs - audioClockSnapshot.ptsMs : -1,
                   audioClockSnapshot.valid ? backPtsBeforeTrimMs - audioClockSnapshot.ptsMs : -1,
                   minRetainPtsMs,
                   maxRetainPtsMs,
                   minRetainPtsMs,
                   effectiveMaxRetainPtsMs,
                   nonNegativeMinRetainPtsMs,
                   effectiveMaxRetainPtsMs,
                   droppedTooOldCount,
                   droppedTooFutureCount,
                   firstDroppedPtsMs,
                   lastDroppedPtsMs,
                   firstDroppedLeadVsTargetMs,
                   lastDroppedLeadVsTargetMs,
                   firstDroppedLeadVsAudioMs,
                   lastDroppedLeadVsAudioMs,
                   depthBeforeTrim,
                   depthAfterTrim,
                   bufferedBeforeTrimMs,
                   bufferedAfterTrimMs,
                   consecutiveQueueOverflowCount_);
    logging::debug("[frame_queue] [gen={}] trim reason={} usingAudioMaster={} refTargetPts={} keepWindowEffective=[{},{}] droppedTooOld={} droppedTooFuture={} firstDroppedPts={} lastDroppedPts={} depth={} bufferedMs={} overflowStreak={}",
                   videoDecodeGeneration_,//trime reason 的打印是为了帮助开发者理解为什么在当前时刻需要丢弃某些帧，以及这些帧的时间戳分布情况。这对于调试和优化播放体验非常有用，尤其是在处理直播流时，帧的及时性和顺序性对用户体验至关重要。
                   reason ? reason : "unknown", //triming reasion 修剪原因
                   usingAudioMaster,
                   referenceTargetPtsMs,
                   minRetainPtsMs,
                   effectiveMaxRetainPtsMs,
                   droppedTooOldCount,
                   droppedTooFutureCount,
                   firstDroppedPtsMs,
                   lastDroppedPtsMs,
                   videoFrameQueue_.size(),
                   videoFrameQueue_.bufferedDurationMs(),
                   consecutiveQueueOverflowCount_);
    logPlaybackSnapshot("frame_queue_trim", referenceTargetPtsMs, true);
}

qint64 LivePlayerSession::maybeReanchorPlaybackClock(qint64 targetPtsMs)
{
    if (!playbackClockStarted() || videoFrameQueue_.empty()) {
        return targetPtsMs;
    }

    const qint64 nowMs = steadyNowMs();
    if (lastPlaybackReanchorWallClockMs_ > 0 &&
        nowMs - lastPlaybackReanchorWallClockMs_ < kReanchorCooldownMs) {
        return targetPtsMs;
    }
    //frontLeadMs是队列中最早帧的时间戳与当前播放时间点之间的差距，backLeadMs是队列中最晚帧的时间戳与当前播放时间点之间的差距。这些差距可以帮助判断当前播放时间点是否过早（frontLeadMs过小）或过晚（backLeadMs过小），以及是否需要重新调整播放时钟（re-anchor）以保持视频播放的流畅性和同步性。
    const qint64 frontLeadMs = videoFrameQueue_.front().ptsMs - targetPtsMs;//frontLeadMs 和 backLeadMs 的计算是为了评估当前播放时间点与队列中最早和最晚帧的时间戳之间的差距。这些差距可以帮助判断是否需要重新调整播放时钟（re-anchor），以保持视频播放的流畅性和同步性。
    const qint64 backLeadMs = videoFrameQueue_.back().ptsMs - targetPtsMs;
    const bool normalReanchorReady =
        frontLeadMs >= kFutureQueueLeadThresholdMs &&
        backLeadMs >= kFutureQueueRetentionWindowMs &&
        consecutiveRenderWaitCount_ >= kReanchorWaitThreshold &&
        consecutiveQueueOverflowCount_ >= kReanchorOverflowThreshold;
    const bool fastPathReanchorReady =
        frontLeadMs >= kReanchorFastPathFrontLeadMs &&
        backLeadMs >= kReanchorFastPathBackLeadMs &&
        consecutiveRenderWaitCount_ >= kReanchorFastPathWaitThreshold &&
        videoFrameQueue_.size() >= kStartupMinBufferedFrameDepth;
    if (!normalReanchorReady && !fastPathReanchorReady) {
        return targetPtsMs;
    }

    const char *reanchorReason = fastPathReanchorReady ? "fast_path" : "wait_overflow";
    const qint64 previousAnchorPtsMs = firstQueuedVideoPtsMs_;
    const qint64 newTargetPtsMs = videoFrameQueue_.front().ptsMs - kReanchorFutureLeadMs;
    firstQueuedVideoPtsMs_ = newTargetPtsMs;
    playbackStartWallClock_ = std::chrono::steady_clock::now();
    lastPlaybackReanchorWallClockMs_ = nowMs;

    appendWarnLog(
        QString("Playback clock re-anchored (%1). target %2 -> %3, front=%4, back=%5.")
            .arg(reanchorReason)
            .arg(targetPtsMs)
            .arg(newTargetPtsMs)
            .arg(videoFrameQueue_.front().ptsMs)
            .arg(videoFrameQueue_.back().ptsMs));
    logging::warn("[playback] [gen={}] reanchor reason={} oldTargetPts={} newTargetPts={} oldAnchorPts={} anchorWallClockMs={} frontPts={} backPts={} frontLeadMs={} backLeadMs={} waitStreak={} overflowStreak={} futureLeadMs={}",
                  videoDecodeGeneration_,
                  reanchorReason,
                  targetPtsMs,
                  newTargetPtsMs,
                  previousAnchorPtsMs,
                  nowMs,
                  videoFrameQueue_.front().ptsMs,
                  videoFrameQueue_.back().ptsMs,
                  frontLeadMs,
                  backLeadMs,
                  consecutiveRenderWaitCount_,
                  consecutiveQueueOverflowCount_,
                  kReanchorFutureLeadMs);
    trimQueuedVideoFramesForPlaybackWindow(newTargetPtsMs, "reanchor");
    logPlaybackSnapshot("playback_reanchor", newTargetPtsMs, true);
    consecutiveRenderWaitCount_ = 0;
    return newTargetPtsMs;
}

void LivePlayerSession::onRenderTick()
{
    updateDecodeBackpressure();
    if (demuxer_.parsedTagCount() > 0) {
        if (!drainParsedTags()) {
            if (reader_ && reader_->isActive()) {
                reader_->close();
            }
            logging::error("[session] [gen={}] pending demux drain failed during render tick", videoDecodeGeneration_);
            clearQueuedVideoFramesAndClock();
            invalidateVideoDecodeGeneration("render tick demux drain fatal cleanup");
            resetAudioPlaybackChain();
            stopVideoDecodeWorker();
            return;
        }
        updateDecodeBackpressure();
    }
    if (videoFrameQueue_.empty()) {
        consecutiveRenderWaitCount_ = 0;
        return;
    }

    const auto audioClockSnapshot = audioOutput_
        ? audioOutput_->clockSnapshot()
        : audio::SdlAudioOutput::ClockSnapshot{};
    const bool usingAudioMaster = playbackStarted_ && audioClockSnapshot.valid;
    if (usingAudioMaster && !audioClockMasterActiveLogged_) {
        audioClockMasterActiveLogged_ = true;
        appendInfoLog("Audio clock is active. Render tick still drives video, but target video pts now follows audio clock.");
    }

    qint64 targetPtsMs = currentTargetVideoPtsMs(audioClockSnapshot.valid ? audioClockSnapshot.ptsMs : -1);
    if (targetPtsMs < 0) {
        logPlaybackSnapshot("render_tick_wait_startup", targetPtsMs, false);
        return;
    }

    const qint64 nowMs = steadyNowMs();
    if (audioClockSnapshot.valid &&
        nowMs - lastAvDriftLogWallClockMs_ >= kPlaybackSnapshotLogIntervalMs) {
        lastAvDriftLogWallClockMs_ = nowMs;
        const qint64 frontVideoPtsMs = videoFrameQueue_.empty() ? -1 : videoFrameQueue_.front().ptsMs;
        const qint64 backVideoPtsMs = videoFrameQueue_.empty() ? -1 : videoFrameQueue_.back().ptsMs;
        const qint64 pendingAudioPcmMs = std::max<qint64>(0, pendingAudioPcmDurationMs_.load());
        logging::debug("[av_sync] [gen={}] targetPts={} audioClockPts={} targetVsAudioMs={} usingAudioMaster={} frontVideoPts={} frontVsAudioMs={} backVideoPts={} backVsAudioMs={} lastShownVideoPts={} lastShownVsAudioMs={} audioQueueBytes={} audioQueueMs={} pendingAudioPcmMs={} audioBacklogMs={} videoDepth={} videoBufferedMs={}",
                       videoDecodeGeneration_,
                       targetPtsMs,
                       audioClockSnapshot.ptsMs,
                       targetPtsMs - audioClockSnapshot.ptsMs,
                       usingAudioMaster,
                       frontVideoPtsMs,
                       frontVideoPtsMs >= 0 ? frontVideoPtsMs - audioClockSnapshot.ptsMs : -1,
                       backVideoPtsMs,
                       backVideoPtsMs >= 0 ? backVideoPtsMs - audioClockSnapshot.ptsMs : -1,
                       lastRenderedVideoPtsMs_,
                       lastRenderedVideoPtsMs_ >= 0 ? lastRenderedVideoPtsMs_ - audioClockSnapshot.ptsMs : -1,
                       audioClockSnapshot.queuedBytes,
                       audioClockSnapshot.queuedDurationMs,
                       pendingAudioPcmMs,
                       audioClockSnapshot.queuedDurationMs + pendingAudioPcmMs,
                       videoFrameQueue_.size(),
                       videoFrameQueue_.bufferedDurationMs());
    }

    logPlaybackSnapshot("render_tick", targetPtsMs, false);
    dropLateQueuedVideoFrames(targetPtsMs);
    if (!usingAudioMaster) {
        targetPtsMs = maybeReanchorPlaybackClock(targetPtsMs);
    }
    tryRenderNextQueuedVideoFrame(targetPtsMs);
    updateDecodeBackpressure();
}

void LivePlayerSession::dropLateQueuedVideoFrames(qint64 targetPtsMs)
{
    int droppedLateCount = 0;
    qint64 firstDroppedPtsMs = -1;
    qint64 lastDroppedPtsMs = -1;

    auto noteLateDropped = [&](qint64 ptsMs) {
        if (firstDroppedPtsMs < 0) {
            firstDroppedPtsMs = ptsMs;
        }
        lastDroppedPtsMs = ptsMs;
    };

    while (!videoFrameQueue_.empty()) {
        const QueuedVideoFrame &queuedFrame = videoFrameQueue_.front();
        if (queuedFrame.generation != videoDecodeGeneration_) {
            logging::debug("[render] [gen={}] drop stale generation frame frame_gen={} current_gen={} pts={} depth={}",
                           videoDecodeGeneration_,
                           queuedFrame.generation,
                           videoDecodeGeneration_,
                           queuedFrame.ptsMs,
                           videoFrameQueue_.size());
            videoFrameQueue_.popFront();
            continue;
        }

        if (queuedFrame.ptsMs < 0) {
            logging::debug("[render] [gen={}] drop invalid pts frame pts={} depth={}",
                           videoDecodeGeneration_,
                           queuedFrame.ptsMs,
                           videoFrameQueue_.size());
            videoFrameQueue_.popFront();
            continue;
        }

        if (videoFrameQueue_.size() <= 1) {
            break;
        }

        const qint64 lateMs = targetPtsMs - queuedFrame.ptsMs;
        if (lateMs <= kLateVideoFrameDropThresholdMs) {
            break;
        }

        noteLateDropped(queuedFrame.ptsMs);
        ++droppedLateCount;
        videoFrameQueue_.popFront();
    }

    if (droppedLateCount <= 0) {
        return;
    }

    consecutiveRenderWaitCount_ = 0;
    logging::debug("[render] [gen={}] drop_late count={} firstDroppedPts={} lastDroppedPts={} targetPts={} depth={} bufferedMs={}",
                   videoDecodeGeneration_,
                   droppedLateCount,
                   firstDroppedPtsMs,
                   lastDroppedPtsMs,
                   targetPtsMs,
                   videoFrameQueue_.size(),
                   videoFrameQueue_.empty() ? 0 : videoFrameQueue_.bufferedDurationMs());
    logPlaybackSnapshot("render_drop_late", targetPtsMs, true);
}

void LivePlayerSession::tryRenderNextQueuedVideoFrame(qint64 targetPtsMs)
{
    if (videoFrameQueue_.empty()) {
        return;
    }

    const QueuedVideoFrame &queuedFrame = videoFrameQueue_.front();
    if (queuedFrame.generation != videoDecodeGeneration_) {
        logging::debug("[render] [gen={}] drop stale generation frame frame_gen={} current_gen={} pts={} depth={}",
                       videoDecodeGeneration_,
                       queuedFrame.generation,
                       videoDecodeGeneration_,
                       queuedFrame.ptsMs,
                       videoFrameQueue_.size());
        videoFrameQueue_.popFront();
        return;
    }

    if (queuedFrame.ptsMs < 0) {
        logging::debug("[render] [gen={}] drop invalid pts frame pts={} depth={}",
                       videoDecodeGeneration_,
                       queuedFrame.ptsMs,
                       videoFrameQueue_.size());
        videoFrameQueue_.popFront();
        return;
    }

    if (queuedFrame.ptsMs > targetPtsMs) {
        ++consecutiveRenderWaitCount_;
        if (consecutiveRenderWaitCount_ == 1 ||
            consecutiveRenderWaitCount_ % kRenderWaitLogEvery == 0) {
            logging::debug("[render] [gen={}] wait_frame pts={} targetPts={} earlyMs={} depth={} bufferedMs={} waitStreak={} overflowStreak={} playbackStarted={}",
                           videoDecodeGeneration_,
                           queuedFrame.ptsMs,
                           targetPtsMs,
                           queuedFrame.ptsMs - targetPtsMs,
                           videoFrameQueue_.size(),
                           videoFrameQueue_.bufferedDurationMs(),
                           consecutiveRenderWaitCount_,
                           consecutiveQueueOverflowCount_,
                           playbackStarted_);
            logPlaybackSnapshot("render_wait", targetPtsMs, true);
        }
        return;
    }

    consecutiveRenderWaitCount_ = 0;
    logging::debug("[render] [gen={}] show_frame pts={} targetPts={} lateMs={} depth={} bufferedMs={} overflowStreak={}",
                   videoDecodeGeneration_,
                   queuedFrame.ptsMs,
                   targetPtsMs,
                   targetPtsMs - queuedFrame.ptsMs,
                   videoFrameQueue_.size(),
                   videoFrameQueue_.bufferedDurationMs(),
                   consecutiveQueueOverflowCount_);
    if (!firstVideoFrameSubmittedToRender_) {
        firstVideoFrameSubmittedToRender_ = true;
        appendInfoLog("Timer-driven live render submitted its first video frame.");
        logging::info("[session] [gen={}] first frame submitted to render pts={} elapsed={}ms",
                      videoDecodeGeneration_,
                      queuedFrame.ptsMs,
                      sessionElapsedMs());
    }

    lastRenderedVideoPtsMs_ = queuedFrame.ptsMs;
    handleDecodedVideoFrame(queuedFrame.frame.get());
    videoFrameQueue_.popFront();
}

void LivePlayerSession::resetPlaybackResources(bool clearState)
{
    logging::info("[session] [gen={}] resetPlaybackResources begin clear_state={} reader_active={} worker_present={}",
                  videoDecodeGeneration_,
                  clearState,
                  reader_ && reader_->isActive(),
                  static_cast<bool>(videoDecodeWorker_));
    if (reader_ && reader_->isActive()) {
        reader_->close();
    }

    clearQueuedVideoFramesAndClock();
    invalidateVideoDecodeGeneration(clearState ? "session destroy/reset clear state" : "session reset for stop/open");
    resetAudioPlaybackChain();
    stopVideoDecodeWorker();
    demuxer_.reset();
    resetVideoConverter();

    if (videoSurface_) {
        QMetaObject::invokeMethod(videoSurface_.data(), "clearFrame", Qt::QueuedConnection);
    }

    if (clearState) {
        currentUrl_.clear();
        state_ = SessionState::Idle;
    }
}

void LivePlayerSession::startAudioDecodeWorker()
{
    stopAudioDecodeWorker();
    if (audioOutput_) {
        audioOutput_->stop();
    }

    audioDecodeWorker_ = std::make_unique<LiveAudioDecodeWorker>(videoDecodeGeneration_);
    logging::info("[session] [gen={}] startAudioDecodeWorker create worker", videoDecodeGeneration_);
    audioDecodeWorker_->setLogCallback([this](const QString &message, quint64 generation) {
        QMetaObject::invokeMethod(this,
                                  [this, message, generation]() {
                                      appendWorkerLog(message, generation);
                                  },
                                  Qt::QueuedConnection);
    });
    audioDecodeWorker_->setErrorCallback([this](const QString &message, quint64 generation) {
        QMetaObject::invokeMethod(this,
                                  [this, message, generation]() {
                                      handleWorkerAudioDecodeError(message, generation);
                                  },
                                  Qt::QueuedConnection);
    });
    audioDecodeWorker_->setPcmReadyCallback([this](
                                                QByteArray pcm,
                                                qint64 ptsMs,
                                                int sampleRate,
                                                int channels,
                                                int sampleCount,
                                                quint64 generation) {
        const qint64 pcmDurationMs = (sampleRate > 0 && sampleCount > 0)
            ? (static_cast<qint64>(sampleCount) * 1000) / sampleRate
            : 0;
        if (pcmDurationMs > 0) {
            pendingAudioPcmDurationMs_.fetch_add(pcmDurationMs);
        }
        pendingAudioPcmCallbackCount_.fetch_add(1);
        const bool invoked = QMetaObject::invokeMethod(this,
                                                       [this, pcm, ptsMs, sampleRate, channels, sampleCount, generation, pcmDurationMs]() {
                                                           handleWorkerDecodedAudioPcm(pcm,
                                                                                       ptsMs,
                                                                                       sampleRate,
                                                                                       channels,
                                                                                       sampleCount,
                                                                                       generation,
                                                                                       pcmDurationMs);
                                                       },
                                                       Qt::QueuedConnection);
        if (!invoked) {
            if (pcmDurationMs > 0) {
                qint64 pendingPcmMs = pendingAudioPcmDurationMs_.load();
                while (!pendingAudioPcmDurationMs_.compare_exchange_weak(
                    pendingPcmMs,
                    std::max<qint64>(0, pendingPcmMs - pcmDurationMs))) {
                }
            }
            int pendingPcmCallbacks = pendingAudioPcmCallbackCount_.load();
            while (!pendingAudioPcmCallbackCount_.compare_exchange_weak(
                pendingPcmCallbacks,
                std::max(0, pendingPcmCallbacks - 1))) {
            }
        }
    });
    audioDecodeWorker_->start();

    appendInfoLog("Audio decode worker started. Audio tags now follow a sibling queue + worker path beside video.");
}

void LivePlayerSession::stopAudioDecodeWorker()
{
    if (!audioDecodeWorker_) {
        logging::debug("[session] [gen={}] stopAudioDecodeWorker skipped because worker is null", videoDecodeGeneration_);
        return;
    }

    logging::info("[session] [gen={}] stopAudioDecodeWorker begin", videoDecodeGeneration_);
    audioDecodeWorker_->stop();
    audioDecodeWorker_.reset();
    logging::info("[session] [gen={}] stopAudioDecodeWorker finished", videoDecodeGeneration_);
}

void LivePlayerSession::startVideoDecodeWorker()
{
    stopVideoDecodeWorker();

    videoDecodeWorker_ = std::make_unique<LiveVideoDecodeWorker>(videoDecodeGeneration_);
    logging::info("[session] [gen={}] startVideoDecodeWorker create worker", videoDecodeGeneration_);
    videoDecodeWorker_->setLogCallback([this](const QString &message, quint64 generation) {
        QMetaObject::invokeMethod(this,
                                  [this, message, generation]() {
                                      appendWorkerLog(message, generation);
                                  },
                                  Qt::QueuedConnection);
    });
    videoDecodeWorker_->setErrorCallback([this](const QString &message, quint64 generation) {
        QMetaObject::invokeMethod(this,
                                  [this, message, generation]() {
                                      handleWorkerDecodeError(message, generation);
                                  },
                                  Qt::QueuedConnection);
    });
    videoDecodeWorker_->setFrameReadyCallback([this](const LiveVideoDecodeWorker::FramePtr &frame,
                                                     qint64 ptsMs,
                                                     quint64 generation) {
        QMetaObject::invokeMethod(this,
                                  [this, frame, ptsMs, generation]() {
                                      handleWorkerDecodedVideoFrame(frame, ptsMs, generation);
                                  },
                                  Qt::QueuedConnection);
    });
    videoDecodeWorker_->start();

    appendInfoLog("Video decode worker started. The session thread now only demuxes and enqueues video tags.");
}

void LivePlayerSession::stopVideoDecodeWorker()
{
    if (!videoDecodeWorker_) {
        logging::debug("[session] [gen={}] stopVideoDecodeWorker skipped because worker is null", videoDecodeGeneration_);
        return;
    }

    logging::info("[session] [gen={}] stopVideoDecodeWorker begin", videoDecodeGeneration_);
    videoDecodeWorker_->stop();
    videoDecodeWorker_.reset();
    logging::info("[session] [gen={}] stopVideoDecodeWorker finished", videoDecodeGeneration_);
}

void LivePlayerSession::invalidateVideoDecodeGeneration(const char *reason)
{
    const quint64 previousGeneration = videoDecodeGeneration_;
    ++videoDecodeGeneration_;
    logging::info("[session] generation switch {} -> {} reason={}",
                  previousGeneration,
                  videoDecodeGeneration_,
                  reason ? reason : "unknown");
}

bool LivePlayerSession::enqueueAudioTag(const protocol::FlvTag &tag)
{
    if (!audioDecodeWorker_) {
        logging::warn("[audio_flow] [gen={}] enqueueAudioTag skipped because worker is null tag_ts={}ms",
                      videoDecodeGeneration_,
                      tag.timestampMs);
        return false;
    }

    logging::debug("[audio_flow] [gen={}] enqueueAudioTag tag_ts={}ms payload={} format={} packet_type={} seq={}",
                   videoDecodeGeneration_,
                   tag.timestampMs,
                   tag.payload.size(),
                   tag.audioSoundFormat,
                   tag.aacPacketType,
                   tag.isSequenceHeader);
    return audioDecodeWorker_->enqueueTag(tag);
}

bool LivePlayerSession::enqueueVideoTag(const protocol::FlvTag &tag)
{
    if (!videoDecodeWorker_) {
        appendErrorLog("Video decode worker is not available. The session cannot consume video tags asynchronously.");
        logging::error("[session] [gen={}] enqueueVideoTag failed because worker is null tag_ts={}ms",
                       videoDecodeGeneration_,
                       tag.timestampMs);
        return false;
    }

    logging::debug("[session] [gen={}] enqueueVideoTag tag_ts={}ms payload={} codec={} keyframe={}",
                   videoDecodeGeneration_,
                   tag.timestampMs,
                   tag.payload.size(),
                   tag.videoCodecId,
                   tag.isKeyframe);
    if (!videoDecodeWorker_->enqueueTag(tag)) {
        appendErrorLog("Video tag queue rejected a video tag. The decode worker is stopping or already stopped.");
        logging::warn("[session] [gen={}] enqueueVideoTag rejected by worker tag_ts={}ms",
                      videoDecodeGeneration_,
                      tag.timestampMs);
        return false;
    }

    return true;
}

void LivePlayerSession::disableAudioPipeline(const QString &reason)
{
    if (audioPipelineFailed_) {
        return;
    }

    audioPipelineFailed_ = true;
    appendErrorLog(reason);
    appendWarnLog("Audio pipeline disabled. The current session keeps the existing video path alive and falls back to the wall-clock video target.");
    logging::error("[audio_flow] [gen={}] audio pipeline disabled reason={}",
                   videoDecodeGeneration_,
                   reason.toUtf8().constData());
    resetAudioPlaybackChain();
}

void LivePlayerSession::appendInfoLog(const QString &message)
{
    const QByteArray utf8Message = message.toUtf8();
    spdlog::info("[live/session] {}", utf8Message.constData());
    emit logMessage(message);
}

void LivePlayerSession::appendWarnLog(const QString &message)
{
    const QByteArray utf8Message = message.toUtf8();
    spdlog::warn("[live/session] {}", utf8Message.constData());
    emit logMessage(message);
}

void LivePlayerSession::appendErrorLog(const QString &message)
{
    const QByteArray utf8Message = message.toUtf8();
    spdlog::error("[live/session] {}", utf8Message.constData());
    emit logMessage(message);
}

void LivePlayerSession::appendWorkerLog(const QString &message, quint64 generation)
{
    if (generation != videoDecodeGeneration_) {
        logging::debug("[session] [gen={}] drop stale worker log from gen={} message={}",
                       videoDecodeGeneration_,
                       generation,
                       message.toUtf8().constData());
        return;
    }

    logging::debug("[session] [gen={}] accept worker log: {}", generation, message.toUtf8().constData());
    appendInfoLog(message);
}

void LivePlayerSession::resetCounters()
{
    bytesReceived_ = 0;
    audioTagCount_ = 0;
    videoTagCount_ = 0;
    scriptTagCount_ = 0;
    firstPayloadObserved_ = false;
    firstAudioTagObserved_ = false;
    firstVideoTagObserved_ = false;
    firstScriptTagObserved_ = false;
    audioSequenceHeaderObserved_ = false;
    videoSequenceHeaderObserved_ = false;
    firstVideoFrameQueued_ = false;
    firstVideoFrameSubmittedToRender_ = false;
    playbackStarted_ = false;
    unsupportedAudioFormatLogged_ = false;
    audioPipelineFailed_ = false;
    audioClockMasterActiveLogged_ = false;
    audioDecodeBackpressureActive_ = false;
    videoDecodeBackpressureActive_ = false;
    ingressBackpressureActive_ = false;
    firstObservedVideoPtsMs_ = -1;
    firstQueuedVideoPtsMs_ = -1;
    consecutiveRenderWaitCount_ = 0;
    consecutiveQueueOverflowCount_ = 0;
    lastPlaybackSnapshotLogWallClockMs_ = 0;
    lastClockPendingLogWallClockMs_ = 0;
    lastPlaybackReanchorWallClockMs_ = 0;
    lastAvDriftLogWallClockMs_ = 0;
    lastIngressBackpressureLogWallClockMs_ = 0;
    pendingAudioPcmDurationMs_.store(0);
    pendingAudioPcmCallbackCount_.store(0);
    emit statsChanged(bytesReceived_, audioTagCount_, videoTagCount_, scriptTagCount_);
}

void LivePlayerSession::setState(SessionState state, const QString &message)
{
    state_ = state;
    const QByteArray utf8Message = message.toUtf8();

    if (state_ == SessionState::Error) {
        spdlog::error("[live/session] state={} message={}", sessionStateName(state_), utf8Message.constData());
    } else {
        spdlog::info("[live/session] state={} message={}", sessionStateName(state_), utf8Message.constData());
    }
    logging::info("[session] [gen={}] state={} message={}",
                  videoDecodeGeneration_,
                  sessionStateName(state_),
                  message.toUtf8().constData());

    emit stateChanged(state_, message);
}

bool LivePlayerSession::drainParsedTags()
{
    protocol::FlvTag tag;
    int drainedAudioTags = 0;
    int drainedVideoTags = 0;
    int drainedOtherTags = 0;
    bool deferredVideoDrain = false;
    bool audioHungryObserved = false;
    qint64 lastTotalAudioMs = 0;
    qint64 lastAudioDeviceQueueMs = 0;
    qint64 lastPendingAudioPcmMs = 0;
    qint64 lastVideoBufferedMs = 0;
    int lastAudioTagDepth = 0;
    int lastVideoTagDepth = 0;

    while (demuxer_.parsedTagCount() > 0) {
        const auto audioBacklog = captureAudioBacklogSnapshot(audioOutput_.get(),
                                                              pendingAudioPcmDurationMs_.load(),
                                                              pendingAudioPcmCallbackCount_.load());
        const bool audioPreferDrain =
            audioBacklog.audioHungry || audioBacklog.totalAudioMs < kAudioDrainPreferMs;
        const bool audioQuotaNeeded =
            audioPreferDrain &&
            (drainedAudioTags < kAudioDrainMinQuota ||
             audioBacklog.clock.queuedDurationMs < kAudioDrainTargetDeviceQueueMs);
        const qint64 videoBufferedMs = videoFrameQueue_.bufferedDurationMs();

        lastTotalAudioMs = audioBacklog.totalAudioMs;
        lastAudioDeviceQueueMs = audioBacklog.clock.queuedDurationMs;
        lastPendingAudioPcmMs = audioBacklog.pendingPcmMs;
        lastVideoBufferedMs = videoBufferedMs;
        lastAudioTagDepth = audioDecodeWorker_ ? audioDecodeWorker_->queuedTagCount() : 0;
        lastVideoTagDepth = videoDecodeWorker_ ? videoDecodeWorker_->queuedTagCount() : 0;
        audioHungryObserved = audioHungryObserved || audioBacklog.audioHungry;

        bool tookPrioritizedAudioTag = false;
        if (audioQuotaNeeded) {
            tookPrioritizedAudioTag = demuxer_.takeNextTagOfType(protocol::FlvTagType::Audio, tag);
        }
        if (!tookPrioritizedAudioTag) {
            const bool shouldDeferVideoDrain =
                demuxer_.peekNextTagType() == protocol::FlvTagType::Video &&
                ingressBackpressureActive_ &&
                videoBufferedMs >= kVideoDrainDeferBufferedMs &&
                !audioBacklog.audioHungry;
            if (shouldDeferVideoDrain) {
                deferredVideoDrain = true;
                break;
            }

            if (!demuxer_.takeNextTag(tag)) {
                break;
            }
        }

        switch (tag.type) {
        case protocol::FlvTagType::Script:
            ++scriptTagCount_;
            ++drainedOtherTags;
            if (!firstScriptTagObserved_) {
                firstScriptTagObserved_ = true;
                appendInfoLog(
                    QString("First script tag arrived at %1 ms, payload=%2 bytes. This is usually metadata.")
                        .arg(tag.timestampMs)
                        .arg(tag.payload.size()));
                appendInfoLog("TODO(user): if you want to study onMetaData, parse this script payload before touching seek or sync.");
            }
            break;

        case protocol::FlvTagType::Audio:
            ++audioTagCount_;
            ++drainedAudioTags;

            if (!firstAudioTagObserved_) {
                firstAudioTagObserved_ = true;
                logging::info("[audio_flow] [gen={}] first audio tag observed ts={}ms payload={} format={}",
                              videoDecodeGeneration_,
                              tag.timestampMs,
                              tag.payload.size(),
                              tag.audioSoundFormat);
                appendInfoLog(
                    QString("first audio tag observed at %1 ms, payload=%2 bytes, format=%3.")
                        .arg(tag.timestampMs)
                        .arg(tag.payload.size())
                        .arg(audioFormatName(tag.audioSoundFormat)));
            }

            if (tag.audioSoundFormat != 10) {
                if (!unsupportedAudioFormatLogged_) {
                    unsupportedAudioFormatLogged_ = true;
                    appendWarnLog(
                        QString("Audio format %1 is not wired in the live audio path yet. This milestone only forwards AAC tags into the decoder.")
                            .arg(audioFormatName(tag.audioSoundFormat)));
                }
                break;
            }

            if (tag.isSequenceHeader && !audioSequenceHeaderObserved_) {
                audioSequenceHeaderObserved_ = true;
                logging::info("[audio_flow] [gen={}] AAC sequence header received ts={}ms payload={}",
                              videoDecodeGeneration_,
                              tag.timestampMs,
                              tag.payload.size());
                appendInfoLog(
                    QString("AAC sequence header received at %1 ms. AudioSpecificConfig will be parsed and passed into the AAC decoder.")
                        .arg(tag.timestampMs));
            }

            if (!audioPipelineFailed_ && !enqueueAudioTag(tag)) {
                disableAudioPipeline(
                    QString("Audio tag queue rejected a live audio packet at %1 ms.").arg(tag.timestampMs));
            }
            updateDecodeBackpressure();
            break;

        case protocol::FlvTagType::Video: {
            ++videoTagCount_;
            ++drainedVideoTags;

            if (!firstVideoTagObserved_) {
                firstVideoTagObserved_ = true;
                appendInfoLog(
                    QString("First video tag arrived at %1 ms, payload=%2 bytes, codec=%3, keyframe=%4.")
                        .arg(tag.timestampMs)
                        .arg(tag.payload.size())
                        .arg(videoCodecName(tag.videoCodecId))
                        .arg(tag.isKeyframe ? "yes" : "no"));
            }

            if (tag.isSequenceHeader && !videoSequenceHeaderObserved_) {
                videoSequenceHeaderObserved_ = true;
                appendInfoLog(
                    QString("AVC sequence header observed at %1 ms. FFmpeg decoder setup now starts from this payload.")
                        .arg(tag.timestampMs));
            }

            // Stage 1 threading only adds an async boundary here:
            // the session thread demuxes tags, and the decode worker thread owns FFmpeg video decode.
            if (!enqueueVideoTag(tag)) {
                setState(SessionState::Error,
                         QString("Failed to enqueue video tag after receiving %1 video tags.").arg(videoTagCount_));
                appendErrorLog("Video decode worker rejected a video tag.");
                logging::error("[session] [gen={}] failed to enqueue video tag_count={} tag_ts={}ms",
                               videoDecodeGeneration_,
                               videoTagCount_,
                               tag.timestampMs);
                return false;
            }
            updateDecodeBackpressure();
            break;
        }

        case protocol::FlvTagType::Unknown:
            ++drainedOtherTags;
            appendWarnLog(
                QString("Unknown FLV tag type 0x%1 observed at %2 ms, payload=%3 bytes.")
                    .arg(QString::number(tag.rawTagType, 16))
                    .arg(tag.timestampMs)
                    .arg(tag.payload.size()));
            break;
        }
    }

    const bool shouldLogDrainRound =
        deferredVideoDrain ||
        ingressBackpressureActive_ ||
        audioHungryObserved ||
        lastTotalAudioMs < kAudioDrainPreferMs;
    if (shouldLogDrainRound &&
        (drainedAudioTags > 0 || drainedVideoTags > 0 || drainedOtherTags > 0 || deferredVideoDrain)) {
        logging::debug("[drain_parsed_tags] [gen={}] drained_audio_tags={} drained_video_tags={} drained_other_tags={} deferred_video={} parsed_tags_remaining={} demux_buffer_bytes={} total_audio_ms={} audio_device_queue_ms={} pending_pcm_ms={} audio_hungry={} ingress_backpressure={} audio_tag_depth={} video_tag_depth={} video_buffered_ms={}",
                       videoDecodeGeneration_,
                       drainedAudioTags,
                       drainedVideoTags,
                       drainedOtherTags,
                       deferredVideoDrain,
                       demuxer_.parsedTagCount(),
                       demuxer_.bufferedByteCount(),
                       lastTotalAudioMs,
                       lastAudioDeviceQueueMs,
                       lastPendingAudioPcmMs,
                       audioHungryObserved,
                       ingressBackpressureActive_,
                       lastAudioTagDepth,
                       lastVideoTagDepth,
                       lastVideoBufferedMs);
    }

    return true;
}

void LivePlayerSession::handleWorkerAudioDecodeError(const QString &message, quint64 generation)
{
    if (generation != videoDecodeGeneration_) {
        logging::debug("[audio_flow] [gen={}] drop stale audio worker error from gen={} message={}",
                       videoDecodeGeneration_,
                       generation,
                       message.toUtf8().constData());
        return;
    }

    disableAudioPipeline(QString("Audio decode worker stopped: %1").arg(message));
}

void LivePlayerSession::handleWorkerDecodedAudioPcm(
    const QByteArray &pcm,
    qint64 ptsMs,
    int sampleRate,
    int channels,
    int sampleCount,
    quint64 generation,
    qint64 pcmDurationMs)
{
    if (pcmDurationMs > 0) {
        qint64 pendingPcmMs = pendingAudioPcmDurationMs_.load();
        while (!pendingAudioPcmDurationMs_.compare_exchange_weak(
            pendingPcmMs,
            std::max<qint64>(0, pendingPcmMs - pcmDurationMs))) {
        }
    }
    int pendingPcmCallbacks = pendingAudioPcmCallbackCount_.load();
    while (!pendingAudioPcmCallbackCount_.compare_exchange_weak(
        pendingPcmCallbacks,
        std::max(0, pendingPcmCallbacks - 1))) {
    }

    if (generation != videoDecodeGeneration_ || audioPipelineFailed_) {
        logging::debug("[audio_flow] [gen={}] drop stale PCM chunk from gen={} pts={}ms bytes={}",
                       videoDecodeGeneration_,
                       generation,
                       ptsMs,
                       pcm.size());
        return;
    }

    if (!audioOutput_) {
        disableAudioPipeline("Audio output is not available when a PCM chunk arrives.");
        return;
    }

    logging::debug("[audio_flow] [gen={}] PCM ready pts={}ms pcm_bytes={} sample_rate={} channels={} samples={}",
                   generation,
                   ptsMs,
                   pcm.size(),
                   sampleRate,
                   channels,
                   sampleCount);
    if (!audioOutput_->enqueuePcm(pcm, ptsMs, sampleRate, channels, sampleCount)) {
        disableAudioPipeline(
            QString("Failed to enqueue decoded PCM into SDL audio output at %1 ms.").arg(ptsMs));
        return;
    }
    updateDecodeBackpressure();
}

void LivePlayerSession::handleWorkerDecodeError(const QString &message, quint64 generation)
{
    if (generation != videoDecodeGeneration_) {
        logging::debug("[session] [gen={}] drop stale worker error from gen={} message={}",
                       videoDecodeGeneration_,
                       generation,
                       message.toUtf8().constData());
        return;
    }

    setState(SessionState::Error, message);
    appendErrorLog("Video decode worker reported a fatal error.");
    logging::error("[session] [gen={}] worker fatal error accepted message={}",
                   generation,
                   message.toUtf8().constData());

    if (reader_ && reader_->isActive()) {
        reader_->close();
    }

    clearQueuedVideoFramesAndClock();
    invalidateVideoDecodeGeneration("worker fatal error");
    resetAudioPlaybackChain();
    stopVideoDecodeWorker();
}

void LivePlayerSession::handleWorkerDecodedVideoFrame(
    const std::shared_ptr<AVFrame> &frame,
    qint64 ptsMs,
    quint64 generation)
{
    if (generation != videoDecodeGeneration_ || !frame) {
        if (frame) {
            logging::debug("[session] [gen={}] drop stale worker frame from gen={} pts={}ms",
                           videoDecodeGeneration_,
                           generation,
                           ptsMs);
        }
        return;
    }

    logging::debug("[session] [gen={}] receive worker frame pts={}ms width={} height={}",
                   generation,
                   ptsMs,
                   frame->width,
                   frame->height);
    if (ptsMs < 0) {
        logging::debug("[frame_queue] [gen={}] drop invalid pts frame pts={} elapsed={}ms",
                       generation,
                       ptsMs,
                       sessionElapsedMs());
        return;
    }

    if (!firstVideoFrameQueued_) {
        firstVideoFrameQueued_ = true;
        firstObservedVideoPtsMs_ = ptsMs;
        appendInfoLog(
            QString("First decoded video frame seen at pts=%1 ms. Enter startup buffering before anchoring the playback clock.")
                .arg(ptsMs));
        logging::info("[playback] [gen={}] startup_first_frame_seen pts={} elapsed={}ms",
                      generation,
                      ptsMs,
                      sessionElapsedMs());
    }

    const qint64 enqueueWallClockMs = steadyNowMs();
    videoFrameQueue_.enqueue(QueuedVideoFrame{
        frame,
        ptsMs,
        enqueueWallClockMs,
        generation,
    });

    const qint64 referenceTargetPtsMs = playbackClockStarted()
        ? currentTargetVideoPtsMs(currentAudioClockPtsMs())
        : videoFrameQueue_.front().ptsMs;
    trimQueuedVideoFramesForPlaybackWindow(referenceTargetPtsMs, "enqueue");

    logging::debug("[frame_queue] [gen={}] enqueue pts={} depth={} frontPts={} backPts={} bufferedMs={} playbackStarted={} elapsed={}ms",
                   generation,
                   ptsMs,
                   videoFrameQueue_.size(),
                   videoFrameQueue_.front().ptsMs,
                   videoFrameQueue_.back().ptsMs,
                   videoFrameQueue_.bufferedDurationMs(),
                   playbackStarted_,
                   sessionElapsedMs());
    logPlaybackSnapshot("frame_queue_enqueue", referenceTargetPtsMs, false);
    maybeStartBufferedPlayback();
    updateDecodeBackpressure();
}

void LivePlayerSession::handleDecodedVideoFrame(const AVFrame *frame)
{
    if (!frame || !videoSurface_) {
        return;
    }

    if (frame->width <= 0 || frame->height <= 0 || frame->format == AV_PIX_FMT_NONE) {
        return;
    }

    const AVPixelFormat sourceFormat = static_cast<AVPixelFormat>(frame->format);
    const int frameWidth = frame->width;
    const int frameHeight = frame->height;
    const int chromaWidth = (frameWidth + 1) / 2;
    const int chromaHeight = (frameHeight + 1) / 2;
    const VideoColorMatrix colorMatrix = resolveColorMatrix(frame);
    const bool fullRange = isFullRangeSource(sourceFormat, frame->color_range);

    QByteArray planeY;
    QByteArray planeU;
    QByteArray planeV;

    if (sourceFormat == AV_PIX_FMT_YUV420P &&
        frame->data[0] &&
        frame->data[1] &&
        frame->data[2]) {
        planeY = copyPlane(frame->data[0], frame->linesize[0], frameWidth, frameHeight);
        planeU = copyPlane(frame->data[1], frame->linesize[1], chromaWidth, chromaHeight);
        planeV = copyPlane(frame->data[2], frame->linesize[2], chromaWidth, chromaHeight);
    } else {
        videoScaleContext_ = sws_getCachedContext(videoScaleContext_,
                                                  frameWidth,
                                                  frameHeight,
                                                  sourceFormat,
                                                  frameWidth,
                                                  frameHeight,
                                                  AV_PIX_FMT_YUV420P,
                                                  SWS_BILINEAR,
                                                  nullptr,
                                                  nullptr,
                                                  nullptr);
        if (!videoScaleContext_) {
            appendErrorLog("Failed to allocate swscale context for live video frame.");
            return;
        }

        planeY = QByteArray(frameWidth * frameHeight, Qt::Uninitialized);
        planeU = QByteArray(chromaWidth * chromaHeight, Qt::Uninitialized);
        planeV = QByteArray(chromaWidth * chromaHeight, Qt::Uninitialized);
        if (planeY.isEmpty() || planeU.isEmpty() || planeV.isEmpty()) {
            return;
        }

        uint8_t *destinationData[4] = {
            reinterpret_cast<uint8_t *>(planeY.data()),
            reinterpret_cast<uint8_t *>(planeU.data()),
            reinterpret_cast<uint8_t *>(planeV.data()),
            nullptr,
        };
        int destinationLinesize[4] = {
            frameWidth,
            chromaWidth,
            chromaWidth,
            0,
        };

        const int scaledHeight = sws_scale(videoScaleContext_,
                                           frame->data,
                                           frame->linesize,
                                           0,
                                           frameHeight,
                                           destinationData,
                                           destinationLinesize);
        if (scaledHeight <= 0) {
            appendErrorLog("sws_scale failed for a live video frame.");
            return;
        }
    }

    if (planeY.isEmpty() || planeU.isEmpty() || planeV.isEmpty()) {
        return;
    }

    // Use Qt::AutoConnection here so the common same-thread path stays direct,
    // while a future cross-thread surface handoff is still protected.
    QMetaObject::invokeMethod(videoSurface_.data(),
                              "presentFrame",
                              Qt::AutoConnection,
                              Q_ARG(int, frameWidth),
                              Q_ARG(int, frameHeight),
                              Q_ARG(QByteArray, planeY),
                              Q_ARG(QByteArray, planeU),
                              Q_ARG(QByteArray, planeV),
                              Q_ARG(int, static_cast<int>(colorMatrix)),
                              Q_ARG(bool, fullRange));
}

void LivePlayerSession::resetVideoConverter()
{
    if (videoScaleContext_) {
        sws_freeContext(videoScaleContext_);
        videoScaleContext_ = nullptr;
    }
}

}  // namespace backend::liveplayer::session
