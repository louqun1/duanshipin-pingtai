#pragma once

#include "liveplayer/service/LivePlayerController.hpp"

#include <QByteArray>
#include <QJsonObject>
#include <QWidget>

class QFrame;
class QHideEvent;
class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;
class QPushButton;
class QTimer;
class VideoOpenGLWidget;

namespace frontend::pages {

class StreamPage final : public QWidget
{
    Q_OBJECT

public:
    explicit StreamPage(
        backend::liveplayer::service::LivePlayerController &livePlayerController,
        QWidget *parent = nullptr);
    void setAuthToken(const QString &token);

protected:
    void hideEvent(QHideEvent *event) override;

private:
    void buildUi();
    void connectController();
    void applyAuthHeader(QNetworkRequest &request, const QString &token) const;
    void appendLog(const QString &message);
    void connectRoomEventStream();
    void disconnectRoomEventStream();
    void ensurePresenceForCurrentWatch();
    void handleCurrentRoomStateReply(QNetworkReply *reply, const QString &reason);
    void handleRoomEventStreamFinished(QNetworkReply *reply);
    void handleRoomEventStreamReadyRead(QNetworkReply *reply);
    void handlePresenceReply(
        QNetworkReply *reply,
        const QString &action,
        const QString &roomKey,
        const QString &token,
        bool tracksActiveSession);
    void processRoomEventStreamMessage(const QByteArray &message);
    void requestStartWatch();
    void requestCurrentRoomState(const QString &reason);
    void retryMixedPlayback();
    void scheduleRoomEventStreamReconnect();
    void sendPresenceAction(
        const QString &action,
        const QString &roomKey,
        const QString &token,
        bool tracksActiveSession);
    void switchPlaybackTarget(const QString &targetUrl, const QString &reason);
    void stopWatching(bool stopPlayback, const QString &reason);
    void syncPlaybackTargetFromRoomState(const QJsonObject &roomObject, const QString &reason);
    void updatePresenceStatus(const QString &message);
    void updateState(
        backend::liveplayer::service::LivePlayerController::PlaybackState state,
        const QString &message);
    void updateStats(qint64 bytesReceived, int audioTagCount, int videoTagCount, int scriptTagCount);
    QString currentRoomKeyDisplay() const;
    QString summarizePresenceValue(const QString &message) const;
    void updateRoomInfo();
    void updateStreamStatus();

    backend::liveplayer::service::LivePlayerController &livePlayerController_;
    QFrame *videoViewport_ = nullptr;
    VideoOpenGLWidget *liveVideoSurface_ = nullptr;
    QLineEdit *streamUrlEdit_ = nullptr;
    QLineEdit *roomKeyEdit_ = nullptr;
    QPushButton *startButton_ = nullptr;
    QPushButton *stopButton_ = nullptr;
    QLabel *roomKeyValueLabel_ = nullptr;
    QLabel *statusValueLabel_ = nullptr;
    QLabel *presenceValueLabel_ = nullptr;
    QLabel *onlineCountValueLabel_ = nullptr;
    QLabel *networkValueLabel_ = nullptr;
    QLabel *demuxValueLabel_ = nullptr;
    QLabel *videoValueLabel_ = nullptr;
    QLabel *audioValueLabel_ = nullptr;
    QLabel *avSyncValueLabel_ = nullptr;
    QLabel *hintValueLabel_ = nullptr;
    QNetworkAccessManager *networkManager_ = nullptr;
    QTimer *mixedPlaybackRetryTimer_ = nullptr;
    QTimer *presenceHeartbeatTimer_ = nullptr;
    QTimer *roomEventStreamReconnectTimer_ = nullptr;
    QString authToken_;
    QString baseWatchStreamUrl_;
    QString watchedRoomKey_;
    QString watchedStreamUrl_;
    QString activePresenceRoomKey_;
    QString activePresenceAuthToken_;
    QString roomEventStreamBaseUrl_;
    QString mixedPlaybackUrl_;
    QString currentPlaybackMessage_;
    QByteArray roomEventStreamBuffer_;
    qint64 lastBytesReceived_ = 0;
    int lastAudioTagCount_ = 0;
    int lastVideoTagCount_ = 0;
    int lastScriptTagCount_ = 0;
    QNetworkReply *roomEventStreamReply_ = nullptr;
    backend::liveplayer::service::LivePlayerController::PlaybackState lastPlaybackState_ =
        backend::liveplayer::service::LivePlayerController::PlaybackState::Idle;
    bool mixedPlaybackActive_ = false;
    bool presenceJoined_ = false;
};

}  // namespace frontend::pages
