#pragma once

#include <QtGlobal>

#include <deque>
#include <memory>
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

    void enqueue(QueuedVideoFrame frame)
    {
        queue_.push_back(std::move(frame));
    }

    const QueuedVideoFrame &front() const
    {
        return queue_.front();
    }

    const QueuedVideoFrame &back() const
    {
        return queue_.back();
    }

    void popFront()
    {
        queue_.pop_front();
    }

    void popBack()
    {
        queue_.pop_back();
    }

    int maxDepth() const
    {
        return maxDepth_;
    }

    qint64 bufferedDurationMs() const
    {
        if (queue_.size() < 2) {
            return 0;
        }

        return queue_.back().ptsMs - queue_.front().ptsMs;
    }

private:
    int maxDepth_ = kDefaultMaxDepth;
    std::deque<QueuedVideoFrame> queue_;
};

}  // namespace backend::liveplayer::session
