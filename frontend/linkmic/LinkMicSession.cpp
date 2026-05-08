#include "linkmic/LinkMicSession.hpp"

#include <QJsonDocument>

namespace frontend::linkmic {
namespace {

QString firstString(const QJsonObject &object, std::initializer_list<const char *> keys)
{
    for (const char *key : keys) {
        const QString value = object.value(QLatin1String(key)).toString().trimmed();
        if (!value.isEmpty()) {
            return value;
        }
    }
    return {};
}

QString extractRequestId(const QJsonObject &message)
{
    return message.value(QStringLiteral("request_id")).toString().trimmed();
}

QString extractRemoteUserId(const QJsonObject &message)
{
    return firstString(message, {"from_user_id", "userId", "peer_id", "user_id", "member_id"});
}

QString extractStateSyncValue(const QJsonObject &message)
{
    if (message.contains(QStringLiteral("payload")) && message.value(QStringLiteral("payload")).isObject()) {
        const QJsonObject payload = message.value(QStringLiteral("payload")).toObject();
        const QString payloadState = payload.value(QStringLiteral("state")).toString().trimmed();
        if (!payloadState.isEmpty()) {
            return payloadState;
        }
    }
    return message.value(QStringLiteral("state")).toString().trimmed();
}

bool messageTargetsLocalUser(const QJsonObject &message, const QString &localUserId)
{
    if (!message.contains(QStringLiteral("to_user_id"))) {
        return true;
    }

    const QString toUserId = message.value(QStringLiteral("to_user_id")).toString().trimmed();
    return toUserId.isEmpty() || toUserId == localUserId || toUserId == QStringLiteral("*");
}

QString compactJson(const QJsonObject &object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

QString joinParamsDetail(const RtcJoinParams &params)
{
    if (!params.isValid()) {
        return QStringLiteral("{}");
    }
    return joinParamsSummary(params);
}

RtcJoinParams parseJoinParams(const QJsonObject &message)
{
    QJsonObject payload;
    if (message.contains(QStringLiteral("payload")) && message.value(QStringLiteral("payload")).isObject()) {
        payload = message.value(QStringLiteral("payload")).toObject();
    }

    RtcJoinParams params;
    params.livekitUrl = firstString(payload, {"livekit_url", "livekitUrl", "url", "signalingUrl"});
    params.roomName = firstString(payload, {"room_name", "roomName"});
    params.token = firstString(payload, {"token"});
    params.participantRole = firstString(payload, {"participant_role", "participantRole", "role"});
    params.raw = payload;
    return params;
}

bool isTerminalState(const LinkMicRoomState &state)
{
    return state.signalState() == state.resetStateForRole() ||
        state.signalState() == QStringLiteral("failed");
}

bool isRtcSignalType(const QString &type)
{
    return type == QStringLiteral("rtc.offer") ||
        type == QStringLiteral("rtc.answer") ||
        type == QStringLiteral("rtc.ice") ||
        type == QStringLiteral("rtc.ice-candidate");
}

}  // namespace

LinkMicSession::LinkMicSession()
{
    requestTimer_.setSingleShot(true);
    QObject::connect(&requestTimer_, &QTimer::timeout, [&]() {
        handleRequestTimeout();
    });
}

void LinkMicSession::setUpdateHandler(UpdateHandler handler)
{
    onUpdated_ = std::move(handler);
}

void LinkMicSession::setMessageSender(MessageSender sender)
{
    messageSender_ = std::move(sender);
}

void LinkMicSession::setRtcSignalHandler(RtcSignalHandler handler)
{
    rtcSignalHandler_ = std::move(handler);
}

void LinkMicSession::setTransportState(SignalingConnectionState state, const QString &detail)
{
    if (transportState_ == state && transportDetail_ == detail) {
        return;
    }

    transportState_ = state;
    transportDetail_ = detail;

    switch (state) {
    case SignalingConnectionState::connecting:
        roomState_.setConnectionState(state);
        appendEvent(QStringLiteral("transport"), QStringLiteral("signaling.connecting"), detail, roomState_.signalState());
        break;
    case SignalingConnectionState::connected:
        roomState_.setConnectionState(state);
        roomState_.markRegistered();
        appendEvent(QStringLiteral("transport"), QStringLiteral("signaling.connected"), detail, roomState_.signalState());
        break;
    case SignalingConnectionState::disconnected:
        stopRequestTimer();
        roomState_.markDisconnected();
        memberDirectory_.clear();
        appendEvent(QStringLiteral("transport"), QStringLiteral("signaling.disconnected"), detail, roomState_.signalState());
        break;
    case SignalingConnectionState::failed:
        stopRequestTimer();
        roomState_.failTransport(detail.isEmpty() ? QStringLiteral("business signaling failure") : detail);
        appendEvent(QStringLiteral("transport"),
                    QStringLiteral("signaling.failed"),
                    roomState_.lastError(),
                    roomState_.signalState(),
                    roomState_.activeRequestId(),
                    roomState_.remoteUserId());
        break;
    }

    notifyUpdated();
}

void LinkMicSession::processSignalMessage(const QJsonObject &message)
{
    handleIncomingMessage(message);
}

bool LinkMicSession::connectToRoom(const LinkMicSessionConfig &config)
{
    if (!config.isValid() || !messageSender_) {
        return false;
    }

    config_ = config;
    eventLog_.clear();
    memberDirectory_.clear();
    memberDirectory_.setLocalUserId(config.userId);
    roomState_.reset(config.role);
    stopRequestTimer();

    roomState_.setConnectionState(
        transportState_ == SignalingConnectionState::connected
            ? SignalingConnectionState::connected
            : SignalingConnectionState::connecting);
    appendEvent(QStringLiteral("local"),
                QStringLiteral("session.connect"),
                QStringLiteral("binding to business signaling transport"),
                roomState_.signalState());
    if (transportState_ == SignalingConnectionState::connected) {
        roomState_.markRegistered();
        appendEvent(QStringLiteral("transport"),
                    QStringLiteral("signaling.connected"),
                    transportDetail_.isEmpty()
                        ? QStringLiteral("business websocket joined room")
                        : transportDetail_,
                    roomState_.signalState());
    }

    notifyUpdated();
    return true;
}

void LinkMicSession::disconnectFromRoom()
{
    stopRequestTimer();
    roomState_.markDisconnected();
    memberDirectory_.clear();
    appendEvent(QStringLiteral("local"),
                QStringLiteral("session.disconnect"),
                QStringLiteral("disconnected from signaling service"),
                roomState_.signalState());
    notifyUpdated();
}

bool LinkMicSession::invite(const QString &targetUserId)
{
    const auto targetMember = memberDirectory_.memberById(targetUserId);
    const bool targetInLinkMic = targetMember.has_value() && targetMember->inLinkMic;
    if (targetUserId.trimmed() == config_.userId ||
        !roomState_.canInvite(targetUserId, targetInLinkMic)) {
        return false;
    }

    const QString requestId = roomState_.beginInvite(config_.userId, targetUserId);
    startRequestTimer(QStringLiteral("waiting for invite response"));
    return sendBusinessMessage(QStringLiteral("linkmic.invite"),
                               targetUserId,
                               requestId,
                               {
                                   {QStringLiteral("initiator_role"), config_.role},
                                   {QStringLiteral("display_name"), config_.displayName},
                                   {QStringLiteral("client_name"), QStringLiteral("stream_page")}
                               },
                               QStringLiteral("sent linkmic.invite"));
}

bool LinkMicSession::apply(const QString &targetUserId)
{
    const auto targetMember = memberDirectory_.memberById(targetUserId);
    const bool targetInLinkMic = targetMember.has_value() && targetMember->inLinkMic;
    if (targetUserId.trimmed() == config_.userId ||
        !roomState_.canApply(targetUserId, targetInLinkMic)) {
        return false;
    }

    const QString requestId = roomState_.beginApply(config_.userId, targetUserId);
    startRequestTimer(QStringLiteral("waiting for host response"));
    return sendBusinessMessage(QStringLiteral("linkmic.apply"),
                               targetUserId,
                               requestId,
                               {
                                   {QStringLiteral("initiator_role"), config_.role},
                                   {QStringLiteral("display_name"), config_.displayName},
                                   {QStringLiteral("client_name"), QStringLiteral("stream_page")}
                               },
                               QStringLiteral("sent linkmic.apply"));
}

bool LinkMicSession::accept()
{
    if (!roomState_.canAccept()) {
        return false;
    }

    const QString requestId = roomState_.activeRequestId();
    const QString remoteUserId = roomState_.remoteUserId();
    const QString acceptedFrom = roomState_.incomingRequestType();
    roomState_.handleOutgoingAccept();
    startRequestTimer(QStringLiteral("waiting for rtc.join-params"));

    return sendBusinessMessage(QStringLiteral("linkmic.accept"),
                               remoteUserId,
                               requestId,
                               {
                                   {QStringLiteral("decision"), QStringLiteral("accept")},
                                   {QStringLiteral("accepted_from"), acceptedFrom},
                                   {QStringLiteral("client_name"), QStringLiteral("stream_page")}
                               },
                               QStringLiteral("sent linkmic.accept"));
}

bool LinkMicSession::reject()
{
    if (!roomState_.canReject()) {
        return false;
    }

    const QString requestId = roomState_.activeRequestId();
    const QString remoteUserId = roomState_.remoteUserId();
    const QString rejectedFrom = roomState_.incomingRequestType();
    roomState_.handleOutgoingReject();
    stopRequestTimer();

    return sendBusinessMessage(QStringLiteral("linkmic.reject"),
                               remoteUserId,
                               requestId,
                               {
                                   {QStringLiteral("decision"), QStringLiteral("reject")},
                                   {QStringLiteral("rejected_from"), rejectedFrom},
                                   {QStringLiteral("client_name"), QStringLiteral("stream_page")}
                               },
                               QStringLiteral("sent linkmic.reject"));
}

bool LinkMicSession::hangup()
{
    if (!roomState_.canTerminate()) {
        return false;
    }

    const QString requestId = roomState_.activeRequestId();
    const QString remoteUserId = roomState_.remoteUserId();
    const QString currentState = roomState_.signalState();
    const bool shouldCancel =
        currentState == QStringLiteral("host-inviting") ||
        currentState == QStringLiteral("waiting-host");
    roomState_.handleOutgoingTerminate();
    stopRequestTimer();

    return sendBusinessMessage(
        shouldCancel ? QStringLiteral("linkmic.cancel") : QStringLiteral("linkmic.hangup"),
        remoteUserId,
        requestId,
        {
            {QStringLiteral("reason"), shouldCancel ? QStringLiteral("manual-cancel") : QStringLiteral("manual-hangup")},
            {QStringLiteral("client_name"), QStringLiteral("stream_page")}
        },
        shouldCancel ? QStringLiteral("sent linkmic.cancel") : QStringLiteral("sent linkmic.hangup"));
}

const LinkMicSessionConfig &LinkMicSession::config() const
{
    return config_;
}

const RoomMemberDirectory &LinkMicSession::memberDirectory() const
{
    return memberDirectory_;
}

const LinkMicRoomState &LinkMicSession::roomState() const
{
    return roomState_;
}

const QVector<LinkMicEventLogEntry> &LinkMicSession::eventLog() const
{
    return eventLog_;
}

void LinkMicSession::handleIncomingMessage(const QJsonObject &message)
{
    const QString type = message.value(QStringLiteral("type")).toString().trimmed();
    const QString requestId = extractRequestId(message);
    const QString remoteUserId = extractRemoteUserId(message);

    if (type == QStringLiteral("signal.error")) {
        stopRequestTimer();
        roomState_.recoverFromSignalError(firstString(message, {"message"}));
        memberDirectory_.clearLinkMicFlags();
        appendEvent(QStringLiteral("recv"),
                    type,
                    roomState_.lastError(),
                    roomState_.signalState(),
                    requestId,
                    remoteUserId);
        notifyUpdated();
        return;
    }

    if (type == QStringLiteral("room.member-list")) {
        memberDirectory_.replaceFromMessage(message);
        appendEvent(QStringLiteral("recv"),
                    type,
                    QStringLiteral("online_count=%1").arg(memberDirectory_.onlineCount()),
                    roomState_.signalState());
        notifyUpdated();
        return;
    }

    if (type == QStringLiteral("room.member-join")) {
        memberDirectory_.applyJoinFromMessage(message);
        appendEvent(QStringLiteral("recv"),
                    type,
                    QStringLiteral("member joined room"),
                    roomState_.signalState(),
                    requestId,
                    remoteUserId);
        notifyUpdated();
        return;
    }

    if (type == QStringLiteral("room.member-leave")) {
        memberDirectory_.applyLeaveFromMessage(message);
        appendEvent(QStringLiteral("recv"),
                    type,
                    QStringLiteral("member left room"),
                    roomState_.signalState(),
                    requestId,
                    remoteUserId);
        if (!roomState_.remoteUserId().isEmpty() && roomState_.remoteUserId() == remoteUserId) {
            stopRequestTimer();
            roomState_.handleHangupOrKick(roomState_.activeRequestId(), remoteUserId);
            memberDirectory_.clearLinkMicFlags();
            appendEvent(QStringLiteral("local"),
                        QStringLiteral("linkmic.reset"),
                        QStringLiteral("active partner left the room"),
                        roomState_.signalState(),
                        requestId,
                        remoteUserId);
        }
        notifyUpdated();
        return;
    }

    if (!messageTargetsLocalUser(message, config_.userId)) {
        return;
    }

    if (remoteUserId == config_.userId && !remoteUserId.isEmpty()) {
        return;
    }

    if (isRtcSignalType(type)) {
        appendEvent(QStringLiteral("recv"),
                    type,
                    compactJson(message),
                    roomState_.signalState(),
                    requestId,
                    remoteUserId);
        if (rtcSignalHandler_) {
            rtcSignalHandler_(message);
        }
        notifyUpdated();
        return;
    }

    if (!roomState_.matchesRequestId(requestId) &&
        (type.startsWith(QStringLiteral("linkmic.")) || type == QStringLiteral("rtc.join-params"))) {
        appendEvent(QStringLiteral("recv"),
                    type,
                    QStringLiteral("ignored message for another active request"),
                    roomState_.signalState(),
                    requestId,
                    remoteUserId);
        notifyUpdated();
        return;
    }

    if (type == QStringLiteral("linkmic.apply")) {
        stopRequestTimer();
        roomState_.handleIncomingApply(requestId, remoteUserId);
        appendEvent(QStringLiteral("recv"),
                    type,
                    QStringLiteral("incoming guest apply"),
                    roomState_.signalState(),
                    requestId,
                    remoteUserId);
        notifyUpdated();
        return;
    }

    if (type == QStringLiteral("linkmic.invite")) {
        stopRequestTimer();
        roomState_.handleIncomingInvite(requestId, remoteUserId);
        appendEvent(QStringLiteral("recv"),
                    type,
                    QStringLiteral("incoming host invite"),
                    roomState_.signalState(),
                    requestId,
                    remoteUserId);
        notifyUpdated();
        return;
    }

    if (type == QStringLiteral("linkmic.accept")) {
        roomState_.handleIncomingAccept(requestId, remoteUserId);
        startRequestTimer(QStringLiteral("waiting for rtc.join-params"));
        appendEvent(QStringLiteral("recv"),
                    type,
                    QStringLiteral("remote side accepted, waiting for rtc.join-params"),
                    roomState_.signalState(),
                    requestId,
                    remoteUserId);
        notifyUpdated();
        return;
    }

    if (type == QStringLiteral("rtc.join-params") || type == QStringLiteral("linkmic.connected")) {
        stopRequestTimer();
        const RtcJoinParams joinParams = parseJoinParams(message);
        roomState_.handleJoinParams(requestId, remoteUserId, joinParams);
        memberDirectory_.clearLinkMicFlags();
        memberDirectory_.setMemberLinkMicState(remoteUserId, true);
        appendEvent(QStringLiteral("recv"),
                    type,
                    joinParamsDetail(joinParams),
                    roomState_.signalState(),
                    requestId,
                    remoteUserId);
        notifyUpdated();
        return;
    }

    if (type == QStringLiteral("linkmic.reject") || type == QStringLiteral("linkmic.cancel")) {
        stopRequestTimer();
        roomState_.handleRejectOrCancel(requestId, remoteUserId);
        memberDirectory_.clearLinkMicFlags();
        appendEvent(QStringLiteral("recv"),
                    type,
                    QStringLiteral("request ended before RTC join"),
                    roomState_.signalState(),
                    requestId,
                    remoteUserId);
        notifyUpdated();
        return;
    }

    if (type == QStringLiteral("linkmic.hangup") || type == QStringLiteral("linkmic.kick")) {
        stopRequestTimer();
        roomState_.handleHangupOrKick(requestId, remoteUserId);
        memberDirectory_.clearLinkMicFlags();
        appendEvent(QStringLiteral("recv"),
                    type,
                    QStringLiteral("linkmic session ended"),
                    roomState_.signalState(),
                    requestId,
                    remoteUserId);
        notifyUpdated();
        return;
    }

    if (type == QStringLiteral("linkmic.state-sync")) {
        const QString syncedState = extractStateSyncValue(message);
        roomState_.handleStateSync(requestId, remoteUserId, syncedState);
        if (roomState_.signalState() == roomState_.activeStateForRole()) {
            memberDirectory_.clearLinkMicFlags();
            memberDirectory_.setMemberLinkMicState(roomState_.remoteUserId(), true);
        } else if (roomState_.signalState() == roomState_.resetStateForRole()) {
            memberDirectory_.clearLinkMicFlags();
            stopRequestTimer();
        }
        appendEvent(QStringLiteral("recv"),
                    type,
                    syncedState.isEmpty() ? compactJson(message) : syncedState,
                    roomState_.signalState(),
                    requestId,
                    remoteUserId);
        notifyUpdated();
        return;
    }

    appendEvent(QStringLiteral("recv"),
                type,
                compactJson(message),
                roomState_.signalState(),
                requestId,
                remoteUserId);
    notifyUpdated();
}

void LinkMicSession::handleRequestTimeout()
{
    const QString detail = roomState_.isWaitingJoinParams()
        ? QStringLiteral("timed out waiting for rtc.join-params")
        : (requestTimerReason_.trimmed().isEmpty() ? QStringLiteral("linkmic request timed out") : requestTimerReason_);
    roomState_.failSession(detail);
    appendEvent(QStringLiteral("local"),
                QStringLiteral("timer.timeout"),
                detail,
                roomState_.signalState(),
                roomState_.activeRequestId(),
                roomState_.remoteUserId());
    notifyUpdated();
}

void LinkMicSession::appendEvent(const QString &direction,
                                 const QString &type,
                                 const QString &detail,
                                 const QString &state,
                                 const QString &requestId,
                                 const QString &remoteUserId)
{
    if (eventLog_.size() >= 200) {
        eventLog_.removeFirst();
    }

    LinkMicEventLogEntry entry;
    entry.timestampUtc = QDateTime::currentDateTimeUtc();
    entry.direction = direction;
    entry.type = type;
    entry.detail = detail;
    entry.state = state;
    entry.requestId = requestId;
    entry.remoteUserId = remoteUserId;
    eventLog_.push_back(std::move(entry));
}

void LinkMicSession::notifyUpdated() const
{
    if (onUpdated_) {
        onUpdated_();
    }
}

void LinkMicSession::startRequestTimer(const QString &reason)
{
    requestTimerReason_ = reason;
    if (config_.requestTimeoutMs > 0) {
        requestTimer_.start(config_.requestTimeoutMs);
    }
}

void LinkMicSession::stopRequestTimer()
{
    requestTimerReason_.clear();
    requestTimer_.stop();
}

bool LinkMicSession::sendBusinessMessage(const QString &type,
                                         const QString &toUserId,
                                         const QString &requestId,
                                         const QJsonObject &payload,
                                         const QString &detail)
{
    appendEvent(QStringLiteral("send"), type, detail, roomState_.signalState(), requestId, toUserId);

    const bool sent = messageSender_(type, config_.roomId, config_.userId, toUserId, requestId, payload);
    if (!sent) {
        stopRequestTimer();
        roomState_.failSession(QStringLiteral("failed to send ") + type);
        appendEvent(QStringLiteral("local"),
                    QStringLiteral("send.failed"),
                    roomState_.lastError(),
                    roomState_.signalState(),
                    requestId,
                    toUserId);
        notifyUpdated();
        return false;
    }

    if (isTerminalState(roomState_)) {
        memberDirectory_.clearLinkMicFlags();
    }

    notifyUpdated();
    return true;
}

}  // namespace frontend::linkmic
