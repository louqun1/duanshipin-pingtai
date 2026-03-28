#include "liveplayer/protocol/FlvDemuxer.hpp"

#include <QtEndian>

namespace backend::liveplayer::protocol {

namespace {

quint32 readUint24BE(const uchar *data)
{
    return (static_cast<quint32>(data[0]) << 16)
        | (static_cast<quint32>(data[1]) << 8)
        | static_cast<quint32>(data[2]);
}

FlvTagType toTagType(quint8 rawTagType)
{
    switch (rawTagType) {
    case 0x08:
        return FlvTagType::Audio;
    case 0x09:
        return FlvTagType::Video;
    case 0x12:
        return FlvTagType::Script;
    default:
        return FlvTagType::Unknown;
    }
}

}  // namespace

void FlvDemuxer::reset()
{
    buffer_.clear();
    parsedTags_.clear();
    headerValidated_ = false;
    lastError_.clear();
}

bool FlvDemuxer::pushBytes(const QByteArray &chunk, FlvFeedReport &report)
{
    report = {};

    if (chunk.isEmpty()) {
        return true;
    }

    buffer_.append(chunk);

    if (!headerValidated_ && !parseFlvHeader(report)) {
        return false;
    }

    if (!headerValidated_) {
        return true;
    }

    while (buffer_.size() >= 15) {
        const auto *data = reinterpret_cast<const uchar *>(buffer_.constData());
        const quint8 rawTagType = data[0];
        const quint32 dataLength = readUint24BE(data + 1);
        const quint32 timestampLower = readUint24BE(data + 4);
        const quint32 timestamp = (static_cast<quint32>(data[7]) << 24) | timestampLower;
        const quint32 tagBodySize = 11 + dataLength;
        const quint32 totalTagSize = tagBodySize + 4;

        if (buffer_.size() < static_cast<int>(totalTagSize)) {
            break;
        }

        const quint32 previousTagSize = qFromBigEndian<quint32>(data + 11 + dataLength);
        if (previousTagSize != tagBodySize) {
            lastError_ = QString(
                "FLV previous tag size mismatch. expected=%1 actual=%2")
                    .arg(tagBodySize)
                    .arg(previousTagSize);
            return false;
        }

        FlvTag tag;
        tag.type = toTagType(rawTagType);
        tag.rawTagType = rawTagType;
        tag.timestampMs = timestamp;
        tag.dataSize = dataLength;
        tag.previousTagSize = previousTagSize;
        tag.payload = buffer_.mid(11, static_cast<int>(dataLength));

        if (tag.type == FlvTagType::Video && !tag.payload.isEmpty()) {
            const quint8 firstByte = static_cast<quint8>(tag.payload[0]);
            tag.videoFrameType = (firstByte >> 4) & 0x0f;
            tag.videoCodecId = firstByte & 0x0f;
            tag.isKeyframe = (tag.videoFrameType == 1);

            if (tag.videoCodecId == 7 && tag.payload.size() >= 2) {
                tag.avcPacketType = static_cast<quint8>(tag.payload[1]);
                tag.isSequenceHeader = (tag.avcPacketType == 0);
            }
        } else if (tag.type == FlvTagType::Audio && !tag.payload.isEmpty()) {
            const quint8 firstByte = static_cast<quint8>(tag.payload[0]);
            tag.audioSoundFormat = (firstByte >> 4) & 0x0f;

            if (tag.audioSoundFormat == 10 && tag.payload.size() >= 2) {
                tag.aacPacketType = static_cast<quint8>(tag.payload[1]);
                tag.isSequenceHeader = (tag.aacPacketType == 0);
            }
        }

        parsedTags_.push_back(tag);
        report.parsedTagCount++;

        switch (tag.type) {
        case FlvTagType::Audio:
            report.audioTags++;
            break;
        case FlvTagType::Video:
            report.videoTags++;
            break;
        case FlvTagType::Script:
            report.scriptTags++;
            break;
        case FlvTagType::Unknown:
            break;
        }

        buffer_.remove(0, static_cast<int>(totalTagSize));
    }

    return true;
}

bool FlvDemuxer::takeNextTag(FlvTag &tag)
{
    if (parsedTags_.empty()) {
        return false;
    }

    tag = parsedTags_.front();
    parsedTags_.pop_front();
    return true;
}

QString FlvDemuxer::lastError() const
{
    return lastError_;
}

bool FlvDemuxer::parseFlvHeader(FlvFeedReport &report)
{
    if (buffer_.size() < 9) {
        return true;
    }

    if (buffer_.mid(0, 3) != "FLV") {
        lastError_ = "Invalid FLV signature.";
        return false;
    }

    const auto *headerBytes = reinterpret_cast<const uchar *>(buffer_.constData());
    const quint32 dataOffset = qFromBigEndian<quint32>(headerBytes + 5);
    const quint32 bytesNeeded = dataOffset + 4;

    if (buffer_.size() < static_cast<int>(bytesNeeded)) {
        return true;
    }

    headerValidated_ = true;
    report.headerValidated = true;
    buffer_.remove(0, static_cast<int>(bytesNeeded));
    return true;
}

}  // namespace backend::liveplayer::protocol
