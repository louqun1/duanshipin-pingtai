#pragma once

#include "liveplayer/protocol/FlvTypes.hpp"

#include <QByteArray>
#include <QString>
#include <QtGlobal>

#include <functional>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
}

namespace backend::liveplayer::decode {

struct VideoDecodeReport {
    bool decoderConfigured = false;
    bool frameDecoded = false;
    int decodedFrameCount = 0;
};

class FlvVideoDecoder final
{
public:
    using LogCallback = std::function<void(const QString &)>;
    using FrameCallback = std::function<void(const AVFrame *frame, qint64 ptsMs)>;

    FlvVideoDecoder();
    ~FlvVideoDecoder();

    void reset();
    void setLogCallback(LogCallback callback);
    void setFrameCallback(FrameCallback callback);

    bool pushTag(const protocol::FlvTag &tag, VideoDecodeReport &report);
    bool isConfigured() const;
    QString lastError() const;

private:
    bool handleAvcSequenceHeader(const protocol::FlvTag &tag, VideoDecodeReport &report);
    bool handleAvcNaluTag(const protocol::FlvTag &tag, VideoDecodeReport &report);
    bool openH264Decoder(const QByteArray &decoderConfigRecord);
    bool submitVideoPacket(
        const uint8_t *data,
        int size,
        qint64 dtsMs,
        qint64 ptsMs,
        bool keyframe,
        VideoDecodeReport &report);
    void emitLog(const QString &message) const;
    void setError(const QString &message);

    AVCodecContext *videoCodecContext_ = nullptr;
    AVFrame *decodedFrame_ = nullptr;
    QByteArray decoderConfigRecord_;
    QString lastError_;
    LogCallback logCallback_;
    FrameCallback frameCallback_;
};

}  // namespace backend::liveplayer::decode
