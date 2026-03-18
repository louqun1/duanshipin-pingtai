#pragma once

#include "domain/user/User.hpp"

#include <QString>

#include <optional>

namespace backend::service::auth {

enum class AuthStatus {
    Success,
    InvalidCredentials,
    UserAlreadyExists,
    ValidationError,
    SessionNotFound,
    StorageError
};

struct AuthResult {
    AuthStatus status = AuthStatus::ValidationError;
    QString message;
    std::optional<backend::domain::user::User> user;

    [[nodiscard]] bool ok() const
    {
        return status == AuthStatus::Success;
    }
};

}  // namespace backend::service::auth
