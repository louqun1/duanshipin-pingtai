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
    bool headerValidated = false;//是否已经成功解析了FLV文件头，只有在headerValidated为true时，才会继续解析后续的FLV标签数据。
    int audioTags = 0;//已经成功解析的音频标签数量。
    int videoTags = 0;//已经成功解析的视频标签数量。
    int scriptTags = 0;//已经成功解析的脚本标签数量。
    int parsedTagCount = 0;//本次数据块中成功解析的标签总数量，包括音频、视频和脚本标签。
};

}  // namespace backend::liveplayer::protocol
