#include "widgets/VideoOpenGLWidget.hpp"
#include "widgets/VideoPlayerWindow.hpp"

#include "playercontroller/service/PlayerController.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

using PlaybackState = backend::playercontroller::service::PlayerController::PlaybackState;

VideoPlayerWindow::VideoPlayerWindow(
    backend::playercontroller::service::PlayerController &playerController,
    QWidget *parent)
    : QWidget(parent)
    , playerController_(playerController)
{
    buildUi();
    connectPlayerController();
    playerController_.attachVideoSurface(videoSurfaceWidget_);
    showEmptyState();
}

void VideoPlayerWindow::showSelectedVideo(
    const QString &videoId,
    const QString &title,
    const QString &creator,
    const QString &duration)
{
    playerTitleLabel_->setText("Opening from Home feed...");
    playerHintLabel_->setText(QString("Forwarding OpenMedia to PlayerController for %1.").arg(videoId));
    playButton_->setText("Opening...");
    playButton_->setEnabled(false);

    playerController_.openMedia(videoId, title, creator, duration);
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
                    "此面板可以在 ijkPlayer 集成于控制器之后持续渲染元数据.");
                queueLabel_->setText(
                    QString("Next player steps\n1. Create or reuse ijkPlayer\n2. Bind render output to this window\n3. Translate engine callbacks into UI events for %1").arg(creator));
            });

    connect(&playerController_, &backend::playercontroller::service::PlayerController::playbackStateChanged,
            this, [this](PlaybackState state, const QString &message) {
                switch (state) {
                case PlaybackState::Idle:
                    playerTitleLabel_->setText("No video selected yet");
                    playButton_->setText("Waiting for selection");
                    playButton_->setEnabled(false);
                    break;
                case PlaybackState::Opening:
                    playerTitleLabel_->setText("PlayerController is opening media");
                    playButton_->setText("Opening...");
                    playButton_->setEnabled(false);
                    break;
                case PlaybackState::Prepared:
                    playerTitleLabel_->setText("PlayerController prepared the media");
                    playButton_->setText("Play");
                    playButton_->setEnabled(true);
                    break;
                case PlaybackState::Playing:
                    playerTitleLabel_->setText("Playback is running through PlayerController");
                    playButton_->setText("Pause");
                    playButton_->setEnabled(true);
                    break;
                case PlaybackState::Paused:
                    playerTitleLabel_->setText("Playback paused");
                    playButton_->setText("Resume");
                    playButton_->setEnabled(true);
                    break;
                case PlaybackState::Stopped:
                    playerTitleLabel_->setText("Playback stopped");
                    playButton_->setText("Play");
                    playButton_->setEnabled(true);
                    break;
                case PlaybackState::Error:
                    playerTitleLabel_->setText("Playback error");
                    playButton_->setText("Retry");
                    playButton_->setEnabled(true);
                    break;
                }

                playerHintLabel_->setText(message);
            });

    connect(&playerController_, &backend::playercontroller::service::PlayerController::ijkPlayerCreated,
            this, [this]() {
                playerHintLabel_->setText("PlayerController created the ijkPlayer placeholder instance.");
            });

    connect(&playerController_, &backend::playercontroller::service::PlayerController::ijkPlayerOpenRequested,
            this, [this](const QString &videoId, const QString &title) {
                playerHintLabel_->setText(
                    QString("Reserved ijkPlayer call: open %1 (%2).").arg(title, videoId));
            });

    connect(playButton_, &QPushButton::clicked,
            &playerController_, &backend::playercontroller::service::PlayerController::requestTogglePlayback);
}

void VideoPlayerWindow::buildUi()
{
    setAttribute(Qt::WA_QuitOnClose, false);
    setWindowTitle("Video Player");
    resize(1120, 760);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(18);

    auto *headerTitle = new QLabel("Short-video player", this);
    headerTitle->setStyleSheet("font-size: 28px; font-weight: 700; color: #0f172a;");
    layout->addWidget(headerTitle);

    auto *headerSummary = new QLabel(
        "This independent window is opened from HomePage video cards. Actual playback can replace the left-side preview surface later.",
        this);
    headerSummary->setWordWrap(true);
    headerSummary->setStyleSheet("font-size: 14px; color: #475569;");
    layout->addWidget(headerSummary);

    auto *contentLayout = new QHBoxLayout();
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(20);

    playerSurface_ = new QFrame(this);
    playerSurface_->setMinimumSize(640, 420);
    playerSurface_->setStyleSheet(
        "background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #0f172a, stop:1 #1d4ed8);"
        "border-radius: 26px;");

    auto *playerLayout = new QVBoxLayout(playerSurface_);
    playerLayout->setContentsMargins(28, 28, 28, 28);
    playerLayout->setSpacing(12);

    videoSurfaceWidget_ = new VideoOpenGLWidget(playerSurface_);
    videoSurfaceWidget_->setMinimumSize(584, 320);
    playerLayout->addWidget(videoSurfaceWidget_, 1);

    playerTitleLabel_ = new QLabel(playerSurface_);
    playerTitleLabel_->setStyleSheet("font-size: 26px; font-weight: 700; color: #f8fafc;");
    playerLayout->addWidget(playerTitleLabel_);

    playerHintLabel_ = new QLabel(playerSurface_);
    playerHintLabel_->setWordWrap(true);
    playerHintLabel_->setStyleSheet("font-size: 14px; color: rgba(248, 250, 252, 0.82);");
    playerLayout->addWidget(playerHintLabel_);
    playerLayout->addStretch();

    auto *playerFooter = new QHBoxLayout();
    playerFooter->setContentsMargins(0, 0, 0, 0);
    playerFooter->setSpacing(12);

    playButton_ = new QPushButton(playerSurface_);
    playButton_->setCursor(Qt::PointingHandCursor);
    playButton_->setStyleSheet(
        "QPushButton {"
        "  padding: 12px 18px;"
        "  border: 0;"
        "  border-radius: 14px;"
        "  background: #f8fafc;"
        "  color: #0f172a;"
        "  font-size: 14px;"
        "  font-weight: 700;"
        "}"
        "QPushButton:disabled {"
        "  background: rgba(248, 250, 252, 0.28);"
        "  color: rgba(248, 250, 252, 0.72);"
        "}");
    playerFooter->addWidget(playButton_, 0, Qt::AlignLeft);
    playerFooter->addStretch();
    playerLayout->addLayout(playerFooter);

    contentLayout->addWidget(playerSurface_, 2);

    auto *sidePanel = new QFrame(this);
    sidePanel->setMinimumWidth(300);
    sidePanel->setStyleSheet(
        "background: #ffffff;"
        "border: 1px solid #dbe4f0;"
        "border-radius: 22px;");

    auto *sideLayout = new QVBoxLayout(sidePanel);
    sideLayout->setContentsMargins(22, 22, 22, 22);
    sideLayout->setSpacing(16);

    auto *infoEyebrow = new QLabel("CURRENT VIDEO", sidePanel);
    infoEyebrow->setStyleSheet(
        "font-size: 11px; font-weight: 700; letter-spacing: 1px; color: #2563eb;");
    sideLayout->addWidget(infoEyebrow);

    videoTitleLabel_ = new QLabel(sidePanel);
    videoTitleLabel_->setWordWrap(true);
    videoTitleLabel_->setStyleSheet("font-size: 22px; font-weight: 700; color: #0f172a;");
    sideLayout->addWidget(videoTitleLabel_);

    videoMetaLabel_ = new QLabel(sidePanel);
    videoMetaLabel_->setWordWrap(true);
    videoMetaLabel_->setStyleSheet("font-size: 13px; color: #475569;");
    sideLayout->addWidget(videoMetaLabel_);

    videoDescriptionLabel_ = new QLabel(sidePanel);
    videoDescriptionLabel_->setWordWrap(true);
    videoDescriptionLabel_->setStyleSheet("font-size: 14px; color: #334155;");
    sideLayout->addWidget(videoDescriptionLabel_);

    queueLabel_ = new QLabel(sidePanel);
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

    contentLayout->addWidget(sidePanel, 1);
    layout->addLayout(contentLayout, 1);
}

void VideoPlayerWindow::showEmptyState()
{
    playerTitleLabel_->setText("No video selected yet");
    playerHintLabel_->setText(
        "Open any short-video card from HomePage. The selected item will be sent to PlayerController from this window.");
    videoTitleLabel_->setText("Choose a short video from Home");
    videoMetaLabel_->setText("No card has been opened yet.");
    videoDescriptionLabel_->setText(
        "This window now owns the PlayerController connection and is ready to host the real player surface later.");
    queueLabel_->setText(
        "What will fit here next\n1. ijkPlayer render output\n2. Playback progress and transport controls\n3. Related actions and comments");
    playButton_->setText("Waiting for selection");
    playButton_->setEnabled(false);
}
