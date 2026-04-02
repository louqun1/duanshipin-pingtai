#include "liveplayer/decode/FlvVideoDecoder.hpp"

#include <cstring>

extern "C" {
#include <libavutil/error.h>
}

namespace backend::liveplayer::decode {

namespace {

QString ffmpegErrorString(int errorCode)
{
    char errorBuffer[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(errorCode, errorBuffer, sizeof(errorBuffer));
    return QString::fromUtf8(errorBuffer);
}

int readSignedInt24BE(const char *data)
{
    const auto *bytes = reinterpret_cast<const unsigned char *>(data);
    int value = (static_cast<int>(bytes[0]) << 16)
        | (static_cast<int>(bytes[1]) << 8)
        | static_cast<int>(bytes[2]);

    if ((value & 0x00800000) != 0) {
        value |= ~0x00ffffff;
    }

    return value;
}

}  // namespace

FlvVideoDecoder::FlvVideoDecoder() = default;

FlvVideoDecoder::~FlvVideoDecoder()
{
    reset();
}

void FlvVideoDecoder::reset()
{
    if (decodedFrame_) {
        av_frame_free(&decodedFrame_);
    }

    if (videoCodecContext_) {
        avcodec_free_context(&videoCodecContext_);
    }

    decoderConfigRecord_.clear();
    lastError_.clear();
}

void FlvVideoDecoder::setLogCallback(LogCallback callback)
{
    logCallback_ = std::move(callback);
}

void FlvVideoDecoder::setFrameCallback(FrameCallback callback)
{
    frameCallback_ = std::move(callback);
}

bool FlvVideoDecoder::pushTag(const protocol::FlvTag &tag, VideoDecodeReport &report)
{
    report = {};

    if (tag.type != protocol::FlvTagType::Video) {
        return true;
    }

    if (tag.videoCodecId != 7) {
        emitLog(QString("Video tag codec=%1 is not wired yet. Current milestone only decodes AVC/H.264 video tags with FFmpeg.")
                    .arg(tag.videoCodecId));
        return true;
    }

    if (tag.payload.size() < 5) {
        setError(QString("AVC video tag is too small to contain FLV video metadata. payload=%1").arg(tag.payload.size()));
        return false;
    }

    if (tag.avcPacketType == 0) {
        return handleAvcSequenceHeader(tag, report);
    }

    if (tag.avcPacketType == 1) {
        return handleAvcNaluTag(tag, report);
    }

    if (tag.avcPacketType == 2) {
        emitLog("AVC end-of-sequence tag observed.");
        return true;
    }

    emitLog(QString("Unknown AVC packet type %1 observed.").arg(tag.avcPacketType));
    return true;
}

bool FlvVideoDecoder::isConfigured() const
{
    return videoCodecContext_ != nullptr;
}

QString FlvVideoDecoder::lastError() const
{
    return lastError_;
}

bool FlvVideoDecoder::handleAvcSequenceHeader(const protocol::FlvTag &tag, VideoDecodeReport &report)
{
    const QByteArray decoderConfigRecord = tag.payload.mid(5);
    if (decoderConfigRecord.isEmpty()) {
        setError("AVC sequence header payload is empty.");
        return false;
    }

    if (!openH264Decoder(decoderConfigRecord)) {
        return false;
    }

    decoderConfigRecord_ = decoderConfigRecord;
    report.decoderConfigured = true;

    emitLog(QString("AVC sequence header arrived at %1 ms. FFmpeg H.264 decoder is now opened from the FLV config record.")
                .arg(tag.timestampMs));
    emitLog("TODO(user): inspect the AVCDecoderConfigurationRecord fields here by hand, especially SPS/PPS and NALU length size.");
    return true;
}

bool FlvVideoDecoder::handleAvcNaluTag(const protocol::FlvTag &tag, VideoDecodeReport &report)
{
    if (!videoCodecContext_) {
        emitLog("Video NALU tag arrived before AVC sequence header. Skip until decoder config is ready.");
        return true;
    }

    const QByteArray naluPayload = tag.payload.mid(5);
    if (naluPayload.isEmpty()) {
        return true;
    }

    const qint64 dtsMs = static_cast<qint64>(tag.timestampMs);
    const int compositionTimeMs = readSignedInt24BE(tag.payload.constData() + 2);
    const qint64 ptsMs = dtsMs + compositionTimeMs;

    // TODO(user): stage 1 already moved video decode behind a tag queue + worker thread.
    // If you want to study live jitter and reordering next, add a real packet/frame queue here.
    return submitVideoPacket(reinterpret_cast<const uint8_t *>(naluPayload.constData()),
                             naluPayload.size(),
                             dtsMs,
                             ptsMs,
                             tag.isKeyframe,
                             report);
}

bool FlvVideoDecoder::openH264Decoder(const QByteArray &decoderConfigRecord)
{
    reset();

    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) {
        setError("FFmpeg H.264 decoder was not found.");
        return false;
    }

    videoCodecContext_ = avcodec_alloc_context3(codec);
    if (!videoCodecContext_) {
        setError("Failed to allocate AVCodecContext for H.264.");
        return false;
    }

    videoCodecContext_->pkt_timebase = AVRational{1, 1000};
    videoCodecContext_->flags2 |= AV_CODEC_FLAG2_CHUNKS;

    videoCodecContext_->extradata = static_cast<uint8_t *>(
        av_mallocz(static_cast<size_t>(decoderConfigRecord.size()) + AV_INPUT_BUFFER_PADDING_SIZE));
    if (!videoCodecContext_->extradata) {
        setError("Failed to allocate decoder extradata for AVC sequence header.");
        return false;
    }

    std::memcpy(videoCodecContext_->extradata,
                decoderConfigRecord.constData(),
                static_cast<size_t>(decoderConfigRecord.size()));
    videoCodecContext_->extradata_size = decoderConfigRecord.size();

    const int openResult = avcodec_open2(videoCodecContext_, codec, nullptr);
    if (openResult < 0) {
        setError(QString("avcodec_open2(H.264) failed: %1").arg(ffmpegErrorString(openResult)));
        return false;
    }

    decodedFrame_ = av_frame_alloc();
    if (!decodedFrame_) {
        setError("Failed to allocate AVFrame for decoded video.");
        return false;
    }

    return true;
}

bool FlvVideoDecoder::submitVideoPacket(
    const uint8_t *data,
    int size,
    qint64 dtsMs,
    qint64 ptsMs,
    bool keyframe,
    VideoDecodeReport &report)
{
    if (!videoCodecContext_ || !decodedFrame_ || !data || size <= 0) {
        return true;
    }

    AVPacket *packet = av_packet_alloc();
    if (!packet) {
        setError("Failed to allocate AVPacket for video decode.");
        return false;
    }

    const int packetAllocResult = av_new_packet(packet, size);
    if (packetAllocResult < 0) {
        av_packet_free(&packet);
        setError(QString("av_new_packet failed: %1").arg(ffmpegErrorString(packetAllocResult)));
        return false;
    }

    std::memcpy(packet->data, data, static_cast<size_t>(size));
    packet->dts = dtsMs;
    packet->pts = ptsMs;
    packet->flags = keyframe ? AV_PKT_FLAG_KEY : 0;

    const int sendResult = avcodec_send_packet(videoCodecContext_, packet);
    av_packet_free(&packet);
    if (sendResult < 0) {
        setError(QString("avcodec_send_packet(H.264) failed: %1").arg(ffmpegErrorString(sendResult)));
        return false;
    }

    while (true) {
        const int receiveResult = avcodec_receive_frame(videoCodecContext_, decodedFrame_);
        if (receiveResult == AVERROR(EAGAIN) || receiveResult == AVERROR_EOF) {
            break;
        }

        if (receiveResult < 0) {
            setError(QString("avcodec_receive_frame(H.264) failed: %1").arg(ffmpegErrorString(receiveResult)));
            return false;
        }

        report.frameDecoded = true;
        ++report.decodedFrameCount;

        const qint64 framePtsMs = (decodedFrame_->best_effort_timestamp == AV_NOPTS_VALUE)
            ? ptsMs
            : decodedFrame_->best_effort_timestamp;

        if (frameCallback_) {
            frameCallback_(decodedFrame_, framePtsMs);
        }

        av_frame_unref(decodedFrame_);
    }

    return true;
}

void FlvVideoDecoder::emitLog(const QString &message) const
{
    if (logCallback_) {
        logCallback_(message);
    }
}

void FlvVideoDecoder::setError(const QString &message)
{
    lastError_ = message;
}

}  // namespace backend::liveplayer::decode
