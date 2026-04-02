#pragma once

#include "liveplayer/protocol/FlvDemuxer.hpp"
#include "liveplayer/session/VideoFrameQueue.hpp"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QtGlobal>

#include <chrono>
#include <memory>

class QWidget;
class QTimer;
struct AVFrame;
struct SwsContext;

namespace backend::liveplayer::protocol {
class HttpFlvStreamReader;
}

namespace backend::liveplayer::session {

class LiveVideoDecodeWorker;

class LivePlayerSession final : public QObject
{
    Q_OBJECT

public:
    enum class SessionState {
        Idle,
        Connecting,
        Reading,
        Playing,
        Stopped,
        Error
    };
    Q_ENUM(SessionState)

    explicit LivePlayerSession(QObject *parent = nullptr);
    ~LivePlayerSession() override;

    void attachVideoSurface(QWidget *surface);
    void open(const QString &url);
    void stop();

signals:
    void stateChanged(SessionState state, const QString &message);
    void logMessage(const QString &message);
    void statsChanged(qint64 bytesReceived, int audioTagCount, int videoTagCount, int scriptTagCount);

private slots:
    void handleReaderConnected(const QString &contentType, int statusCode);
    void handleReaderDataChunk(const QByteArray &chunk);
    void handleReaderError(const QString &message);
    void handleReaderFinished();

private:
    void startRenderTimer();
    void stopRenderTimer();
    void clearQueuedVideoFramesAndClock();
    qint64 sessionElapsedMs() const;
    qint64 currentTargetVideoPtsMs() const;
    void onRenderTick();
    void dropLateQueuedVideoFrames(qint64 targetPtsMs);
    void tryRenderNextQueuedVideoFrame(qint64 targetPtsMs);
    void resetPlaybackResources(bool clearState);
    void startVideoDecodeWorker();
    void stopVideoDecodeWorker();
    void invalidateVideoDecodeGeneration(const char *reason);
    bool enqueueVideoTag(const protocol::FlvTag &tag);
    void appendInfoLog(const QString &message);
    void appendWarnLog(const QString &message);
    void appendErrorLog(const QString &message);
    void appendWorkerLog(const QString &message, quint64 generation);
    void resetCounters();
    void setState(SessionState state, const QString &message);
    bool drainParsedTags();
    void handleWorkerDecodeError(const QString &message, quint64 generation);
    void handleWorkerDecodedVideoFrame(
        const std::shared_ptr<AVFrame> &frame,
        qint64 ptsMs,
        quint64 generation);
    void handleDecodedVideoFrame(const AVFrame *frame);
    void resetVideoConverter();

    protocol::HttpFlvStreamReader *reader_ = nullptr;
    protocol::FlvDemuxer demuxer_;
    std::unique_ptr<LiveVideoDecodeWorker> videoDecodeWorker_;
    QPointer<QWidget> videoSurface_;
    SessionState state_ = SessionState::Idle;
    QString currentUrl_;
    qint64 bytesReceived_ = 0;
    int audioTagCount_ = 0;
    int videoTagCount_ = 0;
    int scriptTagCount_ = 0;
    bool firstPayloadObserved_ = false;
    bool firstAudioTagObserved_ = false;
    bool firstVideoTagObserved_ = false;
    bool firstScriptTagObserved_ = false;
    bool audioSequenceHeaderObserved_ = false;
    bool videoSequenceHeaderObserved_ = false;
    bool firstVideoFrameQueued_ = false;
    bool firstVideoFrameSubmittedToRender_ = false;
    bool audioDecodeTodoLogged_ = false;
    quint64 videoDecodeGeneration_ = 0;
    std::chrono::steady_clock::time_point sessionOpenStartedAt_{};
    std::chrono::steady_clock::time_point playbackStartWallClock_{};
    VideoFrameQueue videoFrameQueue_;
    QTimer *renderTimer_ = nullptr;
    qint64 firstQueuedVideoPtsMs_ = -1;
    SwsContext *videoScaleContext_ = nullptr;
};

}  // namespace backend::liveplayer::session
