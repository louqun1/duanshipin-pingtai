#pragma once

#include <QMainWindow>
#include <QString>

class QButtonGroup;
class QPushButton;
class QShowEvent;
class QStackedWidget;
class QWidget;
class VideoPlayerWindow;

namespace backend::controller::auth {
class AuthController;
}

namespace backend::playercontroller::service {
class PlayerController;
}

namespace backend::liveplayer::service {
class LivePlayerController;
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
    explicit MainWindow(
        backend::controller::auth::AuthController &authController,
        backend::liveplayer::service::LivePlayerController &livePlayerController,
        backend::playercontroller::service::PlayerController &playerController,
        QWidget *parent = nullptr);
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
    void connectPlaybackFlow();
    void connectAccountFlow();
    void switchToPage(int pageIndex);
    void dumpStartupWindowMetrics(const char *stage) const;
    void updateAuthenticatedAccount(
        const QString &username,
        const QString &email,
        const QString &message,
        bool switchToAccount);

protected:
    void showEvent(QShowEvent *event) override;

    QButtonGroup *navigationGroup_ = nullptr;
    QStackedWidget *pageStack_ = nullptr;
    backend::controller::auth::AuthController &authController_;
    backend::liveplayer::service::LivePlayerController &livePlayerController_;
    backend::playercontroller::service::PlayerController &playerController_;
    QString currentUsername_;
    QString currentEmail_;

    VideoPlayerWindow *videoPlayerWindow_ = nullptr;
    frontend::pages::HomePage *homePage_ = nullptr;
    frontend::pages::StreamPage *streamPage_ = nullptr;
    frontend::pages::UploadPage *uploadPage_ = nullptr;
    frontend::pages::AccountPage *accountPage_ = nullptr;
    bool startupMetricsLoggedAfterShow_ = false;
};
