#include "controller/auth/AuthController.hpp"

namespace backend::controller::auth {

AuthController::AuthController(backend::service::auth::AuthService &authService, QObject *parent)
    : QObject(parent)
    , authService_(authService)
{
}

void AuthController::requestLogin(const QString &username, const QString &password)
{
    const auto result = authService_.login(username, password);
    if (result.ok() && result.user.has_value()) {
        emit loginSucceeded(result.user->username, result.user->email);
        return;
    }

    emit loginFailed(result.message);
}

void AuthController::requestRegister(const QString &username, const QString &password, const QString &email)
{
    const auto result = authService_.registerUser(username, password, email);
    if (result.ok() && result.user.has_value()) {
        emit registerSucceeded(result.user->username, result.user->email);
        return;
    }

    emit registerFailed(result.message);
}

}  // namespace backend::controller::auth
