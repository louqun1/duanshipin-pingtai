#pragma once

#include "liveplayer/protocol/FlvTypes.hpp"

#include <QByteArray>
#include <QString>
#include <QtGlobal>

#include <functional>

struct AVCodecContext;
struct AVFrame;
struct SwrContext;
struct AVChannelLayout;

namespace backend::liveplayer::decode {

struct AudioDecodeReport {
    bool decoderConfigured = false;
    bool frameDecoded = false;
    int decodedFrameCount = 0;
    int decodedPcmChunkCount = 0;
};

class FlvAudioDecoder final
{
public:
    using LogCallback = std::function<void(const QString &)>;
    using PcmCallback = std::function<void(QByteArray pcm, qint64 ptsMs, int sampleRate, int channels, int sampleCount)>;

    FlvAudioDecoder();
    ~FlvAudioDecoder();

    void reset();
    void setLogCallback(LogCallback callback);
    void setPcmCallback(PcmCallback callback);

    bool pushTag(const protocol::FlvTag &tag, AudioDecodeReport &report);
    bool isConfigured() const;
    QString lastError() const;

private:
    bool handleAacSequenceHeader(const protocol::FlvTag &tag, AudioDecodeReport &report);
    bool handleAacRawTag(const protocol::FlvTag &tag, AudioDecodeReport &report);
    bool openAacDecoder(const QByteArray &audioSpecificConfig);
    bool submitAudioPacket(const uint8_t *data, int size, qint64 ptsMs, AudioDecodeReport &report);
    bool resolveInputChannelLayout(const AVFrame *frame, AVChannelLayout &layout) const;
    bool ensureResampler(const AVFrame *frame, const AVChannelLayout &inputLayout);
    bool convertDecodedFrameToPcm(
        const AVFrame *frame,
        QByteArray &pcm,
        int &sampleRate,
        int &channels,
        int &sampleCount);
    void emitLog(const QString &message) const;
    void setError(const QString &message);

    AVCodecContext *audioCodecContext_ = nullptr;
    AVFrame *decodedFrame_ = nullptr;
    SwrContext *resampleContext_ = nullptr;
    AVChannelLayout *resampleInputLayout_ = nullptr;
    AVChannelLayout *resampleOutputLayout_ = nullptr;
    int resampleInputSampleRate_ = 0;
    int resampleOutputSampleRate_ = 0;
    int resampleInputFormat_ = -1;
    int resampleOutputFormat_ = -1;
    QByteArray audioSpecificConfig_;
    QString lastError_;
    LogCallback logCallback_;
    PcmCallback pcmCallback_;
};

}  // namespace backend::liveplayer::decode
