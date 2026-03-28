#pragma once

#include "liveplayer/protocol/FlvDemuxer.hpp"

#include <QObject>
#include <QPointer>
#include <QString>

class QWidget;

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
    void resetCounters();
    void setState(SessionState state, const QString &message);
    void drainParsedTags();

    protocol::HttpFlvStreamReader *reader_ = nullptr;
    protocol::FlvDemuxer demuxer_;
    QPointer<QWidget> videoSurface_;
    SessionState state_ = SessionState::Idle;
    QString currentUrl_;
    qint64 bytesReceived_ = 0;
    int audioTagCount_ = 0;
    int videoTagCount_ = 0;
    int scriptTagCount_ = 0;
    bool firstPayloadObserved_ = false;             //是否收到过第一块 payload
    bool firstAudioTagObserved_ = false;
    bool firstVideoTagObserved_ = false;
    bool firstScriptTagObserved_ = false;
    bool audioSequenceHeaderObserved_ = false;
    bool videoSequenceHeaderObserved_ = false;
    bool mediaFlowObserved_ = false;                //是否看到过真正的音频/视频 tag
};

}  // namespace backend::liveplayer::session
