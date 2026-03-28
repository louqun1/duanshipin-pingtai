#include "liveplayer/session/LivePlayerSession.hpp"

#include "liveplayer/protocol/FlvTypes.hpp"
#include "liveplayer/protocol/HttpFlvStreamReader.hpp"

#include <QWidget>

namespace backend::liveplayer::session {

namespace {

QString videoCodecName(quint8 codecId)
{
    switch (codecId) {
    case 2:
        return "Sorenson H.263";
    case 4:
        return "On2 VP6";
    case 7:
        return "AVC/H.264";
    case 12:
        return "HEVC";
    default:
        return QString("codec=%1").arg(codecId);
    }
}

QString audioFormatName(quint8 soundFormat)
{
    switch (soundFormat) {
    case 2:
        return "MP3";
    case 10:
        return "AAC";
    case 11:
        return "Speex";
    default:
        return QString("format=%1").arg(soundFormat);
    }
}

}  // namespace

LivePlayerSession::LivePlayerSession(QObject *parent)
    : QObject(parent)
    , reader_(new protocol::HttpFlvStreamReader(this))
{
    connect(reader_, &protocol::HttpFlvStreamReader::connected,
            this, &LivePlayerSession::handleReaderConnected);
    connect(reader_, &protocol::HttpFlvStreamReader::dataChunkReceived,
            this, &LivePlayerSession::handleReaderDataChunk);
    connect(reader_, &protocol::HttpFlvStreamReader::errorOccurred,
            this, &LivePlayerSession::handleReaderError);
    connect(reader_, &protocol::HttpFlvStreamReader::finished,
            this, &LivePlayerSession::handleReaderFinished);
    connect(reader_, &protocol::HttpFlvStreamReader::logMessage,
            this, &LivePlayerSession::logMessage);
}

void LivePlayerSession::attachVideoSurface(QWidget *surface)
{
    videoSurface_ = surface;

    if (videoSurface_) {
        emit logMessage("Live surface attached. The next step is to hand parsed video tags to your decoder and render the resulting frames here.");
    }
}

void LivePlayerSession::open(const QString &url)
{
    const QString trimmedUrl = url.trimmed();
    if (trimmedUrl.isEmpty()) {
        setState(SessionState::Error, "Live URL is empty.");
        emit logMessage("Refusing to open an empty live URL.");
        return;
    }

    if (reader_->isActive()) {
        reader_->close();
    }

    demuxer_.reset();
    resetCounters();
    currentUrl_ = trimmedUrl;

    setState(SessionState::Connecting, QString("Opening %1").arg(currentUrl_));
    emit logMessage("Opening live stream. This stage will verify the HTTP response, FLV header, and media tags before you connect the decoder pipeline.");
    reader_->open(QUrl(currentUrl_));
}

void LivePlayerSession::stop()
{
    reader_->close();
    demuxer_.reset();
    resetCounters();
    currentUrl_.clear();
    setState(SessionState::Stopped, "Live stream stopped.");
}

void LivePlayerSession::handleReaderConnected(const QString &contentType, int statusCode)
{
    if (!contentType.isEmpty()) {
        emit logMessage(QString("HTTP response received. Content-Type: %1").arg(contentType));
    }

    if (!contentType.isEmpty() && !contentType.contains("flv", Qt::CaseInsensitive)) {
        emit logMessage("Warning: Content-Type does not explicitly contain flv. Continue parsing, but check your server mapping if demux fails.");
    }

    setState(SessionState::Reading,
             QString("HTTP connected (%1, status %2). Waiting for FLV header.")
                 .arg(contentType.isEmpty() ? "unknown content type" : contentType)
                 .arg(statusCode));
}

void LivePlayerSession::handleReaderDataChunk(const QByteArray &chunk)
{
    if (chunk.isEmpty()) {
        return;
    }

    bytesReceived_ += chunk.size();//累加接收的字节数

    protocol::FlvFeedReport report;
    if (!demuxer_.pushBytes(chunk, report)) {//将chunk喂给demuxer解析，report里会有解析结果
        const QString errorMessage = demuxer_.lastError().isEmpty()
            ? QString("FLV demux failed after receiving %1 bytes.").arg(bytesReceived_)
            : demuxer_.lastError();
        setState(SessionState::Error, errorMessage);
        emit logMessage("Demuxer reported a fatal parse error. Check the server output, the tag boundaries, and the previous tag size field.");
        reader_->close();
        return;
    }

        /*Tag Header    说明这条 tag 是什么、大小多少、时间戳多少
        Payload         真正的内容数据
        PreviousTagSize 上一个 tag 的大小字段*/
    if (!firstPayloadObserved_) {
        firstPayloadObserved_ = true;//接收到第一个有效载荷数据块
        emit logMessage(QString("Received first payload chunk: %1 bytes.").arg(chunk.size()));
    }

    if (report.headerValidated) {//FLV头验证成功，说明接下来可以增量地消费tag头和payload了
        emit logMessage("FLV header validated. The session is now consuming tag headers and payloads incrementally.");
    }

    //Tag = Header + Payload + PreviousTagSize      
    if (report.parsedTagCount > 0) {
        drainParsedTags();
    }

    emit statsChanged(bytesReceived_, audioTagCount_, videoTagCount_, scriptTagCount_);
}

void LivePlayerSession::handleReaderError(const QString &message)
{
    setState(SessionState::Error, QString("HTTP-FLV read failed: %1").arg(message));
}

void LivePlayerSession::handleReaderFinished()
{
    if (state_ == SessionState::Error) {
        return;
    }

    setState(SessionState::Stopped, "Live stream finished.");
}

void LivePlayerSession::resetCounters()
{
    bytesReceived_ = 0;
    audioTagCount_ = 0;
    videoTagCount_ = 0;
    scriptTagCount_ = 0;
    firstPayloadObserved_ = false;
    firstAudioTagObserved_ = false;
    firstVideoTagObserved_ = false;
    firstScriptTagObserved_ = false;
    audioSequenceHeaderObserved_ = false;
    videoSequenceHeaderObserved_ = false;
    mediaFlowObserved_ = false;
    emit statsChanged(bytesReceived_, audioTagCount_, videoTagCount_, scriptTagCount_);
}

void LivePlayerSession::setState(SessionState state, const QString &message)
{
    state_ = state;
    emit stateChanged(state_, message);
}

void LivePlayerSession::drainParsedTags()//负责消费这些 tag，并根据 tag 的类型和内容更新状态和日志。它会一直调用 demuxer_.takeNextTag(tag) 直到没有更多的 tag 可供消费为止。
{
    protocol::FlvTag tag;
    while (demuxer_.takeNextTag(tag)) {
        switch (tag.type) {
        case protocol::FlvTagType::Script:
            ++scriptTagCount_;
            if (!firstScriptTagObserved_) {
                firstScriptTagObserved_ = true;
                emit logMessage(
                    QString("First script tag arrived at %1 ms, payload=%2 bytes. This is usually metadata.")
                        .arg(tag.timestampMs)
                        .arg(tag.payload.size()));
            }
            break;

        case protocol::FlvTagType::Audio:
            ++audioTagCount_;
            mediaFlowObserved_ = true;

            if (!firstAudioTagObserved_) {
                firstAudioTagObserved_ = true;
                emit logMessage(
                    QString("First audio tag arrived at %1 ms, payload=%2 bytes, format=%3.")
                        .arg(tag.timestampMs)
                        .arg(tag.payload.size())
                        .arg(audioFormatName(tag.audioSoundFormat)));
            }

            if (tag.isSequenceHeader && !audioSequenceHeaderObserved_) {
                audioSequenceHeaderObserved_ = true;
                emit logMessage(
                    QString("AAC sequence header observed at %1 ms. Decoder config can be initialized from this payload.")
                        .arg(tag.timestampMs));
            }
            break;

        case protocol::FlvTagType::Video:
            ++videoTagCount_;
            mediaFlowObserved_ = true;

            if (!firstVideoTagObserved_) {
                firstVideoTagObserved_ = true;
                emit logMessage(
                    QString("First video tag arrived at %1 ms, payload=%2 bytes, codec=%3, keyframe=%4.")
                        .arg(tag.timestampMs)
                        .arg(tag.payload.size())
                        .arg(videoCodecName(tag.videoCodecId))
                        .arg(tag.isKeyframe ? "yes" : "no"));
            }

            if (tag.isSequenceHeader && !videoSequenceHeaderObserved_) {
                videoSequenceHeaderObserved_ = true;
                emit logMessage(
                    QString("AVC sequence header observed at %1 ms. SPS and PPS should be extracted here before normal frame decode.")
                        .arg(tag.timestampMs));
            }
            break;

        case protocol::FlvTagType::Unknown:
            emit logMessage(
                QString("Unknown FLV tag type 0x%1 observed at %2 ms, payload=%3 bytes.")
                    .arg(QString::number(tag.rawTagType, 16))
                    .arg(tag.timestampMs)
                    .arg(tag.payload.size()));
            break;
        }
    }

    if (mediaFlowObserved_ && state_ != SessionState::Playing) {
        setState(//只要它观察到已经有音频或视频 tag 在流动，就执行这个函数
            SessionState::Playing,
            QString("Media tags are flowing. audio=%1 video=%2 script=%3. Decoder/render hookup is the next step.")
                .arg(audioTagCount_)
                .arg(videoTagCount_)
                .arg(scriptTagCount_));
    }
}

}  // namespace backend::liveplayer::session
