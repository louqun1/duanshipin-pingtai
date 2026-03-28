#pragma once

#include "liveplayer/session/LivePlayerSession.hpp"

#include <QObject>
#include <QString>

class QWidget;

namespace backend::liveplayer::service {

class LivePlayerController final : public QObject
{
    Q_OBJECT

public:
    enum class PlaybackState {
        Idle,
        Connecting,
        Reading,
        Playing,
        Stopped,
        Error
    };
    Q_ENUM(PlaybackState)

    explicit LivePlayerController(QObject *parent = nullptr);

    PlaybackState playbackState() const;
    QString currentUrl() const;

public slots:
    void attachVideoSurface(QWidget *surface);
    void openStream(const QString &url);
    void stopStream();

signals:
    void playbackStateChanged(PlaybackState state, const QString &message);
    void streamUrlChanged(const QString &url);
    void sessionLogAppended(const QString &message);
    void streamStatsChanged(qint64 bytesReceived, int audioTagCount, int videoTagCount, int scriptTagCount);

private slots:
    void handleSessionStateChanged(
        backend::liveplayer::session::LivePlayerSession::SessionState state,
        const QString &message);

private:
    void setPlaybackState(PlaybackState state, const QString &message);

    backend::liveplayer::session::LivePlayerSession *session_ = nullptr;
    PlaybackState playbackState_ = PlaybackState::Idle;
    QString currentUrl_;
};

}  // namespace backend::liveplayer::service
