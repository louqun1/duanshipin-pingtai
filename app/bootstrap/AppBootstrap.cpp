#include "bootstrap/AppBootstrap.hpp"

#include "controller/auth/AuthController.hpp"
#include "liveplayer/service/LivePlayerController.hpp"
#include "playercontroller/service/PlayerController.hpp"
#include "widgets/MainWindow.hpp"

AppBootstrap::AppBootstrap()
{
    authController_ = std::make_unique<backend::controller::auth::AuthController>();
    livePlayerController_ = std::make_unique<backend::liveplayer::service::LivePlayerController>();
    playerController_ = std::make_unique<backend::playercontroller::service::PlayerController>();
    mainWindow_ = std::make_unique<MainWindow>(
        *authController_,
        *livePlayerController_,
        *playerController_);

    authController_->restorePersistedSession();
}

AppBootstrap::~AppBootstrap() = default;

QMainWindow *AppBootstrap::mainWindow() const
{
    return mainWindow_.get();
}
