#pragma once

#include <cstdint>

#include "linkmic/LinkMicTypes.hpp"

namespace frontend::linkmic {

class LinkMicRoomState {
public:
    LinkMicRoomState();

    void reset(const QString &role);
    void markRegistered();
    void markDisconnected();
    void setConnectionState(SignalingConnectionState state);
    void failTransport(const QString &error);
    void failSession(const QString &error);
    void recoverFromSignalError(const QString &error);

    [[nodiscard]] QString beginInvite(const QString &localUserId, const QString &targetUserId);
    [[nodiscard]] QString beginApply(const QString &localUserId, const QString &targetUserId);
    void handleOutgoingAccept();
    void handleOutgoingReject();
    void handleOutgoingTerminate();

    void handleIncomingApply(const QString &requestId, const QString &remoteUserId);
    void handleIncomingInvite(const QString &requestId, const QString &remoteUserId);
    void handleIncomingAccept(const QString &requestId, const QString &remoteUserId);
    void handleJoinParams(const QString &requestId, const QString &remoteUserId, const RtcJoinParams &joinParams);
    void handleRejectOrCancel(const QString &requestId, const QString &remoteUserId);
    void handleHangupOrKick(const QString &requestId, const QString &remoteUserId);
    void handleStateSync(const QString &requestId, const QString &remoteUserId, const QString &syncedState);

    [[nodiscard]] bool isRegistered() const;
    [[nodiscard]] bool isWaitingJoinParams() const;
    [[nodiscard]] bool hasJoinParams() const;
    [[nodiscard]] bool hasActiveRequest() const;
    [[nodiscard]] bool matchesRequestId(const QString &requestId) const;
    [[nodiscard]] bool canInvite(const QString &targetUserId, bool targetInLinkMic) const;
    [[nodiscard]] bool canApply(const QString &targetUserId, bool targetInLinkMic) const;
    [[nodiscard]] bool canAccept() const;
    [[nodiscard]] bool canReject() const;
    [[nodiscard]] bool canTerminate() const;

    [[nodiscard]] QString role() const;
    [[nodiscard]] QString signalState() const;
    [[nodiscard]] QString activeRequestId() const;
    [[nodiscard]] QString remoteUserId() const;
    [[nodiscard]] QString incomingRequestType() const;
    [[nodiscard]] QString lastError() const;
    [[nodiscard]] SignalingConnectionState connectionState() const;
    [[nodiscard]] const RtcJoinParams &joinParams() const;

    [[nodiscard]] QString resetStateForRole() const;
    [[nodiscard]] QString connectingStateForRole() const;
    [[nodiscard]] QString activeStateForRole() const;
    [[nodiscard]] QString endingStateForRole() const;

private:
    [[nodiscard]] bool isControllerRole() const;
    [[nodiscard]] bool canStartNewRequest() const;
    [[nodiscard]] QString nextRequestId(const QString &localUserId, const QString &targetUserId);
    void clearRequestState();

    QString role_;
    QString signalState_;
    QString lastError_;
    QString activeRequestId_;
    QString activeRequestType_;
    QString remoteUserId_;
    QString incomingRequestType_;
    RtcJoinParams joinParams_;
    SignalingConnectionState connectionState_ = SignalingConnectionState::disconnected;
    std::uint64_t requestSequence_ = 0;
    bool registered_ = false;
    bool waitingJoinParams_ = false;
    bool hasJoinParams_ = false;
};

}  // namespace frontend::linkmic
