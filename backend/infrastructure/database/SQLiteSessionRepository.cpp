#include "infrastructure/database/SQLiteSessionRepository.hpp"

#include <QDateTime>
#include <QSqlQuery>
#include <QVariant>

namespace backend::infrastructure::database {

SQLiteSessionRepository::SQLiteSessionRepository(SQLiteDatabase &database)
    : database_(database)
{
}

bool SQLiteSessionRepository::create(const backend::domain::session::Session &session)
{
    if (!database_.open()) {
        return false;
    }

    QSqlQuery query(database_.database());
    query.prepare(
        "INSERT INTO sessions (id, user_id, token_hash, created_at, expires_at, last_seen_at, revoked_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)");
    query.addBindValue(session.id);
    query.addBindValue(session.userId);
    query.addBindValue(session.tokenHash);
    query.addBindValue(session.createdAt.toUTC().toString(Qt::ISODateWithMs));
    query.addBindValue(session.expiresAt.toUTC().toString(Qt::ISODateWithMs));
    query.addBindValue(session.lastSeenAt.toUTC().toString(Qt::ISODateWithMs));
    query.addBindValue(session.revokedAt.has_value()
                           ? QVariant(session.revokedAt->toUTC().toString(Qt::ISODateWithMs))
                           : QVariant());

    return query.exec();
}

std::optional<backend::domain::session::Session> SQLiteSessionRepository::findActiveSession() const
{
    if (!database_.open()) {
        return std::nullopt;
    }

    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);

    QSqlQuery query(database_.database());
    query.prepare(
        "SELECT id, user_id, token_hash, created_at, expires_at, last_seen_at, revoked_at "
        "FROM sessions "
        "WHERE revoked_at IS NULL AND expires_at > ? "
        "ORDER BY created_at DESC "
        "LIMIT 1");
    query.addBindValue(now);

    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }

    return mapSessionRow(query);
}

bool SQLiteSessionRepository::revokeAll()
{
    if (!database_.open()) {
        return false;
    }

    QSqlQuery query(database_.database());
    query.prepare(
        "UPDATE sessions "
        "SET revoked_at = ?, last_seen_at = ? "
        "WHERE revoked_at IS NULL");

    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    query.addBindValue(now);
    query.addBindValue(now);

    return query.exec();
}

backend::domain::session::Session SQLiteSessionRepository::mapSessionRow(QSqlQuery &query)
{
    backend::domain::session::Session session{
        query.value(0).toString(),
        query.value(1).toString(),
        query.value(2).toString(),
        QDateTime::fromString(query.value(3).toString(), Qt::ISODateWithMs),
        QDateTime::fromString(query.value(4).toString(), Qt::ISODateWithMs),
        QDateTime::fromString(query.value(5).toString(), Qt::ISODateWithMs),
        std::nullopt
    };

    const QString revokedAt = query.value(6).toString();
    if (!revokedAt.isEmpty()) {
        session.revokedAt = QDateTime::fromString(revokedAt, Qt::ISODateWithMs);
    }

    return session;
}

}  // namespace backend::infrastructure::database
