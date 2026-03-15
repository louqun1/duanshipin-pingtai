#pragma once

#include <QString>
#include <QWidget>

class QLabel;
class QLineEdit;
class QStackedWidget;
class QWidget;

namespace frontend::pages {

class AccountPage final : public QWidget
{
    Q_OBJECT

public:
    explicit AccountPage(QWidget *parent = nullptr);

    void showLoggedOutState(const QString &message = QString());
    void showAuthenticatedState(const QString &username, const QString &email, const QString &message = QString());

signals:
    void loginRequested(const QString &username, const QString &password);
    void registerRequested(const QString &username, const QString &password, const QString &email);
    void logoutRequested();

private:
    QWidget *buildGuestPanel();
    QWidget *buildProfilePanel();
    void updateGuestStatus(const QString &message, bool isError);
    void updateProfileStatus(const QString &message);

    QStackedWidget *stateStack_ = nullptr;

    QLineEdit *usernameEdit_ = nullptr;
    QLineEdit *passwordEdit_ = nullptr;
    QLineEdit *emailEdit_ = nullptr;

    QLabel *guestStatusLabel_ = nullptr;
    QLabel *profileUsernameValue_ = nullptr;
    QLabel *profileEmailValue_ = nullptr;
    QLabel *profileStatusLabel_ = nullptr;
    // 新增昵称标签
    QLabel *profileNicknameValue_ = nullptr;
};

}  // namespace frontend::pages
