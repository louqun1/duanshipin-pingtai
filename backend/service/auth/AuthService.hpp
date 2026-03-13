#pragma once

#include "repository/user/UserRepository.hpp"
#include "service/auth/AuthResult.hpp"

namespace backend::service::auth {

class AuthService
{
public:
    explicit AuthService(backend::repository::user::UserRepository &userRepository);

    AuthResult login(const QString &username, const QString &password) const;
    AuthResult registerUser(const QString &username, const QString &password, const QString &email);

private:
    backend::repository::user::UserRepository &userRepository_;
};

}  // namespace backend::service::auth
