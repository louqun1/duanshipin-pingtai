#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace backend::controller::auth {

class AuthController final : public QObject
{
    Q_OBJECT

public:
    explicit AuthController(QObject *parent = nullptr);

    QString sessionToken() const;
    bool isAuthenticated() const;

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
    enum class AuthAction {
        Login,
        Register,
        Logout,
        Restore
    };

    void handleAuthReply(QNetworkReply *reply, AuthAction action);
    void persistSessionToken(const QString &token);
    void clearPersistedSessionToken();
    QString loadPersistedSessionToken() const;

    QNetworkAccessManager *networkManager_ = nullptr;
    QString sessionToken_;
};

}  // namespace backend::controller::auth
