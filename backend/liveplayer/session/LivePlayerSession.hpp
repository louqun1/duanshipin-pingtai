#pragma once

#include "liveplayer/decode/FlvVideoDecoder.hpp"
#include "liveplayer/protocol/FlvDemuxer.hpp"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QtGlobal>

class QWidget;
struct AVFrame;
struct SwsContext;

namespace backend::liveplayer::protocol {
class HttpFlvStreamReader;
}

namespace backend::liveplayer::session {

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
    void resetPlaybackResources(bool clearState);
    void appendInfoLog(const QString &message);
    void appendWarnLog(const QString &message);
    void appendErrorLog(const QString &message);
    void resetCounters();
    void setState(SessionState state, const QString &message);
    bool drainParsedTags();
    void handleDecodedVideoFrame(const AVFrame *frame);
    void resetVideoConverter();

    protocol::HttpFlvStreamReader *reader_ = nullptr;
    protocol::FlvDemuxer demuxer_;
    decode::FlvVideoDecoder videoDecoder_;
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
    bool firstVideoFrameDecoded_ = false;
    bool audioDecodeTodoLogged_ = false;
    SwsContext *videoScaleContext_ = nullptr;
};

}  // namespace backend::liveplayer::session
