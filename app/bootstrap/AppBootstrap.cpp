#include "bootstrap/AppBootstrap.hpp"

#include "controller/auth/AuthController.hpp"
#include "playercontroller/service/PlayerController.hpp"
#include "widgets/MainWindow.hpp"

AppBootstrap::AppBootstrap()
{
    authController_ = std::make_unique<backend::controller::auth::AuthController>();
    playerController_ = std::make_unique<backend::playercontroller::service::PlayerController>();
    mainWindow_ = std::make_unique<MainWindow>(*authController_, *playerController_);

    authController_->restorePersistedSession();
}

AppBootstrap::~AppBootstrap() = default;

QMainWindow *AppBootstrap::mainWindow() const
{
    return mainWindow_.get();
}
