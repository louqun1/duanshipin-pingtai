#include "liveplayer/decode/FlvAudioDecoder.hpp"

#include <QByteArray>

#include <cstring>
#include <memory>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

namespace backend::liveplayer::decode {

namespace {

QString ffmpegErrorString(int errorCode)
{
    char errorBuffer[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(errorCode, errorBuffer, sizeof(errorBuffer));
    return QString::fromUtf8(errorBuffer);
}

class BitReader final
{
public:
    explicit BitReader(const QByteArray &data)
        : data_(reinterpret_cast<const quint8 *>(data.constData()))
        , bitCount_(data.size() * 8)
    {
    }

    int readBits(int count)
    {
        if (count <= 0 || bitOffset_ + count > bitCount_) {
            return -1;
        }

        int value = 0;
        for (int index = 0; index < count; ++index) {
            const int absoluteBit = bitOffset_ + index;
            const int byteOffset = absoluteBit / 8;
            const int bitInByte = 7 - (absoluteBit % 8);
            value = (value << 1) | ((data_[byteOffset] >> bitInByte) & 0x01);
        }

        bitOffset_ += count;
        return value;
    }

private:
    const quint8 *data_ = nullptr;
    int bitCount_ = 0;
    int bitOffset_ = 0;
};

QString describeAudioSpecificConfig(const QByteArray &audioSpecificConfig)
{
    static constexpr int kSamplingRates[] = {
        96000, 88200, 64000, 48000, 44100, 32000, 24000,
        22050, 16000, 12000, 11025, 8000, 7350
    };

    if (audioSpecificConfig.isEmpty()) {
        return "empty";
    }

    BitReader reader(audioSpecificConfig);
    int audioObjectType = reader.readBits(5);
    if (audioObjectType == 31) {
        const int extendedType = reader.readBits(6);
        if (extendedType < 0) {
            return QString("raw=%1").arg(QString(audioSpecificConfig.toHex()));
        }
        audioObjectType = 32 + extendedType;
    }

    const int samplingFrequencyIndex = reader.readBits(4);
    if (samplingFrequencyIndex < 0) {
        return QString("raw=%1").arg(QString(audioSpecificConfig.toHex()));
    }

    int sampleRate = 0;
    if (samplingFrequencyIndex == 0x0f) {
        sampleRate = reader.readBits(24);
    } else if (samplingFrequencyIndex >= 0 && samplingFrequencyIndex < 13) {
        sampleRate = kSamplingRates[samplingFrequencyIndex];
    }

    const int channelConfiguration = reader.readBits(4);
    if (channelConfiguration < 0) {
        return QString("raw=%1").arg(QString(audioSpecificConfig.toHex()));
    }

    return QString("objectType=%1 sampleRate=%2 channelConfig=%3 asc=%4")
        .arg(audioObjectType)
        .arg(sampleRate)
        .arg(channelConfiguration)
        .arg(QString(audioSpecificConfig.toHex()));
}
}  // namespace

FlvAudioDecoder::FlvAudioDecoder()
    : resampleInputLayout_(new AVChannelLayout{})
    , resampleOutputLayout_(new AVChannelLayout{})
{
    std::memset(resampleInputLayout_, 0, sizeof(AVChannelLayout));
    std::memset(resampleOutputLayout_, 0, sizeof(AVChannelLayout));
}

FlvAudioDecoder::~FlvAudioDecoder()
{
    reset();
    delete resampleInputLayout_;
    delete resampleOutputLayout_;
}

void FlvAudioDecoder::reset()
{
    if (decodedFrame_) {
        av_frame_free(&decodedFrame_);
    }

    if (audioCodecContext_) {
        avcodec_free_context(&audioCodecContext_);
    }

    if (resampleContext_) {
        swr_free(&resampleContext_);
    }

    if (resampleInputLayout_) {
        av_channel_layout_uninit(resampleInputLayout_);
    }
    if (resampleOutputLayout_) {
        av_channel_layout_uninit(resampleOutputLayout_);
    }

    resampleInputSampleRate_ = 0;
    resampleOutputSampleRate_ = 0;
    resampleInputFormat_ = -1;
    resampleOutputFormat_ = -1;
    audioSpecificConfig_.clear();
    lastError_.clear();
}

void FlvAudioDecoder::setLogCallback(LogCallback callback)
{
    logCallback_ = std::move(callback);
}

void FlvAudioDecoder::setPcmCallback(PcmCallback callback)
{
    pcmCallback_ = std::move(callback);
}

bool FlvAudioDecoder::pushTag(const protocol::FlvTag &tag, AudioDecodeReport &report)
{
    report = {};

    if (tag.type != protocol::FlvTagType::Audio) {
        return true;
    }

    if (tag.audioSoundFormat != 10) {
        emitLog(QString("Audio tag format=%1 is not wired yet. Current milestone only decodes AAC from HTTP-FLV.")
                    .arg(tag.audioSoundFormat));
        return true;
    }

    if (tag.payload.size() < 2) {
        setError(QString("AAC audio tag is too small to contain FLV audio metadata. payload=%1")
                     .arg(tag.payload.size()));
        return false;
    }

    if (tag.aacPacketType == 0) {
        return handleAacSequenceHeader(tag, report);
    }

    if (tag.aacPacketType == 1) {
        return handleAacRawTag(tag, report);
    }

    emitLog(QString("Unknown AAC packet type %1 observed.").arg(tag.aacPacketType));
    return true;
}

bool FlvAudioDecoder::isConfigured() const
{
    return audioCodecContext_ != nullptr;
}

QString FlvAudioDecoder::lastError() const
{
    return lastError_;
}

bool FlvAudioDecoder::handleAacSequenceHeader(const protocol::FlvTag &tag, AudioDecodeReport &report)
{
    const QByteArray audioSpecificConfig = tag.payload.mid(2);
    if (audioSpecificConfig.isEmpty()) {
        setError("AAC sequence header payload is empty.");
        return false;
    }

    if (!openAacDecoder(audioSpecificConfig)) {
        return false;
    }

    audioSpecificConfig_ = audioSpecificConfig;
    report.decoderConfigured = true;

    emitLog(QString("AAC sequence header received at %1 ms. %2.")
                .arg(tag.timestampMs)
                .arg(describeAudioSpecificConfig(audioSpecificConfig)));
    return true;
}

bool FlvAudioDecoder::handleAacRawTag(const protocol::FlvTag &tag, AudioDecodeReport &report)
{
    if (!audioCodecContext_) {
        emitLog("AAC raw packet arrived before sequence header. Skip until AudioSpecificConfig is ready.");
        return true;
    }

    const QByteArray rawAacPayload = tag.payload.mid(2);
    if (rawAacPayload.isEmpty()) {
        return true;
    }

    return submitAudioPacket(reinterpret_cast<const uint8_t *>(rawAacPayload.constData()),
                             rawAacPayload.size(),
                             static_cast<qint64>(tag.timestampMs),
                             report);
}

bool FlvAudioDecoder::openAacDecoder(const QByteArray &audioSpecificConfig)
{
    reset();

    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_AAC);
    if (!codec) {
        setError("FFmpeg AAC decoder was not found.");
        return false;
    }

    audioCodecContext_ = avcodec_alloc_context3(codec);
    if (!audioCodecContext_) {
        setError("Failed to allocate AVCodecContext for AAC.");
        return false;
    }

    audioCodecContext_->pkt_timebase = AVRational{1, 1000};
    audioCodecContext_->flags2 |= AV_CODEC_FLAG2_CHUNKS;

    audioCodecContext_->extradata = static_cast<uint8_t *>(
        av_mallocz(static_cast<size_t>(audioSpecificConfig.size()) + AV_INPUT_BUFFER_PADDING_SIZE));
    if (!audioCodecContext_->extradata) {
        setError("Failed to allocate decoder extradata for AAC sequence header.");
        return false;
    }

    std::memcpy(audioCodecContext_->extradata,
                audioSpecificConfig.constData(),
                static_cast<size_t>(audioSpecificConfig.size()));
    audioCodecContext_->extradata_size = audioSpecificConfig.size();

    const int openResult = avcodec_open2(audioCodecContext_, codec, nullptr);
    if (openResult < 0) {
        setError(QString("avcodec_open2(AAC) failed: %1").arg(ffmpegErrorString(openResult)));
        return false;
    }

    decodedFrame_ = av_frame_alloc();
    if (!decodedFrame_) {
        setError("Failed to allocate AVFrame for decoded audio.");
        return false;
    }

    return true;
}

bool FlvAudioDecoder::submitAudioPacket(
    const uint8_t *data,
    int size,
    qint64 ptsMs,
    AudioDecodeReport &report)
{
    if (!audioCodecContext_ || !decodedFrame_ || !data || size <= 0) {
        return true;
    }

    AVPacket *packet = av_packet_alloc();
    if (!packet) {
        setError("Failed to allocate AVPacket for audio decode.");
        return false;
    }

    const int packetAllocResult = av_new_packet(packet, size);
    if (packetAllocResult < 0) {
        av_packet_free(&packet);
        setError(QString("av_new_packet failed: %1").arg(ffmpegErrorString(packetAllocResult)));
        return false;
    }

    std::memcpy(packet->data, data, static_cast<size_t>(size));
    packet->pts = ptsMs;
    packet->dts = ptsMs;

    const int sendResult = avcodec_send_packet(audioCodecContext_, packet);
    av_packet_free(&packet);
    if (sendResult < 0) {
        setError(QString("avcodec_send_packet(AAC) failed: %1").arg(ffmpegErrorString(sendResult)));
        return false;
    }

    while (true) {
        const int receiveResult = avcodec_receive_frame(audioCodecContext_, decodedFrame_);
        if (receiveResult == AVERROR(EAGAIN) || receiveResult == AVERROR_EOF) {
            break;
        }

        if (receiveResult < 0) {
            setError(QString("avcodec_receive_frame(AAC) failed: %1").arg(ffmpegErrorString(receiveResult)));
            return false;
        }

        if (decodedFrame_->nb_samples <= 0 ||
            decodedFrame_->sample_rate <= 0 ||
            decodedFrame_->format == AV_SAMPLE_FMT_NONE ||
            !decodedFrame_->extended_data) {
            av_frame_unref(decodedFrame_);
            continue;
        }

        QByteArray pcm;
        int sampleRate = 0;
        int channels = 0;
        int sampleCount = 0;
        if (!convertDecodedFrameToPcm(decodedFrame_, pcm, sampleRate, channels, sampleCount)) {
            av_frame_unref(decodedFrame_);
            return false;
        }

        ++report.decodedFrameCount;
        ++report.decodedPcmChunkCount;
        report.frameDecoded = true;

        const qint64 framePtsMs = (decodedFrame_->best_effort_timestamp == AV_NOPTS_VALUE)
            ? ptsMs
            : decodedFrame_->best_effort_timestamp;

        if (pcmCallback_) {
            pcmCallback_(std::move(pcm), framePtsMs, sampleRate, channels, sampleCount);
        }

        av_frame_unref(decodedFrame_);
    }

    return true;
}

bool FlvAudioDecoder::resolveInputChannelLayout(const AVFrame *frame, AVChannelLayout &layout) const
{
    av_channel_layout_uninit(&layout);

    if (frame && frame->ch_layout.nb_channels > 0) {
        if (av_channel_layout_copy(&layout, &frame->ch_layout) >= 0) {
            return true;
        }
    }

    if (audioCodecContext_ && audioCodecContext_->ch_layout.nb_channels > 0) {
        if (av_channel_layout_copy(&layout, &audioCodecContext_->ch_layout) >= 0) {
            return true;
        }
    }

    const int fallbackChannels = frame && frame->ch_layout.nb_channels > 0
        ? frame->ch_layout.nb_channels
        : (audioCodecContext_ && audioCodecContext_->ch_layout.nb_channels > 0
               ? audioCodecContext_->ch_layout.nb_channels
               : 2);
    av_channel_layout_default(&layout, fallbackChannels);
    return true;
}

bool FlvAudioDecoder::ensureResampler(const AVFrame *frame, const AVChannelLayout &inputLayout)
{
    if (!frame) {
        setError("Audio frame is null while configuring the resampler.");
        return false;
    }

    const AVSampleFormat inputFormat = static_cast<AVSampleFormat>(frame->format);
    const AVSampleFormat outputFormat = AV_SAMPLE_FMT_S16;

    if (resampleContext_ &&
        av_channel_layout_compare(resampleInputLayout_, &inputLayout) == 0 &&
        av_channel_layout_compare(resampleOutputLayout_, &inputLayout) == 0 &&
        resampleInputSampleRate_ == frame->sample_rate &&
        resampleOutputSampleRate_ == frame->sample_rate &&
        resampleInputFormat_ == static_cast<int>(inputFormat) &&
        resampleOutputFormat_ == static_cast<int>(outputFormat)) {
        return true;
    }

    AVChannelLayout outputLayout{};
    if (av_channel_layout_copy(&outputLayout, &inputLayout) < 0) {
        setError("Failed to copy output channel layout for the audio resampler.");
        return false;
    }
    const std::unique_ptr<AVChannelLayout, decltype(&av_channel_layout_uninit)> outputLayoutGuard(
        &outputLayout,
        &av_channel_layout_uninit);

    SwrContext *newContext = nullptr;
    const int allocResult = swr_alloc_set_opts2(&newContext,
                                                &outputLayout,
                                                outputFormat,
                                                frame->sample_rate,
                                                &inputLayout,
                                                inputFormat,
                                                frame->sample_rate,
                                                0,
                                                nullptr);
    if (allocResult < 0 || !newContext) {
        setError(QString("swr_alloc_set_opts2 failed: %1").arg(ffmpegErrorString(allocResult)));
        return false;
    }

    const int initResult = swr_init(newContext);
    if (initResult < 0) {
        swr_free(&newContext);
        setError(QString("swr_init failed: %1").arg(ffmpegErrorString(initResult)));
        return false;
    }

    av_channel_layout_uninit(resampleInputLayout_);
    av_channel_layout_uninit(resampleOutputLayout_);
    if (av_channel_layout_copy(resampleInputLayout_, &inputLayout) < 0 ||
        av_channel_layout_copy(resampleOutputLayout_, &outputLayout) < 0) {
        swr_free(&newContext);
        setError("Failed to persist audio resampler channel layouts.");
        return false;
    }

    if (resampleContext_) {
        swr_free(&resampleContext_);
    }

    resampleContext_ = newContext;
    resampleInputSampleRate_ = frame->sample_rate;
    resampleOutputSampleRate_ = frame->sample_rate;
    resampleInputFormat_ = static_cast<int>(inputFormat);
    resampleOutputFormat_ = static_cast<int>(outputFormat);
    return true;
}

bool FlvAudioDecoder::convertDecodedFrameToPcm(
    const AVFrame *frame,
    QByteArray &pcm,
    int &sampleRate,
    int &channels,
    int &sampleCount)
{
    if (!frame || !frame->extended_data) {
        setError("Decoded audio frame is empty.");
        return false;
    }

    AVChannelLayout inputLayout{};
    if (!resolveInputChannelLayout(frame, inputLayout)) {
        setError("Failed to resolve the decoded audio frame channel layout.");
        return false;
    }

    const std::unique_ptr<AVChannelLayout, decltype(&av_channel_layout_uninit)> inputLayoutGuard(
        &inputLayout,
        &av_channel_layout_uninit);

    if (!ensureResampler(frame, inputLayout)) {
        return false;
    }

    const int outputChannels = resampleOutputLayout_->nb_channels;
    if (outputChannels <= 0) {
        setError("Audio resampler produced an invalid output channel count.");
        return false;
    }

    const int outputSamples = av_rescale_rnd(swr_get_delay(resampleContext_, frame->sample_rate) + frame->nb_samples,
                                             frame->sample_rate,
                                             frame->sample_rate,
                                             AV_ROUND_UP);
    if (outputSamples <= 0) {
        setError("Audio resampler produced an invalid output sample count.");
        return false;
    }

    const int outputBufferSize = av_samples_get_buffer_size(nullptr,
                                                            outputChannels,
                                                            outputSamples,
                                                            AV_SAMPLE_FMT_S16,
                                                            1);
    if (outputBufferSize < 0) {
        setError(QString("av_samples_get_buffer_size failed: %1").arg(ffmpegErrorString(outputBufferSize)));
        return false;
    }

    pcm = QByteArray(outputBufferSize, Qt::Uninitialized);
    if (pcm.size() != outputBufferSize) {
        setError("Failed to allocate the PCM output buffer.");
        return false;
    }

    uint8_t *outputData[1] = {
        reinterpret_cast<uint8_t *>(pcm.data())
    };
    const uint8_t **inputData = const_cast<const uint8_t **>(frame->extended_data);
    const int convertedSamples = swr_convert(resampleContext_,
                                             outputData,
                                             outputSamples,
                                             inputData,
                                             frame->nb_samples);
    if (convertedSamples < 0) {
        setError(QString("swr_convert failed: %1").arg(ffmpegErrorString(convertedSamples)));
        return false;
    }

    const int convertedBufferSize = av_samples_get_buffer_size(nullptr,
                                                               outputChannels,
                                                               convertedSamples,
                                                               AV_SAMPLE_FMT_S16,
                                                               1);
    if (convertedBufferSize < 0) {
        setError(QString("av_samples_get_buffer_size(converted) failed: %1")
                     .arg(ffmpegErrorString(convertedBufferSize)));
        return false;
    }

    pcm.resize(convertedBufferSize);
    sampleRate = frame->sample_rate;
    channels = outputChannels;
    sampleCount = convertedSamples;
    return true;
}

void FlvAudioDecoder::emitLog(const QString &message) const
{
    if (logCallback_) {
        logCallback_(message);
    }
}

void FlvAudioDecoder::setError(const QString &message)
{
    lastError_ = message;
}

}  // namespace backend::liveplayer::decode
