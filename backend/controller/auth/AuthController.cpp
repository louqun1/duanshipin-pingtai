#include "controller/auth/AuthController.hpp"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QUrl>

#include <filesystem>

namespace backend::controller::auth {

namespace {

QString apiBaseUrl()
{
    const QString configured = qEnvironmentVariable("FLASHPOINT_API_BASE_URL").trimmed();
    if (!configured.isEmpty()) {
        return configured;
    }

    return QStringLiteral("http://192.168.99.128:8080");
}

QString authLoginUrl()
{
    return apiBaseUrl() + QStringLiteral("/api/auth/login");
}

QString authRegisterUrl()
{
    return apiBaseUrl() + QStringLiteral("/api/auth/register");
}

QString authLogoutUrl()
{
    return apiBaseUrl() + QStringLiteral("/api/auth/logout");
}

QString meUrl()
{
    return apiBaseUrl() + QStringLiteral("/api/me");
}

QString sessionStorePath()
{
    QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dataPath.isEmpty()) {
        dataPath = QDir::currentPath() + QStringLiteral("/data");
    }

    std::error_code errorCode;
    std::filesystem::create_directories(
        std::filesystem::path(dataPath.toStdWString()),
        errorCode);
    return QDir(dataPath).filePath(QStringLiteral("flashpoint_session.json"));
}

QJsonObject parseReplyObject(const QByteArray &bytes)
{
    const QJsonDocument document = QJsonDocument::fromJson(bytes);
    if (!document.isObject()) {
        return QJsonObject();
    }
    return document.object();
}

QString extractErrorMessage(QNetworkReply *reply, const QJsonObject &object, const QString &fallback)
{
    Q_UNUSED(reply);
    const QString apiError = object.value(QStringLiteral("error")).toString();
    if (!apiError.isEmpty()) {
        return apiError;
    }

    return fallback;
}

}  // namespace

AuthController::AuthController(QObject *parent)
    : QObject(parent)
{
    networkManager_ = new QNetworkAccessManager(this);
}

QString AuthController::sessionToken() const
{
    return sessionToken_;
}

bool AuthController::isAuthenticated() const
{
    return !sessionToken_.trimmed().isEmpty();
}

void AuthController::requestLogin(const QString &username, const QString &password)
{
    QNetworkRequest request{QUrl(authLoginUrl())};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QJsonObject payload;
    payload.insert(QStringLiteral("username"), username.trimmed());
    payload.insert(QStringLiteral("password"), password);

    auto *reply = networkManager_->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleAuthReply(reply, AuthAction::Login);
    });
}

void AuthController::requestRegister(const QString &username, const QString &password, const QString &email)
{
    QNetworkRequest request{QUrl(authRegisterUrl())};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QJsonObject payload;
    payload.insert(QStringLiteral("username"), username.trimmed());
    payload.insert(QStringLiteral("password"), password);
    payload.insert(QStringLiteral("email"), email.trimmed());

    auto *reply = networkManager_->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleAuthReply(reply, AuthAction::Register);
    });
}

void AuthController::requestLogout()
{
    if (sessionToken_.isEmpty()) {
        clearPersistedSessionToken();
        emit logoutSucceeded(QStringLiteral("You are already signed out."));
        return;
    }

    QNetworkRequest request{QUrl(authLogoutUrl())};
    request.setRawHeader("Authorization", QByteArray("Bearer ") + sessionToken_.toUtf8());

    auto *reply = networkManager_->post(request, QByteArray());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleAuthReply(reply, AuthAction::Logout);
    });
}

void AuthController::restorePersistedSession()
{
    const QString persistedToken = loadPersistedSessionToken();
    if (persistedToken.isEmpty()) {
        return;
    }

    QNetworkRequest request{QUrl(meUrl())};
    request.setRawHeader("Authorization", QByteArray("Bearer ") + persistedToken.toUtf8());

    auto *reply = networkManager_->get(request);
    reply->setProperty("persistedToken", persistedToken);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleAuthReply(reply, AuthAction::Restore);
    });
}

void AuthController::handleAuthReply(QNetworkReply *reply, AuthAction action)
{
    if (!reply) {
        return;
    }

    const QByteArray responseBytes = reply->readAll();
    const QJsonObject responseObject = parseReplyObject(responseBytes);

    if (reply->error() != QNetworkReply::NoError) {
        const QString message = extractErrorMessage(reply, responseObject, reply->errorString());
        if (action == AuthAction::Restore) {
            clearPersistedSessionToken();
            sessionToken_.clear();
            reply->deleteLater();
            return;
        }

        if (action == AuthAction::Login) {
            emit loginFailed(message);
        } else if (action == AuthAction::Register) {
            emit registerFailed(message);
        } else if (action == AuthAction::Logout) {
            emit logoutFailed(message);
        }

        reply->deleteLater();
        return;
    }

    if (action == AuthAction::Logout) {
        sessionToken_.clear();
        clearPersistedSessionToken();
        const QString message = responseObject.value(QStringLiteral("message")).toString(QStringLiteral("Logout succeeded."));
        emit logoutSucceeded(message);
        reply->deleteLater();
        return;
    }

    const QJsonObject userObject = responseObject.value(QStringLiteral("user")).toObject();
    const QString username = userObject.value(QStringLiteral("username")).toString();
    const QString email = userObject.value(QStringLiteral("email")).toString();

    QString token = responseObject.value(QStringLiteral("accessToken")).toString();
    if (token.isEmpty()) {
        token = reply->property("persistedToken").toString();
    }

    if (!token.isEmpty()) {
        sessionToken_ = token;
        persistSessionToken(token);
    }

    if (action == AuthAction::Login) {
        emit loginSucceeded(username, email);
    } else if (action == AuthAction::Register) {
        emit registerSucceeded(username, email);
    } else if (action == AuthAction::Restore) {
        emit sessionRestored(username, email);
    }

    reply->deleteLater();
}

void AuthController::persistSessionToken(const QString &token)
{
    QFile file(sessionStorePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("accessToken"), token);
    file.write(QJsonDocument(payload).toJson(QJsonDocument::Compact));
}

void AuthController::clearPersistedSessionToken()
{
    QFile::remove(sessionStorePath());
}

QString AuthController::loadPersistedSessionToken() const
{
    QFile file(sessionStorePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }

    const QJsonObject payload = parseReplyObject(file.readAll());
    return payload.value(QStringLiteral("accessToken")).toString().trimmed();
}

}  // namespace backend::controller::auth
