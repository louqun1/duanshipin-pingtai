#pragma once

#include <QObject>
#include <QPointer>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

namespace backend::liveplayer::protocol {

class HttpFlvStreamReader final : public QObject
{
    Q_OBJECT

public:
    explicit HttpFlvStreamReader(QObject *parent = nullptr);

    void open(const QUrl &url);
    void close();
    bool isActive() const;
    void setReadThrottled(bool throttled, qint64 bufferBytes);

signals:
    void connected(const QString &contentType, int statusCode);
    void dataChunkReceived(const QByteArray &chunk);
    void finished();
    void errorOccurred(const QString &message);
    void logMessage(const QString &message);

private slots:
    void handleMetaDataChanged();
    void handleReadyRead();
    void handleFinished();

private:
    QNetworkAccessManager *networkManager_ = nullptr;
    QPointer<QNetworkReply> activeReply_;
    bool headersObserved_ = false;
};

}  // namespace backend::liveplayer::protocol
