#pragma once

#include <QByteArray>
#include <QEvent>
#include <QSet>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QGridLayout;
class QLabel;
class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;
class QResizeEvent;
class QScrollArea;
class QShowEvent;
class QTimer;

namespace frontend::components {
class VideoCard;
}

namespace frontend::pages {

class HomePage final : public QWidget
{
    Q_OBJECT

public:
    explicit HomePage(QWidget *parent = nullptr);
    void setAuthToken(const QString &token);
    void refreshFeed();

signals:
    void playRequested(
        const QString &mediaUrl,
        const QString &videoId,
        const QString &title,
        const QString &creator,
        const QString &duration);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    struct RemoteVideoItem {
        QString id;
        QString title;
        QString creator;
        QString uploaderUsername;
        QString duration;
        QString status;
        QString coverUrl;
        QString playUrl;
        qint64 likeCount = 0;
        bool likedByMe = false;
    };

    void applyAuthHeader(QNetworkRequest &request) const;
    void buildUi();
    void connectEventStream();
    void disconnectEventStream();
    void fetchFeed();
    void handleFeedReply(QNetworkReply *reply);
    void handleEventStreamFinished(QNetworkReply *reply);
    void handleEventStreamReadyRead(QNetworkReply *reply);
    void processEventStreamMessage(const QByteArray &message);
    void requestCardCover(const QString &coverUrl, frontend::components::VideoCard *card);
    void requestLikeToggle(const QString &videoId, bool shouldLike);
    void handleLikeToggleReply(QNetworkReply *reply, const QString &videoId);
    void requestVideoDetail(const RemoteVideoItem &item);
    void handleVideoDetailReply(QNetworkReply *reply, RemoteVideoItem fallbackItem);
    void scheduleEventStreamReconnect();
    frontend::components::VideoCard *findCardByVideoId(const QString &videoId) const;
    void updateVideoLikeState(const QString &videoId, qint64 likeCount, bool likedByMe);
    void relayoutCards();
    void clearCards();
    void setStatusMessage(const QString &message);

    QScrollArea *feedScrollArea_ = nullptr;
    QWidget *feedContainer_ = nullptr;
    QGridLayout *feedGrid_ = nullptr;
    QLabel *feedStatsLabel_ = nullptr;
    QNetworkAccessManager *networkManager_ = nullptr;
    QVector<frontend::components::VideoCard *> cards_;
    QVector<RemoteVideoItem> feedItems_;
    QStringList apiBaseUrls_;
    int apiBaseUrlIndex_ = 0;
    QString activeApiBaseUrl_;
    bool feedRequested_ = false;
    bool feedRefreshQueued_ = false;
    QNetworkReply *eventStreamReply_ = nullptr;
    QTimer *eventStreamReconnectTimer_ = nullptr;
    QByteArray eventStreamBuffer_;
    QString eventStreamBaseUrl_;
    QString authToken_;
    QSet<QString> pendingLikeVideoIds_;
};

}  // namespace frontend::pages
