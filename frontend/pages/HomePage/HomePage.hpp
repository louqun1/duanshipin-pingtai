#pragma once

#include <QByteArray>
#include <QEvent>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QGridLayout;
class QLabel;
class QNetworkAccessManager;
class QNetworkReply;
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
        QString duration;
        QString status;
        QString coverUrl;
        QString playUrl;
    };

    void buildUi();
    void connectEventStream();
    void disconnectEventStream();
    void fetchFeed();
    void handleFeedReply(QNetworkReply *reply);
    void handleEventStreamFinished(QNetworkReply *reply);
    void handleEventStreamReadyRead(QNetworkReply *reply);
    void processEventStreamMessage(const QByteArray &message);
    void requestCardCover(const QString &coverUrl, frontend::components::VideoCard *card);
    void requestVideoDetail(const RemoteVideoItem &item);
    void handleVideoDetailReply(QNetworkReply *reply, RemoteVideoItem fallbackItem);
    void scheduleEventStreamReconnect();
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
};

}  // namespace frontend::pages
