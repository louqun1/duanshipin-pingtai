#include "infrastructure/database/InMemoryUserRepository.hpp"

namespace backend::infrastructure::database {

InMemoryUserRepository::InMemoryUserRepository()
{
    users_.append(StoredUser{
        backend::domain::user::User{
            "seed-user",
            "demo",
            "demo@example.com",
            ""
        },
        "123456"
    });
}

std::optional<backend::domain::user::User> InMemoryUserRepository::findByUsername(const QString &username) const
{
    for (const auto &entry : users_) {
        if (entry.user.username.compare(username, Qt::CaseInsensitive) == 0) {
            return entry.user;
        }
    }

    return std::nullopt;
}

bool InMemoryUserRepository::validateCredentials(const QString &username, const QString &password) const
{
    for (const auto &entry : users_) {
        if (entry.user.username.compare(username, Qt::CaseInsensitive) == 0 && entry.password == password) {
            return true;
        }
    }

    return false;
}

bool InMemoryUserRepository::save(const backend::domain::user::User &user, const QString &password)
{
    if (findByUsername(user.username).has_value()) {
        return false;
    }

    users_.append(StoredUser{user, password});
    return true;
}

}  // namespace backend::infrastructure::database
