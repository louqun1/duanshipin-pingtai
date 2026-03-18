#include "infrastructure/database/SQLiteDatabase.hpp"

#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

#include <filesystem>
#include <system_error>
#include <utility>

namespace backend::infrastructure::database {

namespace {

constexpr auto kCreateUsersTable = R"SQL(
CREATE TABLE IF NOT EXISTS users (
    id TEXT PRIMARY KEY,
    username TEXT NOT NULL UNIQUE COLLATE NOCASE,
    email TEXT NOT NULL,
    avatar_path TEXT NOT NULL DEFAULT '',
    password_hash TEXT NOT NULL,
    password_salt TEXT NOT NULL,
    created_at TEXT NOT NULL
);
)SQL";

constexpr auto kCreateSessionsTable = R"SQL(
CREATE TABLE IF NOT EXISTS sessions (
    id TEXT PRIMARY KEY,
    user_id TEXT NOT NULL,
    token_hash TEXT NOT NULL,
    created_at TEXT NOT NULL,
    expires_at TEXT NOT NULL,
    last_seen_at TEXT NOT NULL,
    revoked_at TEXT,
    FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE
);
)SQL";

constexpr auto kCreateUsersUsernameIndex = R"SQL(
CREATE UNIQUE INDEX IF NOT EXISTS idx_users_username
ON users(username COLLATE NOCASE);
)SQL";

constexpr auto kCreateSessionsUserIndex = R"SQL(
CREATE INDEX IF NOT EXISTS idx_sessions_user_id
ON sessions(user_id);
)SQL";

constexpr auto kCreateSessionsLookupIndex = R"SQL(
CREATE INDEX IF NOT EXISTS idx_sessions_active_lookup
ON sessions(revoked_at, expires_at, created_at);
)SQL";

}  // namespace

SQLiteDatabase::SQLiteDatabase(QString databaseFilePath)
    : databaseFilePath_(std::move(databaseFilePath))
    , connectionName_(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
}

SQLiteDatabase::~SQLiteDatabase()
{
    if (database_.isOpen()) {
        database_.close();
    }

    const QString connectionName = connectionName_;
    database_ = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName);
}

bool SQLiteDatabase::open()
{
    if (database_.isOpen()) {
        return true;
    }

    if (!ensureParentDirectory()) {
        return false;
    }

    database_ = QSqlDatabase::addDatabase("QSQLITE", connectionName_);
    database_.setDatabaseName(databaseFilePath_);

    if (!database_.open()) {
        lastError_ = database_.lastError().text();
        return false;
    }

    if (!executeStatement("PRAGMA foreign_keys = ON;")) {
        return false;
    }

    return initializeSchema();
}

bool SQLiteDatabase::isOpen() const
{
    return database_.isOpen();
}

QString SQLiteDatabase::lastError() const
{
    return lastError_;
}

QString SQLiteDatabase::databaseFilePath() const
{
    return databaseFilePath_;
}

QSqlDatabase SQLiteDatabase::database() const
{
    return database_;
}

bool SQLiteDatabase::ensureParentDirectory() const
{
    const QFileInfo fileInfo(databaseFilePath_);
    if (fileInfo.dir().exists()) {
        return true;
    }

    std::error_code errorCode;
    const auto created = std::filesystem::create_directories(
        std::filesystem::path(fileInfo.absolutePath().toStdWString()),
        errorCode);

    return created || !errorCode;
}

bool SQLiteDatabase::initializeSchema()
{
    return executeStatement(QString::fromUtf8(kCreateUsersTable))
        && executeStatement(QString::fromUtf8(kCreateSessionsTable))
        && executeStatement(QString::fromUtf8(kCreateUsersUsernameIndex))
        && executeStatement(QString::fromUtf8(kCreateSessionsUserIndex))
        && executeStatement(QString::fromUtf8(kCreateSessionsLookupIndex));
}

bool SQLiteDatabase::executeStatement(const QString &statement)
{
    QSqlQuery query(database_);
    if (query.exec(statement)) {
        return true;
    }

    lastError_ = query.lastError().text();
    return false;
}

}  // namespace backend::infrastructure::database
