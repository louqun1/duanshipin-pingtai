#pragma once

#include "liveplayer/decode/FlvAudioDecoder.hpp"
#include "liveplayer/protocol/FlvTypes.hpp"

#include <QMutex>
#include <QThread>
#include <QWaitCondition>
#include <QString>
#include <QtGlobal>

#include <chrono>
#include <deque>
#include <functional>

namespace backend::liveplayer::session {

class AudioTagQueue final
{
public:
    explicit AudioTagQueue(quint64 generation = 0);

    bool enqueue(const protocol::FlvTag &tag);
    bool waitAndPop(protocol::FlvTag &tag);
    void stop();
    void reset();
    bool isStopped();

private:
    quint64 generation_ = 0;
    QMutex mutex_;
    QWaitCondition waitCondition_;
    std::deque<protocol::FlvTag> queue_;
    bool stopped_ = false;
};

class LiveAudioDecodeWorker final : public QThread
{
public:
    using LogCallback = std::function<void(const QString &, quint64 generation)>;
    using ErrorCallback = std::function<void(const QString &, quint64 generation)>;
    using PcmReadyCallback = std::function<void(
        QByteArray pcm,
        qint64 ptsMs,
        int sampleRate,
        int channels,
        int sampleCount,
        quint64 generation)>;

    explicit LiveAudioDecodeWorker(quint64 generation, QObject *parent = nullptr);
    ~LiveAudioDecodeWorker() override;

    void setLogCallback(LogCallback callback);
    void setErrorCallback(ErrorCallback callback);
    void setPcmReadyCallback(PcmReadyCallback callback);

    bool enqueueTag(const protocol::FlvTag &tag);
    void stop();

protected:
    void run() override;

private:
    void emitLog(const QString &message);
    void emitFatalError(const QString &message);

    quint64 generation_ = 0;
    AudioTagQueue queue_;
    decode::FlvAudioDecoder decoder_;
    LogCallback logCallback_;
    ErrorCallback errorCallback_;
    PcmReadyCallback pcmReadyCallback_;
    bool firstFrameDecoded_ = false;
    std::chrono::steady_clock::time_point workerStartedAt_{};
};

}  // namespace backend::liveplayer::session
