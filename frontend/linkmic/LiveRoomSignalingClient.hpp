#pragma once

#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QUrl>

class QWebSocket;

namespace frontend::linkmic {

class LiveRoomSignalingClient final : public QObject
{
    Q_OBJECT

public:
    enum class State {
        Disconnected,
        Connecting,
        Joined,
        Error,
    };
    Q_ENUM(State)

    struct Config {
        QUrl signalingUrl;
        QString roomKey;
        QString accessToken;
        QString currentUserId;
        QString roomRole;
        QString joinRole;
        int heartbeatIntervalMs = 60 * 1000;

        [[nodiscard]] bool isValid() const
        {
            return signalingUrl.isValid() &&
                !roomKey.trimmed().isEmpty() &&
                !accessToken.trimmed().isEmpty() &&
                !currentUserId.trimmed().isEmpty();
        }
    };

    explicit LiveRoomSignalingClient(QObject *parent = nullptr);
    ~LiveRoomSignalingClient() override;

    [[nodiscard]] bool isAvailable() const;
    [[nodiscard]] State state() const;
    [[nodiscard]] QString stateText() const;
    [[nodiscard]] QString lastError() const;
    [[nodiscard]] QString activeRoomKey() const;
    [[nodiscard]] QString currentUserId() const;
    [[nodiscard]] QString roomRole() const;
    [[nodiscard]] bool isJoined() const;

    void connectToRoom(const Config &config);
    void disconnectFromRoom(const QString &reason = {});

    bool sendBusinessMessage(const QString &type,
                             const QString &fromUserId,
                             const QString &toUserId,
                             const QString &requestId,
                             const QJsonObject &payload);

signals:
    void stateChanged(frontend::linkmic::LiveRoomSignalingClient::State state, const QString &detail);
    void messageReceived(const QJsonObject &message);

private:
    void transitionState(State state, const QString &detail);
    void sendJoin();
    void sendHeartbeat();
    void sendLeave();
    bool sendRawMessage(const QJsonObject &message);
    void handleSocketConnected();
    void handleSocketDisconnected();
    void handleSocketError(const QString &errorText);
    void handleSocketTextMessage(const QString &text);

    Config config_;
    State state_ = State::Disconnected;
    QString lastError_;
    QString lastDetail_;
    QString pendingCloseReason_;
    bool leaveSent_ = false;
    QTimer heartbeatTimer_;
#ifdef FLASHPOINT_HAS_QT_WEBSOCKETS
    QWebSocket *socket_ = nullptr;
#endif
};

}  // namespace frontend::linkmic
