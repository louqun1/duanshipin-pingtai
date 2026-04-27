#include "liveplayer/audio/SdlAudioOutput.hpp"

#include "liveplayer/logging/LiveWatchLogger.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>

namespace backend::liveplayer::audio {

namespace {

constexpr int kPreferredAudioPacketSamples = 512;
constexpr int kWantedAudioDeviceSamples = 512;

qint64 steadyNowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}  // namespace

SdlAudioOutput::SdlAudioOutput() = default;

SdlAudioOutput::~SdlAudioOutput()
{
    stop();
}

void SdlAudioOutput::setLogCallback(LogCallback callback)
{
    logCallback_ = std::move(callback);
}

bool SdlAudioOutput::enqueuePcm(
    const QByteArray &pcm,
    qint64 ptsMs,
    int sampleRate,
    int channels,
    int sampleCount)
{
    if (pcm.isEmpty()) {
        return true;
    }

    if (ptsMs < 0 || sampleRate <= 0 || channels <= 0 || sampleCount <= 0) {
        logging::error("[audio_output] reject PCM chunk because metadata is invalid pts={} sample_rate={} channels={} sample_count={} bytes={}",
                       ptsMs,
                       sampleRate,
                       channels,
                       sampleCount,
                       pcm.size());
        return false;
    }

    bool shouldUnpause = false;
    {
        std::lock_guard<std::mutex> locker(mutex_);
        if (!openDeviceIfNeeded(sampleRate, channels)) {
            return false;
        }

        if (sampleRate != outputSampleRate_ || channels != outputChannels_) {
            logging::error("[audio_output] reject PCM chunk because format changed current={}Hz/{}ch incoming={}Hz/{}ch",
                           outputSampleRate_,
                           outputChannels_,
                           sampleRate,
                           channels);
            return false;
        }

        const int bytesPerFrame = channels * static_cast<int>(sizeof(qint16));
        if (bytesPerFrame <= 0 || (pcm.size() % bytesPerFrame) != 0) {
            logging::error("[audio_output] reject PCM chunk because bytes are not aligned bytes={} bytes_per_frame={}",
                           pcm.size(),
                           bytesPerFrame);
            return false;
        }

        const int totalFrames = pcm.size() / bytesPerFrame;
        const int packetFrames = std::max(1, std::min(kPreferredAudioPacketSamples, totalFrames));
        int frameOffset = 0;
        int packetCount = 0;
        while (frameOffset < totalFrames) {
            const int chunkFrames = std::min(packetFrames, totalFrames - frameOffset);
            const int chunkBytes = chunkFrames * bytesPerFrame;
            const qint64 chunkPtsMs = ptsMs + (static_cast<qint64>(frameOffset) * 1000) / sampleRate;
            if (!enqueuePcmChunkLocked(pcm.mid(frameOffset * bytesPerFrame, chunkBytes),
                                       chunkPtsMs,
                                       sampleRate,
                                       channels,
                                       chunkFrames)) {
                return false;
            }
            frameOffset += chunkFrames;
            ++packetCount;
        }

        shouldUnpause = deviceId_ != 0 && callbackCount_ == 0;

        logging::debug("[audio_output] PCM packetized pts={}ms bytes={} samples={} packet_samples={} packets={} queue_bytes={} queue_ms={}",
                       ptsMs,
                       pcm.size(),
                       sampleCount,
                       packetFrames,
                       packetCount,
                       queuedBytes_,
                       queuedDurationMsLocked());
    }

    if (shouldUnpause && deviceId_ != 0) {
        SDL_PauseAudioDevice(deviceId_, 0);
    }

    return true;
}

bool SdlAudioOutput::enqueuePcmChunkLocked(
    const QByteArray &pcm,
    qint64 ptsMs,
    int sampleRate,
    int channels,
    int sampleCount)
{
    if (pcm.isEmpty() || sampleCount <= 0) {
        return true;
    }

    queue_.push_back(QueuedPcmChunk{
        pcm,
        ptsMs,
        0,
        sampleRate,
        channels,
        sampleCount,
        channels * static_cast<int>(sizeof(qint16)),
        sampleRate * channels * static_cast<int>(sizeof(qint16)),
    });
    queuedBytes_ += pcm.size();
    return true;
}

void SdlAudioOutput::stop()
{
    SDL_AudioDeviceID deviceId = 0;
    bool quitAudioSubsystem = false;

    {
        std::lock_guard<std::mutex> locker(mutex_);
        queue_.clear();
        queuedBytes_ = 0;
        deviceId = deviceId_;
        deviceId_ = 0;
        obtainedSpec_ = {};
        outputSampleRate_ = 0;
        outputChannels_ = 0;
        outputBytesPerFrame_ = 0;
        outputBytesPerSecond_ = 0;
        firstCallbackObserved_ = false;
        callbackCount_ = 0;
        starvationCount_ = 0;
        lastClockLogWallClockMs_ = 0;
        clockValid_.store(false);
        currentClockPtsMs_.store(-1);
        quitAudioSubsystem = initializedAudioSubsystem_;
        initializedAudioSubsystem_ = false;
    }

    if (deviceId != 0) {
        SDL_PauseAudioDevice(deviceId, 1);
        SDL_CloseAudioDevice(deviceId);
    }

    if (quitAudioSubsystem && (SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) != 0) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
}

SdlAudioOutput::ClockSnapshot SdlAudioOutput::clockSnapshot() const
{
    std::lock_guard<std::mutex> locker(mutex_);
    return ClockSnapshot{
        clockValid_.load(),
        currentClockPtsMs_.load(),
        queuedBytes_,
        queuedDurationMsLocked(),
        outputSampleRate_,
        outputChannels_,
        deviceId_ != 0,
    };
}

void SdlAudioOutput::sdlAudioCallback(void *userdata, Uint8 *stream, int len)
{
    if (!userdata || !stream || len <= 0) {
        return;
    }

    static_cast<SdlAudioOutput *>(userdata)->handleAudioCallback(stream, len);
}

void SdlAudioOutput::handleAudioCallback(Uint8 *stream, int len)
{
    std::memset(stream, 0, static_cast<size_t>(len));

    bool logFirstCallback = false;
    bool logStarvation = false;
    bool logClockUpdate = false;
    int copiedBytes = 0;
    int requestedSamples = 0;
    int queuedBytesAfter = 0;
    qint64 queuedDurationAfterMs = 0;
    qint64 latestClockPtsMs = -1;

    {
        std::lock_guard<std::mutex> locker(mutex_);
        ++callbackCount_;
        requestedSamples = outputBytesPerFrame_ > 0 ? len / outputBytesPerFrame_ : 0;
        logFirstCallback = !firstCallbackObserved_;
        if (logFirstCallback) {
            firstCallbackObserved_ = true;
        }

        int remainingBytes = len;
        int outputOffset = 0;
        while (remainingBytes > 0 && !queue_.empty()) {
            QueuedPcmChunk &chunk = queue_.front();
            const int availableBytes = chunk.pcm.size() - chunk.offsetBytes;
            if (availableBytes <= 0) {
                queue_.pop_front();
                continue;
            }

            const int copyBytes = std::min(availableBytes, remainingBytes);
            std::memcpy(stream + outputOffset,
                        chunk.pcm.constData() + chunk.offsetBytes,
                        static_cast<size_t>(copyBytes));
            chunk.offsetBytes += copyBytes;
            copiedBytes += copyBytes;
            queuedBytes_ -= copyBytes;
            remainingBytes -= copyBytes;
            outputOffset += copyBytes;
            latestClockPtsMs = clockPtsForOffsetLocked(chunk) - outputLatencyMsLocked();

            if (chunk.offsetBytes >= chunk.pcm.size()) {
                queue_.pop_front();
            }
        }

        if (copiedBytes > 0 && latestClockPtsMs >= 0) {
            currentClockPtsMs_.store(latestClockPtsMs);
            logClockUpdate = !clockValid_.load();
            clockValid_.store(true);
        }

        if (remainingBytes > 0) {
            ++starvationCount_;
            logStarvation = starvationCount_ == 1 || (starvationCount_ % 10) == 0;
        } else {
            starvationCount_ = 0;
        }

        queuedBytesAfter = queuedBytes_;
        queuedDurationAfterMs = queuedDurationMsLocked();

        const qint64 nowMs = steadyNowMs();
        if (clockValid_.load() &&
            nowMs - lastClockLogWallClockMs_ >= 500 &&
            latestClockPtsMs >= 0) {
            lastClockLogWallClockMs_ = nowMs;
            logging::debug("[audio_clock] update pts={}ms queue_bytes={} queue_ms={} callback_count={}",
                           latestClockPtsMs,
                           queuedBytesAfter,
                           queuedDurationAfterMs,
                           callbackCount_);
        }

        if (callbackCount_ % 50 == 0) {
            logging::debug("[audio_output] audio callback pull bytes={} samples={} copied={} queue_bytes={} queue_ms={} callbacks={}",
                           len,
                           requestedSamples,
                           copiedBytes,
                           queuedBytesAfter,
                           queuedDurationAfterMs,
                           callbackCount_);
        }
    }

    if (logFirstCallback) {
        emitLog(QString("audio callback pull bytes=%1 samples=%2 queueBytes=%3.")
                    .arg(len)
                    .arg(requestedSamples)
                    .arg(queuedBytesAfter),
                false);
    }

    if (logStarvation) {
        emitLog(QString("audio buffer starvation requestedBytes=%1 copiedBytes=%2 queueBytes=%3.")
                    .arg(len)
                    .arg(copiedBytes)
                    .arg(queuedBytesAfter),
                true);
    }

    if (logClockUpdate && latestClockPtsMs >= 0) {
        emitLog(QString("audio clock update pts=%1 ms, queuedBytes=%2, queued=%3 ms.")
                    .arg(latestClockPtsMs)
                    .arg(queuedBytesAfter)
                    .arg(queuedDurationAfterMs),
                false);
    }
}

bool SdlAudioOutput::ensureAudioSubsystem()
{
    if ((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) != 0) {
        return true;
    }

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        logging::error("[audio_output] SDL_InitSubSystem(SDL_INIT_AUDIO) failed: {}", SDL_GetError());
        return false;
    }

    initializedAudioSubsystem_ = true;
    logging::info("[audio_output] SDL audio subsystem initialized");
    return true;
}

bool SdlAudioOutput::openDeviceIfNeeded(int sampleRate, int channels)
{
    if (deviceId_ != 0) {
        return true;
    }

    if (!ensureAudioSubsystem()) {
        return false;
    }

    SDL_AudioSpec wantedSpec{};
    wantedSpec.freq = sampleRate;
    wantedSpec.format = AUDIO_S16SYS;
    wantedSpec.channels = static_cast<Uint8>(channels);
    wantedSpec.silence = 0;
    wantedSpec.samples = kWantedAudioDeviceSamples;
    wantedSpec.callback = &SdlAudioOutput::sdlAudioCallback;
    wantedSpec.userdata = this;

    deviceId_ = SDL_OpenAudioDevice(nullptr, 0, &wantedSpec, &obtainedSpec_, 0);
    if (deviceId_ == 0) {
        logging::error("[audio_output] SDL_OpenAudioDevice failed: {}", SDL_GetError());
        return false;
    }

    outputSampleRate_ = obtainedSpec_.freq;
    outputChannels_ = obtainedSpec_.channels;
    outputBytesPerFrame_ = outputChannels_ * (SDL_AUDIO_BITSIZE(obtainedSpec_.format) / 8);
    outputBytesPerSecond_ = outputSampleRate_ * outputBytesPerFrame_;

    logging::info("[audio_output] audio device opened freq={} channels={} samples={} buffer_bytes={}",
                  outputSampleRate_,
                  outputChannels_,
                  obtainedSpec_.samples,
                  obtainedSpec_.size);
    emitLog(QString("audio device opened freq=%1 channels=%2 samples=%3 bufferBytes=%4.")
                .arg(outputSampleRate_)
                .arg(outputChannels_)
                .arg(obtainedSpec_.samples)
                .arg(obtainedSpec_.size),
            false);
    return true;
}

qint64 SdlAudioOutput::queuedDurationMsLocked() const
{
    qint64 totalDurationMs = 0;
    for (const QueuedPcmChunk &chunk : queue_) {
        const int remainingBytes = chunk.pcm.size() - chunk.offsetBytes;
        if (remainingBytes <= 0 || chunk.bytesPerSecond <= 0) {
            continue;
        }

        totalDurationMs += (static_cast<qint64>(remainingBytes) * 1000) / chunk.bytesPerSecond;
    }

    return totalDurationMs;
}

qint64 SdlAudioOutput::clockPtsForOffsetLocked(const QueuedPcmChunk &chunk) const
{
    if (chunk.sampleRate <= 0 || chunk.bytesPerFrame <= 0) {
        return chunk.startPtsMs;
    }

    const qint64 consumedSamples = chunk.offsetBytes / chunk.bytesPerFrame;
    return chunk.startPtsMs + (consumedSamples * 1000) / chunk.sampleRate;
}

qint64 SdlAudioOutput::outputLatencyMsLocked() const
{
    if (outputBytesPerSecond_ <= 0 || obtainedSpec_.size <= 0) {
        return 0;
    }

    return (static_cast<qint64>(obtainedSpec_.size) * 1000) / outputBytesPerSecond_;
}

void SdlAudioOutput::emitLog(const QString &message, bool warning) const
{
    if (warning) {
        logging::warn("[audio_output] {}", message.toUtf8().constData());
    } else {
        logging::info("[audio_output] {}", message.toUtf8().constData());
    }

    if (logCallback_) {
        logCallback_(message, warning);
    }
}

}  // namespace backend::liveplayer::audio
