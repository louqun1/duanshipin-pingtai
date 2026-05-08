#pragma once

#include <functional>

#include <QJsonObject>
#include <QTimer>
#include <QVector>

#include "linkmic/LinkMicRoomState.hpp"
#include "linkmic/RoomMemberDirectory.hpp"

namespace frontend::linkmic {

class LinkMicSession {
public:
    using UpdateHandler = std::function<void()>;
    using MessageSender =
        std::function<bool(const QString &type,
                           const QString &roomId,
                           const QString &fromUserId,
                           const QString &toUserId,
                           const QString &requestId,
                           const QJsonObject &payload)>;
    using RtcSignalHandler = std::function<void(const QJsonObject &message)>;

    LinkMicSession();

    void setUpdateHandler(UpdateHandler handler);
    void setMessageSender(MessageSender sender);
    void setRtcSignalHandler(RtcSignalHandler handler);

    void setTransportState(SignalingConnectionState state, const QString &detail);
    void processSignalMessage(const QJsonObject &message);

    bool connectToRoom(const LinkMicSessionConfig &config);
    void disconnectFromRoom();

    bool invite(const QString &targetUserId);
    bool apply(const QString &targetUserId);
    bool accept();
    bool reject();
    bool hangup();

    [[nodiscard]] const LinkMicSessionConfig &config() const;
    [[nodiscard]] const RoomMemberDirectory &memberDirectory() const;
    [[nodiscard]] const LinkMicRoomState &roomState() const;
    [[nodiscard]] const QVector<LinkMicEventLogEntry> &eventLog() const;

private:
    void handleRequestTimeout();
    void handleIncomingMessage(const QJsonObject &message);

    void appendEvent(const QString &direction,
                     const QString &type,
                     const QString &detail,
                     const QString &state = {},
                     const QString &requestId = {},
                     const QString &remoteUserId = {});
    void notifyUpdated() const;
    void startRequestTimer(const QString &reason);
    void stopRequestTimer();
    bool sendBusinessMessage(const QString &type,
                             const QString &toUserId,
                             const QString &requestId,
                             const QJsonObject &payload,
                             const QString &detail);

    LinkMicSessionConfig config_;
    RoomMemberDirectory memberDirectory_;
    LinkMicRoomState roomState_;
    MessageSender messageSender_;
    RtcSignalHandler rtcSignalHandler_;
    QVector<LinkMicEventLogEntry> eventLog_;
    QTimer requestTimer_;
    QString requestTimerReason_;
    SignalingConnectionState transportState_ = SignalingConnectionState::disconnected;
    QString transportDetail_;
    UpdateHandler onUpdated_;
};

}  // namespace frontend::linkmic
