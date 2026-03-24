#pragma once

#include "playercontroller/service/PlayerController.hpp"

#include <QCloseEvent>
#include <QKeyEvent>
#include <QRect>
#include <QResizeEvent>
#include <QString>
#include <QWidget>

class QGraphicsOpacityEffect;
class QHBoxLayout;
class QEvent;
class QLabel;
class QPropertyAnimation;
class QPushButton;
class QFrame;
class QSlider;
class QTimer;
class QVBoxLayout;
class VideoOpenGLWidget;

class VideoPlayerWindow final : public QWidget
{
    Q_OBJECT

public:
    explicit VideoPlayerWindow(
        backend::playercontroller::service::PlayerController &playerController,
        QWidget *parent = nullptr);

    void showSelectedVideo(
        const QString &mediaUrl,
        const QString &videoId,
        const QString &title,
        const QString &creator,
        const QString &duration);

private:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void buildUi();
    void connectPlayerController();
    void showEmptyState();
    void updatePlayButton(backend::playercontroller::service::PlayerController::PlaybackState state);
    void updateProgressDisplay(int positionMs, int durationMs);
    void updateVolumeDisplay(int volume, bool muted);
    void toggleFullscreen();
    void setFullscreen(bool fullscreen);
    void applyWindowMode(bool fullscreen);
    void updateFullscreenButton();
    void refreshViewportChrome();
    void positionOverlayWidgets();
    void showFullscreenControls();
    void hideFullscreenControls();
    void scheduleFullscreenControlsHide();
    void setMouseCursorHidden(bool hidden);
    void installInteractionTracking(QWidget *widget);

    backend::playercontroller::service::PlayerController &playerController_;
    QVBoxLayout *windowLayout_ = nullptr;
    QWidget *headerWidget_ = nullptr;
    QHBoxLayout *contentLayout_ = nullptr;
    QFrame *playerSurface_ = nullptr;
    QVBoxLayout *playerSurfaceLayout_ = nullptr;
    QFrame *videoViewport_ = nullptr;
    VideoOpenGLWidget *videoSurfaceWidget_ = nullptr;
    QWidget *topOverlayWidget_ = nullptr;
    QWidget *controlBar_ = nullptr;
    QFrame *sidePanel_ = nullptr;
    QLabel *playerTitleLabel_ = nullptr;
    QLabel *playerHintLabel_ = nullptr;
    QLabel *currentTimeLabel_ = nullptr;
    QLabel *durationTimeLabel_ = nullptr;
    QLabel *videoTitleLabel_ = nullptr;
    QLabel *videoMetaLabel_ = nullptr;
    QLabel *videoDescriptionLabel_ = nullptr;
    QLabel *queueLabel_ = nullptr;
    QSlider *progressSlider_ = nullptr;
    QSlider *volumeSlider_ = nullptr;
    QLabel *volumeValueLabel_ = nullptr;
    QPushButton *muteButton_ = nullptr;
    QPushButton *playButton_ = nullptr;
    QPushButton *fullscreenButton_ = nullptr;
    QGraphicsOpacityEffect *controlBarOpacityEffect_ = nullptr;
    QPropertyAnimation *controlBarOpacityAnimation_ = nullptr;
    QTimer *fullscreenOverlayTimer_ = nullptr;
    QRect normalGeometry_;
    bool restoreMaximized_ = false;
    bool isFullscreen_ = false;
    bool isMouseCursorHidden_ = false;
    bool isSliderScrubbing_ = false;
};
