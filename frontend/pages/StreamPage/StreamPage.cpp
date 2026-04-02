#include "pages/StreamPage/StreamPage.hpp"

#include "liveplayer/service/LivePlayerController.hpp"
#include "widgets/VideoOpenGLWidget.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTime>
#include <QVBoxLayout>

namespace frontend::pages {

using PlaybackState = backend::liveplayer::service::LivePlayerController::PlaybackState;

StreamPage::StreamPage(
    backend::liveplayer::service::LivePlayerController &livePlayerController,
    QWidget *parent)
    : QWidget(parent)
    , livePlayerController_(livePlayerController)
{
    buildUi();
    connectController();
    livePlayerController_.attachVideoSurface(liveVideoSurface_);
    updateState(PlaybackState::Idle, "Live HTTP-FLV learning pipeline is ready.");
    updateStats(0, 0, 0, 0);
    appendLog("Current milestone: HTTP chunk -> FLV tag -> H.264(ffmpeg) -> VideoOpenGLWidget. Audio stays TODO(user).");
}

void StreamPage::buildUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(32, 28, 32, 28);
    layout->setSpacing(20);

    auto *title = new QLabel("Live streaming lab", this);
    title->setStyleSheet("font-size: 28px; font-weight: 700; color: #0f172a;");
    layout->addWidget(title);

    auto *summary = new QLabel(
        "This page now uses the manual HTTP-FLV learning chain for live watch: Qt reads chunks, FlvDemuxer parses tags, then FFmpeg decodes H.264 video tags. AAC/audio output and A/V sync are intentionally left for the next milestone.",
        this);
    summary->setWordWrap(true);
    summary->setStyleSheet("font-size: 14px; color: #475569;");
    layout->addWidget(summary);

    auto *contentLayout = new QHBoxLayout();
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(20);

    auto *previewPanel = new QFrame(this);
    // Let the main window honor smaller initial widths. We only keep the vertical
    // floor here, and let the horizontal size be controlled by the layout stretch.
    previewPanel->setMinimumHeight(360);
    previewPanel->setStyleSheet(
        "background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #111827, stop:1 #0f766e);"
        "border-radius: 26px;");

    auto *previewLayout = new QVBoxLayout(previewPanel);
    previewLayout->setContentsMargins(28, 28, 28, 28);
    previewLayout->setSpacing(12);

    auto *previewTitle = new QLabel("Live viewport", previewPanel);
    previewTitle->setStyleSheet("font-size: 26px; font-weight: 700; color: #f8fafc;");
    previewLayout->addWidget(previewTitle);

    auto *previewHint = new QLabel(
        "The live viewport is fed by your own chunk/tag path. This first milestone only wires video decode/render so you can study the data flow end to end.",
        previewPanel);
    previewHint->setWordWrap(true);
    previewHint->setStyleSheet("font-size: 14px; color: rgba(248, 250, 252, 0.82);");
    previewLayout->addWidget(previewHint);

    videoViewport_ = new QFrame(previewPanel);
    videoViewport_->setMinimumHeight(220);
    videoViewport_->setStyleSheet(
        "background: rgba(15, 23, 42, 0.88);"
        "border: 1px solid rgba(226, 232, 240, 0.18);"
        "border-radius: 18px;");

    auto *viewportLayout = new QVBoxLayout(videoViewport_);
    viewportLayout->setContentsMargins(18, 18, 18, 18);
    viewportLayout->setSpacing(10);

    auto *viewportTitle = new QLabel("Render target placeholder", videoViewport_);
    viewportTitle->setStyleSheet("font-size: 22px; font-weight: 700; color: #e2e8f0;");
    viewportLayout->addWidget(viewportTitle);

    liveVideoSurface_ = new VideoOpenGLWidget(videoViewport_);
    liveVideoSurface_->setMinimumHeight(200);
    liveVideoSurface_->setStyleSheet(
        "background: rgba(2, 6, 23, 0.96);"
        "border-radius: 14px;");
    viewportLayout->addWidget(liveVideoSurface_, 1);

    hintValueLabel_ = new QLabel(
        "Data flow: QNetworkReply chunk -> FlvDemuxer tag -> AVC sequence header -> avcodec_send_packet/receive_frame -> this surface.",
        videoViewport_);
    hintValueLabel_->setWordWrap(true);
    hintValueLabel_->setStyleSheet("font-size: 13px; color: rgba(226, 232, 240, 0.8);");
    viewportLayout->addWidget(hintValueLabel_);

    previewLayout->addWidget(videoViewport_, 1);
    contentLayout->addWidget(previewPanel, 2);

    auto *sidePanel = new QFrame(this);
    sidePanel->setMinimumWidth(260);
    sidePanel->setStyleSheet(
        "background: #ffffff;"
        "border: 1px solid #dbe4f0;"
        "border-radius: 22px;");

    auto *sideLayout = new QVBoxLayout(sidePanel);
    sideLayout->setContentsMargins(22, 22, 22, 22);
    sideLayout->setSpacing(16);

    auto *eyebrow = new QLabel("LIVE MODULE", sidePanel);
    eyebrow->setStyleSheet(
        "font-size: 11px; font-weight: 700; letter-spacing: 1px; color: #0f766e;");
    sideLayout->addWidget(eyebrow);

    auto *panelTitle = new QLabel("HTTP-FLV watch entry", sidePanel);
    panelTitle->setWordWrap(true);
    panelTitle->setStyleSheet("font-size: 22px; font-weight: 700; color: #0f172a;");
    sideLayout->addWidget(panelTitle);

    auto *streamLabel = new QLabel("HTTP-FLV URL", sidePanel);
    streamLabel->setStyleSheet("font-size: 12px; font-weight: 700; color: #0f172a;");
    sideLayout->addWidget(streamLabel);

    streamUrlEdit_ = new QLineEdit("http://192.168.99.128:18080/live/livestream.flv", sidePanel);
    streamUrlEdit_->setPlaceholderText("http://192.168.99.128:18080/live/livestream.flv");
    streamUrlEdit_->setStyleSheet(
        "padding: 10px 12px;"
        "border: 1px solid #cbd5e1;"
        "border-radius: 12px;"
        "background: #f8fafc;"
        "color: #333333;");
    sideLayout->addWidget(streamUrlEdit_);

    auto *buttonRow = new QHBoxLayout();
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->setSpacing(10);

    startButton_ = new QPushButton("Start Watch", sidePanel);
    startButton_->setStyleSheet(
        "padding: 10px 14px;"
        "border: 0;"
        "border-radius: 12px;"
        "font-weight: 700;"
        "color: #f8fafc;"
        "background: #0f766e;");
    buttonRow->addWidget(startButton_);

    stopButton_ = new QPushButton("Stop", sidePanel);
    stopButton_->setStyleSheet(
        "padding: 10px 14px;"
        "border: 1px solid #cbd5e1;"
        "border-radius: 12px;"
        "font-weight: 700;"
        "color: #0f172a;"
        "background: #ffffff;");
    buttonRow->addWidget(stopButton_);

    sideLayout->addLayout(buttonRow);

    statusValueLabel_ = new QLabel(sidePanel);
    statusValueLabel_->setWordWrap(true);
    statusValueLabel_->setStyleSheet(
        "padding: 14px 16px;"
        "background: #eff6ff;"
        "border: 1px solid #bfdbfe;"
        "border-radius: 14px;"
        "font-size: 13px;"
        "color: #1d4ed8;");
    sideLayout->addWidget(statusValueLabel_);

    statsValueLabel_ = new QLabel(sidePanel);
    statsValueLabel_->setWordWrap(true);
    statsValueLabel_->setStyleSheet(
        "padding: 14px 16px;"
        "background: #f8fafc;"
        "border: 1px solid #e2e8f0;"
        "border-radius: 14px;"
        "font-size: 13px;"
        "color: #334155;");
    sideLayout->addWidget(statsValueLabel_);

    auto *panelBody = new QLabel(
        "This delivery stays intentionally small: verify HTTP-FLV, observe tag counts, decode H.264 video with FFmpeg, then render. AAC, audio output, queues, and A/V sync are left as TODO(user).",
        sidePanel);
    panelBody->setWordWrap(true);
    panelBody->setStyleSheet("font-size: 14px; color: #334155;");
    sideLayout->addWidget(panelBody);

    auto *checklist = new QLabel(
        "Current code path\n1. HTTP GET stream\n2. QByteArray chunk\n3. FLV header + tag parsing\n4. H.264 sequence header / NALU tag -> FFmpeg decoder\n5. AVFrame -> VideoOpenGLWidget",
        sidePanel);
    checklist->setWordWrap(true);
    checklist->setStyleSheet(
        "padding: 16px;"
        "background: #f8fafc;"
        "border: 1px solid #e2e8f0;"
        "border-radius: 16px;"
        "font-size: 13px;"
        "color: #334155;");
    sideLayout->addWidget(checklist);
    sideLayout->addStretch();

    contentLayout->addWidget(sidePanel, 1);
    layout->addLayout(contentLayout, 1);

    auto *logPanel = new QFrame(this);
    logPanel->setStyleSheet(
        "background: #ffffff;"
        "border: 1px solid #dbe4f0;"
        "border-radius: 22px;");

    auto *logLayout = new QVBoxLayout(logPanel);
    logLayout->setContentsMargins(22, 22, 22, 22);
    logLayout->setSpacing(12);

    auto *logTitle = new QLabel("Learning log", logPanel);
    logTitle->setStyleSheet("font-size: 20px; font-weight: 700; color: #0f172a;");
    logLayout->addWidget(logTitle);

    logOutput_ = new QPlainTextEdit(logPanel);
    logOutput_->setReadOnly(true);
    logOutput_->setStyleSheet(
        "border: 1px solid #e2e8f0;"
        "border-radius: 14px;"
        "padding: 12px;"
        "background: #0f172a;"
        "color: #e2e8f0;");
    logOutput_->setMinimumHeight(140);
    logLayout->addWidget(logOutput_);

    layout->addWidget(logPanel);
}

void StreamPage::connectController()
{
    connect(startButton_, &QPushButton::clicked, this, [this]() {
        const QString liveUrl = streamUrlEdit_->text().trimmed();
        appendLog(QString("Start requested for %1").arg(liveUrl));
        livePlayerController_.openStream(liveUrl);
    });

    connect(stopButton_, &QPushButton::clicked,
            &livePlayerController_, &backend::liveplayer::service::LivePlayerController::stopStream);

    connect(&livePlayerController_,
            &backend::liveplayer::service::LivePlayerController::playbackStateChanged,
            this,
            [this](PlaybackState state, const QString &message) {
                updateState(state, message);
            });

    connect(&livePlayerController_,
            &backend::liveplayer::service::LivePlayerController::sessionLogAppended,
            this,
            [this](const QString &message) {
                appendLog(message);
            });

    connect(&livePlayerController_,
            &backend::liveplayer::service::LivePlayerController::streamStatsChanged,
            this,
            [this](qint64 bytesReceived, int audioTagCount, int videoTagCount, int scriptTagCount) {
                updateStats(bytesReceived, audioTagCount, videoTagCount, scriptTagCount);
            });
}

void StreamPage::appendLog(const QString &message)
{
    if (!logOutput_) {
        return;
    }

    const QString timestamp = QTime::currentTime().toString("HH:mm:ss");
    logOutput_->appendPlainText(QString("[%1] %2").arg(timestamp, message));
}

void StreamPage::updateState(PlaybackState state, const QString &message)
{
    QString stateLabel;
    switch (state) {
    case PlaybackState::Idle:
        stateLabel = "Idle";
        break;
    case PlaybackState::Connecting:
        stateLabel = "Connecting";
        break;
    case PlaybackState::Reading:
        stateLabel = "Reading";
        break;
    case PlaybackState::Playing:
        stateLabel = "Playing";
        break;
    case PlaybackState::Stopped:
        stateLabel = "Stopped";
        break;
    case PlaybackState::Error:
        stateLabel = "Error";
        break;
    }

    statusValueLabel_->setText(QString("State: %1\n%2").arg(stateLabel, message));

    const bool busy = (state == PlaybackState::Connecting || state == PlaybackState::Reading);
    startButton_->setEnabled(!busy);
    stopButton_->setEnabled(state != PlaybackState::Idle && state != PlaybackState::Stopped);

    if (state == PlaybackState::Playing) {
        hintValueLabel_->setText(
            "Your own chunk/tag + FFmpeg H.264 path has produced decoded video frames. TODO(user): add AAC decode, audio output, and A/V sync.");
    }
}

void StreamPage::updateStats(qint64 bytesReceived, int audioTagCount, int videoTagCount, int scriptTagCount)
{
    if (bytesReceived == 0 && audioTagCount == 0 && videoTagCount == 0 && scriptTagCount == 0) {
        statsValueLabel_->setText(
            "Waiting for first HTTP chunk/tag.\nThis milestone counts bytes and FLV tags again.\nVideo decode is wired; audio is still TODO(user).");
        return;
    }

    statsValueLabel_->setText(
        QString("Bytes received: %1\nScript tags: %2\nAudio tags: %3\nVideo tags: %4")
            .arg(bytesReceived)
            .arg(scriptTagCount)
            .arg(audioTagCount)
            .arg(videoTagCount));
}

}  // namespace frontend::pages
