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
    activeReply_ = networkManager_->get(request);

    emit logMessage(QString("HTTP-FLV GET %1").arg(url.toString()));

    connect(activeReply_, &QNetworkReply::metaDataChanged,
            this, &HttpFlvStreamReader::handleMetaDataChanged);//服务端有响应
    connect(activeReply_, &QIODevice::readyRead,
            this, &HttpFlvStreamReader::handleReadyRead);//服务端有数据可读
    connect(activeReply_, &QNetworkReply::finished,
            this, &HttpFlvStreamReader::handleFinished);//服务端关闭连接或者发生错误
    connect(activeReply_, &QNetworkReply::errorOccurred,
            this, [this](QNetworkReply::NetworkError) {
                if (!activeReply_) {
                    return;
                }

                emit errorOccurred(activeReply_->errorString());
            });

    // TODO: add timeout / retry policy after the basic one-shot reading path is stable.
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

void HttpFlvStreamReader::handleMetaDataChanged()//服务端有响应就会触发这个槽函数，里面会读取响应头里的状态码和内容类型，并通过 connected 信号通知上层
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
