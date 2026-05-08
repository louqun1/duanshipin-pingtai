#include "pages/StreamPage/StreamPage.hpp"

#include "liveplayer/service/LivePlayerController.hpp"
#include "widgets/VideoOpenGLWidget.hpp"

#include <QFrame>
#include <QGridLayout>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QStringList>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <spdlog/spdlog.h>

namespace frontend::pages {

using PlaybackState = backend::liveplayer::service::LivePlayerController::PlaybackState;

namespace {

constexpr int kPresenceHeartbeatIntervalMs = 60 * 1000;

QFrame *createSidebarSection(QWidget *parent, const QString &title, QVBoxLayout **sectionLayout)
{
    auto *section = new QFrame(parent);
    section->setStyleSheet(
        "background: #f8fafc;"
        "border: 1px solid #e2e8f0;"
        "border-radius: 16px;");

    auto *layout = new QVBoxLayout(section);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(10);

    auto *titleLabel = new QLabel(title, section);
    titleLabel->setStyleSheet("font-size: 14px; font-weight: 700; color: #0f172a;");
    layout->addWidget(titleLabel);

    if (sectionLayout) {
        *sectionLayout = layout;
    }

    return section;
}

QLabel *createInfoValueLabel(QWidget *parent)
{
    auto *label = new QLabel(parent);
    label->setWordWrap(false);
    label->setStyleSheet("font-size: 13px; font-weight: 600; color: #0f172a;");
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

QLabel *createStatusValueLabel(QWidget *parent)
{
    auto *label = new QLabel(parent);
    label->setWordWrap(false);
    label->setStyleSheet("font-size: 13px; font-weight: 600; color: #0f172a;");
    return label;
}

void setValueTone(QLabel *label, const QString &color)
{
    if (!label) {
        return;
    }

    label->setStyleSheet(
        QString("font-size: 13px; font-weight: 600; color: %1;").arg(color));
}

QLabel *createRowLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setStyleSheet("font-size: 12px; font-weight: 700; color: #475569;");
    return label;
}

QToolButton *createHelpButton(const QString &tooltip, QWidget *parent)
{
    auto *help = new QToolButton(parent);
    help->setText("?");
    help->setAutoRaise(true);
    help->setCursor(Qt::PointingHandCursor);
    help->setToolTip(tooltip);
    help->setFixedSize(18, 18);
    help->setStyleSheet(
        "QToolButton {"
        "  border: 1px solid #cbd5e1;"
        "  border-radius: 9px;"
        "  color: #475569;"
        "  background: #ffffff;"
        "  font-size: 11px;"
        "  font-weight: 700;"
        "}"
        "QToolButton:hover { background: #eff6ff; color: #1d4ed8; }");
    return help;
}

QString stateLabelText(PlaybackState state)
{
    switch (state) {
    case PlaybackState::Idle:
        return QStringLiteral("Idle");
    case PlaybackState::Connecting:
        return QStringLiteral("Connecting");
    case PlaybackState::Reading:
        return QStringLiteral("Watching");
    case PlaybackState::Playing:
        return QStringLiteral("Watching");
    case PlaybackState::Stopped:
        return QStringLiteral("Stopped");
    case PlaybackState::Error:
        return QStringLiteral("Error");
    }

    return QStringLiteral("Unknown");
}

QString apiBaseUrl()
{
    const QString configured = qEnvironmentVariable("FLASHPOINT_API_BASE_URL").trimmed();
    if (!configured.isEmpty()) {
        return configured;
    }

    // return QStringLiteral("http://192.168.99.128:8080");
    return QStringLiteral("http://192.168.3.28:8080");
}

QString normalizeRoomKey(const QString &roomKey)
{
    return roomKey.trimmed().toLower();
}

QString liveRoomPresenceUrl(const QString &baseUrl, const QString &roomKey)
{
    return QString("%1/api/live-rooms/%2/presence").arg(baseUrl, roomKey);
}

QJsonObject parseReplyObject(const QByteArray &bytes)
{
    const QJsonDocument document = QJsonDocument::fromJson(bytes);
    if (!document.isObject()) {
        return QJsonObject();
    }

    return document.object();
}

QString extractApiErrorMessage(const QString &fallback, const QJsonObject &object)
{
    const QString apiError = object.value("error").toString().trimmed();
    return apiError.isEmpty() ? fallback : apiError;
}

}  // namespace

StreamPage::StreamPage(
    backend::liveplayer::service::LivePlayerController &livePlayerController,
    QWidget *parent)
    : QWidget(parent)
    , livePlayerController_(livePlayerController)
{
    networkManager_ = new QNetworkAccessManager(this);
    presenceHeartbeatTimer_ = new QTimer(this);
    presenceHeartbeatTimer_->setInterval(kPresenceHeartbeatIntervalMs);
    presenceHeartbeatTimer_->setSingleShot(false);
    connect(presenceHeartbeatTimer_, &QTimer::timeout, this, [this]() {
        if (activePresenceRoomKey_.isEmpty() || activePresenceAuthToken_.isEmpty()) {
            return;
        }

        sendPresenceAction(
            QStringLiteral("heartbeat"),
            activePresenceRoomKey_,
            activePresenceAuthToken_,
            true);
    });

    buildUi();
    connectController();
    livePlayerController_.attachVideoSurface(liveVideoSurface_);
    updateState(PlaybackState::Idle, "Live HTTP-FLV learning pipeline is ready.");
    updatePresenceStatus("Presence idle.\nSign in and enter a room key to report join/heartbeat/leave.");
    updateStats(0, 0, 0, 0);
    appendLog("Current milestone: HTTP chunk -> FLV tag -> H.264(ffmpeg) -> VideoOpenGLWidget. Audio stays TODO(user).");
}

void StreamPage::setAuthToken(const QString &token)
{
    const QString normalized = token.trimmed();
    if (authToken_ == normalized) {
        return;
    }

    const QString previousAuthToken = authToken_;
    const QString previousPresenceToken = activePresenceAuthToken_;
    authToken_ = normalized;

    if (authToken_.isEmpty()) {
        if (!activePresenceRoomKey_.isEmpty()) {
            appendLog("Auth token cleared while room presence is active; sending leave.");

            const QString roomKeyToLeave = activePresenceRoomKey_;
            const QString tokenToLeave = previousPresenceToken.isEmpty()
                ? previousAuthToken
                : previousPresenceToken;

            if (presenceHeartbeatTimer_) {
                presenceHeartbeatTimer_->stop();
            }
            activePresenceRoomKey_.clear();
            activePresenceAuthToken_.clear();
            presenceJoined_ = false;

            updatePresenceStatus(QString("Signing out; leaving room %1 ...").arg(roomKeyToLeave));
            sendPresenceAction(QStringLiteral("leave"), roomKeyToLeave, tokenToLeave, false);
            return;
        }

        updatePresenceStatus("Presence idle.\nSign in and enter a room key to report join/heartbeat/leave.");
        return;
    }

    if (!watchedRoomKey_.isEmpty() && activePresenceRoomKey_.isEmpty()) {
        appendLog("Auth token restored while live watch is running; retrying room presence join.");
        ensurePresenceForCurrentWatch();
        return;
    }

    if (!activePresenceRoomKey_.isEmpty() && previousPresenceToken != authToken_) {
        appendLog("Auth token changed while room presence is active; refreshing the presence session.");

        const QString roomKeyToLeave = activePresenceRoomKey_;
        const QString tokenToLeave = previousPresenceToken;

        if (presenceHeartbeatTimer_) {
            presenceHeartbeatTimer_->stop();
        }
        activePresenceRoomKey_.clear();
        activePresenceAuthToken_.clear();
        presenceJoined_ = false;

        sendPresenceAction(QStringLiteral("leave"), roomKeyToLeave, tokenToLeave, false);
        ensurePresenceForCurrentWatch();
    }
}

void StreamPage::hideEvent(QHideEvent *event)
{
    stopWatching(true, "Stream page hidden; stopped live watch and room presence.");
    QWidget::hideEvent(event);
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
        "Render surface for the current HTTP-FLV -> FLV -> FFmpeg video path.",
        videoViewport_);
    hintValueLabel_->setWordWrap(true);
    hintValueLabel_->setStyleSheet("font-size: 13px; color: rgba(226, 232, 240, 0.8);");
    viewportLayout->addWidget(hintValueLabel_);

    previewLayout->addWidget(videoViewport_, 1);
    contentLayout->addWidget(previewPanel, 2);

    auto *sideScrollArea = new QScrollArea(this);
    sideScrollArea->setWidgetResizable(true);
    sideScrollArea->setFrameShape(QFrame::NoFrame);
    sideScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sideScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    sideScrollArea->setMinimumWidth(320);
    sideScrollArea->setStyleSheet("background: transparent;");

    auto *sidePanel = new QFrame(sideScrollArea);
    sidePanel->setMinimumWidth(320);
    sidePanel->setStyleSheet(
        "background: #ffffff;"
        "border: 1px solid #dbe4f0;"
        "border-radius: 22px;");

    auto *sideLayout = new QVBoxLayout(sidePanel);
    sideLayout->setContentsMargins(18, 18, 18, 18);
    sideLayout->setSpacing(12);

    QVBoxLayout *watchControlLayout = nullptr;
    auto *watchControlSection = createSidebarSection(sidePanel, "Watch Control", &watchControlLayout);

    auto *streamLabel = new QLabel("HTTP-FLV URL", watchControlSection);
    streamLabel->setStyleSheet("font-size: 12px; font-weight: 700; color: #0f172a;");
    watchControlLayout->addWidget(streamLabel);

    streamUrlEdit_ = new QLineEdit("http://192.168.3.28:18080/live/livestream.flv", watchControlSection);
    streamUrlEdit_->setPlaceholderText("http://192.168.3.28:18080/live/livestream.flv");
    streamUrlEdit_->setClearButtonEnabled(true);
    streamUrlEdit_->setStyleSheet(
        "padding: 9px 12px;"
        "border: 1px solid #cbd5e1;"
        "border-radius: 10px;"
        "background: #f8fafc;"
        "color: #333333;");
    watchControlLayout->addWidget(streamUrlEdit_);

    auto *roomKeyLabel = new QLabel("Live Room Key", watchControlSection);
    roomKeyLabel->setStyleSheet("font-size: 12px; font-weight: 700; color: #0f172a;");
    watchControlLayout->addWidget(roomKeyLabel);

    roomKeyEdit_ = new QLineEdit(watchControlSection);
    roomKeyEdit_->setPlaceholderText("stage1-room");
    roomKeyEdit_->setClearButtonEnabled(true);
    roomKeyEdit_->setStyleSheet(
        "padding: 9px 12px;"
        "border: 1px solid #cbd5e1;"
        "border-radius: 10px;"
        "background: #f8fafc;"
        "color: #333333;");
    watchControlLayout->addWidget(roomKeyEdit_);

    auto *buttonRow = new QHBoxLayout();
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->setSpacing(8);

    startButton_ = new QPushButton("Start Watch", watchControlSection);
    startButton_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    startButton_->setStyleSheet(
        "padding: 9px 14px;"
        "border: 0;"
        "border-radius: 10px;"
        "font-weight: 700;"
        "color: #f8fafc;"
        "background: #0f766e;");
    buttonRow->addWidget(startButton_, 1);

    stopButton_ = new QPushButton("Stop", watchControlSection);
    stopButton_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    stopButton_->setStyleSheet(
        "padding: 9px 14px;"
        "border: 1px solid #cbd5e1;"
        "border-radius: 10px;"
        "font-weight: 700;"
        "color: #0f172a;"
        "background: #ffffff;");
    buttonRow->addWidget(stopButton_, 1);

    watchControlLayout->addLayout(buttonRow);
    sideLayout->addWidget(watchControlSection);

    QVBoxLayout *roomInfoLayout = nullptr;
    auto *roomInfoSection = createSidebarSection(sidePanel, "Room Info", &roomInfoLayout);
    auto *roomInfoGrid = new QGridLayout();
    roomInfoGrid->setContentsMargins(0, 0, 0, 0);
    roomInfoGrid->setHorizontalSpacing(8);
    roomInfoGrid->setVerticalSpacing(8);
    roomInfoGrid->setColumnStretch(1, 1);

    roomKeyValueLabel_ = createInfoValueLabel(roomInfoSection);
    presenceValueLabel_ = createInfoValueLabel(roomInfoSection);
    onlineCountValueLabel_ = createInfoValueLabel(roomInfoSection);

    roomInfoGrid->addWidget(createRowLabel("Room Key", roomInfoSection), 0, 0);
    roomInfoGrid->addWidget(roomKeyValueLabel_, 0, 1);

    roomInfoGrid->addWidget(createRowLabel("Presence", roomInfoSection), 1, 0);
    roomInfoGrid->addWidget(presenceValueLabel_, 1, 1);
    roomInfoGrid->addWidget(
        createHelpButton(
            "Room presence is reported separately through the room presence API and is not part of HTTP-FLV playback itself.",
            roomInfoSection),
        1,
        2,
        Qt::AlignTop);

    roomInfoGrid->addWidget(createRowLabel("Online Count", roomInfoSection), 2, 0);
    roomInfoGrid->addWidget(onlineCountValueLabel_, 2, 1);
    roomInfoGrid->addWidget(
        createHelpButton(
            "A dedicated viewer count backend is not wired yet, so this field stays 0 for now.",
            roomInfoSection),
        2,
        2,
        Qt::AlignTop);

    roomInfoLayout->addLayout(roomInfoGrid);
    sideLayout->addWidget(roomInfoSection);

    QVBoxLayout *streamStatusLayout = nullptr;
    auto *streamStatusSection = createSidebarSection(sidePanel, "Stream Status", &streamStatusLayout);
    auto *streamStatusGrid = new QGridLayout();
    streamStatusGrid->setContentsMargins(0, 0, 0, 0);
    streamStatusGrid->setHorizontalSpacing(8);
    streamStatusGrid->setVerticalSpacing(8);
    streamStatusGrid->setColumnStretch(1, 1);

    statusValueLabel_ = createStatusValueLabel(streamStatusSection);
    networkValueLabel_ = createStatusValueLabel(streamStatusSection);
    demuxValueLabel_ = createStatusValueLabel(streamStatusSection);
    videoValueLabel_ = createStatusValueLabel(streamStatusSection);
    audioValueLabel_ = createStatusValueLabel(streamStatusSection);
    avSyncValueLabel_ = createStatusValueLabel(streamStatusSection);

    streamStatusGrid->addWidget(createRowLabel("State", streamStatusSection), 0, 0);
    streamStatusGrid->addWidget(statusValueLabel_, 0, 1);
    streamStatusGrid->addWidget(
        createHelpButton(
            "Overall watch session state: idle, connecting, watching, stopped, or error.",
            streamStatusSection),
        0,
        2,
        Qt::AlignTop);

    streamStatusGrid->addWidget(createRowLabel("Network", streamStatusSection), 1, 0);
    streamStatusGrid->addWidget(networkValueLabel_, 1, 1);
    streamStatusGrid->addWidget(
        createHelpButton(
            "HTTP-FLV network receive state. It becomes receiving after response chunks arrive from QNetworkReply.",
            streamStatusSection),
        1,
        2,
        Qt::AlignTop);

    streamStatusGrid->addWidget(createRowLabel("Demux", streamStatusSection), 2, 0);
    streamStatusGrid->addWidget(demuxValueLabel_, 2, 1);
    streamStatusGrid->addWidget(
        createHelpButton(
            "FLV demux state. FlvDemuxer parses incoming bytes into script, audio, and video tags.",
            streamStatusSection),
        2,
        2,
        Qt::AlignTop);

    streamStatusGrid->addWidget(createRowLabel("Video", streamStatusSection), 3, 0);
    streamStatusGrid->addWidget(videoValueLabel_, 3, 1);
    streamStatusGrid->addWidget(
        createHelpButton(
            "H.264 video decode/render state. Video tags are parsed and sent to FFmpeg decoder before rendering.",
            streamStatusSection),
        3,
        2,
        Qt::AlignTop);

    streamStatusGrid->addWidget(createRowLabel("Audio", streamStatusSection), 4, 0);
    streamStatusGrid->addWidget(audioValueLabel_, 4, 1);
    streamStatusGrid->addWidget(
        createHelpButton(
            "Audio path state. AAC tag detection may exist, but full audio output can be implemented in a later milestone.",
            streamStatusSection),
        4,
        2,
        Qt::AlignTop);

    streamStatusGrid->addWidget(createRowLabel("A/V Sync", streamStatusSection), 5, 0);
    streamStatusGrid->addWidget(avSyncValueLabel_, 5, 1);
    streamStatusGrid->addWidget(
        createHelpButton(
            "A/V sync state. Current milestone may render video first; audio clock and sync policy can be added later.",
            streamStatusSection),
        5,
        2,
        Qt::AlignTop);

    streamStatusLayout->addLayout(streamStatusGrid);
    sideLayout->addWidget(streamStatusSection);

    auto *debugSection = new QFrame(sidePanel);
    debugSection->setStyleSheet(
        "background: #f8fafc;"
        "border: 1px solid #e2e8f0;"
        "border-radius: 16px;");
    auto *debugLayout = new QVBoxLayout(debugSection);
    debugLayout->setContentsMargins(10, 10, 10, 10);
    debugLayout->setSpacing(8);

    auto *debugToggleButton = new QToolButton(debugSection);
    debugToggleButton->setText("Debug Details");
    debugToggleButton->setCheckable(true);
    debugToggleButton->setChecked(false);
    debugToggleButton->setArrowType(Qt::RightArrow);
    debugToggleButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    debugToggleButton->setStyleSheet(
        "QToolButton {"
        "  border: 0;"
        "  padding: 4px 2px;"
        "  font-size: 14px;"
        "  font-weight: 700;"
        "  color: #0f172a;"
        "}"
        "QToolButton:hover { color: #0f766e; }");
    debugLayout->addWidget(debugToggleButton);

    auto *debugContent = new QWidget(debugSection);
    auto *debugContentLayout = new QVBoxLayout(debugContent);
    debugContentLayout->setContentsMargins(4, 0, 4, 4);
    debugContentLayout->setSpacing(0);

    auto *debugDetailsLabel = new QLabel(
        "Current code path:\n"
        "QNetworkReply chunk\n"
        "-> FlvDemuxer tag\n"
        "-> AVC sequence header\n"
        "-> avcodec_send_packet\n"
        "-> avcodec_receive_frame\n"
        "-> render surface\n\n"
        "Data flow:\n"
        "Network chunks come from Qt QNetworkReply, then FlvDemuxer turns bytes into script/audio/video tags.\n\n"
        "Pipeline notes:\n"
        "Video decode/render is wired in the current milestone. Presence is reported through the room presence API, separate from HTTP-FLV playback.\n\n"
        "Milestone notes:\n"
        "Audio output and A/V sync stay as later milestones.",
        debugContent);
    debugDetailsLabel->setWordWrap(true);
    debugDetailsLabel->setStyleSheet("font-size: 13px; color: #334155; line-height: 1.45;");
    debugContentLayout->addWidget(debugDetailsLabel);

    debugContent->setVisible(false);
    connect(debugToggleButton, &QToolButton::toggled, this, [debugToggleButton, debugContent](bool checked) {
        debugToggleButton->setArrowType(checked ? Qt::DownArrow : Qt::RightArrow);
        debugContent->setVisible(checked);
    });

    debugLayout->addWidget(debugContent);
    sideLayout->addWidget(debugSection);
    sideLayout->addStretch();

    sideScrollArea->setWidget(sidePanel);
    contentLayout->addWidget(sideScrollArea, 1);
    layout->addLayout(contentLayout, 1);

}

void StreamPage::connectController()
{
    connect(startButton_, &QPushButton::clicked, this, &StreamPage::requestStartWatch);

    connect(stopButton_, &QPushButton::clicked, this, [this]() {
        stopWatching(true, "Stopped live watch and room presence.");
    });

    connect(roomKeyEdit_, &QLineEdit::textChanged, this, [this]() {
        updateRoomInfo();
    });

    connect(&livePlayerController_,
            &backend::liveplayer::service::LivePlayerController::playbackStateChanged,
            this,
            [this](PlaybackState state, const QString &message) {
                updateState(state, message);

                if (state == PlaybackState::Stopped) {
                    stopWatching(false, "Live playback stopped; leaving room presence.");
                } else if (state == PlaybackState::Error) {
                    stopWatching(false, "Live playback failed; leaving room presence.");
                }
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

void StreamPage::applyAuthHeader(QNetworkRequest &request, const QString &token) const
{
    const QString normalized = token.trimmed();
    if (normalized.isEmpty()) {
        return;
    }

    request.setRawHeader("Authorization", QByteArray("Bearer ") + normalized.toUtf8());
}

void StreamPage::appendLog(const QString &message)
{
    if (message.trimmed().isEmpty()) {
        return;
    }

    const QByteArray utf8Message = message.toUtf8();
    spdlog::info("[stream/page] {}", utf8Message.constData());
}

void StreamPage::ensurePresenceForCurrentWatch()
{
    if (watchedRoomKey_.isEmpty()) {
        updatePresenceStatus("Presence idle.\nEnter a room key to report join/heartbeat/leave.");
        return;
    }

    if (authToken_.isEmpty()) {
        updatePresenceStatus(
            QString("Presence waiting.\nRoom %1 is configured, but sign-in is required before join can be reported.")
                .arg(watchedRoomKey_));
        appendLog("Presence join skipped because there is no auth token.");
        return;
    }

    if (activePresenceRoomKey_ == watchedRoomKey_ &&
        activePresenceAuthToken_ == authToken_ &&
        presenceJoined_)
    {
        if (presenceHeartbeatTimer_ && !presenceHeartbeatTimer_->isActive()) {
            presenceHeartbeatTimer_->start();
        }
        updatePresenceStatus(
            QString("Presence active.\nRoom: %1\nHeartbeat: every %2 seconds")
                .arg(watchedRoomKey_)
                .arg(kPresenceHeartbeatIntervalMs / 1000));
        return;
    }

    if (!activePresenceRoomKey_.isEmpty() &&
        (activePresenceRoomKey_ != watchedRoomKey_ || activePresenceAuthToken_ != authToken_))
    {
        appendLog(
            QString("Switching room presence from %1 to %2.")
                .arg(activePresenceRoomKey_, watchedRoomKey_));
        sendPresenceAction(
            QStringLiteral("leave"),
            activePresenceRoomKey_,
            activePresenceAuthToken_,
            false);
    }

    if (presenceHeartbeatTimer_) {
        presenceHeartbeatTimer_->stop();
    }
    activePresenceRoomKey_ = watchedRoomKey_;
    activePresenceAuthToken_ = authToken_;
    presenceJoined_ = false;

    updatePresenceStatus(QString("Joining room %1 ...").arg(activePresenceRoomKey_));
    appendLog(QString("Sending presence join for room %1.").arg(activePresenceRoomKey_));
    sendPresenceAction(
        QStringLiteral("join"),
        activePresenceRoomKey_,
        activePresenceAuthToken_,
        true);
}

void StreamPage::handlePresenceReply(
    QNetworkReply *reply,
    const QString &action,
    const QString &roomKey,
    const QString &token,
    bool tracksActiveSession)
{
    if (!reply) {
        return;
    }

    const QByteArray responseBytes = reply->readAll();
    const QJsonObject responseObject = parseReplyObject(responseBytes);
    const QNetworkReply::NetworkError networkError = reply->error();
    const QString replyErrorString = reply->errorString();
    reply->deleteLater();

    const bool isCurrentActiveSession =
        tracksActiveSession &&
        roomKey == activePresenceRoomKey_ &&
        token == activePresenceAuthToken_;

    if (networkError != QNetworkReply::NoError) {
        const QString errorMessage = extractApiErrorMessage(replyErrorString, responseObject);
        appendLog(QString("Presence %1 failed for room %2: %3").arg(action, roomKey, errorMessage));

        if (action == "join" && isCurrentActiveSession) {
            presenceJoined_ = false;
            if (presenceHeartbeatTimer_) {
                presenceHeartbeatTimer_->stop();
            }
            updatePresenceStatus(
                QString("Failed to join room %1.\n%2").arg(roomKey, errorMessage));
        } else if (action == "leave") {
            updatePresenceStatus(
                QString("Presence idle.\nLeave for room %1 failed: %2").arg(roomKey, errorMessage));
        } else if (action == "heartbeat" && isCurrentActiveSession) {
            updatePresenceStatus(
                QString("Presence active, but heartbeat failed for room %1.\n%2")
                    .arg(roomKey, errorMessage));
        }
        return;
    }

    const QJsonObject roomObject = responseObject.value("room").toObject();
    const int onlineMemberCount = roomObject.value("onlineMemberCount").toInt(-1);
    const int memberCount = responseObject.value("members").toArray().size();

    if (action == "leave") {
        appendLog(QString("Presence leave ok for room %1.").arg(roomKey));
        updatePresenceStatus(QString("Presence idle.\nLeft room %1.").arg(roomKey));
        return;
    }

    if (!isCurrentActiveSession) {
        appendLog(
            QString("Presence %1 reply for room %2 ignored because the active watch changed.")
                .arg(action, roomKey));
        return;
    }

    presenceJoined_ = true;
    if (presenceHeartbeatTimer_ && !presenceHeartbeatTimer_->isActive()) {
        presenceHeartbeatTimer_->start();
    }

    QStringList lines;
    lines << "Presence active";
    lines << QString("Room: %1").arg(roomKey);
    if (onlineMemberCount >= 0) {
        lines << QString("Online members: %1").arg(onlineMemberCount);
    } else if (responseObject.contains("members")) {
        lines << QString("Members in response: %1").arg(memberCount);
    }
    lines << QString("Heartbeat: every %1 seconds").arg(kPresenceHeartbeatIntervalMs / 1000);
    updatePresenceStatus(lines.join('\n'));

    const QString countDetail = onlineMemberCount >= 0
        ? QString::number(onlineMemberCount)
        : QString::number(memberCount);
    appendLog(
        QString("Presence %1 ok for room %2. online_count=%3")
            .arg(action, roomKey, countDetail));
}

void StreamPage::requestStartWatch()
{
    const QString liveUrl = streamUrlEdit_ ? streamUrlEdit_->text().trimmed() : QString();
    if (liveUrl.isEmpty()) {
        appendLog("Enter an HTTP-FLV URL before starting.");
        updatePresenceStatus("Presence idle.\nStart watch after entering a live URL.");
        return;
    }

    const QString roomKey = normalizeRoomKey(roomKeyEdit_ ? roomKeyEdit_->text() : QString());
    const bool hasExistingWatch =
        !watchedStreamUrl_.isEmpty() || !watchedRoomKey_.isEmpty() || !activePresenceRoomKey_.isEmpty();
    if (hasExistingWatch && (watchedStreamUrl_ != liveUrl || watchedRoomKey_ != roomKey)) {
        stopWatching(false, "Restarting previous room presence before opening a new stream.");
    }

    watchedStreamUrl_ = liveUrl;
    watchedRoomKey_ = roomKey;

    appendLog(QString("Start requested for %1").arg(liveUrl));
    if (watchedRoomKey_.isEmpty()) {
        appendLog("Room key is empty; skipping room presence join.");
        updatePresenceStatus("Presence idle.\nEnter a room key to report join/heartbeat/leave.");
    } else {
        ensurePresenceForCurrentWatch();
    }

    livePlayerController_.openStream(liveUrl);
}

void StreamPage::sendPresenceAction(
    const QString &action,
    const QString &roomKey,
    const QString &token,
    bool tracksActiveSession)
{
    if (roomKey.trimmed().isEmpty()) {
        return;
    }
    if (token.trimmed().isEmpty()) {
        appendLog(QString("Presence %1 skipped for room %2 because auth token is empty.").arg(action, roomKey));
        return;
    }

    QNetworkRequest request(QUrl(liveRoomPresenceUrl(apiBaseUrl(), roomKey)));
    request.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute,
        QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    applyAuthHeader(request, token);

    QJsonObject payload;
    payload.insert(QStringLiteral("action"), action);

    auto *reply =
        networkManager_->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, action, roomKey, token, tracksActiveSession]() {
        handlePresenceReply(reply, action, roomKey, token, tracksActiveSession);
    });
}

void StreamPage::stopWatching(bool stopPlayback, const QString &reason)
{
    const bool hadWatchTarget = !watchedStreamUrl_.isEmpty() || !watchedRoomKey_.isEmpty();
    const bool hadPresence = !activePresenceRoomKey_.isEmpty();
    const PlaybackState playbackState = livePlayerController_.playbackState();
    const bool shouldStopPlayback =
        stopPlayback &&
        playbackState != PlaybackState::Idle &&
        playbackState != PlaybackState::Stopped;

    if (!hadWatchTarget && !hadPresence && !shouldStopPlayback) {
        return;
    }

    if (!reason.trimmed().isEmpty()) {
        appendLog(reason);
    }

    const QString roomKeyToLeave = activePresenceRoomKey_;
    const QString tokenToLeave = activePresenceAuthToken_.isEmpty()
        ? authToken_
        : activePresenceAuthToken_;

    watchedStreamUrl_.clear();
    watchedRoomKey_.clear();
    activePresenceRoomKey_.clear();
    activePresenceAuthToken_.clear();
    presenceJoined_ = false;

    if (presenceHeartbeatTimer_) {
        presenceHeartbeatTimer_->stop();
    }

    if (!roomKeyToLeave.isEmpty()) {
        updatePresenceStatus(QString("Leaving room %1 ...").arg(roomKeyToLeave));
        sendPresenceAction(QStringLiteral("leave"), roomKeyToLeave, tokenToLeave, false);
    } else if (authToken_.isEmpty()) {
        updatePresenceStatus("Presence idle.\nSign in and enter a room key to report join/heartbeat/leave.");
    } else {
        updatePresenceStatus("Presence idle.\nEnter a room key and start watch to join the room.");
    }

    if (shouldStopPlayback) {
        livePlayerController_.stopStream();
    }
}

QString StreamPage::currentRoomKeyDisplay() const
{
    if (!watchedRoomKey_.trimmed().isEmpty()) {
        return watchedRoomKey_.trimmed();
    }

    if (roomKeyEdit_) {
        const QString draftRoomKey = normalizeRoomKey(roomKeyEdit_->text());
        if (!draftRoomKey.isEmpty()) {
            return draftRoomKey;
        }
    }

    return QStringLiteral("--");
}

QString StreamPage::summarizePresenceValue(const QString &message) const
{
    const QString trimmed = message.trimmed();
    if (trimmed.isEmpty()) {
        return QStringLiteral("Idle");
    }

    if (trimmed.startsWith("Joining room", Qt::CaseInsensitive)) {
        return QStringLiteral("Joining");
    }
    if (trimmed.startsWith("Presence waiting", Qt::CaseInsensitive)) {
        return QStringLiteral("Waiting Auth");
    }
    if (trimmed.startsWith("Presence active", Qt::CaseInsensitive)) {
        return QStringLiteral("Active");
    }
    if (trimmed.startsWith("Leaving room", Qt::CaseInsensitive) ||
        trimmed.startsWith("Signing out; leaving", Qt::CaseInsensitive))
    {
        return QStringLiteral("Leaving");
    }
    if (trimmed.contains("failed", Qt::CaseInsensitive)) {
        return QStringLiteral("Error");
    }

    return QStringLiteral("Idle");
}

void StreamPage::updateRoomInfo()
{
    if (roomKeyValueLabel_) {
        const QString roomKey = currentRoomKeyDisplay();
        roomKeyValueLabel_->setText(roomKey);
        roomKeyValueLabel_->setToolTip(roomKey == "--"
            ? QStringLiteral("No room key is active yet.")
            : roomKey);
    }

    if (onlineCountValueLabel_) {
        onlineCountValueLabel_->setText(QStringLiteral("0"));
        onlineCountValueLabel_->setToolTip(
            QStringLiteral("A dedicated viewer count backend is not wired yet, so this field stays 0 for now."));
    }
}

void StreamPage::updateStreamStatus()
{
    const QString stateValue = stateLabelText(lastPlaybackState_);
    QString stateColor = QStringLiteral("#0f172a");
    if (lastPlaybackState_ == PlaybackState::Error) {
        stateColor = QStringLiteral("#dc2626");
    } else if (lastPlaybackState_ == PlaybackState::Connecting) {
        stateColor = QStringLiteral("#b45309");
    } else if (lastPlaybackState_ == PlaybackState::Reading || lastPlaybackState_ == PlaybackState::Playing) {
        stateColor = QStringLiteral("#0f766e");
    } else if (lastPlaybackState_ == PlaybackState::Idle || lastPlaybackState_ == PlaybackState::Stopped) {
        stateColor = QStringLiteral("#475569");
    }

    if (statusValueLabel_) {
        statusValueLabel_->setText(stateValue);
        statusValueLabel_->setToolTip(currentPlaybackMessage_);
        setValueTone(statusValueLabel_, stateColor);
    }

    QString networkValue = QStringLiteral("Waiting");
    QString networkColor = QStringLiteral("#475569");
    if (lastPlaybackState_ == PlaybackState::Error) {
        networkValue = QStringLiteral("Error");
        networkColor = QStringLiteral("#dc2626");
    } else if (lastPlaybackState_ == PlaybackState::Stopped) {
        networkValue = QStringLiteral("Stopped");
    } else if (lastPlaybackState_ == PlaybackState::Connecting) {
        networkValue = QStringLiteral("Connecting");
        networkColor = QStringLiteral("#b45309");
    } else if (lastBytesReceived_ > 0 || lastPlaybackState_ == PlaybackState::Reading ||
               lastPlaybackState_ == PlaybackState::Playing)
    {
        networkValue = QStringLiteral("Receiving");
        networkColor = QStringLiteral("#0f766e");
    }
    if (networkValueLabel_) {
        networkValueLabel_->setText(networkValue);
        networkValueLabel_->setToolTip(QString("Bytes received: %1").arg(lastBytesReceived_));
        setValueTone(networkValueLabel_, networkColor);
    }

    QString demuxValue = QStringLiteral("Waiting");
    QString demuxColor = QStringLiteral("#475569");
    const bool hasAnyTag =
        lastScriptTagCount_ > 0 || lastAudioTagCount_ > 0 || lastVideoTagCount_ > 0;
    if (lastPlaybackState_ == PlaybackState::Error) {
        demuxValue = QStringLiteral("Error");
        demuxColor = QStringLiteral("#dc2626");
    } else if (lastPlaybackState_ == PlaybackState::Stopped) {
        demuxValue = QStringLiteral("Stopped");
    } else if (hasAnyTag) {
        demuxValue = QStringLiteral("Parsing");
        demuxColor = QStringLiteral("#0f766e");
    }
    if (demuxValueLabel_) {
        demuxValueLabel_->setText(demuxValue);
        demuxValueLabel_->setToolTip(
            QString("Script tags: %1\nAudio tags: %2\nVideo tags: %3")
                .arg(lastScriptTagCount_)
                .arg(lastAudioTagCount_)
                .arg(lastVideoTagCount_));
        setValueTone(demuxValueLabel_, demuxColor);
    }

    QString videoValue = QStringLiteral("Ready");
    QString videoColor = QStringLiteral("#1d4ed8");
    if (lastPlaybackState_ == PlaybackState::Error) {
        videoValue = QStringLiteral("Error");
        videoColor = QStringLiteral("#dc2626");
    } else if (lastVideoTagCount_ > 0 || lastPlaybackState_ == PlaybackState::Playing) {
        videoValue = QStringLiteral("Active");
        videoColor = QStringLiteral("#0f766e");
    }
    if (videoValueLabel_) {
        videoValueLabel_->setText(videoValue);
        videoValueLabel_->setToolTip(QString("Video tags: %1").arg(lastVideoTagCount_));
        setValueTone(videoValueLabel_, videoColor);
    }

    QString audioValue = lastAudioTagCount_ > 0
        ? QStringLiteral("Tagged")
        : QStringLiteral("TODO");
    QString audioColor = lastAudioTagCount_ > 0
        ? QStringLiteral("#b45309")
        : QStringLiteral("#475569");
    if (audioValueLabel_) {
        audioValueLabel_->setText(audioValue);
        audioValueLabel_->setToolTip(QString("Audio tags: %1").arg(lastAudioTagCount_));
        setValueTone(audioValueLabel_, audioColor);
    }

    if (avSyncValueLabel_) {
        avSyncValueLabel_->setText(QStringLiteral("Pending"));
        avSyncValueLabel_->setToolTip(
            QStringLiteral("Audio clock and sync policy are not enabled in this milestone yet."));
        setValueTone(avSyncValueLabel_, QStringLiteral("#475569"));
    }
}

void StreamPage::updatePresenceStatus(const QString &message)
{
    if (!presenceValueLabel_) {
        return;
    }

    const QString summary = summarizePresenceValue(message);
    presenceValueLabel_->setText(summary);
    presenceValueLabel_->setToolTip(message);

    if (summary == "Active") {
        setValueTone(presenceValueLabel_, QStringLiteral("#0f766e"));
    } else if (summary == "Joining" || summary == "Waiting Auth" || summary == "Leaving") {
        setValueTone(presenceValueLabel_, QStringLiteral("#b45309"));
    } else if (summary == "Error") {
        setValueTone(presenceValueLabel_, QStringLiteral("#dc2626"));
    } else {
        setValueTone(presenceValueLabel_, QStringLiteral("#475569"));
    }

    updateRoomInfo();
}

void StreamPage::updateState(PlaybackState state, const QString &message)
{
    lastPlaybackState_ = state;
    currentPlaybackMessage_ = message;
    updateStreamStatus();

    const bool busy = (state == PlaybackState::Connecting || state == PlaybackState::Reading);
    startButton_->setEnabled(!busy);
    stopButton_->setEnabled(state != PlaybackState::Idle && state != PlaybackState::Stopped);

    if (state == PlaybackState::Playing) {
        hintValueLabel_->setText(
            "Decoded video frames are reaching this surface.");
    } else {
        hintValueLabel_->setText(
            "Render surface for the current HTTP-FLV -> FLV -> FFmpeg video path.");
    }
}

void StreamPage::updateStats(qint64 bytesReceived, int audioTagCount, int videoTagCount, int scriptTagCount)
{
    lastBytesReceived_ = bytesReceived;
    lastAudioTagCount_ = audioTagCount;
    lastVideoTagCount_ = videoTagCount;
    lastScriptTagCount_ = scriptTagCount;
    updateStreamStatus();
}

}  // namespace frontend::pages
