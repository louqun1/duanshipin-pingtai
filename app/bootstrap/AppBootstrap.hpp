#pragma once

#include <QMainWindow>

#include <memory>

class MainWindow;

namespace backend::controller::auth {
class AuthController;
}

namespace backend::playercontroller::service {
class PlayerController;
}

class AppBootstrap
{
public:
    AppBootstrap();
    ~AppBootstrap();

    AppBootstrap(const AppBootstrap &) = delete;
    AppBootstrap &operator=(const AppBootstrap &) = delete;

    QMainWindow *mainWindow() const;

private:
    std::unique_ptr<backend::controller::auth::AuthController> authController_;
    std::unique_ptr<backend::playercontroller::service::PlayerController> playerController_;
    std::unique_ptr<MainWindow> mainWindow_;
};
