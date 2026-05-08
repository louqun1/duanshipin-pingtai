#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QUrl>

namespace frontend::linkmic {

enum class SignalingConnectionState {
    disconnected,
    connecting,
    connected,
    failed,
};

struct LinkMicSessionConfig {
    QUrl signalingUrl;
    QString roomId;
    QString userId;
    QString displayName;
    QString role = "controller";
    int requestTimeoutMs = 10000;

    [[nodiscard]] bool isValid() const
    {
        return signalingUrl.isValid() &&
            !roomId.trimmed().isEmpty() &&
            !userId.trimmed().isEmpty() &&
            !role.trimmed().isEmpty();
    }
};

struct RoomMember {
    QString userId;
    QString displayName;
    QString avatarUrl;
    QString role;
    bool online = true;
    bool inLinkMic = false;
    QJsonObject raw;
};

struct RtcJoinParams {
    QString livekitUrl;
    QString roomName;
    QString token;
    QString participantRole;
    QJsonObject raw;

    [[nodiscard]] bool isValid() const
    {
        return !livekitUrl.trimmed().isEmpty() ||
            !roomName.trimmed().isEmpty() ||
            !token.trimmed().isEmpty() ||
            !participantRole.trimmed().isEmpty();
    }
};

struct LinkMicEventLogEntry {
    QDateTime timestampUtc;
    QString direction;
    QString type;
    QString detail;
    QString state;
    QString requestId;
    QString remoteUserId;
};

[[nodiscard]] inline QString connectionStateText(SignalingConnectionState state)
{
    switch (state) {
    case SignalingConnectionState::disconnected:
        return QStringLiteral("disconnected");
    case SignalingConnectionState::connecting:
        return QStringLiteral("connecting");
    case SignalingConnectionState::connected:
        return QStringLiteral("connected");
    case SignalingConnectionState::failed:
        return QStringLiteral("failed");
    }
    return QStringLiteral("disconnected");
}

[[nodiscard]] inline QString displayNameForMember(const RoomMember &member)
{
    return member.displayName.trimmed().isEmpty() ? member.userId : member.displayName;
}

[[nodiscard]] inline QString joinParamsSummary(const RtcJoinParams &params)
{
    if (!params.isValid()) {
        return QStringLiteral("pending");
    }

    QStringList parts;
    if (!params.roomName.trimmed().isEmpty()) {
        parts.push_back(QStringLiteral("room=") + params.roomName);
    }
    if (!params.participantRole.trimmed().isEmpty()) {
        parts.push_back(QStringLiteral("role=") + params.participantRole);
    }
    if (!params.livekitUrl.trimmed().isEmpty()) {
        parts.push_back(QStringLiteral("url=") + params.livekitUrl);
    }
    if (!params.token.trimmed().isEmpty()) {
        parts.push_back(QStringLiteral("token=received"));
    }
    return parts.join(QStringLiteral(" | "));
}

}  // namespace frontend::linkmic
