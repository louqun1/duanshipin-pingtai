#include "widgets/MainWindow.hpp"

#include "controller/auth/AuthController.hpp"
#include "playercontroller/service/PlayerController.hpp"
#include "pages/AccountPage/AccountPage.hpp"
#include "pages/HomePage/HomePage.hpp"
#include "pages/StreamPage/StreamPage.hpp"
#include "pages/UploadPage/UploadPage.hpp"
#include "widgets/VideoPlayerWindow.hpp"

#include <QButtonGroup>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>
#include <QtGlobal>

MainWindow::MainWindow(
    backend::controller::auth::AuthController &authController,
    backend::playercontroller::service::PlayerController &playerController,
    QWidget *parent)
    : QMainWindow(parent)
    , authController_(authController)
    , playerController_(playerController)
{
    buildUi();
    connectNavigation();
    connectPlaybackFlow();
    connectAccountFlow();
    switchToPage(Home);
}

void MainWindow::buildUi()
{
    setWindowTitle("Flashpoint Short Videos");
    resize(1280, 820);

    auto *central = new QWidget(this);
    auto *layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    layout->addWidget(createNavigation());

    pageStack_ = new QStackedWidget(central);
    pageStack_->setObjectName("pageStack");

    homePage_ = new frontend::pages::HomePage(pageStack_);
    streamPage_ = new frontend::pages::StreamPage(pageStack_);
    uploadPage_ = new frontend::pages::UploadPage(pageStack_);
    accountPage_ = new frontend::pages::AccountPage(pageStack_);

    pageStack_->addWidget(homePage_);
    pageStack_->addWidget(streamPage_);
    pageStack_->addWidget(uploadPage_);
    pageStack_->addWidget(accountPage_);

    layout->addWidget(pageStack_, 1);
    setCentralWidget(central);

    setStyleSheet(
        "QMainWindow { background: #f4f7fb; }"
        "#navigationPanel { background: #101828; }"
        "#brandLabel { color: #f8fafc; font-size: 18px; font-weight: 700; }"
        "#captionLabel { color: #94a3b8; font-size: 12px; }"
        "QPushButton[navButton=\"true\"] {"
        "  text-align: left;"
        "  border: 0;"
        "  padding: 14px 16px;"
        "  margin: 4px 12px;"
        "  border-radius: 10px;"
        "  color: #cbd5e1;"
        "  background: transparent;"
        "}"
        "QPushButton[navButton=\"true\"]:hover { background: #1f2937; }"
        "QPushButton[navButton=\"true\"]:checked {"
        "  color: #f8fafc;"
        "  background: #2563eb;"
        "}"
        "#pageStack { background: #f4f7fb; }");
}

QWidget *MainWindow::createNavigation()
{
    auto *panel = new QFrame(this);
    panel->setObjectName("navigationPanel");
    panel->setFixedWidth(240);

    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(16, 20, 16, 20);
    layout->setSpacing(6);

    auto *brand = new QLabel("Flashpoint", panel);
    brand->setObjectName("brandLabel");
    layout->addWidget(brand);

    auto *caption = new QLabel("Desktop rebuild skeleton", panel);
    caption->setObjectName("captionLabel");
    layout->addWidget(caption);
    layout->addSpacing(18);

    navigationGroup_ = new QButtonGroup(panel);
    navigationGroup_->setExclusive(true);

    layout->addWidget(createNavigationButton("主页", Home));
    layout->addWidget(createNavigationButton("直播", Stream));
    layout->addWidget(createNavigationButton("上传", Upload));
    layout->addWidget(createNavigationButton("我的", Account));
    layout->addStretch();

    return panel;
}

QPushButton *MainWindow::createNavigationButton(const QString &label, int pageIndex)
{
    auto *button = new QPushButton(label, this);
    button->setProperty("navButton", true);
    button->setCheckable(true);
    navigationGroup_->addButton(button, pageIndex);
    return button;
}

void MainWindow::connectNavigation()
{
    connect(navigationGroup_, &QButtonGroup::idClicked, this, &MainWindow::switchToPage);
    connect(uploadPage_, &frontend::pages::UploadPage::uploadSucceeded, this, [this]() {
        if (homePage_) {
            homePage_->refreshFeed();
        }
        switchToPage(Home);
    });
}

void MainWindow::connectPlaybackFlow()
{
    connect(homePage_, &frontend::pages::HomePage::playRequested,
            this, [this](const QString &mediaUrl,
                         const QString &videoId,
                         const QString &title,
                         const QString &creator,
                         const QString &duration) {
                if (!videoPlayerWindow_) {
                    videoPlayerWindow_ = new VideoPlayerWindow(playerController_);
                }

                videoPlayerWindow_->showSelectedVideo(mediaUrl, videoId, title, creator, duration);
                videoPlayerWindow_->show();
                videoPlayerWindow_->raise();
                videoPlayerWindow_->activateWindow();
            });
}

void MainWindow::connectAccountFlow()
{
    connect(accountPage_, &frontend::pages::AccountPage::loginRequested,
            &authController_, &backend::controller::auth::AuthController::requestLogin);
    connect(accountPage_, &frontend::pages::AccountPage::registerRequested,
            &authController_, &backend::controller::auth::AuthController::requestRegister);
    connect(accountPage_, &frontend::pages::AccountPage::logoutRequested,
            &authController_, &backend::controller::auth::AuthController::requestLogout);

    connect(&authController_, &backend::controller::auth::AuthController::loginSucceeded,
            this, [this](const QString &username, const QString &email) {
                updateAuthenticatedAccount(username, email, QString("欢迎回来，%1。").arg(username), true);
            });
    connect(&authController_, &backend::controller::auth::AuthController::registerSucceeded,
            this, [this](const QString &username, const QString &email) {
                updateAuthenticatedAccount(username, email, QString("已为 %1 创建账户。").arg(username), true);
            });
    connect(&authController_, &backend::controller::auth::AuthController::sessionRestored,
            this, [this](const QString &username, const QString &email) {
                updateAuthenticatedAccount(username, email, QString("%1，欢迎回来。").arg(username), false);
            });
    connect(&authController_, &backend::controller::auth::AuthController::loginFailed,
            this, [this](const QString &message) {
                currentUsername_.clear();
                currentEmail_.clear();
                accountPage_->showLoggedOutState(message);
                switchToPage(Account);
            });
    connect(&authController_, &backend::controller::auth::AuthController::registerFailed,
            this, [this](const QString &message) {
                currentUsername_.clear();
                currentEmail_.clear();
                accountPage_->showLoggedOutState(message);
                switchToPage(Account);
            });
    connect(&authController_, &backend::controller::auth::AuthController::logoutSucceeded,
            this, [this](const QString &) {
                currentUsername_.clear();
                currentEmail_.clear();
                accountPage_->showLoggedOutState("您已经退出登录。");
                switchToPage(Account);
            });
    connect(&authController_, &backend::controller::auth::AuthController::logoutFailed,
            this, [this](const QString &message) {
                accountPage_->showAuthenticatedState(currentUsername_, currentEmail_, message);
                switchToPage(Account);
            });
}

void MainWindow::switchToPage(int pageIndex)
{
    if (!pageStack_) {
        return;
    }

    const int safeIndex = qBound(0, pageIndex, pageStack_->count() - 1);
    pageStack_->setCurrentIndex(safeIndex);

    if (auto *button = navigationGroup_->button(safeIndex)) {
        button->setChecked(true);
    }
}

void MainWindow::updateAuthenticatedAccount(
    const QString &username,
    const QString &email,
    const QString &message,
    bool switchToAccount)
{
    currentUsername_ = username;
    currentEmail_ = email;
    accountPage_->showAuthenticatedState(username, email, message);

    if (switchToAccount) {
        switchToPage(Account);
    }
}
