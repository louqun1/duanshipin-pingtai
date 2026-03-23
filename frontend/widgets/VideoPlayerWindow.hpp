#pragma once

#include "playercontroller/service/PlayerController.hpp"

#include <QCloseEvent>
#include <QString>
#include <QWidget>

class QLabel;
class QPushButton;
class QWidget;
class QFrame;
class QSlider;
class VideoOpenGLWidget;

class VideoPlayerWindow final : public QWidget
{
    Q_OBJECT

public:
    explicit VideoPlayerWindow(
        backend::playercontroller::service::PlayerController &playerController,
        QWidget *parent = nullptr);

    void showSelectedVideo(
        const QString &videoId,
        const QString &title,
        const QString &creator,
        const QString &duration);

private:
    void closeEvent(QCloseEvent *event) override;
    void buildUi();
    void connectPlayerController();
    void showEmptyState();
    void updateProgressDisplay(int positionMs, int durationMs);

    backend::playercontroller::service::PlayerController &playerController_;
    QFrame *playerSurface_ = nullptr;
    VideoOpenGLWidget *videoSurfaceWidget_ = nullptr;
    QLabel *playerTitleLabel_ = nullptr;
    QLabel *playerHintLabel_ = nullptr;
    QLabel *currentTimeLabel_ = nullptr;
    QLabel *durationTimeLabel_ = nullptr;
    QLabel *videoTitleLabel_ = nullptr;
    QLabel *videoMetaLabel_ = nullptr;
    QLabel *videoDescriptionLabel_ = nullptr;
    QLabel *queueLabel_ = nullptr;
    QSlider *progressSlider_ = nullptr;
    QPushButton *playButton_ = nullptr;
    bool isSliderScrubbing_ = false;
};
