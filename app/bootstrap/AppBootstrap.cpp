#include "bootstrap/AppBootstrap.hpp"

#include "controller/auth/AuthController.hpp"
#include "infrastructure/database/SQLiteDatabase.hpp"
#include "infrastructure/database/SQLiteSessionRepository.hpp"
#include "infrastructure/database/SQLiteUserRepository.hpp"
#include "service/auth/AuthService.hpp"
#include "playercontroller/service/PlayerController.hpp"
#include "widgets/MainWindow.hpp"

#include <QDir>
#include <QStandardPaths>

#include <stdexcept>

namespace {

QString resolveDatabaseFilePath()
{
    QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dataPath.isEmpty()) {
        dataPath = QDir::currentPath() + "/data";
    }

    return QDir(dataPath).filePath("flashpoint.sqlite3");
}

}  // namespace

AppBootstrap::AppBootstrap()//C++ 14  make_unique  避免手动使用new和delete
{
    sqliteDatabase_ = std::make_unique<backend::infrastructure::database::SQLiteDatabase>(resolveDatabaseFilePath());
    if (!sqliteDatabase_->open()) {
        const QByteArray errorBytes =
            QString("Failed to open SQLite database at %1: %2")
                .arg(sqliteDatabase_->databaseFilePath(), sqliteDatabase_->lastError())
                .toLocal8Bit();
        throw std::runtime_error(
            errorBytes.constData());
    }

    userRepository_ = std::make_unique<backend::infrastructure::database::SQLiteUserRepository>(*sqliteDatabase_);
    sessionRepository_ = std::make_unique<backend::infrastructure::database::SQLiteSessionRepository>(*sqliteDatabase_);
    authService_ = std::make_unique<backend::service::auth::AuthService>(*userRepository_, *sessionRepository_);
    authController_ = std::make_unique<backend::controller::auth::AuthController>(*authService_);
    playerController_ = std::make_unique<backend::playercontroller::service::PlayerController>();
    mainWindow_ = std::make_unique<MainWindow>(*authController_, *playerController_);

    authController_->restorePersistedSession();
}

AppBootstrap::~AppBootstrap() = default;

QMainWindow *AppBootstrap::mainWindow() const
{
    return mainWindow_.get();
}
