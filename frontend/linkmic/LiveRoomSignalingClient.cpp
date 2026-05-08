#include "linkmic/LiveRoomSignalingClient.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

#ifdef FLASHPOINT_HAS_QT_WEBSOCKETS
#include <QAbstractSocket>
#include <QNetworkProxy>
#include <QWebSocket>
#endif

#include <spdlog/spdlog.h>

namespace frontend::linkmic {
namespace {

std::string toStdString(const QString &value)
{
    const QByteArray utf8 = value.toUtf8();
    return std::string(utf8.constData(), static_cast<std::size_t>(utf8.size()));
}

QString socketStateText(int stateValue)
{
    switch (stateValue) {
#ifdef FLASHPOINT_HAS_QT_WEBSOCKETS
    case QAbstractSocket::UnconnectedState:
        return QStringLiteral("unconnected");
    case QAbstractSocket::HostLookupState:
        return QStringLiteral("host-lookup");
    case QAbstractSocket::ConnectingState:
        return QStringLiteral("connecting");
    case QAbstractSocket::ConnectedState:
        return QStringLiteral("connected");
    case QAbstractSocket::BoundState:
        return QStringLiteral("bound");
    case QAbstractSocket::ClosingState:
        return QStringLiteral("closing");
    case QAbstractSocket::ListeningState:
        return QStringLiteral("listening");
#else
    Q_UNUSED(stateValue);
#endif
    default:
        return QStringLiteral("unknown");
    }
}

qint64 currentTimestampMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

QString canonicalSignalType(const QString &type)
{
    if (type == QStringLiteral("rtc.ice")) {
        return QStringLiteral("rtc.ice-candidate");
    }
    return type.trimmed();
}

QJsonObject buildBusinessEnvelope(const QString &type,
                                  const QString &roomKey,
                                  const QString &fromUserId,
                                  const QString &toUserId,
                                  const QString &requestId,
                                  const QJsonObject &payload)
{
    return QJsonObject{
        {QStringLiteral("type"), type},
        {QStringLiteral("roomKey"), roomKey},
        {QStringLiteral("from_user_id"), fromUserId},
        {QStringLiteral("to_user_id"), toUserId},
        {QStringLiteral("request_id"), requestId},
        {QStringLiteral("payload"), payload},
        {QStringLiteral("ts_ms"), currentTimestampMs()}
    };
}

QString extractMessageType(const QJsonObject &message)
{
    return message.value(QStringLiteral("type")).toString().trimmed();
}

QString extractRequestId(const QJsonObject &message)
{
    return message.value(QStringLiteral("request_id")).toString().trimmed();
}

QString extractToUserId(const QJsonObject &message)
{
    return message.value(QStringLiteral("to_user_id")).toString().trimmed();
}

bool isBusinessRtcType(const QString &type)
{
    return type == QStringLiteral("rtc.offer") ||
        type == QStringLiteral("rtc.answer") ||
        type == QStringLiteral("rtc.ice") ||
        type == QStringLiteral("rtc.ice-candidate");
}

}  // namespace

LiveRoomSignalingClient::LiveRoomSignalingClient(QObject *parent)
    : QObject(parent)
{
    heartbeatTimer_.setSingleShot(false);
    QObject::connect(&heartbeatTimer_, &QTimer::timeout, this, [this]() {
        sendHeartbeat();
    });

#ifdef FLASHPOINT_HAS_QT_WEBSOCKETS
    socket_ = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    socket_->setProxy(QNetworkProxy::NoProxy);
    spdlog::info("[stream/signal] websocket initialized proxy=NoProxy");

    QObject::connect(socket_, &QWebSocket::connected, this, [this]() {
        handleSocketConnected();
    });
    QObject::connect(socket_, &QWebSocket::disconnected, this, [this]() {
        handleSocketDisconnected();
    });
    QObject::connect(socket_, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        handleSocketError(socket_ ? socket_->errorString() : QStringLiteral("unknown websocket error"));
    });
    QObject::connect(socket_, &QWebSocket::textMessageReceived, this, [this](const QString &text) {
        handleSocketTextMessage(text);
    });
#endif
}

LiveRoomSignalingClient::~LiveRoomSignalingClient()
{
    disconnectFromRoom(QStringLiteral("stream page destroyed"));
}

bool LiveRoomSignalingClient::isAvailable() const
{
#ifdef FLASHPOINT_HAS_QT_WEBSOCKETS
    return true;
#else
    return false;
#endif
}

LiveRoomSignalingClient::State LiveRoomSignalingClient::state() const
{
    return state_;
}

QString LiveRoomSignalingClient::stateText() const
{
    switch (state_) {
    case State::Disconnected:
        return QStringLiteral("Disconnected");
    case State::Connecting:
        return QStringLiteral("Connecting");
    case State::Joined:
        return QStringLiteral("Joined");
    case State::Error:
        return QStringLiteral("Error");
    }
    return QStringLiteral("Disconnected");
}

QString LiveRoomSignalingClient::lastError() const
{
    return lastError_;
}

QString LiveRoomSignalingClient::activeRoomKey() const
{
    return config_.roomKey;
}

QString LiveRoomSignalingClient::currentUserId() const
{
    return config_.currentUserId;
}

QString LiveRoomSignalingClient::roomRole() const
{
    return config_.roomRole;
}

bool LiveRoomSignalingClient::isJoined() const
{
    return state_ == State::Joined;
}

void LiveRoomSignalingClient::connectToRoom(const Config &config)
{
    config_ = config;
    lastError_.clear();
    lastDetail_.clear();
    pendingCloseReason_.clear();
    leaveSent_ = false;

    if (config_.heartbeatIntervalMs > 0) {
        heartbeatTimer_.setInterval(config_.heartbeatIntervalMs);
    } else {
        heartbeatTimer_.setInterval(60 * 1000);
    }

    if (!config_.isValid()) {
        transitionState(State::Error, QStringLiteral("business signaling config is incomplete"));
        return;
    }

#ifndef FLASHPOINT_HAS_QT_WEBSOCKETS
    transitionState(State::Error, QStringLiteral("Qt6 WebSockets is not available in this build"));
    return;
#else
    if (!socket_) {
        transitionState(State::Error, QStringLiteral("websocket client is not initialized"));
        return;
    }

    heartbeatTimer_.stop();
    if (socket_->state() != QAbstractSocket::UnconnectedState) {
        socket_->abort();
    }

    spdlog::info(
        "[stream/signal] ws connecting roomKey={} currentUserId={} role={} wsState={} signalingUrl={}",
        toStdString(config_.roomKey),
        toStdString(config_.currentUserId),
        toStdString(config_.roomRole),
        toStdString(socketStateText(static_cast<int>(socket_->state()))),
        toStdString(config_.signalingUrl.toString(QUrl::FullyEncoded)));
    transitionState(State::Connecting, QStringLiteral("Opening business WebSocket"));
    socket_->open(config_.signalingUrl);
#endif
}

void LiveRoomSignalingClient::disconnectFromRoom(const QString &reason)
{
    heartbeatTimer_.stop();
    pendingCloseReason_ = reason.trimmed();

#ifdef FLASHPOINT_HAS_QT_WEBSOCKETS
    if (socket_ && socket_->state() == QAbstractSocket::ConnectedState) {
        sendLeave();
        socket_->flush();
        socket_->close();
        return;
    }
    if (socket_ && socket_->state() != QAbstractSocket::UnconnectedState) {
        socket_->close();
        return;
    }
#endif

    if (state_ != State::Disconnected) {
        transitionState(State::Disconnected,
                        pendingCloseReason_.isEmpty()
                            ? QStringLiteral("Business WebSocket closed")
                            : pendingCloseReason_);
    }
}

bool LiveRoomSignalingClient::sendBusinessMessage(const QString &type,
                                                  const QString &fromUserId,
                                                  const QString &toUserId,
                                                  const QString &requestId,
                                                  const QJsonObject &payload)
{
    const QString messageType = canonicalSignalType(type);
    if (messageType.isEmpty() || !isJoined()) {
        return false;
    }

    spdlog::info(
        "[stream/signal] send {} roomKey={} currentUserId={} targetUserId={} requestId={} role={} wsState={}",
        toStdString(messageType),
        toStdString(config_.roomKey),
        toStdString(config_.currentUserId),
        toStdString(toUserId),
        toStdString(requestId),
        toStdString(config_.roomRole),
#ifdef FLASHPOINT_HAS_QT_WEBSOCKETS
        toStdString(socketStateText(static_cast<int>(socket_ ? socket_->state() : QAbstractSocket::UnconnectedState))));
#else
        "unavailable");
#endif

    return sendRawMessage(
        buildBusinessEnvelope(messageType, config_.roomKey, fromUserId, toUserId, requestId, payload));
}

void LiveRoomSignalingClient::transitionState(State state, const QString &detail)
{
    state_ = state;
    lastDetail_ = detail.trimmed();
    if (state != State::Error) {
        lastError_.clear();
    } else if (lastError_.isEmpty()) {
        lastError_ = lastDetail_;
    }
    emit stateChanged(state_, lastDetail_);
}

void LiveRoomSignalingClient::sendJoin()
{
    // The backend still uses the historical live.anchor.join message name.
    // Product semantics here are "user joins the live room business websocket".
    // Both the room owner and ordinary viewers must send this join so the server
    // can bind businessRooms[roomKey].peers[userId] for routable linkmic signals.
    QJsonObject joinMessage{
        {QStringLiteral("type"), QStringLiteral("live.anchor.join")},
        {QStringLiteral("token"), config_.accessToken},
        {QStringLiteral("roomKey"), config_.roomKey},
        {QStringLiteral("role"), config_.joinRole.isEmpty() ? QStringLiteral("audience") : config_.joinRole}
    };

    spdlog::info(
        "[stream/signal] send live room join roomKey={} currentUserId={} requestId={} role={} wsState={}",
        toStdString(config_.roomKey),
        toStdString(config_.currentUserId),
        "",
        toStdString(config_.roomRole),
#ifdef FLASHPOINT_HAS_QT_WEBSOCKETS
        toStdString(socketStateText(static_cast<int>(socket_ ? socket_->state() : QAbstractSocket::UnconnectedState))));
#else
        "unavailable");
#endif

    sendRawMessage(joinMessage);
}

void LiveRoomSignalingClient::sendHeartbeat()
{
    if (!isJoined()) {
        return;
    }

    QJsonObject heartbeatMessage{
        {QStringLiteral("type"), QStringLiteral("live.anchor.heartbeat")},
        {QStringLiteral("roomKey"), config_.roomKey}
    };

    spdlog::info(
        "[stream/signal] heartbeat roomKey={} currentUserId={} requestId={} role={} wsState={}",
        toStdString(config_.roomKey),
        toStdString(config_.currentUserId),
        "",
        toStdString(config_.roomRole),
#ifdef FLASHPOINT_HAS_QT_WEBSOCKETS
        toStdString(socketStateText(static_cast<int>(socket_ ? socket_->state() : QAbstractSocket::UnconnectedState))));
#else
        "unavailable");
#endif

    sendRawMessage(heartbeatMessage);
}

void LiveRoomSignalingClient::sendLeave()
{
    if (leaveSent_ || config_.roomKey.trimmed().isEmpty()) {
        return;
    }

    leaveSent_ = true;
    QJsonObject leaveMessage{
        {QStringLiteral("type"), QStringLiteral("live.anchor.leave")},
        {QStringLiteral("roomKey"), config_.roomKey}
    };

    spdlog::info(
        "[stream/signal] leave roomKey={} currentUserId={} requestId={} role={} wsState={}",
        toStdString(config_.roomKey),
        toStdString(config_.currentUserId),
        "",
        toStdString(config_.roomRole),
#ifdef FLASHPOINT_HAS_QT_WEBSOCKETS
        toStdString(socketStateText(static_cast<int>(socket_ ? socket_->state() : QAbstractSocket::UnconnectedState))));
#else
        "unavailable");
#endif

    sendRawMessage(leaveMessage);
}

bool LiveRoomSignalingClient::sendRawMessage(const QJsonObject &message)
{
#ifndef FLASHPOINT_HAS_QT_WEBSOCKETS
    Q_UNUSED(message);
    lastError_ = QStringLiteral("Qt6 WebSockets is not available in this build");
    return false;
#else
    if (!socket_ || socket_->state() != QAbstractSocket::ConnectedState) {
        lastError_ = QStringLiteral("business websocket is not connected");
        return false;
    }

    const QString text = QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact));
    if (socket_->sendTextMessage(text) < 0) {
        lastError_ = QStringLiteral("failed to send websocket message");
        return false;
    }
    return true;
#endif
}

void LiveRoomSignalingClient::handleSocketConnected()
{
    spdlog::info(
        "[stream/signal] ws open roomKey={} currentUserId={} role={} wsState={}",
        toStdString(config_.roomKey),
        toStdString(config_.currentUserId),
        toStdString(config_.roomRole),
#ifdef FLASHPOINT_HAS_QT_WEBSOCKETS
        toStdString(socketStateText(static_cast<int>(socket_ ? socket_->state() : QAbstractSocket::UnconnectedState))));
#else
        "unavailable");
#endif

    transitionState(State::Connecting, QStringLiteral("Business WebSocket open, waiting for join ack"));
    sendJoin();
}

void LiveRoomSignalingClient::handleSocketDisconnected()
{
    heartbeatTimer_.stop();
    leaveSent_ = false;
    const QString detail = pendingCloseReason_.isEmpty()
        ? QStringLiteral("Business WebSocket disconnected")
        : pendingCloseReason_;

#ifdef FLASHPOINT_HAS_QT_WEBSOCKETS
    spdlog::warn(
        "[stream/signal] ws disconnected roomKey={} currentUserId={} role={} wsState={} detail={}",
        toStdString(config_.roomKey),
        toStdString(config_.currentUserId),
        toStdString(config_.roomRole),
        toStdString(socketStateText(static_cast<int>(socket_ ? socket_->state() : QAbstractSocket::UnconnectedState))),
        toStdString(detail));
#endif

    pendingCloseReason_.clear();
    if (state_ != State::Error) {
        transitionState(State::Disconnected, detail);
    }
}

void LiveRoomSignalingClient::handleSocketError(const QString &errorText)
{
    heartbeatTimer_.stop();
    lastError_ = errorText.trimmed().isEmpty()
        ? QStringLiteral("unknown websocket error")
        : errorText.trimmed();

    spdlog::error(
        "[stream/signal] ws error roomKey={} currentUserId={} role={} wsState={} message={}",
        toStdString(config_.roomKey),
        toStdString(config_.currentUserId),
        toStdString(config_.roomRole),
#ifdef FLASHPOINT_HAS_QT_WEBSOCKETS
        toStdString(socketStateText(static_cast<int>(socket_ ? socket_->state() : QAbstractSocket::UnconnectedState))),
#else
        "unavailable",
#endif
        toStdString(lastError_));
    transitionState(State::Error, lastError_);
}

void LiveRoomSignalingClient::handleSocketTextMessage(const QString &text)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        handleSocketError(QStringLiteral("invalid websocket json: %1").arg(parseError.errorString()));
        return;
    }

    const QJsonObject message = document.object();
    const QString type = extractMessageType(message);

    if (type == QStringLiteral("live.anchor.joined") ||
        type == QStringLiteral("live.room.joined"))
    {
        leaveSent_ = false;
        heartbeatTimer_.start();
        config_.currentUserId = QString::number(message.value(QStringLiteral("userId")).toInteger(config_.currentUserId.toLongLong()));
        spdlog::info(
            "[stream/signal] joined room roomKey={} currentUserId={} requestId={} role={} wsState={}",
            toStdString(config_.roomKey),
            toStdString(config_.currentUserId),
            "",
            toStdString(config_.roomRole),
#ifdef FLASHPOINT_HAS_QT_WEBSOCKETS
            toStdString(socketStateText(static_cast<int>(socket_ ? socket_->state() : QAbstractSocket::UnconnectedState))));
#else
            "unavailable");
#endif
        transitionState(State::Joined, QStringLiteral("Business WebSocket joined room"));
        emit messageReceived(message);
        return;
    }

    if (type == QStringLiteral("room.member-list")) {
        const int memberCount = message.value(QStringLiteral("members")).toArray().size() > 0
            ? message.value(QStringLiteral("members")).toArray().size()
            : message.value(QStringLiteral("peers")).toArray().size();
        spdlog::info(
            "[stream/signal] recv room.member-list roomKey={} currentUserId={} targetUserId={} requestId={} role={} wsState={} members={}",
            toStdString(config_.roomKey),
            toStdString(config_.currentUserId),
            "",
            "",
            toStdString(config_.roomRole),
#ifdef FLASHPOINT_HAS_QT_WEBSOCKETS
            toStdString(socketStateText(static_cast<int>(socket_ ? socket_->state() : QAbstractSocket::UnconnectedState))),
#else
            "unavailable",
#endif
            memberCount);
    } else if (type == QStringLiteral("signal.error")) {
        spdlog::warn(
            "[stream/signal] recv signal.error roomKey={} currentUserId={} targetUserId={} requestId={} role={} wsState={} message={}",
            toStdString(config_.roomKey),
            toStdString(config_.currentUserId),
            toStdString(extractToUserId(message)),
            toStdString(extractRequestId(message)),
            toStdString(config_.roomRole),
#ifdef FLASHPOINT_HAS_QT_WEBSOCKETS
            toStdString(socketStateText(static_cast<int>(socket_ ? socket_->state() : QAbstractSocket::UnconnectedState))),
#else
            "unavailable",
#endif
            toStdString(message.value(QStringLiteral("message")).toString().trimmed()));
    } else if (isBusinessRtcType(type)) {
        spdlog::info(
            "[stream/signal] recv {} roomKey={} currentUserId={} targetUserId={} requestId={} role={} wsState={}",
            toStdString(type),
            toStdString(config_.roomKey),
            toStdString(config_.currentUserId),
            toStdString(extractToUserId(message)),
            toStdString(extractRequestId(message)),
            toStdString(config_.roomRole),
#ifdef FLASHPOINT_HAS_QT_WEBSOCKETS
            toStdString(socketStateText(static_cast<int>(socket_ ? socket_->state() : QAbstractSocket::UnconnectedState))));
#else
            "unavailable");
#endif
    }

    if (type == QStringLiteral("live.anchor.error") ||
        type == QStringLiteral("live.room.error"))
    {
        lastError_ = message.value(QStringLiteral("message")).toString().trimmed();
        if (lastError_.isEmpty()) {
            lastError_ = QStringLiteral("business websocket join failed");
        }
        transitionState(State::Error, lastError_);
    }

    emit messageReceived(message);
}

}  // namespace frontend::linkmic
