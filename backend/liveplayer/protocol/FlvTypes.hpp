#pragma once

#include <QByteArray>
#include <QtGlobal>

namespace backend::liveplayer::protocol {

enum class FlvTagType : quint8 {
    Audio = 0x08,
    Video = 0x09,
    Script = 0x12,
    Unknown = 0xff
};

struct FlvTag {
    FlvTagType type = FlvTagType::Unknown;
    quint32 timestampMs = 0;
    quint32 dataSize = 0;
    quint32 previousTagSize = 0;
    quint8 rawTagType = 0;
    QByteArray payload;
    quint8 videoFrameType = 0;
    quint8 videoCodecId = 0;
    quint8 avcPacketType = 0xff;
    quint8 audioSoundFormat = 0;
    quint8 aacPacketType = 0xff;
    bool isSequenceHeader = false;
    bool isKeyframe = false;
};

struct FlvFeedReport {
    bool headerValidated = false;
    int audioTags = 0;
    int videoTags = 0;
    int scriptTags = 0;
    int parsedTagCount = 0;
};

}  // namespace backend::liveplayer::protocol
