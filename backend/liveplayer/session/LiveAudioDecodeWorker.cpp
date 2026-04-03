#include "liveplayer/session/LiveAudioDecodeWorker.hpp"

#include "liveplayer/logging/LiveWatchLogger.hpp"

#include <QMutexLocker>

namespace backend::liveplayer::session {

AudioTagQueue::AudioTagQueue(quint64 generation)
    : generation_(generation)
{
}

bool AudioTagQueue::enqueue(const protocol::FlvTag &tag)
{
    QMutexLocker locker(&mutex_);
    if (stopped_) {
        logging::warn("[audio_queue] [gen={}] reject enqueue after stop tag_ts={}ms payload={} packet_type={} seq={}",
                      generation_,
                      tag.timestampMs,
                      tag.payload.size(),
                      tag.aacPacketType,
                      tag.isSequenceHeader);
        return false;
    }

    const int depthBefore = static_cast<int>(queue_.size());
    queue_.push_back(tag);
    const int depthAfter = static_cast<int>(queue_.size());
    waitCondition_.wakeOne();

    logging::debug("[audio_queue] [gen={}] audio packet enqueued tag_ts={}ms payload={} packet_type={} seq={} depth={} -> {}",
                   generation_,
                   tag.timestampMs,
                   tag.payload.size(),
                   tag.aacPacketType,
                   tag.isSequenceHeader,
                   depthBefore,
                   depthAfter);
    return true;
}

bool AudioTagQueue::waitAndPop(protocol::FlvTag &tag)
{
    QMutexLocker locker(&mutex_);
    while (!stopped_ && queue_.empty()) {
        waitCondition_.wait(&mutex_);
    }

    if (queue_.empty()) {
        logging::debug("[audio_queue] [gen={}] waitAndPop returns false because queue is stopped and empty",
                       generation_);
        return false;
    }

    tag = std::move(queue_.front());
    queue_.pop_front();

    logging::debug("[audio_queue] [gen={}] audio packet dequeued tag_ts={}ms payload={} packet_type={} seq={} depth_after={}",
                   generation_,
                   tag.timestampMs,
                   tag.payload.size(),
                   tag.aacPacketType,
                   tag.isSequenceHeader,
                   queue_.size());
    return true;
}

void AudioTagQueue::stop()
{
    QMutexLocker locker(&mutex_);
    const int clearedCount = static_cast<int>(queue_.size());
    stopped_ = true;
    queue_.clear();
    waitCondition_.wakeAll();

    logging::info("[audio_queue] [gen={}] stop queue, cleared_packets={}", generation_, clearedCount);
}

void AudioTagQueue::reset()
{
    QMutexLocker locker(&mutex_);
    const int clearedCount = static_cast<int>(queue_.size());
    queue_.clear();
    stopped_ = false;

    logging::info("[audio_queue] [gen={}] reset queue, cleared_packets={}, state=ready",
                  generation_,
                  clearedCount);
}

bool AudioTagQueue::isStopped()
{
    QMutexLocker locker(&mutex_);
    return stopped_;
}

LiveAudioDecodeWorker::LiveAudioDecodeWorker(quint64 generation, QObject *parent)
    : QThread(parent)
    , generation_(generation)
    , queue_(generation)
{
    decoder_.setLogCallback([this](const QString &message) {
        emitLog(message);
    });
    decoder_.setPcmCallback([this](QByteArray pcm, qint64 ptsMs, int sampleRate, int channels, int sampleCount) {
        if (!pcmReadyCallback_) {
            return;
        }

        if (!firstFrameDecoded_) {
            firstFrameDecoded_ = true;
            const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::steady_clock::now() - workerStartedAt_)
                                       .count();
            emitLog(QString("first decoded audio frame pts=%1 ms, samples=%2, sampleRate=%3, channels=%4, pcmBytes=%5, elapsed=%6 ms.")
                        .arg(ptsMs)
                        .arg(sampleCount)
                        .arg(sampleRate)
                        .arg(channels)
                        .arg(pcm.size())
                        .arg(elapsedMs));
            logging::info("[audio_worker] [gen={}] first decoded audio frame pts={}ms samples={} sample_rate={} channels={} pcm_bytes={} elapsed={}ms",
                          generation_,
                          ptsMs,
                          sampleCount,
                          sampleRate,
                          channels,
                          pcm.size(),
                          elapsedMs);
        }

        logging::debug("[audio_worker] [gen={}] dispatch PCM chunk pts={}ms pcm_bytes={} sample_rate={} channels={} samples={}",
                       generation_,
                       ptsMs,
                       pcm.size(),
                       sampleRate,
                       channels,
                       sampleCount);
        pcmReadyCallback_(std::move(pcm), ptsMs, sampleRate, channels, sampleCount, generation_);
    });
}

LiveAudioDecodeWorker::~LiveAudioDecodeWorker()
{
    stop();
}

void LiveAudioDecodeWorker::setLogCallback(LogCallback callback)
{
    logCallback_ = std::move(callback);
}

void LiveAudioDecodeWorker::setErrorCallback(ErrorCallback callback)
{
    errorCallback_ = std::move(callback);
}

void LiveAudioDecodeWorker::setPcmReadyCallback(PcmReadyCallback callback)
{
    pcmReadyCallback_ = std::move(callback);
}

bool LiveAudioDecodeWorker::enqueueTag(const protocol::FlvTag &tag)
{
    return queue_.enqueue(tag);
}

void LiveAudioDecodeWorker::stop()
{
    logging::info("[audio_worker] [gen={}] stop requested", generation_);
    queue_.stop();
    if (isRunning()) {
        wait();
    }
    decoder_.reset();
    queue_.reset();
}

void LiveAudioDecodeWorker::run()
{
    firstFrameDecoded_ = false;
    workerStartedAt_ = std::chrono::steady_clock::now();
    decoder_.reset();
    logging::info("[audio_worker] [gen={}] worker thread started", generation_);

    protocol::FlvTag tag;
    while (queue_.waitAndPop(tag)) {
        logging::debug("[audio_worker] [gen={}] decode tag_ts={}ms payload={} packet_type={} seq={}",
                       generation_,
                       tag.timestampMs,
                       tag.payload.size(),
                       tag.aacPacketType,
                       tag.isSequenceHeader);
        decode::AudioDecodeReport report;
        if (!decoder_.pushTag(tag, report)) {
            const QString errorMessage = decoder_.lastError().isEmpty()
                ? QString("FFmpeg audio decode failed in the live audio worker thread.")
                : decoder_.lastError();
            logging::error("[audio_worker] [gen={}] decode error: {}", generation_, errorMessage.toUtf8().constData());
            queue_.stop();
            emitFatalError(errorMessage);
            break;
        }
    }

    logging::info("[audio_worker] [gen={}] worker loop exits after waitAndPop returned false, queue_stopped={}",
                  generation_,
                  queue_.isStopped());
    decoder_.reset();
    logging::info("[audio_worker] [gen={}] worker thread finished", generation_);
}

void LiveAudioDecodeWorker::emitLog(const QString &message)
{
    if (logCallback_) {
        logging::debug("[audio_worker] [gen={}] decoder log -> session: {}",
                       generation_,
                       message.toUtf8().constData());
        logCallback_(message, generation_);
    }
}

void LiveAudioDecodeWorker::emitFatalError(const QString &message)
{
    if (errorCallback_) {
        logging::error("[audio_worker] [gen={}] dispatch fatal error to session: {}",
                       generation_,
                       message.toUtf8().constData());
        errorCallback_(message, generation_);
    }
}

}  // namespace backend::liveplayer::session
