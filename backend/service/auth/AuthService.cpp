#include "service/auth/AuthService.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QUuid>

namespace backend::service::auth {

namespace {

constexpr int kSessionLifetimeDays = 30;//constexpr  常量表达式  在编译时求值  提高性能  C++ 11引入

QString hashSessionToken(const QString &token)
{//将传入的会话令牌（token）进行SHA-256 哈希运算
    const QByteArray digest = QCryptographicHash::hash(token.toUtf8(), QCryptographicHash::Sha256);
    return QString::fromLatin1(digest.toHex());
}

backend::domain::session::Session buildSession(const QString &userId)
{
    const QDateTime now = QDateTime::currentDateTimeUtc();//返回当前UTC时间(UTC+时间差=本地时间)
    const QString rawToken =
        QUuid::createUuid().toString(QUuid::WithoutBraces)
        + QUuid::createUuid().toString(QUuid::WithoutBraces);

    return {
        QUuid::createUuid().toString(QUuid::WithoutBraces),
        userId,
        hashSessionToken(rawToken),
        now,
        now.addDays(kSessionLifetimeDays),
        now,
        std::nullopt
    };
}

}  // namespace

AuthService::AuthService(
    backend::repository::user::UserRepository &userRepository,
    backend::repository::session::SessionRepository &sessionRepository)
    : userRepository_(userRepository)
    , sessionRepository_(sessionRepository)
{
}

AuthResult AuthService::login(const QString &username, const QString &password)
{
    if (username.trimmed().isEmpty() || password.isEmpty()) {
        return {
            AuthStatus::ValidationError,
            "Username and password are required.",
            std::nullopt
        };
    }

    if (!userRepository_.validateCredentials(username, password)) {
        return {
            AuthStatus::InvalidCredentials,
            "Invalid username or password.",
            std::nullopt
        };
    }

    const auto user = userRepository_.findByUsername(username);
    if (!user.has_value()) {
        return {
            AuthStatus::InvalidCredentials,
            "Invalid username or password.",
            std::nullopt
        };
    }

    if (!sessionRepository_.revokeAll() || !sessionRepository_.create(buildSession(user->id))) {
        return {
            AuthStatus::StorageError,
            "Unable to persist the login session.",
            std::nullopt
        };
    }

    return {
        AuthStatus::Success,
        "Login succeeded.",
        user
    };
}

AuthResult AuthService::registerUser(const QString &username, const QString &password, const QString &email)
{
    if (username.trimmed().isEmpty() || password.isEmpty() || email.trimmed().isEmpty()) {
        return {
            AuthStatus::ValidationError,
            "Username, password, and email are required.",
            std::nullopt
        };
    }

    if (userRepository_.findByUsername(username).has_value()) {
        return {
            AuthStatus::UserAlreadyExists,
            "Username already exists.",
            std::nullopt
        };
    }

    const backend::domain::user::User user{
        QUuid::createUuid().toString(QUuid::WithoutBraces),
        username.trimmed(),
        email.trimmed(),
        ""
    };

    if (!userRepository_.save(user, password)) {
        return {
            AuthStatus::StorageError,
            "Unable to save the new user.",
            std::nullopt
        };
    }

    if (!sessionRepository_.revokeAll() || !sessionRepository_.create(buildSession(user.id))) {
        return {
            AuthStatus::StorageError,
            "Registration succeeded, but the login session could not be persisted.",
            std::nullopt
        };
    }

    return {
        AuthStatus::Success,
        "Registration succeeded.",
        user
    };
}

AuthResult AuthService::restorePersistedSession()
{
    const auto session = sessionRepository_.findActiveSession();
    if (!session.has_value()) {
        return {
            AuthStatus::SessionNotFound,
            "No persisted session was found.",
            std::nullopt
        };
    }

    const auto user = userRepository_.findById(session->userId);
    if (!user.has_value()) {
        sessionRepository_.revokeAll();
        return {
            AuthStatus::SessionNotFound,
            "The stored session is no longer valid.",
            std::nullopt
        };
    }

    return {
        AuthStatus::Success,
        "Restored persisted session.",
        user
    };
}

AuthResult AuthService::logout()
{
    if (!sessionRepository_.revokeAll()) {
        return {
            AuthStatus::StorageError,
            "Unable to clear the persisted session.",
            std::nullopt
        };
    }

    return {
        AuthStatus::Success,
        "Logout succeeded.",
        std::nullopt
    };
}

}  // namespace backend::service::auth
