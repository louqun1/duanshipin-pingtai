#pragma once

#include "repository/user/UserRepository.hpp"

#include <QVector>

namespace backend::infrastructure::database {

class InMemoryUserRepository final : public backend::repository::user::UserRepository
{
public:
    InMemoryUserRepository();

    std::optional<backend::domain::user::User> findByUsername(const QString &username) const override;
    bool validateCredentials(const QString &username, const QString &password) const override;
    bool save(const backend::domain::user::User &user, const QString &password) override;

private:
    struct StoredUser {
        backend::domain::user::User user;
        QString password;
    };

    QVector<StoredUser> users_;
};

}  // namespace backend::infrastructure::database
