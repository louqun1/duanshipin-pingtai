#pragma once

#include <QSqlDatabase>
#include <QString>

namespace backend::infrastructure::database {

class SQLiteDatabase
{
public:
    explicit SQLiteDatabase(QString databaseFilePath);
    ~SQLiteDatabase();

    SQLiteDatabase(const SQLiteDatabase &) = delete;
    SQLiteDatabase &operator=(const SQLiteDatabase &) = delete;

    [[nodiscard]] bool open();
    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] QString lastError() const;
    [[nodiscard]] QString databaseFilePath() const;
    [[nodiscard]] QSqlDatabase database() const;

private:
    bool ensureParentDirectory() const;
    bool initializeSchema();
    bool executeStatement(const QString &statement);

    QString databaseFilePath_;
    QString connectionName_;
    QSqlDatabase database_;
    QString lastError_;
};

}  // namespace backend::infrastructure::database
