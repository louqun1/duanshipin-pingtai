#include "widgets/MainWindow.hpp"

#include "controller/auth/AuthController.hpp"
#include "liveplayer/service/LivePlayerController.hpp"
#include "playercontroller/service/PlayerController.hpp"
#include "pages/AccountPage/AccountPage.hpp"
#include "pages/HomePage/HomePage.hpp"
#include "pages/StreamPage/StreamPage.hpp"
#include "pages/UploadPage/UploadPage.hpp"
#include "widgets/VideoPlayerWindow.hpp"

#include <QAbstractButton>
#include <QAbstractAnimation>
#include <QButtonGroup>
#include <QEasingCurve>
#include <QEnterEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMetaObject>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyle>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QtGlobal>
#include <functional>
#include <spdlog/spdlog.h>

namespace {

constexpr int kExpandedSideNavWidth = 240;
constexpr int kCollapsedSideNavWidth = 64;
constexpr int kSideNavAnimationDurationMs = 180;
constexpr int kSideNavAutoHideDelayMs = 250;

QString sizeText(const QSize &size)
{
    return QString("%1x%2").arg(size.width()).arg(size.height());
}

class HoverAwareNavigationFrame final : public QFrame
{
public:
    using QFrame::QFrame;

    std::function<void()> onEnter;
    std::function<void()> onLeave;

protected:
    void enterEvent(QEnterEvent *event) override
    {
        QFrame::enterEvent(event);

        if (onEnter) {
            onEnter();
        }
    }

    void leaveEvent(QEvent *event) override
    {
        QFrame::leaveEvent(event);

        if (onLeave) {
            onLeave();
        }
    }
};

class CurrentPageStackedWidget final : public QStackedWidget
{
public:
    using QStackedWidget::QStackedWidget;

    QSize sizeHint() const override
    {
        if (const QWidget *page = currentWidget()) {
            return page->sizeHint();
        }

        return QStackedWidget::sizeHint();
    }

    QSize minimumSizeHint() const override
    {
        if (const QWidget *page = currentWidget()) {
            return page->minimumSizeHint();
        }

        return QStackedWidget::minimumSizeHint();
    }
};

}

MainWindow::MainWindow(
    backend::controller::auth::AuthController &authController,
    backend::liveplayer::service::LivePlayerController &livePlayerController,
    backend::playercontroller::service::PlayerController &playerController,
    QWidget *parent)
    : QMainWindow(parent)
    , authController_(authController)
    , livePlayerController_(livePlayerController)
    , playerController_(playerController)
{
    buildUi();
    dumpStartupWindowMetrics("after_build_ui");
    connectNavigation();
    connectPlaybackFlow();
    connectAccountFlow();
    switchToPage(Home);
}

void MainWindow::buildUi()
{
    setWindowTitle("Flashpoint 短视频");
    resize(1280, 600);

    auto *central = new QWidget(this);
    auto *layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    layout->addWidget(createNavigation());

    sideNavHideTimer_ = new QTimer(this);
    sideNavHideTimer_->setSingleShot(true);
    sideNavHideTimer_->setInterval(kSideNavAutoHideDelayMs);
    connect(sideNavHideTimer_, &QTimer::timeout, this, &MainWindow::collapseSideNav);

    sideNavAnim_ = new QPropertyAnimation(this, "sideNavWidth", this);
    sideNavAnim_->setDuration(kSideNavAnimationDurationMs);
    sideNavAnim_->setEasingCurve(QEasingCurve::InOutCubic);

    pageStack_ = new CurrentPageStackedWidget(central);
    pageStack_->setObjectName("pageStack");

    homePage_ = new frontend::pages::HomePage(pageStack_);
    streamPage_ = new frontend::pages::StreamPage(livePlayerController_, pageStack_);
    uploadPage_ = new frontend::pages::UploadPage(pageStack_);
    accountPage_ = new frontend::pages::AccountPage(pageStack_);

    homePage_->setAuthToken(authController_.sessionToken());
    streamPage_->setAuthToken(authController_.sessionToken());
    uploadPage_->setAuthToken(authController_.sessionToken());

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
        "  border: 0;"
        "  border-radius: 10px;"
        "  color: #cbd5e1;"
        "  background: transparent;"
        "}"
        "QPushButton[navButton=\"true\"][navExpanded=\"true\"] {"
        "  text-align: left;"
        "  padding: 14px 16px;"
        "  margin: 4px 12px;"
        "}"
        "QPushButton[navButton=\"true\"][navExpanded=\"false\"] {"
        "  text-align: center;"
        "  padding: 14px 0;"
        "  margin: 4px 10px;"
        "  font-weight: 700;"
        "}"
        "QPushButton[navButton=\"true\"]:hover { background: #1f2937; }"
        "QPushButton[navButton=\"true\"]:checked {"
        "  color: #f8fafc;"
        "  background: #2563eb;"
        "}"
        "#pageStack { background: #f4f7fb; }");
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);

    if (!startupMetricsLoggedAfterShow_) {
        startupMetricsLoggedAfterShow_ = true;
        dumpStartupWindowMetrics("after_showEvent");
    }
}

QWidget *MainWindow::createNavigation()
{
    auto *panel = new HoverAwareNavigationFrame(this);
    panel->setObjectName("navigationPanel");
    panel->setMinimumWidth(kCollapsedSideNavWidth);
    panel->setMaximumWidth(kCollapsedSideNavWidth);
    panel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    navigationPanel_ = panel;

    panel->onEnter = [this]() {
        stopSideNavCollapse();
        expandSideNav();
    };
    panel->onLeave = [this]() {
        scheduleSideNavCollapse();
    };

    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(16, 20, 16, 20);
    layout->setSpacing(6);

    brandLabel_ = new QLabel("Flashpoint", panel);
    brandLabel_->setObjectName("brandLabel");
    layout->addWidget(brandLabel_);

    captionLabel_ = new QLabel("桌面端骨架重建", panel);
    captionLabel_->setObjectName("captionLabel");
    layout->addWidget(captionLabel_);
    layout->addSpacing(18);

    navigationGroup_ = new QButtonGroup(panel);
    navigationGroup_->setExclusive(true);

    layout->addWidget(createNavigationButton("主页", "首", ":/nav-icons/home.png", Home));
    layout->addWidget(createNavigationButton("直播", "播", ":/nav-icons/live.png", Stream));
    layout->addWidget(createNavigationButton("上传", "传", ":/nav-icons/upload.png", Upload));
    layout->addWidget(createNavigationButton("我的", "我", ":/nav-icons/me.png", Account));
    layout->addStretch();

    return panel;
}

QPushButton *MainWindow::createNavigationButton(
    const QString &expandedLabel,
    const QString &collapsedLabel,
    const QString &iconPath,
    int pageIndex)
{
    auto *button = new QPushButton(expandedLabel, this);
    button->setProperty("navButton", true);
    button->setProperty("navExpanded", true);
    button->setProperty("navExpandedText", expandedLabel);
    button->setProperty("navCollapsedText", collapsedLabel);
    button->setProperty("navIconPath", iconPath);
    button->setCheckable(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setToolTip(expandedLabel);
    button->setIconSize(QSize(28, 28));
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
                videoPlayerWindow_->raise();
                videoPlayerWindow_->activateWindow();
                videoPlayerWindow_->show();
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
                updateAuthenticatedAccount(username, email, QString(), true);
            });
    connect(&authController_, &backend::controller::auth::AuthController::registerSucceeded,
            this, [this](const QString &username, const QString &email) {
                updateAuthenticatedAccount(username, email, QString(), true);
            });
    connect(&authController_, &backend::controller::auth::AuthController::sessionRestored,
            this, [this](const QString &username, const QString &email) {
                updateAuthenticatedAccount(username, email, QString(), false);
            });
}

void MainWindow::switchToPage(int pageIndex)
{
    if (!pageStack_ || !navigationGroup_) {
        return;
    }

    pageStack_->setCurrentIndex(pageIndex);
    QAbstractButton *button = navigationGroup_->button(pageIndex);
    if (button) {
        button->setChecked(true);
    }
}

int MainWindow::sideNavWidth() const
{
    return navigationPanel_ ? navigationPanel_->width() : kCollapsedSideNavWidth;
}

void MainWindow::setSideNavWidth(int width)
{
    if (!navigationPanel_) {
        return;
    }

    const int safeWidth = qMax(kCollapsedSideNavWidth, qMin(width, kExpandedSideNavWidth));
    navigationPanel_->setMinimumWidth(safeWidth);
    navigationPanel_->setMaximumWidth(safeWidth);
    navigationPanel_->updateGeometry();
}

void MainWindow::expandSideNav()
{
    stopSideNavCollapse();

    if (sideNavExpanded_
        && (!sideNavAnim_ || sideNavAnim_->state() != QAbstractAnimation::Running)) {
        return;
    }

    sideNavExpanded_ = true;
    updateSideNavPresentation();
    animateSideNavTo(kExpandedSideNavWidth);
}

void MainWindow::collapseSideNav()
{
    if (sideNavPinned_) {
        return;
    }

    if (!sideNavExpanded_
        && (!sideNavAnim_ || sideNavAnim_->state() != QAbstractAnimation::Running)) {
        return;
    }

    sideNavExpanded_ = false;
    updateSideNavPresentation();
    animateSideNavTo(kCollapsedSideNavWidth);
}

void MainWindow::scheduleSideNavCollapse()
{
    if (sideNavPinned_ || !sideNavHideTimer_) {
        return;
    }

    sideNavHideTimer_->start();
}

void MainWindow::stopSideNavCollapse()
{
    if (!sideNavHideTimer_) {
        return;
    }

    sideNavHideTimer_->stop();
}

void MainWindow::animateSideNavTo(int targetWidth)
{
    if (!sideNavAnim_) {
        setSideNavWidth(targetWidth);
        return;
    }

    sideNavAnim_->stop();
    sideNavAnim_->setStartValue(sideNavWidth());
    sideNavAnim_->setEndValue(targetWidth);
    sideNavAnim_->start();
}

void MainWindow::updateSideNavPresentation()
{
    if (brandLabel_) {
        brandLabel_->setText(sideNavExpanded_ ? "Flashpoint" : "FP");
        brandLabel_->setAlignment(sideNavExpanded_
            ? (Qt::AlignLeft | Qt::AlignVCenter)
            : Qt::AlignCenter);
    }

    if (captionLabel_) {
        captionLabel_->setVisible(sideNavExpanded_);
    }

    if (!navigationGroup_) {
        return;
    }

    const QList<QAbstractButton *> buttons = navigationGroup_->buttons();
    for (QAbstractButton *abstractButton : buttons) {
        auto *button = qobject_cast<QPushButton *>(abstractButton);
        if (!button) {
            continue;
        }

        button->setProperty("navExpanded", sideNavExpanded_);

        if (sideNavExpanded_) {
            button->setText(button->property("navExpandedText").toString());
            button->setIcon(QIcon());
        } else {
            const QString iconPath = button->property("navIconPath").toString();
            button->setText(QString());
            button->setIcon(QIcon(iconPath));
        }

        button->style()->unpolish(button);
        button->style()->polish(button);
        button->update();
    }
}

void MainWindow::dumpStartupWindowMetrics(const char *stage) const
{
    const QWidget *currentPage = pageStack_ ? pageStack_->currentWidget() : nullptr;
    const QWidget *central = centralWidget();

    const QByteArray stageUtf8 = QByteArray(stage ? stage : "unknown");
    const QByteArray windowSize = sizeText(size()).toUtf8();
    const QByteArray windowMinSize = sizeText(minimumSize()).toUtf8();
    const QByteArray windowMinHint = sizeText(minimumSizeHint()).toUtf8();
    const QByteArray windowHint = sizeText(sizeHint()).toUtf8();
    const QByteArray centralMinSize = central ? sizeText(central->minimumSize()).toUtf8() : QByteArray("-");
    const QByteArray centralMinHint = central ? sizeText(central->minimumSizeHint()).toUtf8() : QByteArray("-");
    const QByteArray pageStackMinSize = pageStack_ ? sizeText(pageStack_->minimumSize()).toUtf8() : QByteArray("-");
    const QByteArray pageStackMinHint = pageStack_ ? sizeText(pageStack_->minimumSizeHint()).toUtf8() : QByteArray("-");
    const QByteArray currentPageName = currentPage
        ? QByteArray(currentPage->metaObject()->className())
        : QByteArray("-");
    const QByteArray currentPageMinSize = currentPage ? sizeText(currentPage->minimumSize()).toUtf8() : QByteArray("-");
    const QByteArray currentPageMinHint = currentPage ? sizeText(currentPage->minimumSizeHint()).toUtf8() : QByteArray("-");
    const QByteArray currentPageHint = currentPage ? sizeText(currentPage->sizeHint()).toUtf8() : QByteArray("-");

    spdlog::info(
        "[ui/mainwindow] stage={} window_size={} window_min={} window_min_hint={} window_hint={} "
        "central_min={} central_min_hint={} page_stack_min={} page_stack_min_hint={} "
        "current_page={} current_page_min={} current_page_min_hint={} current_page_hint={}",
        stageUtf8.constData(),
        windowSize.constData(),
        windowMinSize.constData(),
        windowMinHint.constData(),
        windowHint.constData(),
        centralMinSize.constData(),
        centralMinHint.constData(),
        pageStackMinSize.constData(),
        pageStackMinHint.constData(),
        currentPageName.constData(),
        currentPageMinSize.constData(),
        currentPageMinHint.constData(),
        currentPageHint.constData());
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

