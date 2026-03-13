#include "bootstrap/AppBootstrap.hpp"

#include "controller/auth/AuthController.hpp"
#include "infrastructure/database/InMemoryUserRepository.hpp"
#include "service/auth/AuthService.hpp"
#include "widgets/MainWindow.hpp"

AppBootstrap::AppBootstrap()
{
    userRepository_ = std::make_unique<backend::infrastructure::database::InMemoryUserRepository>();
    authService_ = std::make_unique<backend::service::auth::AuthService>(*userRepository_);
    authController_ = std::make_unique<backend::controller::auth::AuthController>(*authService_);
    mainWindow_ = std::make_unique<MainWindow>(*authController_);
}

AppBootstrap::~AppBootstrap() = default;

QMainWindow *AppBootstrap::mainWindow() const
{
    return mainWindow_.get();
}
