#include "linkmic/LinkMicRoomState.hpp"

#include <QDateTime>

namespace frontend::linkmic {

LinkMicRoomState::LinkMicRoomState()
{
    reset(QStringLiteral("controller"));
}

void LinkMicRoomState::reset(const QString &role)
{
    role_ = role.trimmed().isEmpty() ? QStringLiteral("controller") : role.trimmed();
    signalState_ = resetStateForRole();
    lastError_.clear();
    connectionState_ = SignalingConnectionState::disconnected;
    registered_ = false;
    clearRequestState();
}

void LinkMicRoomState::markRegistered()
{
    registered_ = true;
    lastError_.clear();
}

void LinkMicRoomState::markDisconnected()
{
    connectionState_ = SignalingConnectionState::disconnected;
    registered_ = false;
    signalState_ = resetStateForRole();
    lastError_.clear();
    clearRequestState();
}

void LinkMicRoomState::setConnectionState(SignalingConnectionState state)
{
    connectionState_ = state;
    if (state == SignalingConnectionState::connected) {
        lastError_.clear();
    }
}

void LinkMicRoomState::failTransport(const QString &error)
{
    connectionState_ = SignalingConnectionState::failed;
    signalState_ = QStringLiteral("failed");
    lastError_ = error.trimmed();
}

void LinkMicRoomState::failSession(const QString &error)
{
    signalState_ = QStringLiteral("failed");
    lastError_ = error.trimmed();
    waitingJoinParams_ = false;
}

void LinkMicRoomState::recoverFromSignalError(const QString &error)
{
    signalState_ = resetStateForRole();
    lastError_ = error.trimmed();
    waitingJoinParams_ = false;
    hasJoinParams_ = false;
    joinParams_ = {};
    clearRequestState();
}

QString LinkMicRoomState::beginInvite(const QString &localUserId, const QString &targetUserId)
{
    lastError_.clear();
    activeRequestId_ = nextRequestId(localUserId, targetUserId);
    activeRequestType_ = QStringLiteral("linkmic.invite");
    remoteUserId_ = targetUserId.trimmed();
    incomingRequestType_.clear();
    waitingJoinParams_ = false;
    hasJoinParams_ = false;
    joinParams_ = {};
    signalState_ = QStringLiteral("host-inviting");
    return activeRequestId_;
}

QString LinkMicRoomState::beginApply(const QString &localUserId, const QString &targetUserId)
{
    lastError_.clear();
    activeRequestId_ = nextRequestId(localUserId, targetUserId);
    activeRequestType_ = QStringLiteral("linkmic.apply");
    remoteUserId_ = targetUserId.trimmed();
    incomingRequestType_.clear();
    waitingJoinParams_ = false;
    hasJoinParams_ = false;
    joinParams_ = {};
    signalState_ = QStringLiteral("waiting-host");
    return activeRequestId_;
}

void LinkMicRoomState::handleOutgoingAccept()
{
    signalState_ = connectingStateForRole();
    incomingRequestType_.clear();
    waitingJoinParams_ = true;
    hasJoinParams_ = false;
    joinParams_ = {};
    lastError_.clear();
}

void LinkMicRoomState::handleOutgoingReject()
{
    signalState_ = resetStateForRole();
    lastError_.clear();
    clearRequestState();
}

void LinkMicRoomState::handleOutgoingTerminate()
{
    signalState_ = endingStateForRole();
    waitingJoinParams_ = false;
    lastError_.clear();
}

void LinkMicRoomState::handleIncomingApply(const QString &requestId, const QString &remoteUserId)
{
    lastError_.clear();
    activeRequestId_ = requestId.trimmed();
    activeRequestType_ = QStringLiteral("linkmic.apply");
    incomingRequestType_ = QStringLiteral("linkmic.apply");
    remoteUserId_ = remoteUserId.trimmed();
    waitingJoinParams_ = false;
    hasJoinParams_ = false;
    joinParams_ = {};
    signalState_ = QStringLiteral("guest-applying");
}

void LinkMicRoomState::handleIncomingInvite(const QString &requestId, const QString &remoteUserId)
{
    lastError_.clear();
    activeRequestId_ = requestId.trimmed();
    activeRequestType_ = QStringLiteral("linkmic.invite");
    incomingRequestType_ = QStringLiteral("linkmic.invite");
    remoteUserId_ = remoteUserId.trimmed();
    waitingJoinParams_ = false;
    hasJoinParams_ = false;
    joinParams_ = {};
    signalState_ = QStringLiteral("guest-invited");
}

void LinkMicRoomState::handleIncomingAccept(const QString &requestId, const QString &remoteUserId)
{
    if (!requestId.trimmed().isEmpty()) {
        activeRequestId_ = requestId.trimmed();
    }
    if (!remoteUserId.trimmed().isEmpty()) {
        remoteUserId_ = remoteUserId.trimmed();
    }
    incomingRequestType_.clear();
    waitingJoinParams_ = true;
    hasJoinParams_ = false;
    joinParams_ = {};
    lastError_.clear();
    signalState_ = connectingStateForRole();
}

void LinkMicRoomState::handleJoinParams(
    const QString &requestId,
    const QString &remoteUserId,
    const RtcJoinParams &joinParams)
{
    if (!requestId.trimmed().isEmpty()) {
        activeRequestId_ = requestId.trimmed();
    }
    if (!remoteUserId.trimmed().isEmpty()) {
        remoteUserId_ = remoteUserId.trimmed();
    }
    joinParams_ = joinParams;
    hasJoinParams_ = joinParams.isValid();
    waitingJoinParams_ = false;
    incomingRequestType_.clear();
    lastError_.clear();
    signalState_ = activeStateForRole();
}

void LinkMicRoomState::handleRejectOrCancel(const QString &requestId, const QString &remoteUserId)
{
    if (!requestId.trimmed().isEmpty()) {
        activeRequestId_ = requestId.trimmed();
    }
    if (!remoteUserId.trimmed().isEmpty()) {
        remoteUserId_ = remoteUserId.trimmed();
    }

    signalState_ = resetStateForRole();
    lastError_.clear();
    clearRequestState();
}

void LinkMicRoomState::handleHangupOrKick(const QString &requestId, const QString &remoteUserId)
{
    if (!requestId.trimmed().isEmpty()) {
        activeRequestId_ = requestId.trimmed();
    }
    if (!remoteUserId.trimmed().isEmpty()) {
        remoteUserId_ = remoteUserId.trimmed();
    }

    signalState_ = resetStateForRole();
    lastError_.clear();
    clearRequestState();
}

void LinkMicRoomState::handleStateSync(
    const QString &requestId,
    const QString &remoteUserId,
    const QString &syncedState)
{
    if (!requestId.trimmed().isEmpty()) {
        activeRequestId_ = requestId.trimmed();
    }
    if (!remoteUserId.trimmed().isEmpty()) {
        remoteUserId_ = remoteUserId.trimmed();
    }

    const QString nextState = syncedState.trimmed();
    if (nextState.isEmpty()) {
        return;
    }

    signalState_ = nextState;
    lastError_.clear();

    if (nextState == resetStateForRole()) {
        clearRequestState();
        return;
    }

    if (nextState == activeStateForRole()) {
        waitingJoinParams_ = false;
    }
}

bool LinkMicRoomState::isRegistered() const
{
    return registered_;
}

bool LinkMicRoomState::isWaitingJoinParams() const
{
    return waitingJoinParams_;
}

bool LinkMicRoomState::hasJoinParams() const
{
    return hasJoinParams_;
}

bool LinkMicRoomState::hasActiveRequest() const
{
    return !activeRequestId_.isEmpty();
}

bool LinkMicRoomState::matchesRequestId(const QString &requestId) const
{
    if (activeRequestId_.isEmpty()) {
        return true;
    }
    if (requestId.trimmed().isEmpty()) {
        return false;
    }
    return activeRequestId_ == requestId.trimmed();
}

bool LinkMicRoomState::canInvite(const QString &targetUserId, bool targetInLinkMic) const
{
    return isControllerRole() &&
        canStartNewRequest() &&
        !targetUserId.trimmed().isEmpty() &&
        !targetInLinkMic;
}

bool LinkMicRoomState::canApply(const QString &targetUserId, bool targetInLinkMic) const
{
    return !isControllerRole() &&
        canStartNewRequest() &&
        !targetUserId.trimmed().isEmpty() &&
        !targetInLinkMic;
}

bool LinkMicRoomState::canAccept() const
{
    return !activeRequestId_.isEmpty() &&
        (signalState_ == QStringLiteral("guest-applying") ||
         signalState_ == QStringLiteral("guest-invited"));
}

bool LinkMicRoomState::canReject() const
{
    return canAccept();
}

bool LinkMicRoomState::canTerminate() const
{
    if (activeRequestId_.isEmpty()) {
        return false;
    }

    return signalState_ == QStringLiteral("host-inviting") ||
        signalState_ == QStringLiteral("waiting-host") ||
        signalState_ == connectingStateForRole() ||
        signalState_ == activeStateForRole() ||
        signalState_ == endingStateForRole();
}

QString LinkMicRoomState::role() const
{
    return role_;
}

QString LinkMicRoomState::signalState() const
{
    return signalState_;
}

QString LinkMicRoomState::activeRequestId() const
{
    return activeRequestId_;
}

QString LinkMicRoomState::remoteUserId() const
{
    return remoteUserId_;
}

QString LinkMicRoomState::incomingRequestType() const
{
    return incomingRequestType_;
}

QString LinkMicRoomState::lastError() const
{
    return lastError_;
}

SignalingConnectionState LinkMicRoomState::connectionState() const
{
    return connectionState_;
}

const RtcJoinParams &LinkMicRoomState::joinParams() const
{
    return joinParams_;
}

QString LinkMicRoomState::resetStateForRole() const
{
    return isControllerRole() ? QStringLiteral("live-only") : QStringLiteral("idle");
}

QString LinkMicRoomState::connectingStateForRole() const
{
    return isControllerRole() ? QStringLiteral("linkmic-connecting") : QStringLiteral("rtc-connecting");
}

QString LinkMicRoomState::activeStateForRole() const
{
    return isControllerRole() ? QStringLiteral("linkmic-active") : QStringLiteral("rtc-active");
}

QString LinkMicRoomState::endingStateForRole() const
{
    return isControllerRole() ? QStringLiteral("linkmic-ending") : QStringLiteral("ending");
}

bool LinkMicRoomState::isControllerRole() const
{
    return role_.compare(QStringLiteral("controller"), Qt::CaseInsensitive) == 0;
}

bool LinkMicRoomState::canStartNewRequest() const
{
    return registered_ &&
        connectionState_ == SignalingConnectionState::connected &&
        activeRequestId_.isEmpty() &&
        signalState_ == resetStateForRole();
}

QString LinkMicRoomState::nextRequestId(const QString &localUserId, const QString &targetUserId)
{
    ++requestSequence_;
    return QStringLiteral("stream-%1-to-%2-%3-%4")
        .arg(localUserId.trimmed())
        .arg(targetUserId.trimmed())
        .arg(QDateTime::currentMSecsSinceEpoch())
        .arg(requestSequence_);
}

void LinkMicRoomState::clearRequestState()
{
    activeRequestId_.clear();
    activeRequestType_.clear();
    remoteUserId_.clear();
    incomingRequestType_.clear();
    waitingJoinParams_ = false;
    hasJoinParams_ = false;
    joinParams_ = {};
}

}  // namespace frontend::linkmic
