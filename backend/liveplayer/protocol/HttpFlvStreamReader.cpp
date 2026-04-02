#include "liveplayer/protocol/HttpFlvStreamReader.hpp"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace backend::liveplayer::protocol {

HttpFlvStreamReader::HttpFlvStreamReader(QObject *parent)
    : QObject(parent)
    , networkManager_(new QNetworkAccessManager(this))
{
}

void HttpFlvStreamReader::open(const QUrl &url)
{
    close();

    if (!url.isValid() || url.scheme().isEmpty()) {
        emit errorOccurred("Live URL is invalid.");
        return;
    }

    QNetworkRequest request(url);
    request.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute,
        QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader, "FlashpointLivePlayer/0.1");
    request.setRawHeader("Accept", "*/*");

    headersObserved_ = false;
    activeReply_ = networkManager_->get(request);//请求发出后会触发QNetworkReply的metaDataChanged、readyRead、finished、errorOccurred等信号，分别对应HTTP响应头到达、数据块到达、请求完成和请求错误事件。

    emit logMessage(QString("HTTP-FLV GET %1").arg(url.toString()));

    connect(activeReply_, &QNetworkReply::metaDataChanged,//HTTP响应头到达事件，表示服务器已经发送了响应头，客户端可以通过QNetworkReply的header()函数获取响应头信息，如Content-Type、Content-Length等。
            this, &HttpFlvStreamReader::handleMetaDataChanged);
    connect(activeReply_, &QIODevice::readyRead,//数据块到达事件，表示服务器已经发送了一部分响应体数据，客户端可以通过QNetworkReply的read()或readAll()函数读取这些数据块。
            this, &HttpFlvStreamReader::handleReadyRead);
    connect(activeReply_, &QNetworkReply::finished,//请求完成事件，表示服务器已经完成了响应的发送，客户端可以通过QNetworkReply的isFinished()函数检查请求是否完成，并进行相应的处理，如关闭连接、释放资源等。
            this, &HttpFlvStreamReader::handleFinished);
    connect(activeReply_, &QNetworkReply::errorOccurred,
            this, [this](QNetworkReply::NetworkError) {
                if (!activeReply_) {
                    return;
                }

                emit errorOccurred(activeReply_->errorString());
            });

    // TODO(user): add timeout / retry policy after the one-shot learning path is stable.
}

void HttpFlvStreamReader::close()
{
    if (!activeReply_) {
        return;
    }

    disconnect(activeReply_, nullptr, this, nullptr);
    activeReply_->abort();
    activeReply_->deleteLater();
    activeReply_.clear();
    headersObserved_ = false;
}

bool HttpFlvStreamReader::isActive() const
{
    return !activeReply_.isNull();
}

void HttpFlvStreamReader::handleMetaDataChanged()
{
    if (!activeReply_ || headersObserved_) {
        return;
    }

    headersObserved_ = true;

    const int statusCode = activeReply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString contentType = activeReply_->header(QNetworkRequest::ContentTypeHeader).toString();

    emit connected(contentType, statusCode);
}

void HttpFlvStreamReader::handleReadyRead()
{
    if (!activeReply_) {
        return;
    }

    const QByteArray chunk = activeReply_->readAll();
    if (chunk.isEmpty()) {
        return;
    }

    emit dataChunkReceived(chunk);
}

void HttpFlvStreamReader::handleFinished()
{
    if (!activeReply_) {
        return;
    }

    activeReply_->deleteLater();
    activeReply_.clear();
    headersObserved_ = false;
    emit finished();
}

}  // namespace backend::liveplayer::protocol
