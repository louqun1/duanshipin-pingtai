#include "pages/HomePage/HomePage.hpp"

#include "components/VideoCard/VideoCard.hpp"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QPointer>
#include <QResizeEvent>
#include <QScrollArea>
#include <QShowEvent>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace frontend::pages {

namespace {

constexpr int kEventStreamReconnectDelayMs = 2000;

QString formatDurationMs(qint64 durationMs)
{
    if (durationMs <= 0) {
        return "00:00";
    }

    const qint64 totalSeconds = durationMs / 1000;
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds % 3600) / 60;
    const qint64 seconds = totalSeconds % 60;

    if (hours > 0) {
        return QString("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0'));
    }

    return QString("%1:%2")
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

QString cardAccentStart(int index)
{
    static const QStringList palette = {
        "#2563eb", "#f97316", "#0f766e", "#7c3aed",
        "#dc2626", "#ca8a04", "#1d4ed8", "#059669"
    };
    return palette.at(index % palette.size());
}

QString cardAccentEnd(int index)
{
    static const QStringList palette = {
        "#38bdf8", "#fb7185", "#2dd4bf", "#c084fc",
        "#fb7185", "#facc15", "#22d3ee", "#34d399"
    };
    return palette.at(index % palette.size());
}

QString apiBaseUrl()
{
    const QString configured = qEnvironmentVariable("FLASHPOINT_API_BASE_URL").trimmed();
    if (!configured.isEmpty()) {
        return configured;
    }

    return QStringLiteral("http://192.168.99.128:8080");
}

QStringList apiBaseUrlCandidates()
{
    QStringList candidates;
    candidates << apiBaseUrl()
               << QStringLiteral("http://192.168.99.128:8080")
               << QStringLiteral("http://localhost:8080")
               << QStringLiteral("http://127.0.0.1:8080");
    candidates.removeDuplicates();
    return candidates;
}

QString apiVideosUrl(const QString &baseUrl)
{
    return QString("%1/api/videos").arg(baseUrl);
}

QString apiEventsUrl(const QString &baseUrl)
{
    return QString("%1/api/events").arg(baseUrl);
}

QString apiVideoDetailUrl(const QString &baseUrl, const QString &videoId)
{
    return QString("%1/api/videos/%2").arg(baseUrl, videoId);
}

bool isConnectionFailure(QNetworkReply::NetworkError error)
{
    return error == QNetworkReply::ConnectionRefusedError ||
           error == QNetworkReply::HostNotFoundError ||
           error == QNetworkReply::TimeoutError;
}

}  // namespace

HomePage::HomePage(QWidget *parent)
    : QWidget(parent)
{
    networkManager_ = new QNetworkAccessManager(this);//发送HTTP/HTTPS GET 请求，发送 POST 请求，下载文件，上传数据，处理网络响应等。
    eventStreamReconnectTimer_ = new QTimer(this);
    eventStreamReconnectTimer_->setInterval(kEventStreamReconnectDelayMs);
    eventStreamReconnectTimer_->setSingleShot(true);
    connect(eventStreamReconnectTimer_, &QTimer::timeout, this, [this]() {
        connectEventStream();
    });
    apiBaseUrls_ = apiBaseUrlCandidates();
    buildUi();
    setStatusMessage(QString("Connecting to %1 ...").arg(apiVideosUrl(apiBaseUrls_.value(apiBaseUrlIndex_))));
    fetchFeed();
}

void HomePage::refreshFeed()
{
    setStatusMessage(QString("Refreshing %1 ...").arg(apiVideosUrl(apiBaseUrls_.value(apiBaseUrlIndex_, apiBaseUrl()))));
    if (feedRequested_) {
        feedRefreshQueued_ = true;
        return;
    }
    fetchFeed();
}

bool HomePage::eventFilter(QObject *watched, QEvent *event)
{
    if (feedScrollArea_ &&
        watched == feedScrollArea_->viewport() &&
        (event->type() == QEvent::Resize || event->type() == QEvent::Show))
    {
        QMetaObject::invokeMethod(this, [this]() { relayoutCards(); }, Qt::QueuedConnection);
    }

    return QWidget::eventFilter(watched, event);
}

void HomePage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    relayoutCards();
}

void HomePage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    QMetaObject::invokeMethod(this, [this]() { relayoutCards(); }, Qt::QueuedConnection);
}

void HomePage::buildUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(32, 28, 32, 28);
    layout->setSpacing(20);

    auto *headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(16);

    auto *titleBlock = new QVBoxLayout();
    titleBlock->setContentsMargins(0, 0, 0, 0);
    titleBlock->setSpacing(6);
    headerLayout->addLayout(titleBlock, 1);

    feedStatsLabel_ = new QLabel(this);
    feedStatsLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    feedStatsLabel_->setStyleSheet(
        "padding: 12px 16px;"
        "border: 1px solid #dbe4f0;"
        "border-radius: 14px;"
        "background: #ffffff;"
        "font-size: 13px;"
        "font-weight: 600;"
        "color: #334155;");
    headerLayout->addWidget(feedStatsLabel_);

    layout->addLayout(headerLayout);

    feedScrollArea_ = new QScrollArea(this);
    feedScrollArea_->setFrameShape(QFrame::NoFrame);
    feedScrollArea_->setWidgetResizable(true);
    feedScrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    feedScrollArea_->setStyleSheet(
        "QScrollArea { background: transparent; }"
        "QScrollArea > QWidget > QWidget { background: transparent; }");
    feedScrollArea_->viewport()->installEventFilter(this);

    feedContainer_ = new QWidget(feedScrollArea_);
    feedGrid_ = new QGridLayout(feedContainer_);
    feedGrid_->setContentsMargins(0, 4, 0, 0);
    feedGrid_->setHorizontalSpacing(18);
    feedGrid_->setVerticalSpacing(18);
    feedGrid_->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    feedScrollArea_->setWidget(feedContainer_);
    layout->addWidget(feedScrollArea_, 1);
}

void HomePage::connectEventStream()
{
    const QString baseUrl = activeApiBaseUrl_.isEmpty()
        ? apiBaseUrls_.value(apiBaseUrlIndex_, apiBaseUrl())
        : activeApiBaseUrl_;
    if (baseUrl.isEmpty()) {
        return;
    }

    if (eventStreamReply_ && eventStreamBaseUrl_ == baseUrl) {
        return;
    }

    disconnectEventStream();
    if (eventStreamReconnectTimer_) {
        eventStreamReconnectTimer_->stop();
    }

    eventStreamBaseUrl_ = baseUrl;
    eventStreamBuffer_.clear();

    QNetworkRequest request(QUrl(apiEventsUrl(baseUrl)));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setRawHeader("Accept", "text/event-stream");

    auto *reply = networkManager_->get(request);
    eventStreamReply_ = reply;

    connect(reply, &QNetworkReply::readyRead, this, [this, reply]() {
        handleEventStreamReadyRead(reply);
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleEventStreamFinished(reply);
    });
}

void HomePage::disconnectEventStream()
{
    if (!eventStreamReply_) {
        eventStreamBuffer_.clear();
        eventStreamBaseUrl_.clear();
        return;
    }

    auto *reply = eventStreamReply_;
    eventStreamReply_ = nullptr;
    eventStreamBuffer_.clear();
    eventStreamBaseUrl_.clear();

    disconnect(reply, nullptr, this, nullptr);
    reply->abort();
    reply->deleteLater();
}

void HomePage::fetchFeed()
{
    if (feedRequested_) {
        return;
    }

    feedRequested_ = true;
    const QString baseUrl = apiBaseUrls_.value(apiBaseUrlIndex_, apiBaseUrl());
    auto *reply = networkManager_->get(QNetworkRequest(QUrl(apiVideosUrl(baseUrl))));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {//这次请求完成后，自动调用 handleFeedReply(reply)
        handleFeedReply(reply);
    });
}

void HomePage::handleFeedReply(QNetworkReply *reply)
{
    const QString attemptedBaseUrl = apiBaseUrls_.value(apiBaseUrlIndex_, apiBaseUrl());
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        if (isConnectionFailure(reply->error()) && (apiBaseUrlIndex_ + 1) < apiBaseUrls_.size()) {
            ++apiBaseUrlIndex_;
            const QString nextBaseUrl = apiBaseUrls_.at(apiBaseUrlIndex_);
            setStatusMessage(
                QString("Failed to reach %1: %2. Retrying %3 ...")
                    .arg(apiVideosUrl(attemptedBaseUrl), reply->errorString(), apiVideosUrl(nextBaseUrl)));

            auto *retryReply = networkManager_->get(QNetworkRequest(QUrl(apiVideosUrl(nextBaseUrl))));
            connect(retryReply, &QNetworkReply::finished, this, [this, retryReply]() {
                handleFeedReply(retryReply);
            });
            return;
        }

        feedRequested_ = false;
        setStatusMessage(
            QString("Failed to load %1: %2. Start server/cmd/api or set FLASHPOINT_API_BASE_URL.")
                .arg(apiVideosUrl(attemptedBaseUrl), reply->errorString()));
        if (feedRefreshQueued_) {
            feedRefreshQueued_ = false;
            fetchFeed();
        }
        return;
    }

    const auto document = QJsonDocument::fromJson(reply->readAll());
    if (!document.isObject()) {
        feedRequested_ = false;
        setStatusMessage(QString("Invalid response from %1.").arg(apiVideosUrl(attemptedBaseUrl)));
        if (feedRefreshQueued_) {
            feedRefreshQueued_ = false;
            fetchFeed();
        }
        return;
    }

    feedRequested_ = false;
    activeApiBaseUrl_ = attemptedBaseUrl;

    const auto items = document.object().value("items").toArray();
    clearCards();
    feedItems_.clear();
    cards_.reserve(items.size());
    feedItems_.reserve(items.size());

    for (int index = 0; index < items.size(); ++index) {
        const auto object = items.at(index).toObject();

        RemoteVideoItem item;
        item.id = QString::number(object.value("id").toVariant().toLongLong());
        item.title = object.value("title").toString("Untitled video");
        item.uploaderUsername = object.value("uploaderUsername").toString();
        item.duration = formatDurationMs(object.value("durationMs").toVariant().toLongLong());
        item.status = object.value("status").toString();
        item.coverUrl = object.value("coverUrl").toString();
        item.playUrl = object.value("playUrl").toString();
        item.creator = item.uploaderUsername.isEmpty()
            ? QString("Status %1").arg(item.status)
            : QString("@%1").arg(item.uploaderUsername);

        feedItems_.append(item);

        frontend::components::VideoCardData cardData{
            item.id,
            item.title,
            item.creator,
            item.duration,
            cardAccentStart(index),
            cardAccentEnd(index)
        };

        auto *card = new frontend::components::VideoCard(cardData, feedContainer_);
        requestCardCover(item.coverUrl, card);
        connect(card, &QPushButton::clicked, this, [this, item]() {
            requestVideoDetail(item);
        });
        cards_.append(card);
    }

    connectEventStream();
    relayoutCards();
    setStatusMessage(QString("Loaded %1 videos from %2").arg(cards_.size()).arg(apiVideosUrl(activeApiBaseUrl_)));
    if (feedRefreshQueued_) {
        feedRefreshQueued_ = false;
        fetchFeed();
    }
}

void HomePage::handleEventStreamReadyRead(QNetworkReply *reply)
{
    if (!reply || reply != eventStreamReply_) {
        return;
    }

    eventStreamBuffer_.append(reply->readAll());

    int messageBoundary = eventStreamBuffer_.indexOf("\n\n");
    while (messageBoundary >= 0) {
        const QByteArray message = eventStreamBuffer_.left(messageBoundary);
        eventStreamBuffer_.remove(0, messageBoundary + 2);
        processEventStreamMessage(message);
        messageBoundary = eventStreamBuffer_.indexOf("\n\n");
    }
}

void HomePage::handleEventStreamFinished(QNetworkReply *reply)
{
    if (!reply) {
        return;
    }

    const bool isActiveReply = (reply == eventStreamReply_);
    if (isActiveReply) {
        eventStreamReply_ = nullptr;
        eventStreamBuffer_.clear();
    }
    reply->deleteLater();

    if (isActiveReply) {
        scheduleEventStreamReconnect();
    }
}

void HomePage::processEventStreamMessage(const QByteArray &message)
{
    QByteArray eventName;
    bool hasData = false;

    for (QByteArray line : message.split('\n')) {
        if (line.endsWith('\r')) {
            line.chop(1);
        }
        if (line.isEmpty() || line.startsWith(':')) {
            continue;
        }

        const int separatorIndex = line.indexOf(':');
        const QByteArray fieldName = separatorIndex >= 0 ? line.left(separatorIndex) : line;
        QByteArray fieldValue = separatorIndex >= 0 ? line.mid(separatorIndex + 1) : QByteArray();
        if (fieldValue.startsWith(' ')) {
            fieldValue.remove(0, 1);
        }

        if (fieldName == "event") {
            eventName = fieldValue;
        } else if (fieldName == "data") {
            hasData = true;
        }
    }

    if (eventName == "video.updated" && hasData) {
        refreshFeed();
    }
}

void HomePage::requestCardCover(const QString &coverUrl, frontend::components::VideoCard *card)
{
    if (!card) {
        return;
    }

    const QUrl imageUrl(coverUrl);
    if (!imageUrl.isValid() || imageUrl.isEmpty()) {
        return;
    }

    QNetworkRequest request(imageUrl);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    auto *reply = networkManager_->get(request);
    const QPointer<frontend::components::VideoCard> safeCard(card);
    connect(reply, &QNetworkReply::finished, this, [reply, safeCard]() {
        reply->deleteLater();

        if (!safeCard || reply->error() != QNetworkReply::NoError) {
            return;
        }

        QPixmap pixmap;
        if (!pixmap.loadFromData(reply->readAll())) {
            return;
        }

        safeCard->setPosterPixmap(pixmap);
    });
}

void HomePage::requestVideoDetail(const RemoteVideoItem &item)
{
    const QString baseUrl = activeApiBaseUrl_.isEmpty()
        ? apiBaseUrls_.value(apiBaseUrlIndex_, apiBaseUrl())
        : activeApiBaseUrl_;
    setStatusMessage(QString("Loading %1 ...").arg(apiVideoDetailUrl(baseUrl, item.id)));

    auto *reply = networkManager_->get(QNetworkRequest(QUrl(apiVideoDetailUrl(baseUrl, item.id))));
    connect(reply, &QNetworkReply::finished, this, [this, reply, item]() {
        handleVideoDetailReply(reply, item);
    });
}

void HomePage::handleVideoDetailReply(QNetworkReply *reply, RemoteVideoItem fallbackItem)
{
    const QString baseUrl = activeApiBaseUrl_.isEmpty()
        ? apiBaseUrls_.value(apiBaseUrlIndex_, apiBaseUrl())
        : activeApiBaseUrl_;
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        setStatusMessage(
            QString("Failed to load %1: %2")
                .arg(apiVideoDetailUrl(baseUrl, fallbackItem.id), reply->errorString()));
        return;
    }

    const auto document = QJsonDocument::fromJson(reply->readAll());
    if (!document.isObject()) {
        setStatusMessage(QString("Invalid response from %1.").arg(apiVideoDetailUrl(baseUrl, fallbackItem.id)));
        return;
    }

    const auto object = document.object();
    const QString playUrl = object.value("playUrl").toString();
    const QString status = object.value("status").toString(fallbackItem.status);
    const QString title = object.value("title").toString(fallbackItem.title);
    const QString duration = formatDurationMs(object.value("durationMs").toVariant().toLongLong());

    if (status != "ready" || playUrl.isEmpty()) {
        setStatusMessage(QString("Video %1 is not ready yet. Current status: %2").arg(fallbackItem.id, status));
        return;
    }

    emit playRequested(
        playUrl,
        fallbackItem.id,
        title,
        fallbackItem.creator,
        duration);
    setStatusMessage(QString("Opening %1").arg(title));
}

void HomePage::scheduleEventStreamReconnect()
{
    if (!eventStreamReconnectTimer_ || activeApiBaseUrl_.isEmpty()) {
        return;
    }

    if (!eventStreamReconnectTimer_->isActive()) {
        eventStreamReconnectTimer_->start();
    }
}

void HomePage::relayoutCards()
{
    if (!feedScrollArea_ || !feedGrid_ || cards_.isEmpty()) {
        return;
    }

    while (auto *item = feedGrid_->takeAt(0)) {
        delete item;
    }

    const QMargins margins = feedGrid_->contentsMargins();
    const int availableWidth = feedScrollArea_->viewport()->width() - margins.left() - margins.right();
    if (availableWidth <= 0) {
        return;
    }

    constexpr int preferredCardWidth = 248;
    constexpr int minimumCardWidth = 216;
    const int spacing = feedGrid_->horizontalSpacing();

    int columns = qMax(1, (availableWidth + spacing) / (preferredCardWidth + spacing));
    while (columns > 1) {
        const int candidateWidth = (availableWidth - ((columns - 1) * spacing)) / columns;
        if (candidateWidth >= minimumCardWidth) {
            break;
        }

        --columns;
    }

    const int cardWidth = qMax(
        minimumCardWidth,
        (availableWidth - ((columns - 1) * spacing)) / columns);

    for (int index = 0; index < cards_.size(); ++index) {
        auto *card = cards_.at(index);
        card->setCardWidth(cardWidth);
        feedGrid_->addWidget(card, index / columns, index % columns, Qt::AlignTop | Qt::AlignLeft);
    }

    const int estimatedVisibleRows = qMax(1, feedScrollArea_->viewport()->height() / 410);
    const int visibleCardCount = qMin(cards_.size(), estimatedVisibleRows * columns);
    feedStatsLabel_->setText(
        QString("%1 videos  |  %2 per row  |  about %3 visible")
            .arg(cards_.size())
            .arg(columns)
            .arg(visibleCardCount));
}

void HomePage::clearCards()
{
    qDeleteAll(cards_);
    cards_.clear();
}

void HomePage::setStatusMessage(const QString &message)
{
    if (feedStatsLabel_) {
        feedStatsLabel_->setText(message);
    }
}

}  // namespace frontend::pages
