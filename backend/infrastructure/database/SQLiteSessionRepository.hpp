#pragma once

#include "infrastructure/database/SQLiteDatabase.hpp"
#include "repository/session/SessionRepository.hpp"

class QSqlQuery;

namespace backend::infrastructure::database {

class SQLiteSessionRepository final : public backend::repository::session::SessionRepository
{
public:
    explicit SQLiteSessionRepository(SQLiteDatabase &database);

    bool create(const backend::domain::session::Session &session) override;
    std::optional<backend::domain::session::Session> findActiveSession() const override;
    bool revokeAll() override;

private:
    static backend::domain::session::Session mapSessionRow(QSqlQuery &query);

    SQLiteDatabase &database_;
};

}  // namespace backend::infrastructure::database
