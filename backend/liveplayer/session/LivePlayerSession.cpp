#include "liveplayer/session/LivePlayerSession.hpp"

#include "liveplayer/protocol/FlvTypes.hpp"
#include "liveplayer/protocol/HttpFlvStreamReader.hpp"

#include <QByteArray>
#include <QMetaObject>
#include <QUrl>
#include <QWidget>
#include <spdlog/spdlog.h>

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

}  // namespace

LivePlayerSession::LivePlayerSession(QObject *parent)
    : QObject(parent)
    , reader_(new protocol::HttpFlvStreamReader(this))
{
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

    videoDecoder_.setLogCallback([this](const QString &message) {
        appendInfoLog(message);
    });
    videoDecoder_.setFrameCallback([this](const AVFrame *frame, qint64 ptsMs) {
        Q_UNUSED(ptsMs);
        handleDecodedVideoFrame(frame);
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

    resetPlaybackResources(false);
    resetCounters();
    currentUrl_ = trimmedUrl;

    if (videoSurface_) {
        QMetaObject::invokeMethod(videoSurface_.data(), "clearFrame", Qt::QueuedConnection);
    }

    setState(SessionState::Connecting, QString("Opening %1").arg(currentUrl_));
    appendInfoLog("Data flow 1/3: QNetworkReply::readyRead -> QByteArray chunk.");
    appendInfoLog("Data flow 2/3: FlvDemuxer::pushBytes -> FLV tag header + payload.");
    appendInfoLog("Data flow 3/3: AVC video tag -> FFmpeg avcodec_send_packet/receive_frame -> VideoOpenGLWidget.");
    appendInfoLog("Current milestone only wires manual H.264 video decode/render. AAC, audio output, queues, and A/V sync stay as TODO(user).");
    reader_->open(QUrl(currentUrl_));
}

void LivePlayerSession::stop()
{
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
        if (reader_->isActive()) {
            reader_->close();
        }
        return;
    }

    if (!firstPayloadObserved_) {
        firstPayloadObserved_ = true;
        appendInfoLog(QString("Received first payload chunk: %1 bytes.").arg(chunk.size()));
    }

    if (report.headerValidated) {
        appendInfoLog("FLV header validated. The session is now consuming tag headers and payloads incrementally.");
    }

    if (report.parsedTagCount > 0 && !drainParsedTags()) {
        if (reader_->isActive()) {
            reader_->close();
        }
        return;
    }

    emit statsChanged(bytesReceived_, audioTagCount_, videoTagCount_, scriptTagCount_);
}

void LivePlayerSession::handleReaderError(const QString &message)
{
    setState(SessionState::Error, QString("HTTP-FLV read failed: %1").arg(message));
    appendErrorLog(QString("Network layer reported an error: %1").arg(message));
}

void LivePlayerSession::handleReaderFinished()
{
    if (state_ == SessionState::Error) {
        return;
    }

    setState(SessionState::Stopped, "Live stream finished.");
    appendInfoLog("HTTP-FLV reader finished.");
}

void LivePlayerSession::resetPlaybackResources(bool clearState)
{
    if (reader_ && reader_->isActive()) {
        reader_->close();
    }

    demuxer_.reset();
    videoDecoder_.reset();
    resetVideoConverter();

    if (videoSurface_) {
        QMetaObject::invokeMethod(videoSurface_.data(), "clearFrame", Qt::QueuedConnection);
    }

    if (clearState) {
        currentUrl_.clear();
        state_ = SessionState::Idle;
    }
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
    firstVideoFrameDecoded_ = false;
    audioDecodeTodoLogged_ = false;
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

    emit stateChanged(state_, message);
}

bool LivePlayerSession::drainParsedTags()
{
    protocol::FlvTag tag;
    while (demuxer_.takeNextTag(tag)) {
        switch (tag.type) {
        case protocol::FlvTagType::Script:
            ++scriptTagCount_;
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

            if (!firstAudioTagObserved_) {
                firstAudioTagObserved_ = true;
                appendInfoLog(
                    QString("First audio tag arrived at %1 ms, payload=%2 bytes, format=%3.")
                        .arg(tag.timestampMs)
                        .arg(tag.payload.size())
                        .arg(audioFormatName(tag.audioSoundFormat)));
            }

            if (tag.isSequenceHeader && !audioSequenceHeaderObserved_) {
                audioSequenceHeaderObserved_ = true;
                appendInfoLog(
                    QString("AAC sequence header observed at %1 ms. This is where your AAC decoder config should be initialized.")
                        .arg(tag.timestampMs));
                appendInfoLog("TODO(user): create a sibling AAC decoder that feeds payload[2..] into FFmpeg instead of delegating audio to an existing black-box player.");
            } else if (!audioDecodeTodoLogged_) {
                audioDecodeTodoLogged_ = true;
                appendInfoLog("TODO(user): audio tags are flowing. Next milestone is AAC decode, audio output, clocking, and A/V sync.");
            }
            break;

        case protocol::FlvTagType::Video: {
            ++videoTagCount_;

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

            decode::VideoDecodeReport report;
            if (!videoDecoder_.pushTag(tag, report)) {
                const QString errorMessage = videoDecoder_.lastError().isEmpty()
                    ? QString("FFmpeg video decode failed after receiving %1 video tags.").arg(videoTagCount_)
                    : videoDecoder_.lastError();
                setState(SessionState::Error, errorMessage);
                appendErrorLog("Video decoder bridge reported a fatal error.");
                return false;
            }

            if (report.frameDecoded && !firstVideoFrameDecoded_) {
                firstVideoFrameDecoded_ = true;
                setState(SessionState::Playing,
                         "First H.264 frame decoded through your chunk/tag + FFmpeg path. TODO(user): add AAC decode, audio output, and A/V sync.");
                appendInfoLog("Manual live video path reached the first decoded frame.");
            }
            break;
        }

        case protocol::FlvTagType::Unknown:
            appendWarnLog(
                QString("Unknown FLV tag type 0x%1 observed at %2 ms, payload=%3 bytes.")
                    .arg(QString::number(tag.rawTagType, 16))
                    .arg(tag.timestampMs)
                    .arg(tag.payload.size()));
            break;
        }
    }

    return true;
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

    QMetaObject::invokeMethod(videoSurface_.data(),
                              "presentFrame",
                              Qt::QueuedConnection,
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
