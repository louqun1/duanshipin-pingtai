#pragma once

#include <QMainWindow>

#include <memory>

class MainWindow;

namespace backend::controller::auth {
class AuthController;
}

namespace backend::infrastructure::database {
class InMemoryUserRepository;
}

namespace backend::service::auth {
class AuthService;
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
    std::unique_ptr<backend::infrastructure::database::InMemoryUserRepository> userRepository_;
    std::unique_ptr<backend::service::auth::AuthService> authService_;
    std::unique_ptr<backend::controller::auth::AuthController> authController_;
    std::unique_ptr<MainWindow> mainWindow_;
};
