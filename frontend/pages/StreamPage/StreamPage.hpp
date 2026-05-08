#pragma once

#include "linkmic/LinkMicSession.hpp"
#include "liveplayer/service/LivePlayerController.hpp"

#include <QWidget>

class QFrame;
class QTableWidget;
class QHideEvent;
class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;
class QPushButton;
class QTimer;
class VideoOpenGLWidget;

namespace frontend::linkmic {
class LiveRoomSignalingClient;
}

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
    void ensurePresenceForCurrentWatch();
    void ensureSignalingForCurrentWatch();
    void handleCurrentUserReply(
        QNetworkReply *reply,
        const QString &roomKey,
        const QString &token,
        quint64 revision);
    void handleLiveRoomDetailReply(
        QNetworkReply *reply,
        const QString &roomKey,
        const QString &token,
        quint64 revision);
    void handlePresenceReply(
        QNetworkReply *reply,
        const QString &action,
        const QString &roomKey,
        const QString &token,
        bool tracksActiveSession);
    bool sendLinkMicMessage(
        const QString &type,
        const QString &roomId,
        const QString &fromUserId,
        const QString &toUserId,
        const QString &requestId,
        const QJsonObject &payload);
    void requestStartWatch();
    void sendPresenceAction(
        const QString &action,
        const QString &roomKey,
        const QString &token,
        bool tracksActiveSession);
    void stopBusinessSignaling(const QString &reason);
    void stopWatching(bool stopPlayback, const QString &reason);
    void updatePresenceStatus(const QString &message);
    void updateState(
        backend::liveplayer::service::LivePlayerController::PlaybackState state,
        const QString &message);
    void updateStats(qint64 bytesReceived, int audioTagCount, int videoTagCount, int scriptTagCount);
    QString currentRoomKeyDisplay() const;
    QString currentRoomScopedRole() const;
    QString currentRoomScopedRoleDisplay() const;
    QString currentJoinRole() const;
    QString selectedLinkMicTargetUserId() const;
    QString summarizePresenceValue(const QString &message) const;
    void updateRoomInfo();
    void populateLinkMicMemberTable();
    void refreshLinkMicPanel();
    void updateStreamStatus();
    void updateLinkMicActionButtons();

    backend::liveplayer::service::LivePlayerController &livePlayerController_;
    frontend::linkmic::LinkMicSession linkMicSession_;
    QFrame *videoViewport_ = nullptr;
    VideoOpenGLWidget *liveVideoSurface_ = nullptr;
    QLineEdit *streamUrlEdit_ = nullptr;
    QLineEdit *roomKeyEdit_ = nullptr;
    QPushButton *startButton_ = nullptr;
    QPushButton *stopButton_ = nullptr;
    QPushButton *inviteLinkMicButton_ = nullptr;
    QPushButton *applyLinkMicButton_ = nullptr;
    QPushButton *acceptLinkMicButton_ = nullptr;
    QPushButton *rejectLinkMicButton_ = nullptr;
    QPushButton *hangupLinkMicButton_ = nullptr;
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
    QLabel *signalValueLabel_ = nullptr;
    QLabel *signalRoleValueLabel_ = nullptr;
    QLabel *signalRoomValueLabel_ = nullptr;
    QLabel *signalTargetValueLabel_ = nullptr;
    QTableWidget *linkMicMemberTable_ = nullptr;
    QNetworkAccessManager *networkManager_ = nullptr;
    QTimer *presenceHeartbeatTimer_ = nullptr;
    frontend::linkmic::LiveRoomSignalingClient *signalingClient_ = nullptr;
    QString authToken_;
    QString currentUserId_;
    QString currentUsername_;
    QString watchedRoomKey_;
    QString watchedStreamUrl_;
    QString activePresenceRoomKey_;
    QString activePresenceAuthToken_;
    QString activeSignalRoomKey_;
    QString activeSignalAuthToken_;
    QString activeSignalUrl_;
    QString currentPlaybackMessage_;
    qint64 currentUserNumericId_ = 0;
    qint64 currentRoomOwnerUserId_ = 0;
    int lastKnownRoomOnlineCount_ = 0;
    quint64 signalRequestRevision_ = 0;
    bool linkMicSessionBound_ = false;
    qint64 lastBytesReceived_ = 0;
    int lastAudioTagCount_ = 0;
    int lastVideoTagCount_ = 0;
    int lastScriptTagCount_ = 0;
    backend::liveplayer::service::LivePlayerController::PlaybackState lastPlaybackState_ =
        backend::liveplayer::service::LivePlayerController::PlaybackState::Idle;
    bool presenceJoined_ = false;
};

}  // namespace frontend::pages
