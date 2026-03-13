#include "service/auth/AuthService.hpp"

namespace backend::service::auth {

AuthService::AuthService(backend::repository::user::UserRepository &userRepository)
    : userRepository_(userRepository)
{
}

AuthResult AuthService::login(const QString &username, const QString &password) const
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

    return {
        AuthStatus::Success,
        "Login succeeded.",
        userRepository_.findByUsername(username)
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
        username.trimmed(),
        username.trimmed(),
        email.trimmed(),
        ""
    };

    userRepository_.save(user, password);

    return {
        AuthStatus::Success,
        "Registration succeeded.",
        user
    };
}

}  // namespace backend::service::auth
