#include "playerEngine/ff_ffplay_def.hpp"

#include <cstring>

#include <spdlog/spdlog.h>

AVPacket flush_pkt;

namespace
{

    uint8_t kFlushPacketMarker = 0;

    void ensure_flush_packet_initialized()
    {
        if (flush_pkt.data == &kFlushPacketMarker)
        {
            return;
        }

        flush_pkt = AVPacket{};
        flush_pkt.data = &kFlushPacketMarker;
        flush_pkt.size = 0;
    }

    void sync_packet_queue_stats(PacketQueue *q)
    {
        q->nb_packets = static_cast<int>(q->packets.size());
    }

    int packet_queue_put_private(PacketQueue *q, AVPacket *pkt)
    {
        if (q->abort_request)
        {
            return -1;
        }

        q->packets.emplace_back();
        MyAVPacketList &entry = q->packets.back();
        std::memset(&entry, 0, sizeof(entry));

        if (pkt == &flush_pkt)
        {
            entry.pkt = *pkt;
            ++q->serial;
        }
        else
        {
            av_packet_move_ref(&entry.pkt, pkt);
        }

        entry.serial = q->serial;
        q->size += entry.pkt.size + static_cast<int>(sizeof(MyAVPacketList));
        q->duration += entry.pkt.duration;
        sync_packet_queue_stats(q);
        q->cond.notify_one();
        return 0;
    }

    void frame_queue_unref_item(Frame *vp)
    {
        if (vp && vp->frame)
        {
            av_frame_unref(vp->frame);
        }
    }

} // namespace

int packet_queue_put(PacketQueue *q, AVPacket *pkt)
{
    std::lock_guard<std::mutex> lock(q->mutex);
    const int ret = packet_queue_put_private(q, pkt);
    if (pkt != &flush_pkt && ret < 0)
    {
        av_packet_unref(pkt);
    }
    return ret;
}

int packet_queue_put_nullpacket(PacketQueue *q, int stream_index)
{
    AVPacket pkt{};
    pkt.data = nullptr;
    pkt.size = 0;
    pkt.stream_index = stream_index;
    return packet_queue_put(q, &pkt);
}

int packet_queue_init(PacketQueue *q)
{
    std::lock_guard<std::mutex> lock(q->mutex);
    ensure_flush_packet_initialized();
    for (auto &entry : q->packets)
    {
        av_packet_unref(&entry.pkt);
    }
    q->packets.clear();
    q->nb_packets = 0;
    q->size = 0;
    q->duration = 0;
    q->abort_request = 1;
    q->serial = 0;
    return 0;
}

void packet_queue_flush(PacketQueue *q)
{
    std::lock_guard<std::mutex> lock(q->mutex);
    for (auto &entry : q->packets)
    {
        if (&entry.pkt != &flush_pkt)
        {
            av_packet_unref(&entry.pkt);
        }
    }
    q->packets.clear();
    q->size = 0;
    q->duration = 0;
    sync_packet_queue_stats(q);
}

void packet_queue_destroy(PacketQueue *q)
{
    {
        std::lock_guard<std::mutex> lock(q->mutex);
        q->abort_request = 1;
        for (auto &entry : q->packets)
        {
            if (&entry.pkt != &flush_pkt)
            {
                av_packet_unref(&entry.pkt);
            }
        }
        q->packets.clear();
        q->size = 0;
        q->duration = 0;
        sync_packet_queue_stats(q);
    }
    q->cond.notify_all();
}

void packet_queue_abort(PacketQueue *q)
{
    {
        std::lock_guard<std::mutex> lock(q->mutex);
        q->abort_request = 1;
    }
    q->cond.notify_all();
}

void packet_queue_start(PacketQueue *q)
{
    std::lock_guard<std::mutex> lock(q->mutex);
    q->abort_request = 0;
    packet_queue_put_private(q, &flush_pkt);
}

double packet_queue_cache_duration(PacketQueue *q, AVRational time_base, double packet_duration)
{
    std::lock_guard<std::mutex> lock(q->mutex);
    if (q->packets.empty())
    {
        return 0.0;
    }

    const MyAVPacketList &first_pkt = q->packets.front();
    const MyAVPacketList &last_pkt = q->packets.back();
    const double packets_duration = packet_duration * q->nb_packets;

    if (q->nb_packets < 2 ||
        first_pkt.pkt.dts == AV_NOPTS_VALUE ||
        last_pkt.pkt.dts == AV_NOPTS_VALUE)
    {
        return packets_duration;
    }

    const int64_t temp_pts = last_pkt.pkt.dts - first_pkt.pkt.dts;
    const double pts_duration = temp_pts * av_q2d(time_base);
    return pts_duration < 60.0 ? pts_duration : packets_duration;
}

int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block, int *serial)
{
    std::unique_lock<std::mutex> lock(q->mutex);

    for (;;)
    {
        if (q->abort_request)
        {
            return -1;
        }

        if (!q->packets.empty())
        {
            MyAVPacketList entry = {};
            entry = std::move(q->packets.front());
            q->packets.pop_front();
            q->size -= entry.pkt.size + static_cast<int>(sizeof(MyAVPacketList));
            q->duration -= entry.pkt.duration;
            sync_packet_queue_stats(q);
            av_packet_move_ref(pkt, &entry.pkt);
            if (serial)
            {
                *serial = entry.serial;
            }
            return 1;
        }

        if (!block)
        {
            return 0;
        }

        q->cond.wait(lock, [q]()
                     { return q->abort_request || !q->packets.empty(); });
    }
}

int frame_queue_init(FrameQueue *f, PacketQueue *pktq, int max_size, int keep_last)
{
    std::lock_guard<std::mutex> lock(f->mutex);
    f->pktq = pktq;
    f->max_size = FFMIN(max_size, FRAME_QUEUE_SIZE);
    f->keep_last = !!keep_last;
    f->rindex = 0;
    f->windex = 0;
    f->size = 0;
    f->rindex_shown = 0;

    for (int i = 0; i < f->max_size; ++i)
    {
        frame_queue_unref_item(&f->queue[i]);
        if (!f->queue[i].frame)
        {
            f->queue[i].frame = av_frame_alloc();
            if (!f->queue[i].frame)
            {
                return AVERROR(ENOMEM);
            }
        }
    }
    return 0;
}

void frame_queue_destory(FrameQueue *f)
{
    std::lock_guard<std::mutex> lock(f->mutex);
    for (int i = 0; i < f->max_size; ++i)
    {
        Frame *vp = &f->queue[i];
        frame_queue_unref_item(vp);
        av_frame_free(&vp->frame);
    }
    f->size = 0;
    f->rindex = 0;
    f->windex = 0;
    f->rindex_shown = 0;
}

void frame_queue_signal(FrameQueue *f)
{
    f->cond.notify_all();
}

Frame *frame_queue_peek(FrameQueue *f)
{
    return &f->queue[(f->rindex + f->rindex_shown) % f->max_size];
}

Frame *frame_queue_peek_next(FrameQueue *f)
{
    return &f->queue[(f->rindex + f->rindex_shown + 1) % f->max_size];
}

Frame *frame_queue_peek_last(FrameQueue *f)
{
    return &f->queue[f->rindex];
}

Frame *frame_queue_peek_writable(FrameQueue *f)
{
    std::unique_lock<std::mutex> lock(f->mutex);
    f->cond.wait(lock, [f]()
                 { return f->size < f->max_size || f->pktq->abort_request; });

    if (f->pktq->abort_request)
    {
        return nullptr;
    }

    return &f->queue[f->windex];
}

Frame *frame_queue_peek_readable(FrameQueue *f)
{
    std::unique_lock<std::mutex> lock(f->mutex);
    f->cond.wait(lock, [f]()
                 { return f->size > 0 || f->pktq->abort_request; });

    if (f->pktq->abort_request)
    {
        return nullptr;
    }

    return &f->queue[(f->rindex + f->rindex_shown) % f->max_size];
}

void frame_queue_push(FrameQueue *f)
{
    {
        std::lock_guard<std::mutex> lock(f->mutex);
        if (++f->windex == f->max_size)
        {
            f->windex = 0;
        }
        ++f->size;
    }
    f->cond.notify_all();
}

void frame_queue_next(FrameQueue *f)
{
    {
        std::lock_guard<std::mutex> lock(f->mutex);
        if (f->keep_last && !f->rindex_shown)
        {
            f->rindex_shown = 1;
            return;
        }

        frame_queue_unref_item(&f->queue[f->rindex]);
        if (++f->rindex == f->max_size)
        {
            f->rindex = 0;
        }
        --f->size;
    }
    f->cond.notify_all();
}

int frame_queue_nb_remaining(FrameQueue *f)
{
    std::lock_guard<std::mutex> lock(f->mutex);
    return f->size - f->rindex_shown;
}

int64_t frame_queue_last_pos(FrameQueue *f)
{
    std::lock_guard<std::mutex> lock(f->mutex);
    Frame *fp = &f->queue[f->rindex];
    if (f->rindex_shown && fp->serial == f->pktq->serial)
    {
        return fp->pos;
    }
    return -1;
}

double get_clock(Clock *c)
{
    if (*c->queue_serial != c->serial)
    {
        return NAN;
    }

    if (c->paused)
    {
        return c->pts;
    }

    const double time = av_gettime_relative() / 1000000.0;
    return c->pts_drift + time - (time - c->last_updated) * (1.0 - c->speed);
}

void set_clock_at(Clock *c, double pts, int serial, double time)
{
    c->pts = pts;
    c->last_updated = time;
    c->pts_drift = c->pts - time;
    c->serial = serial;
}

void set_clock(Clock *c, double pts, int serial)
{
    const double time = av_gettime_relative() / 1000000.0;
    set_clock_at(c, pts, serial, time);
}

void init_clock(Clock *c, int *queue_serial)
{
    c->speed = 1.0;
    c->paused = 0;
    c->queue_serial = queue_serial;
    set_clock(c, NAN, -1);
}

void ffp_reset_statistic(FFStatistic *dcc)
{
    std::memset(dcc, 0, sizeof(FFStatistic));
}
