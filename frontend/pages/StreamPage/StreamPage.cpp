#include "pages/StreamPage/StreamPage.hpp"

#include "liveplayer/service/LivePlayerController.hpp"

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
    livePlayerController_.attachVideoSurface(videoViewport_);
    updateState(PlaybackState::Idle, "Live player skeleton is ready.");
    updateStats(0, 0, 0, 0);
    appendLog("The current milestone already covers HTTP-FLV reading, FLV header parsing, and tag-level demux diagnostics.");
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
        "This page is now wired to a minimal HTTP-FLV learning skeleton. The service side can stay on RTMP ingest + HTTP-FLV output, while you focus on the player path here.",
        this);
    summary->setWordWrap(true);
    summary->setStyleSheet("font-size: 14px; color: #475569;");
    layout->addWidget(summary);

    auto *contentLayout = new QHBoxLayout();
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(20);

    auto *previewPanel = new QFrame(this);
    previewPanel->setMinimumSize(640, 420);
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
        "Decoded live frames will eventually land on this surface. Right now the backend skeleton stops at HTTP-FLV reading and FLV parsing hooks.",
        previewPanel);
    previewHint->setWordWrap(true);
    previewHint->setStyleSheet("font-size: 14px; color: rgba(248, 250, 252, 0.82);");
    previewLayout->addWidget(previewHint);

    videoViewport_ = new QFrame(previewPanel);
    videoViewport_->setMinimumHeight(260);
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

    hintValueLabel_ = new QLabel(
        "HTTP-FLV reading and FLV tag parsing are wired. The next step is to hand video tags to your decoder and render frames here.",
        videoViewport_);
    hintValueLabel_->setWordWrap(true);
    hintValueLabel_->setStyleSheet("font-size: 13px; color: rgba(226, 232, 240, 0.8);");
    viewportLayout->addWidget(hintValueLabel_);
    viewportLayout->addStretch();

    previewLayout->addWidget(videoViewport_, 1);
    contentLayout->addWidget(previewPanel, 2);

    auto *sidePanel = new QFrame(this);
    sidePanel->setMinimumWidth(320);
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

    streamUrlEdit_ = new QLineEdit("http://192.168.99.128:8080/live/room1.flv", sidePanel);
    streamUrlEdit_->setPlaceholderText("http://192.168.99.128:8080/live/room1.flv");
    streamUrlEdit_->setStyleSheet(
        "padding: 10px 12px;"
        "border: 1px solid #cbd5e1;"
        "border-radius: 12px;"
        "background: #f8fafc;");
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
        "Keep the first loop small: OBS or FFmpeg pushes RTMP to a live server, the client reads HTTP-FLV, parses FLV tags, then hands samples into your live decoder path.",
        sidePanel);
    panelBody->setWordWrap(true);
    panelBody->setStyleSheet("font-size: 14px; color: #334155;");
    sideLayout->addWidget(panelBody);

    auto *checklist = new QLabel(
        "Current code path\n1. HTTP GET stream\n2. Validate FLV header\n3. Parse tag header and payload\n4. Split script, audio, and video tags\n5. Prepare decoder handoff",
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
    logOutput_->setMinimumHeight(180);
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
            "Media tags are already flowing into LivePlayerSession. Wire them into your decoder queues, then render the decoded frames on this surface.");
    }
}

void StreamPage::updateStats(qint64 bytesReceived, int audioTagCount, int videoTagCount, int scriptTagCount)
{
    statsValueLabel_->setText(
        QString("Bytes received: %1\nScript tags: %2\nAudio tags: %3\nVideo tags: %4")
            .arg(bytesReceived)
            .arg(scriptTagCount)
            .arg(audioTagCount)
            .arg(videoTagCount));
}

}  // namespace frontend::pages
