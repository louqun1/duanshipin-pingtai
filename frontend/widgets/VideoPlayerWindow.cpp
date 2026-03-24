#include "widgets/VideoOpenGLWidget.hpp"
#include "widgets/VideoPlayerWindow.hpp"

#include "playercontroller/service/PlayerController.hpp"

#include <QEvent>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSize>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

#include <limits>
#include <QtGlobal>
#include <QtResource>

using PlaybackState = backend::playercontroller::service::PlayerController::PlaybackState;

bool ensurePlayerControlResourcesLoaded()
{
    Q_INIT_RESOURCE(FrontendWidgets);
    return true;
}

namespace
{

constexpr int kWindowMargin = 24;
constexpr int kWindowSpacing = 18;
constexpr int kContentSpacing = 20;
constexpr int kOverlayMargin = 24;
constexpr int kFullscreenHideDelayMs = 1800;
constexpr int kOverlayFadeDurationMs = 180;
constexpr int kControlButtonIconSize = 20;

const bool kPlayerControlResourcesLoaded = ensurePlayerControlResourcesLoaded();

QString formatPlaybackTime(int totalMilliseconds)
{
    if (totalMilliseconds < 0)
    {
        totalMilliseconds = 0;
    }

    const int totalSeconds = totalMilliseconds / 1000;
    const int hours = totalSeconds / 3600;
    const int minutes = (totalSeconds % 3600) / 60;
    const int seconds = totalSeconds % 60;

    if (hours > 0)
    {
        return QString("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0'));
    }

    return QString("%1:%2")
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

int clampSliderValue(qint64 value)
{
    if (value < 0)
    {
        return 0;
    }
    if (value > std::numeric_limits<int>::max())
    {
        return std::numeric_limits<int>::max();
    }
    return static_cast<int>(value);
}

QString volumeButtonText(int volume, bool muted)
{
    if (muted || volume <= 0)
    {
        return "Muted";
    }

    if (volume < 35)
    {
        return "Vol Low";
    }

    if (volume < 70)
    {
        return "Vol Mid";
    }

    return "Vol High";
}

QIcon playerControlIcon(const QString &alias)
{
    return QIcon(QString(":/player-controls/%1").arg(alias));
}

QIcon playButtonIcon(PlaybackState state)
{
    return state == PlaybackState::Playing
               ? playerControlIcon("pause.png")
               : playerControlIcon("play.png");
}

QIcon volumeButtonIcon(int volume, bool muted)
{
    if (muted || volume <= 0)
    {
        return playerControlIcon("volume-muted.png");
    }

    if (volume < 35)
    {
        return playerControlIcon("volume-low.png");
    }

    if (volume < 70)
    {
        return playerControlIcon("volume-mid.png");
    }

    return playerControlIcon("volume-high.png");
}

QString windowStyleSheet()
{
    return QStringLiteral("background: #e2e8f0;");
}

QString playerSurfaceStyle(bool fullscreen)
{
    if (fullscreen)
    {
        return QStringLiteral(
            "background: #020617;"
            "border-radius: 0px;");
    }

    return QStringLiteral(
        "background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #0f172a, stop:1 #1d4ed8);"
        "border-radius: 26px;");
}

QString videoViewportStyle(bool fullscreen)
{
    return QStringLiteral(
        "background: #020617;"
        "border-radius: %1px;")
        .arg(fullscreen ? 0 : 22);
}

QString overlayCardStyle()
{
    return QStringLiteral(
        "background: rgba(15, 23, 42, 0.28);"
        "border: 0;"
        "border-radius: 18px;");
}

QString primaryControlButtonStyle()
{
    return QStringLiteral(
        "QPushButton {"
        "  min-width: 44px;"
        "  min-height: 44px;"
        "  padding: 0;"
        "  border: 0;"
        "  border-radius: 14px;"
        "  background: transparent;"
        "  color: #f8fafc;"
        "  font-size: 14px;"
        "  font-weight: 700;"
        "}"
        "QPushButton:hover { background: rgba(248, 250, 252, 0.10); }"
        "QPushButton:pressed { background: rgba(248, 250, 252, 0.16); }"
        "QPushButton:disabled {"
        "  background: transparent;"
        "  color: rgba(248, 250, 252, 0.40);"
        "}");
}

QString secondaryControlButtonStyle()
{
    return QStringLiteral(
        "QPushButton {"
        "  min-width: 44px;"
        "  min-height: 44px;"
        "  padding: 0;"
        "  border: 0;"
        "  border-radius: 14px;"
        "  background: transparent;"
        "  color: #f8fafc;"
        "  font-size: 13px;"
        "  font-weight: 700;"
        "}"
        "QPushButton:hover { background: rgba(248, 250, 252, 0.10); }"
        "QPushButton:pressed { background: rgba(248, 250, 252, 0.16); }");
}

QString sliderStyle(const QString &filledColor, const QString &trackColor, const QString &handleColor)
{
    return QStringLiteral(
        "QSlider::groove:horizontal {"
        "  height: 6px;"
        "  border-radius: 3px;"
        "  background: %1;"
        "}"
        "QSlider::sub-page:horizontal {"
        "  border-radius: 3px;"
        "  background: %2;"
        "}"
        "QSlider::add-page:horizontal {"
        "  border-radius: 3px;"
        "  background: rgba(248, 250, 252, 0.10);"
        "}"
        "QSlider::handle:horizontal {"
        "  width: 14px;"
        "  margin: -5px 0;"
        "  border-radius: 7px;"
        "  background: %3;"
        "}")
        .arg(trackColor, filledColor, handleColor);
}

} // namespace

VideoPlayerWindow::VideoPlayerWindow(
    backend::playercontroller::service::PlayerController &playerController,
    QWidget *parent)
    : QWidget(parent)
    , playerController_(playerController)
{
    Q_UNUSED(kPlayerControlResourcesLoaded);
    buildUi();
    connectPlayerController();
    playerController_.attachVideoSurface(videoSurfaceWidget_);
    showEmptyState();
}

void VideoPlayerWindow::showSelectedVideo(
    const QString &mediaUrl,
    const QString &videoId,
    const QString &title,
    const QString &creator,
    const QString &duration)
{
    playerTitleLabel_->setText("Opening from Home feed...");
    playerHintLabel_->setText(QString("Forwarding OpenMedia to PlayerController for %1.").arg(videoId));
    playButton_->setEnabled(false);
    updatePlayButton(PlaybackState::Opening);
    progressSlider_->setEnabled(false);
    updateProgressDisplay(0, 0);
    refreshViewportChrome();
    showFullscreenControls();
    scheduleFullscreenControlsHide();

    playerController_.openMedia(mediaUrl, videoId, title, creator, duration);
}

bool VideoPlayerWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonDblClick &&
        (watched == playerSurface_ || watched == videoViewport_))
    {
        toggleFullscreen();
        return true;
    }

    switch (event->type())
    {
    case QEvent::Enter:
    case QEvent::HoverMove:
    case QEvent::MouseButtonPress:
    case QEvent::MouseMove:
    case QEvent::Wheel:
        showFullscreenControls();
        scheduleFullscreenControlsHide();
        break;
    case QEvent::Leave:
        scheduleFullscreenControlsHide();
        break;
    default:
        break;
    }

    return QWidget::eventFilter(watched, event);
}

void VideoPlayerWindow::closeEvent(QCloseEvent *event)
{
    fullscreenOverlayTimer_->stop();
    playerController_.releasePlaybackResources();
    showEmptyState();
    QWidget::closeEvent(event);
}

void VideoPlayerWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    positionOverlayWidgets();
}

void VideoPlayerWindow::keyPressEvent(QKeyEvent *event)
{
    if ((event->key() == Qt::Key_Escape) && isFullscreen_)
    {
        setFullscreen(false);
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_F || event->key() == Qt::Key_F11)
    {
        toggleFullscreen();
        event->accept();
        return;
    }

    QWidget::keyPressEvent(event);
}

void VideoPlayerWindow::connectPlayerController()
{
    connect(&playerController_, &backend::playercontroller::service::PlayerController::mediaChanged,
            this, [this](const QString &videoId,
                         const QString &title,
                         const QString &creator,
                         const QString &duration) {
                setWindowTitle(QString("Video Player - %1").arg(title));
                videoTitleLabel_->setText(title);
                videoMetaLabel_->setText(QString("%1  |  Duration %2  |  ID %3").arg(creator, duration, videoId));
                videoDescriptionLabel_->setText(
                    "The Home -> MainWindow -> VideoPlayerWindow -> PlayerController flow is now connected. "
                    "The video viewport now hosts a reusable overlay control bar.");
                queueLabel_->setText(
                    QString("Next player steps\n1. Reuse one control bar in windowed/fullscreen modes\n2. Keep render output on this surface\n3. Translate engine callbacks into UI events for %1").arg(creator));
                refreshViewportChrome();
            });

    connect(&playerController_, &backend::playercontroller::service::PlayerController::playbackStateChanged,
            this, [this](PlaybackState state, const QString &message) {
                switch (state) {
                case PlaybackState::Idle:
                    playerTitleLabel_->setText("No video selected yet");
                    playButton_->setEnabled(false);
                    progressSlider_->setEnabled(false);
                    break;
                case PlaybackState::Opening:
                    playerTitleLabel_->setText("PlayerController is opening media");
                    playButton_->setEnabled(false);
                    progressSlider_->setEnabled(false);
                    break;
                case PlaybackState::Prepared:
                    playerTitleLabel_->setText("PlayerController prepared the media");
                    playButton_->setEnabled(true);
                    progressSlider_->setEnabled(progressSlider_->maximum() > 0);
                    break;
                case PlaybackState::Playing:
                    playerTitleLabel_->setText("Playback is running through PlayerController");
                    playButton_->setEnabled(true);
                    progressSlider_->setEnabled(progressSlider_->maximum() > 0);
                    break;
                case PlaybackState::Paused:
                    playerTitleLabel_->setText("Playback paused");
                    playButton_->setEnabled(true);
                    progressSlider_->setEnabled(progressSlider_->maximum() > 0);
                    break;
                case PlaybackState::Stopped:
                    playerTitleLabel_->setText("Playback stopped");
                    playButton_->setEnabled(true);
                    progressSlider_->setEnabled(progressSlider_->maximum() > 0);
                    break;
                case PlaybackState::Error:
                    playerTitleLabel_->setText("Playback error");
                    playButton_->setEnabled(true);
                    progressSlider_->setEnabled(false);
                    break;
                }

                updatePlayButton(state);
                playerHintLabel_->setText(message);
                refreshViewportChrome();
                showFullscreenControls();
                scheduleFullscreenControlsHide();
            });

    connect(&playerController_, &backend::playercontroller::service::PlayerController::playbackProgressChanged,
            this, [this](qint64 positionMs, qint64 durationMs) {
                const int sliderMaximum = clampSliderValue(durationMs);
                const int sliderValue = clampSliderValue(positionMs);
                const PlaybackState state = playerController_.playbackState();
                const bool canSeek = sliderMaximum > 0 &&
                                     (state == PlaybackState::Prepared ||
                                      state == PlaybackState::Playing ||
                                      state == PlaybackState::Paused ||
                                      state == PlaybackState::Stopped);

                {
                    QSignalBlocker blocker(progressSlider_);
                    progressSlider_->setRange(0, sliderMaximum);
                    if (!isSliderScrubbing_)
                    {
                        progressSlider_->setValue(sliderValue);
                    }
                }

                progressSlider_->setEnabled(canSeek);
                updateProgressDisplay(isSliderScrubbing_ ? progressSlider_->value() : sliderValue,
                                      sliderMaximum);
            });

    connect(&playerController_, &backend::playercontroller::service::PlayerController::playbackVolumeChanged,
            this, [this](int volume, bool muted) {
                {
                    QSignalBlocker blocker(volumeSlider_);
                    volumeSlider_->setValue(volume);
                }
                updateVolumeDisplay(volume, muted);
            });

    connect(&playerController_, &backend::playercontroller::service::PlayerController::ijkPlayerCreated,
            this, [this]() {
                playerHintLabel_->setText("PlayerController created the ijkPlayer placeholder instance.");
                refreshViewportChrome();
            });

    connect(&playerController_, &backend::playercontroller::service::PlayerController::ijkPlayerOpenRequested,
            this, [this](const QString &videoId, const QString &title) {
                playerHintLabel_->setText(
                    QString("Reserved ijkPlayer call: open %1 (%2).").arg(title, videoId));
                refreshViewportChrome();
            });

    connect(playButton_, &QPushButton::clicked,
            &playerController_, &backend::playercontroller::service::PlayerController::requestTogglePlayback);

    connect(fullscreenButton_, &QPushButton::clicked,
            this, &VideoPlayerWindow::toggleFullscreen);

    connect(progressSlider_, &QSlider::sliderPressed, this, [this]() {
        isSliderScrubbing_ = true;
        showFullscreenControls();
        fullscreenOverlayTimer_->stop();
        updateProgressDisplay(progressSlider_->value(), progressSlider_->maximum());
    });
    connect(progressSlider_, &QSlider::sliderMoved, this, [this](int value) {
        showFullscreenControls();
        updateProgressDisplay(value, progressSlider_->maximum());
    });
    connect(progressSlider_, &QSlider::sliderReleased, this, [this]() {
        isSliderScrubbing_ = false;
        playerController_.requestSeek(progressSlider_->value());
        scheduleFullscreenControlsHide();
    });

    connect(volumeSlider_, &QSlider::valueChanged,
            &playerController_, &backend::playercontroller::service::PlayerController::requestSetVolume);
    connect(muteButton_, &QPushButton::clicked,
            &playerController_, &backend::playercontroller::service::PlayerController::requestToggleMute);
}

void VideoPlayerWindow::buildUi()
{
    setAttribute(Qt::WA_QuitOnClose, false);
    setWindowTitle("Video Player");
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    resize(1704, 794);
    setStyleSheet(windowStyleSheet());

    windowLayout_ = new QVBoxLayout(this);
    windowLayout_->setContentsMargins(kWindowMargin, kWindowMargin, kWindowMargin, kWindowMargin);
    windowLayout_->setSpacing(kWindowSpacing);

    headerWidget_ = new QWidget(this);
    auto *headerLayout = new QVBoxLayout(headerWidget_);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(8);

    auto *headerTitle = new QLabel("Short-video player", headerWidget_);
    headerTitle->setStyleSheet("font-size: 28px; font-weight: 700; color: #0f172a;");
    headerLayout->addWidget(headerTitle);

    auto *headerSummary = new QLabel(
        "Windowed and fullscreen playback now share one reusable control bar. "
        "In fullscreen, the controls float over the video and fade in on mouse movement.",
        headerWidget_);
    headerSummary->setWordWrap(true);
    headerSummary->setStyleSheet("font-size: 14px; color: #475569;");
    headerLayout->addWidget(headerSummary);

    windowLayout_->addWidget(headerWidget_);

    contentLayout_ = new QHBoxLayout();
    contentLayout_->setContentsMargins(0, 0, 0, 0);
    contentLayout_->setSpacing(kContentSpacing);
    windowLayout_->addLayout(contentLayout_, 1);

    playerSurface_ = new QFrame(this);
    playerSurface_->setMinimumSize(640, 420);
    playerSurface_->setStyleSheet(playerSurfaceStyle(false));
    contentLayout_->addWidget(playerSurface_, 2);

    playerSurfaceLayout_ = new QVBoxLayout(playerSurface_);
    playerSurfaceLayout_->setContentsMargins(20, 20, 20, 20);
    playerSurfaceLayout_->setSpacing(0);

    videoViewport_ = new QFrame(playerSurface_);
    videoViewport_->setMinimumSize(584, 320);
    videoViewport_->setStyleSheet(videoViewportStyle(false));
    playerSurfaceLayout_->addWidget(videoViewport_, 1);

    videoSurfaceWidget_ = new VideoOpenGLWidget(videoViewport_);
    videoSurfaceWidget_->setMinimumSize(584, 320);
    videoSurfaceWidget_->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    topOverlayWidget_ = new QWidget(videoViewport_);
    topOverlayWidget_->setStyleSheet(overlayCardStyle());
    auto *topOverlayLayout = new QVBoxLayout(topOverlayWidget_);
    topOverlayLayout->setContentsMargins(18, 16, 18, 16);
    topOverlayLayout->setSpacing(6);

    playerTitleLabel_ = new QLabel(topOverlayWidget_);
    playerTitleLabel_->setStyleSheet("font-size: 22px; font-weight: 700; color: #f8fafc;");
    topOverlayLayout->addWidget(playerTitleLabel_);

    playerHintLabel_ = new QLabel(topOverlayWidget_);
    playerHintLabel_->setWordWrap(true);
    playerHintLabel_->setStyleSheet("font-size: 13px; color: rgba(248, 250, 252, 0.82);");
    topOverlayLayout->addWidget(playerHintLabel_);

    controlBar_ = new QFrame(videoViewport_);
    controlBar_->setStyleSheet(overlayCardStyle());
    auto *controlBarLayout = new QHBoxLayout(controlBar_);
    controlBarLayout->setContentsMargins(18, 14, 18, 14);
    controlBarLayout->setSpacing(12);

    playButton_ = new QPushButton(controlBar_);
    playButton_->setCursor(Qt::PointingHandCursor);
    playButton_->setStyleSheet(primaryControlButtonStyle());
    playButton_->setIconSize(QSize(kControlButtonIconSize, kControlButtonIconSize));
    controlBarLayout->addWidget(playButton_, 0, Qt::AlignVCenter);

    currentTimeLabel_ = new QLabel("00:00", controlBar_);
    currentTimeLabel_->setMinimumWidth(48);
    currentTimeLabel_->setStyleSheet("font-size: 12px; font-weight: 700; color: #f8fafc;");
    controlBarLayout->addWidget(currentTimeLabel_, 0, Qt::AlignVCenter);

    progressSlider_ = new QSlider(Qt::Horizontal, controlBar_);
    progressSlider_->setRange(0, 0);
    progressSlider_->setEnabled(false);
    progressSlider_->setCursor(Qt::PointingHandCursor);
    progressSlider_->setStyleSheet(
        sliderStyle("#f8fafc", "rgba(248, 250, 252, 0.18)", "#ffffff"));
    controlBarLayout->addWidget(progressSlider_, 1, Qt::AlignVCenter);

    durationTimeLabel_ = new QLabel("00:00", controlBar_);
    durationTimeLabel_->setMinimumWidth(48);
    durationTimeLabel_->setStyleSheet("font-size: 12px; font-weight: 700; color: #f8fafc;");
    controlBarLayout->addWidget(durationTimeLabel_, 0, Qt::AlignVCenter);

    muteButton_ = new QPushButton(controlBar_);
    muteButton_->setCursor(Qt::PointingHandCursor);
    muteButton_->setStyleSheet(secondaryControlButtonStyle());
    muteButton_->setIconSize(QSize(kControlButtonIconSize, kControlButtonIconSize));
    controlBarLayout->addWidget(muteButton_, 0, Qt::AlignVCenter);

    volumeSlider_ = new QSlider(Qt::Horizontal, controlBar_);
    volumeSlider_->setRange(0, 100);
    volumeSlider_->setValue(50);
    volumeSlider_->setFixedWidth(140);
    volumeSlider_->setCursor(Qt::PointingHandCursor);
    volumeSlider_->setStyleSheet(
        sliderStyle("#f59e0b", "rgba(248, 250, 252, 0.18)", "#fef3c7"));
    controlBarLayout->addWidget(volumeSlider_, 0, Qt::AlignVCenter);

    volumeValueLabel_ = new QLabel("50%", controlBar_);
    volumeValueLabel_->setMinimumWidth(38);
    volumeValueLabel_->setStyleSheet("font-size: 12px; font-weight: 700; color: #f8fafc;");
    controlBarLayout->addWidget(volumeValueLabel_, 0, Qt::AlignVCenter);

    fullscreenButton_ = new QPushButton(controlBar_);
    fullscreenButton_->setCursor(Qt::PointingHandCursor);
    fullscreenButton_->setStyleSheet(secondaryControlButtonStyle());
    fullscreenButton_->setIconSize(QSize(kControlButtonIconSize, kControlButtonIconSize));
    controlBarLayout->addWidget(fullscreenButton_, 0, Qt::AlignVCenter);

    controlBarOpacityEffect_ = new QGraphicsOpacityEffect(controlBar_);
    controlBarOpacityEffect_->setOpacity(1.0);
    controlBar_->setGraphicsEffect(controlBarOpacityEffect_);

    controlBarOpacityAnimation_ = new QPropertyAnimation(controlBarOpacityEffect_, "opacity", this);
    controlBarOpacityAnimation_->setDuration(kOverlayFadeDurationMs);
    connect(controlBarOpacityAnimation_, &QPropertyAnimation::finished, this, [this]() {
        if (controlBarOpacityEffect_->opacity() <= 0.01)
        {
            controlBar_->hide();
        }
    });

    fullscreenOverlayTimer_ = new QTimer(this);
    fullscreenOverlayTimer_->setInterval(kFullscreenHideDelayMs);
    fullscreenOverlayTimer_->setSingleShot(true);
    connect(fullscreenOverlayTimer_, &QTimer::timeout, this, &VideoPlayerWindow::hideFullscreenControls);

    sidePanel_ = new QFrame(this);
    sidePanel_->setMinimumWidth(300);
    sidePanel_->setStyleSheet(
        "background: #ffffff;"
        "border: 1px solid #dbe4f0;"
        "border-radius: 22px;");
    contentLayout_->addWidget(sidePanel_, 1);

    auto *sideLayout = new QVBoxLayout(sidePanel_);
    sideLayout->setContentsMargins(22, 22, 22, 22);
    sideLayout->setSpacing(16);

    auto *infoEyebrow = new QLabel("CURRENT VIDEO", sidePanel_);
    infoEyebrow->setStyleSheet(
        "font-size: 11px; font-weight: 700; letter-spacing: 1px; color: #2563eb;");
    sideLayout->addWidget(infoEyebrow);

    videoTitleLabel_ = new QLabel(sidePanel_);
    videoTitleLabel_->setWordWrap(true);
    videoTitleLabel_->setStyleSheet("font-size: 22px; font-weight: 700; color: #0f172a;");
    sideLayout->addWidget(videoTitleLabel_);

    videoMetaLabel_ = new QLabel(sidePanel_);
    videoMetaLabel_->setWordWrap(true);
    videoMetaLabel_->setStyleSheet("font-size: 13px; color: #475569;");
    sideLayout->addWidget(videoMetaLabel_);

    videoDescriptionLabel_ = new QLabel(sidePanel_);
    videoDescriptionLabel_->setWordWrap(true);
    videoDescriptionLabel_->setStyleSheet("font-size: 14px; color: #334155;");
    sideLayout->addWidget(videoDescriptionLabel_);

    queueLabel_ = new QLabel(sidePanel_);
    queueLabel_->setWordWrap(true);
    queueLabel_->setStyleSheet(
        "padding: 16px;"
        "background: #f8fafc;"
        "border: 1px solid #e2e8f0;"
        "border-radius: 16px;"
        "font-size: 13px;"
        "color: #334155;");
    sideLayout->addWidget(queueLabel_);
    sideLayout->addStretch();

    installInteractionTracking(this);
    installInteractionTracking(playerSurface_);
    installInteractionTracking(videoViewport_);
    installInteractionTracking(controlBar_);
    installInteractionTracking(playButton_);
    installInteractionTracking(progressSlider_);
    installInteractionTracking(muteButton_);
    installInteractionTracking(volumeSlider_);
    installInteractionTracking(fullscreenButton_);

    updatePlayButton(PlaybackState::Idle);
    updateFullscreenButton();
    refreshViewportChrome();
    positionOverlayWidgets();
}

void VideoPlayerWindow::showEmptyState()
{
    playerTitleLabel_->setText("No video selected yet");
    playerHintLabel_->setText(
        "Open any short-video card from HomePage. The selected item will be sent to PlayerController from this window.");
    videoTitleLabel_->setText("Choose a short video from Home");
    videoMetaLabel_->setText("No card has been opened yet.");
    videoDescriptionLabel_->setText(
        "This window owns the PlayerController connection and can now reuse the same transport bar in normal and fullscreen modes.");
    queueLabel_->setText(
        "What fits here now\n1. Open a video from Home\n2. Reuse the overlay control bar\n3. Enter fullscreen with the button, double-click, or F11");
    playButton_->setEnabled(false);
    updatePlayButton(PlaybackState::Idle);
    progressSlider_->setEnabled(false);
    {
        QSignalBlocker blocker(progressSlider_);
        progressSlider_->setRange(0, 0);
        progressSlider_->setValue(0);
    }
    isSliderScrubbing_ = false;
    updateProgressDisplay(0, 0);
    updateVolumeDisplay(volumeSlider_->value(), volumeSlider_->value() == 0);
    refreshViewportChrome();
    showFullscreenControls();
}

void VideoPlayerWindow::updatePlayButton(PlaybackState state)
{
    QString tooltipText;

    switch (state)
    {
    case PlaybackState::Idle:
        tooltipText = "No video selected";
        break;
    case PlaybackState::Opening:
        tooltipText = "Opening video";
        break;
    case PlaybackState::Prepared:
    case PlaybackState::Stopped:
        tooltipText = "Play";
        break;
    case PlaybackState::Playing:
        tooltipText = "Pause";
        break;
    case PlaybackState::Paused:
        tooltipText = "Resume playback";
        break;
    case PlaybackState::Error:
        tooltipText = "Retry playback";
        break;
    }

    playButton_->setIcon(playButtonIcon(state));
    playButton_->setText(QString());
    playButton_->setToolTip(tooltipText);
}

void VideoPlayerWindow::updateProgressDisplay(int positionMs, int durationMs)
{
    currentTimeLabel_->setText(formatPlaybackTime(positionMs));
    durationTimeLabel_->setText(formatPlaybackTime(durationMs));
}

void VideoPlayerWindow::updateVolumeDisplay(int volume, bool muted)
{
    muteButton_->setIcon(volumeButtonIcon(volume, muted));
    muteButton_->setText(QString());
    muteButton_->setToolTip(muted || volume <= 0 ? "Unmute" : "Mute");
    volumeValueLabel_->setText(QString("%1%").arg(volume));
}

void VideoPlayerWindow::toggleFullscreen()
{
    setFullscreen(!isFullscreen_);
}

void VideoPlayerWindow::setFullscreen(bool fullscreen)
{
    if (isFullscreen_ == fullscreen)
    {
        return;
    }

    if (fullscreen)
    {
        normalGeometry_ = geometry();
        restoreMaximized_ = isMaximized();
    }

    isFullscreen_ = fullscreen;
    applyWindowMode(fullscreen);

    if (fullscreen)
    {
        showFullScreen();
        showFullscreenControls();
        scheduleFullscreenControlsHide();
    }
    else
    {
        fullscreenOverlayTimer_->stop();
        setMouseCursorHidden(false);
        showNormal();
        if (restoreMaximized_)
        {
            showMaximized();
        }
        else if (normalGeometry_.isValid())
        {
            setGeometry(normalGeometry_);
        }
        showFullscreenControls();
    }

    updateFullscreenButton();
    refreshViewportChrome();
    positionOverlayWidgets();
}

void VideoPlayerWindow::applyWindowMode(bool fullscreen)
{
    if (fullscreen)
    {
        setStyleSheet("background: #020617;");
        windowLayout_->setContentsMargins(0, 0, 0, 0);
        windowLayout_->setSpacing(0);
        contentLayout_->setSpacing(0);
        playerSurfaceLayout_->setContentsMargins(0, 0, 0, 0);
        headerWidget_->hide();
        sidePanel_->hide();
    }
    else
    {
        setStyleSheet(windowStyleSheet());
        windowLayout_->setContentsMargins(kWindowMargin, kWindowMargin, kWindowMargin, kWindowMargin);
        windowLayout_->setSpacing(kWindowSpacing);
        contentLayout_->setSpacing(kContentSpacing);
        playerSurfaceLayout_->setContentsMargins(20, 20, 20, 20);
        headerWidget_->show();
        sidePanel_->show();
    }

    playerSurface_->setStyleSheet(playerSurfaceStyle(fullscreen));
    videoViewport_->setStyleSheet(videoViewportStyle(fullscreen));
}

void VideoPlayerWindow::updateFullscreenButton()
{
    fullscreenButton_->setIcon(playerControlIcon("fullscreen.png"));
    fullscreenButton_->setText(QString());
    fullscreenButton_->setToolTip(isFullscreen_ ? "Exit fullscreen" : "Enter fullscreen");
}

void VideoPlayerWindow::refreshViewportChrome()
{
    const bool showTopOverlay =
        !isFullscreen_ ||
        !playerController_.hasMediaLoaded() ||
        playerController_.playbackState() == PlaybackState::Error;
    topOverlayWidget_->setVisible(showTopOverlay);
    positionOverlayWidgets();
}

void VideoPlayerWindow::positionOverlayWidgets()
{
    if (!videoViewport_ || !videoSurfaceWidget_)
    {
        return;
    }

    videoSurfaceWidget_->setGeometry(videoViewport_->rect());

    if (topOverlayWidget_->isVisible())
    {
        const int overlayWidth = qMax(0, qMin(videoViewport_->width() - (kOverlayMargin * 2), 560));
        topOverlayWidget_->setGeometry(
            kOverlayMargin,
            kOverlayMargin,
            overlayWidth,
            topOverlayWidget_->sizeHint().height());
        topOverlayWidget_->raise();
    }

    const int availableWidth = qMax(0, videoViewport_->width() - (kOverlayMargin * 2));
    const int preferredWidth = qMin(availableWidth, 1040);
    const int left = qMax(0, (videoViewport_->width() - preferredWidth) / 2);
    const int top = qMax(kOverlayMargin,
                         videoViewport_->height() - controlBar_->sizeHint().height() - kOverlayMargin);
    controlBar_->setGeometry(left, top, preferredWidth, controlBar_->sizeHint().height());
    controlBar_->raise();
}

void VideoPlayerWindow::showFullscreenControls()
{
    controlBarOpacityAnimation_->stop();
    controlBar_->show();
    controlBar_->raise();
    setMouseCursorHidden(false);

    if (!isFullscreen_)
    {
        controlBarOpacityEffect_->setOpacity(1.0);
        return;
    }

    controlBarOpacityAnimation_->setStartValue(controlBarOpacityEffect_->opacity());
    controlBarOpacityAnimation_->setEndValue(1.0);
    controlBarOpacityAnimation_->start();
}

void VideoPlayerWindow::hideFullscreenControls()
{
    if (!isFullscreen_ || isSliderScrubbing_ || controlBar_->underMouse())
    {
        scheduleFullscreenControlsHide();
        setMouseCursorHidden(false);
        return;
    }

    controlBarOpacityAnimation_->stop();
    controlBarOpacityAnimation_->setStartValue(controlBarOpacityEffect_->opacity());
    controlBarOpacityAnimation_->setEndValue(0.0);
    controlBarOpacityAnimation_->start();
    setMouseCursorHidden(true);
}

void VideoPlayerWindow::scheduleFullscreenControlsHide()
{
    if (!isFullscreen_)
    {
        fullscreenOverlayTimer_->stop();
        setMouseCursorHidden(false);
        return;
    }

    if (isSliderScrubbing_)
    {
        return;
    }

    fullscreenOverlayTimer_->start();
}

void VideoPlayerWindow::setMouseCursorHidden(bool hidden)
{
    if (!isFullscreen_)
    {
        hidden = false;
    }

    if (isMouseCursorHidden_ == hidden)
    {
        return;
    }

    isMouseCursorHidden_ = hidden;
    if (hidden)
    {
        setCursor(Qt::BlankCursor);
        return;
    }

    unsetCursor();
}

void VideoPlayerWindow::installInteractionTracking(QWidget *widget)
{
    if (!widget)
    {
        return;
    }

    widget->setMouseTracking(true);
    widget->setAttribute(Qt::WA_Hover, true);
    widget->installEventFilter(this);
}
