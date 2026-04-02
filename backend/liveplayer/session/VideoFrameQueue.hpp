#pragma once

#include <QtGlobal>

#include <deque>
#include <memory>
#include <optional>
#include <utility>

struct AVFrame;

namespace backend::liveplayer::session {

struct QueuedVideoFrame
{
    std::shared_ptr<AVFrame> frame;
    qint64 ptsMs = -1;
    qint64 enqueueWallClockMs = 0;
    quint64 generation = 0;
};

class VideoFrameQueue final
{
public:
    static constexpr int kDefaultMaxDepth = 10;

    explicit VideoFrameQueue(int maxDepth = kDefaultMaxDepth)
        : maxDepth_(maxDepth > 0 ? maxDepth : kDefaultMaxDepth)
    {
    }

    void clear()
    {
        queue_.clear();
    }

    bool empty() const
    {
        return queue_.empty();
    }

    int size() const
    {
        return static_cast<int>(queue_.size());
    }

    std::optional<QueuedVideoFrame> enqueue(QueuedVideoFrame frame)
    {
        queue_.push_back(std::move(frame));
        if (static_cast<int>(queue_.size()) <= maxDepth_) {
            return std::nullopt;
        }

        QueuedVideoFrame droppedFrame = std::move(queue_.front());
        queue_.pop_front();
        return droppedFrame;
    }

    const QueuedVideoFrame &front() const
    {
        return queue_.front();
    }

    void popFront()
    {
        queue_.pop_front();
    }

private:
    int maxDepth_ = kDefaultMaxDepth;
    std::deque<QueuedVideoFrame> queue_;
};

}  // namespace backend::liveplayer::session
