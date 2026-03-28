#pragma once

#include "liveplayer/service/LivePlayerController.hpp"

#include <QWidget>

class QFrame;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

namespace frontend::pages {

class StreamPage final : public QWidget
{
    Q_OBJECT

public:
    explicit StreamPage(
        backend::liveplayer::service::LivePlayerController &livePlayerController,
        QWidget *parent = nullptr);

private:
    void buildUi();
    void connectController();
    void appendLog(const QString &message);
    void updateState(
        backend::liveplayer::service::LivePlayerController::PlaybackState state,
        const QString &message);
    void updateStats(qint64 bytesReceived, int audioTagCount, int videoTagCount, int scriptTagCount);

    backend::liveplayer::service::LivePlayerController &livePlayerController_;
    QFrame *videoViewport_ = nullptr;
    QLineEdit *streamUrlEdit_ = nullptr;
    QPushButton *startButton_ = nullptr;
    QPushButton *stopButton_ = nullptr;
    QLabel *statusValueLabel_ = nullptr;
    QLabel *statsValueLabel_ = nullptr;
    QLabel *hintValueLabel_ = nullptr;
    QPlainTextEdit *logOutput_ = nullptr;
};

}  // namespace frontend::pages
