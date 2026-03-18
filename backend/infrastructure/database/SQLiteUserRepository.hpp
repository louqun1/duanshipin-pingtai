#pragma once

#include "infrastructure/database/SQLiteDatabase.hpp"
#include "repository/user/UserRepository.hpp"

class QSqlQuery;

namespace backend::infrastructure::database {

class SQLiteUserRepository final : public backend::repository::user::UserRepository
{
public:
    explicit SQLiteUserRepository(SQLiteDatabase &database);

    std::optional<backend::domain::user::User> findById(const QString &id) const override;
    std::optional<backend::domain::user::User> findByUsername(const QString &username) const override;
    bool validateCredentials(const QString &username, const QString &password) const override;
    bool save(const backend::domain::user::User &user, const QString &password) override;

private:
    void ensureSeedUser();
    static QString generateSalt();
    static QString hashPassword(const QString &password, const QString &salt);
    std::optional<backend::domain::user::User> mapUserRow(QSqlQuery &query) const;

    SQLiteDatabase &database_;
};

}  // namespace backend::infrastructure::database
