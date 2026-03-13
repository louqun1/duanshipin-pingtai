#pragma once

#include "domain/user/User.hpp"

#include <QString>

#include <optional>

namespace backend::repository::user {

class UserRepository
{
public:
    virtual ~UserRepository() = default;

    virtual std::optional<backend::domain::user::User> findByUsername(const QString &username) const = 0;
    virtual bool validateCredentials(const QString &username, const QString &password) const = 0;
    virtual bool save(const backend::domain::user::User &user, const QString &password) = 0;
};

}  // namespace backend::repository::user
