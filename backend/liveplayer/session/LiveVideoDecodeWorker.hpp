#pragma once

#include "liveplayer/decode/FlvVideoDecoder.hpp"
#include "liveplayer/protocol/FlvTypes.hpp"

#include <QMutex>
#include <QThread>
#include <QWaitCondition>
#include <QString>
#include <QtGlobal>

#include <chrono>
#include <atomic>
#include <deque>
#include <functional>
#include <memory>

struct AVFrame;

namespace backend::liveplayer::session {

class VideoTagQueue final
{
public:
    explicit VideoTagQueue(quint64 generation = 0);

    bool enqueue(const protocol::FlvTag &tag);
    bool waitAndPop(protocol::FlvTag &tag);
    void stop();
    void reset();
    bool isStopped();
    int size();

private:
    quint64 generation_ = 0;
    QMutex mutex_;
    QWaitCondition waitCondition_;
    std::deque<protocol::FlvTag> queue_;
    bool stopped_ = false;
};

class LiveVideoDecodeWorker final : public QThread
{
public:
    using FramePtr = std::shared_ptr<AVFrame>;
    using LogCallback = std::function<void(const QString &, quint64 generation)>;
    using ErrorCallback = std::function<void(const QString &, quint64 generation)>;
    using FrameReadyCallback = std::function<void(FramePtr frame, qint64 ptsMs, quint64 generation)>;

    explicit LiveVideoDecodeWorker(quint64 generation, QObject *parent = nullptr);
    ~LiveVideoDecodeWorker() override;

    void setLogCallback(LogCallback callback);
    void setErrorCallback(ErrorCallback callback);
    void setFrameReadyCallback(FrameReadyCallback callback);
    void setDecodeBackpressure(bool enabled, qint64 audioClockPtsMs, qint64 backLeadMs, int queueDepth, qint64 bufferedDurationMs);

    bool enqueueTag(const protocol::FlvTag &tag);
    int queuedTagCount();
    void stop();

protected:
    void run() override;

private:
    void emitLog(const QString &message);
    void emitFatalError(const QString &message);

    quint64 generation_ = 0;
    VideoTagQueue queue_;
    decode::FlvVideoDecoder decoder_;
    LogCallback logCallback_;
    ErrorCallback errorCallback_;
    FrameReadyCallback frameReadyCallback_;
    std::atomic<bool> decodeBackpressureEnabled_{false};
    std::atomic<qint64> decodeBackpressureAudioClockPtsMs_{-1};
    std::atomic<qint64> decodeBackpressureBackLeadMs_{0};
    std::atomic<int> decodeBackpressureQueueDepth_{0};
    std::atomic<qint64> decodeBackpressureBufferedMs_{0};
    bool firstFrameDecoded_ = false;
    std::chrono::steady_clock::time_point workerStartedAt_{};
};

}  // namespace backend::liveplayer::session
