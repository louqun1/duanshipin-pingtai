#pragma once

#include "liveplayer/protocol/FlvTypes.hpp"

#include <QByteArray>
#include <QString>

#include <deque>

namespace backend::liveplayer::protocol {

class FlvDemuxer
{
public:
    void reset();
    bool pushBytes(const QByteArray &chunk, FlvFeedReport &report);
    bool takeNextTag(FlvTag &tag);
    FlvTagType peekNextTagType() const;
    bool takeNextTagOfType(FlvTagType type, FlvTag &tag);
    QString lastError() const;
    int parsedTagCount() const;
    int bufferedByteCount() const;

private:
    bool parseFlvHeader(FlvFeedReport &report);

    QByteArray buffer_;
    std::deque<FlvTag> parsedTags_;
    bool headerValidated_ = false;
    QString lastError_;
};

}  // namespace backend::liveplayer::protocol
