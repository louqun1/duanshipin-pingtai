#pragma once

#include <QByteArray>
#include <QString>
#include <QtGlobal>

#include <SDL.h>

#include <atomic>
#include <deque>
#include <functional>
#include <mutex>

namespace backend::liveplayer::audio {

class SdlAudioOutput final
{
public:
    struct ClockSnapshot
    {
        bool valid = false;
        qint64 ptsMs = -1;
        int queuedBytes = 0;
        qint64 queuedDurationMs = 0;
        int sampleRate = 0;
        int channels = 0;
        bool deviceOpen = false;
    };

    using LogCallback = std::function<void(const QString &message, bool warning)>;

    SdlAudioOutput();
    ~SdlAudioOutput();

    void setLogCallback(LogCallback callback);
    bool enqueuePcm(const QByteArray &pcm, qint64 ptsMs, int sampleRate, int channels, int sampleCount);
    void stop();
    ClockSnapshot clockSnapshot() const;

private:
    struct QueuedPcmChunk
    {
        QByteArray pcm;
        qint64 startPtsMs = -1;
        int offsetBytes = 0;
        int sampleRate = 0;
        int channels = 0;
        int sampleCount = 0;
        int bytesPerFrame = 0;
        int bytesPerSecond = 0;
    };

    static void sdlAudioCallback(void *userdata, Uint8 *stream, int len);
    void handleAudioCallback(Uint8 *stream, int len);
    bool ensureAudioSubsystem();
    bool openDeviceIfNeeded(int sampleRate, int channels);
    qint64 queuedDurationMsLocked() const;
    qint64 clockPtsForOffsetLocked(const QueuedPcmChunk &chunk) const;
    qint64 outputLatencyMsLocked() const;
    bool enqueuePcmChunkLocked(
        const QByteArray &pcm,
        qint64 ptsMs,
        int sampleRate,
        int channels,
        int sampleCount);
    void emitLog(const QString &message, bool warning) const;

    mutable std::mutex mutex_;
    std::deque<QueuedPcmChunk> queue_;
    LogCallback logCallback_;
    SDL_AudioDeviceID deviceId_ = 0;
    SDL_AudioSpec obtainedSpec_{};
    bool initializedAudioSubsystem_ = false;
    bool firstCallbackObserved_ = false;
    int callbackCount_ = 0;
    int starvationCount_ = 0;
    int queuedBytes_ = 0;
    int outputSampleRate_ = 0;
    int outputChannels_ = 0;
    int outputBytesPerFrame_ = 0;
    int outputBytesPerSecond_ = 0;
    qint64 lastClockLogWallClockMs_ = 0;
    std::atomic<bool> clockValid_{false};
    std::atomic<qint64> currentClockPtsMs_{-1};
};

}  // namespace backend::liveplayer::audio
