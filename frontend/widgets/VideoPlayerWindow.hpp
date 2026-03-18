#pragma once

#include "playercontroller/service/PlayerController.hpp"

#include <QString>
#include <QWidget>

class QLabel;
class QPushButton;
class QWidget;

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
    void buildUi();
    void connectPlayerController();
    void showEmptyState();

    backend::playercontroller::service::PlayerController &playerController_;
    QWidget *playerSurface_ = nullptr;
    QLabel *playerTitleLabel_ = nullptr;
    QLabel *playerHintLabel_ = nullptr;
    QLabel *videoTitleLabel_ = nullptr;
    QLabel *videoMetaLabel_ = nullptr;
    QLabel *videoDescriptionLabel_ = nullptr;
    QLabel *queueLabel_ = nullptr;
    QPushButton *playButton_ = nullptr;
};
