#include "infrastructure/database/SQLiteUserRepository.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace backend::infrastructure::database {

SQLiteUserRepository::SQLiteUserRepository(SQLiteDatabase &database)
    : database_(database)
{
    ensureSeedUser();
}

std::optional<backend::domain::user::User> SQLiteUserRepository::findById(const QString &id) const
{
    if (!database_.open()) {
        return std::nullopt;
    }

    QSqlQuery query(database_.database());
    query.prepare(
        "SELECT id, username, email, avatar_path "
        "FROM users "
        "WHERE id = ?");
    query.addBindValue(id);

    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }

    return mapUserRow(query);
}

std::optional<backend::domain::user::User> SQLiteUserRepository::findByUsername(const QString &username) const
{
    if (!database_.open()) {
        return std::nullopt;
    }

    QSqlQuery query(database_.database());
    query.prepare(
        "SELECT id, username, email, avatar_path "
        "FROM users "
        "WHERE username = ? COLLATE NOCASE");
    query.addBindValue(username.trimmed());

    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }

    return mapUserRow(query);
}

bool SQLiteUserRepository::validateCredentials(const QString &username, const QString &password) const
{
    if (!database_.open()) {
        return false;
    }

    QSqlQuery query(database_.database());
    query.prepare(
        "SELECT password_hash, password_salt "
        "FROM users "
        "WHERE username = ? COLLATE NOCASE");
    query.addBindValue(username.trimmed());

    if (!query.exec() || !query.next()) {
        return false;
    }

    const QString storedHash = query.value(0).toString();
    const QString storedSalt = query.value(1).toString();
    return storedHash == hashPassword(password, storedSalt);
}

bool SQLiteUserRepository::save(const backend::domain::user::User &user, const QString &password)
{
    if (!database_.open() || findByUsername(user.username).has_value()) {
        return false;
    }

    const QString salt = generateSalt();
    const QString passwordHash = hashPassword(password, salt);
    const QString userId = user.id.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : user.id;
    const QString createdAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);

    QSqlQuery query(database_.database());
    query.prepare(
        "INSERT INTO users (id, username, email, avatar_path, password_hash, password_salt, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)");
    query.addBindValue(userId);
    query.addBindValue(user.username.trimmed());
    query.addBindValue(user.email.trimmed());
    query.addBindValue(user.avatarPath);
    query.addBindValue(passwordHash);
    query.addBindValue(salt);
    query.addBindValue(createdAt);

    return query.exec();
}

void SQLiteUserRepository::ensureSeedUser()
{
    if (findByUsername("demo").has_value()) {
        return;
    }

    const backend::domain::user::User demoUser{
        "seed-user",
        "demo",
        "demo@example.com",
        ""
    };
    save(demoUser, "123456");
}

QString SQLiteUserRepository::generateSalt()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString SQLiteUserRepository::hashPassword(const QString &password, const QString &salt)
{
    const QByteArray digest = QCryptographicHash::hash(
        QByteArray(password.toUtf8() + ":" + salt.toUtf8()),
        QCryptographicHash::Sha256);
    return QString::fromLatin1(digest.toHex());
}

std::optional<backend::domain::user::User> SQLiteUserRepository::mapUserRow(QSqlQuery &query) const
{
    return backend::domain::user::User{
        query.value(0).toString(),
        query.value(1).toString(),
        query.value(2).toString(),
        query.value(3).toString()
    };
}

}  // namespace backend::infrastructure::database
