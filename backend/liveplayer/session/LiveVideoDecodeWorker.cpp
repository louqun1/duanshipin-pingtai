#include "liveplayer/session/LiveVideoDecodeWorker.hpp"

#include "liveplayer/logging/LiveWatchLogger.hpp"

#include <QMutexLocker>

extern "C" {
#include <libavutil/frame.h>
}

namespace backend::liveplayer::session {

VideoTagQueue::VideoTagQueue(quint64 generation)
    : generation_(generation)
{
}

bool VideoTagQueue::enqueue(const protocol::FlvTag &tag)
{
    QMutexLocker locker(&mutex_);
    if (stopped_) {
        logging::warn(
            "[queue] [gen={}] reject enqueue after stop tag_ts={}ms payload={} codec={} keyframe={}",
            generation_,
            tag.timestampMs,
            tag.payload.size(),
            tag.videoCodecId,
            tag.isKeyframe);
        return false;
    }

    const int depthBefore = static_cast<int>(queue_.size());
    queue_.push_back(tag);
    const int depthAfter = static_cast<int>(queue_.size());
    waitCondition_.wakeOne();

    logging::debug(
        "[queue] [gen={}] enqueue tag_ts={}ms payload={} codec={} keyframe={} depth={} -> {}",
        generation_,
        tag.timestampMs,
        tag.payload.size(),
        tag.videoCodecId,
        tag.isKeyframe,
        depthBefore,
        depthAfter);
    return true;
}

bool VideoTagQueue::waitAndPop(protocol::FlvTag &tag)
{
    QMutexLocker locker(&mutex_);
    while (!stopped_ && queue_.empty()) {
        waitCondition_.wait(&mutex_);
    }

    if (queue_.empty()) {
        logging::debug("[queue] [gen={}] waitAndPop returns false because queue is stopped and empty",
                       generation_);
        return false;
    }

    tag = std::move(queue_.front());
    queue_.pop_front();

    logging::debug(
        "[queue] [gen={}] dequeue tag_ts={}ms payload={} codec={} keyframe={} depth_after={}",
        generation_,
        tag.timestampMs,
        tag.payload.size(),
        tag.videoCodecId,
        tag.isKeyframe,
        queue_.size());
    return true;
}

void VideoTagQueue::stop()
{
    QMutexLocker locker(&mutex_);
    const int clearedCount = static_cast<int>(queue_.size());
    stopped_ = true;
    queue_.clear();
    waitCondition_.wakeAll();

    logging::info("[queue] [gen={}] stop queue, cleared_tags={}", generation_, clearedCount);
}

void VideoTagQueue::reset()
{
    QMutexLocker locker(&mutex_);
    const int clearedCount = static_cast<int>(queue_.size());
    queue_.clear();
    stopped_ = false;

    logging::info("[queue] [gen={}] reset queue, cleared_tags={}, state=ready", generation_, clearedCount);
}

bool VideoTagQueue::isStopped()
{
    QMutexLocker locker(&mutex_);
    return stopped_;
}

LiveVideoDecodeWorker::LiveVideoDecodeWorker(quint64 generation, QObject *parent)
    : QThread(parent)
    , generation_(generation)
    , queue_(generation)
{
    decoder_.setLogCallback([this](const QString &message) {
        emitLog(message);
    });
    decoder_.setFrameCallback([this](const AVFrame *frame, qint64 ptsMs) {
        if (!frame) {
            return;
        }

        AVFrame *clonedFrame = av_frame_clone(frame);
        if (!clonedFrame) {
            emitFatalError("Failed to clone decoded AVFrame for cross-thread handoff.");
            return;
        }

        if (!frameReadyCallback_) {
            av_frame_free(&clonedFrame);
            return;
        }

        if (!firstFrameDecoded_) {
            firstFrameDecoded_ = true;
            const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::steady_clock::now() - workerStartedAt_)
                                       .count();
            logging::info("[worker] [gen={}] first decoded frame ready pts={}ms elapsed={}ms",
                          generation_,
                          ptsMs,
                          elapsedMs);
        }

        logging::debug("[worker] [gen={}] dispatch decoded frame to session pts={}ms", generation_, ptsMs);
        frameReadyCallback_(FramePtr(clonedFrame, [](AVFrame *ownedFrame) {
                                av_frame_free(&ownedFrame);
                            }),
                            ptsMs,
                            generation_);
    });
}

LiveVideoDecodeWorker::~LiveVideoDecodeWorker()
{
    stop();
}

void LiveVideoDecodeWorker::setLogCallback(LogCallback callback)
{
    logCallback_ = std::move(callback);
}

void LiveVideoDecodeWorker::setErrorCallback(ErrorCallback callback)
{
    errorCallback_ = std::move(callback);
}

void LiveVideoDecodeWorker::setFrameReadyCallback(FrameReadyCallback callback)
{
    frameReadyCallback_ = std::move(callback);
}

bool LiveVideoDecodeWorker::enqueueTag(const protocol::FlvTag &tag)
{
    return queue_.enqueue(tag);
}

void LiveVideoDecodeWorker::stop()
{
    logging::info("[worker] [gen={}] stop requested", generation_);
    queue_.stop();
    if (isRunning()) {
        wait();
    }
    decoder_.reset();
    queue_.reset();
}

void LiveVideoDecodeWorker::run()
{
    firstFrameDecoded_ = false;
    workerStartedAt_ = std::chrono::steady_clock::now();
    decoder_.reset();
    logging::info("[worker] [gen={}] worker thread started", generation_);

    protocol::FlvTag tag;
    while (queue_.waitAndPop(tag)) {
        logging::debug(
            "[worker] [gen={}] decode tag_ts={}ms payload={} codec={} keyframe={}",
            generation_,
            tag.timestampMs,
            tag.payload.size(),
            tag.videoCodecId,
            tag.isKeyframe);
        decode::VideoDecodeReport report;
        if (!decoder_.pushTag(tag, report)) {
            const QString errorMessage = decoder_.lastError().isEmpty()
                ? QString("FFmpeg video decode failed in the live video worker thread.")
                : decoder_.lastError();
            logging::error("[worker] [gen={}] decode error: {}", generation_, errorMessage.toUtf8().constData());
            queue_.stop();
            emitFatalError(errorMessage);
            break;
        }
    }

    logging::info("[worker] [gen={}] worker loop exits after waitAndPop returned false, queue_stopped={}",
                  generation_,
                  queue_.isStopped());
    decoder_.reset();
    logging::info("[worker] [gen={}] worker thread finished", generation_);
}

void LiveVideoDecodeWorker::emitLog(const QString &message)
{
    if (logCallback_) {
        logging::debug("[worker] [gen={}] decoder log -> session: {}", generation_, message.toUtf8().constData());
        logCallback_(message, generation_);
    }
}

void LiveVideoDecodeWorker::emitFatalError(const QString &message)
{
    if (errorCallback_) {
        logging::error("[worker] [gen={}] dispatch fatal error to session: {}", generation_, message.toUtf8().constData());
        errorCallback_(message, generation_);
    }
}

}  // namespace backend::liveplayer::session
