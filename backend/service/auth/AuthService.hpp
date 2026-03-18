#pragma once

#include "repository/session/SessionRepository.hpp"
#include "repository/user/UserRepository.hpp"
#include "service/auth/AuthResult.hpp"

namespace backend::service::auth {

class AuthService
{
public:
    AuthService(
        backend::repository::user::UserRepository &userRepository,
        backend::repository::session::SessionRepository &sessionRepository);

    AuthResult login(const QString &username, const QString &password);
    AuthResult registerUser(const QString &username, const QString &password, const QString &email);
    AuthResult restorePersistedSession();
    AuthResult logout();

private:
    backend::repository::user::UserRepository &userRepository_;
    backend::repository::session::SessionRepository &sessionRepository_;
};

}  // namespace backend::service::auth
