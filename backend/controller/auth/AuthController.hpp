#pragma once

#include "service/auth/AuthService.hpp"

#include <QObject>

namespace backend::controller::auth {

class AuthController final : public QObject
{
    Q_OBJECT

public:
    explicit AuthController(backend::service::auth::AuthService &authService, QObject *parent = nullptr);

public slots:
    void requestLogin(const QString &username, const QString &password);
    void requestRegister(const QString &username, const QString &password, const QString &email);
    void requestLogout();
    void restorePersistedSession();

signals:
    void loginSucceeded(const QString &username, const QString &email);
    void loginFailed(const QString &message);
    void registerSucceeded(const QString &username, const QString &email);
    void registerFailed(const QString &message);
    void logoutSucceeded(const QString &message);
    void logoutFailed(const QString &message);
    void sessionRestored(const QString &username, const QString &email);

private:
    backend::service::auth::AuthService &authService_;
};

}  // namespace backend::controller::auth
