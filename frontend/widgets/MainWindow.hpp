#pragma once

#include <QMainWindow>
#include <QString>

class QButtonGroup;
class QPushButton;
class QStackedWidget;
class QWidget;

namespace backend::controller::auth {
class AuthController;
}

namespace frontend::pages {
class AccountPage;
class HomePage;
class StreamPage;
class UploadPage;
}

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(backend::controller::auth::AuthController &authController, QWidget *parent = nullptr);
    ~MainWindow() override = default;

    MainWindow(const MainWindow &) = delete;
    MainWindow &operator=(const MainWindow &) = delete;

private:
    enum PageIndex {
        Home = 0,
        Stream,
        Upload,
        Account
    };

    void buildUi();
    QWidget *createNavigation();
    QPushButton *createNavigationButton(const QString &label, int pageIndex);
    void connectNavigation();
    void connectAccountFlow();
    void switchToPage(int pageIndex);
    void showAuthenticatedAccount(const QString &username, const QString &email, const QString &message);

    QButtonGroup *navigationGroup_ = nullptr;
    QStackedWidget *pageStack_ = nullptr;
    backend::controller::auth::AuthController &authController_;

    frontend::pages::HomePage *homePage_ = nullptr;
    frontend::pages::StreamPage *streamPage_ = nullptr;
    frontend::pages::UploadPage *uploadPage_ = nullptr;
    frontend::pages::AccountPage *accountPage_ = nullptr;
};
