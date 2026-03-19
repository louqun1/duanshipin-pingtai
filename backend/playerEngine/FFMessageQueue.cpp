#include "FFMessageQueue.hpp"

#include <cstring>

extern "C"
{
#include "libavutil/mem.h"
}

#include "FFMessage.hpp"

namespace
{

    void sync_queue_stats(MessageQueue *q)
    {
        q->nb_messages = static_cast<int>(q->messages.size());
    }

    void free_message_contents(AVMessage *msg)
    {
        msg_free_res(msg);
        msg->next = nullptr;
    }

} // namespace

void msg_free_res(AVMessage *msg)
{
    if (!msg || !msg->obj)
    {
        return;
    }

    if (msg->free_l)
    {
        msg->free_l(msg->obj);
    }
    msg->obj = nullptr;
}

int msg_queue_put_private(MessageQueue *q, AVMessage *msg)
{
    if (q->abort_request)
    {
        return -1;
    }

    AVMessage queued = *msg;
    queued.next = nullptr;
    q->messages.push_back(queued);
    ++q->alloc_count;
    sync_queue_stats(q);
    q->cond.notify_one();
    return 0;
}

int msg_queue_put(MessageQueue *q, AVMessage *msg)
{
    std::lock_guard<std::mutex> lock(q->mutex);
    return msg_queue_put_private(q, msg);
}

void msg_init_msg(AVMessage *msg)
{
    std::memset(msg, 0, sizeof(AVMessage));
}

void msg_queue_put_simple1(MessageQueue *q, int what)
{
    AVMessage msg;
    msg_init_msg(&msg);
    msg.what = what;
    msg_queue_put(q, &msg);
}

void msg_queue_put_simple2(MessageQueue *q, int what, int arg1)
{
    AVMessage msg;
    msg_init_msg(&msg);
    msg.what = what;
    msg.arg1 = arg1;
    msg_queue_put(q, &msg);
}

void msg_queue_put_simple3(MessageQueue *q, int what, int arg1, int arg2)
{
    AVMessage msg;
    msg_init_msg(&msg);
    msg.what = what;
    msg.arg1 = arg1;
    msg.arg2 = arg2;
    msg_queue_put(q, &msg);
}

void msg_obj_free_l(void *obj)
{
    av_free(obj);
}

void msg_queue_put_simple4(MessageQueue *q, int what, int arg1, int arg2, void *obj, int obj_len)
{
    AVMessage msg;
    msg_init_msg(&msg);
    msg.what = what;
    msg.arg1 = arg1;
    msg.arg2 = arg2;

    if (obj && obj_len > 0)
    {
        msg.obj = av_malloc(obj_len);
        if (msg.obj)
        {
            std::memcpy(msg.obj, obj, static_cast<size_t>(obj_len));
            msg.free_l = msg_obj_free_l;
        }
    }

    msg_queue_put(q, &msg);
}

void msg_queue_init(MessageQueue *q)
{
    std::lock_guard<std::mutex> lock(q->mutex);
    for (auto &msg : q->messages)
    {
        free_message_contents(&msg);
    }
    q->messages.clear();
    q->nb_messages = 0;
    q->abort_request = 1;
    q->recycle_count = 0;
    q->alloc_count = 0;
}

void msg_queue_flush(MessageQueue *q)
{
    std::lock_guard<std::mutex> lock(q->mutex);
    for (auto &msg : q->messages)
    {
        free_message_contents(&msg);
    }
    q->messages.clear();
    sync_queue_stats(q);
}

void msg_queue_destroy(MessageQueue *q)
{
    {
        std::lock_guard<std::mutex> lock(q->mutex);
        q->abort_request = 1;
        for (auto &msg : q->messages)
        {
            free_message_contents(&msg);
        }
        q->messages.clear();
        q->recycle_count = 0;
        sync_queue_stats(q);
    }
    q->cond.notify_all();
}

void msg_queue_abort(MessageQueue *q)
{
    {
        std::lock_guard<std::mutex> lock(q->mutex);
        q->abort_request = 1;
    }
    q->cond.notify_all();
}

void msg_queue_start(MessageQueue *q)
{
    std::lock_guard<std::mutex> lock(q->mutex);
    q->abort_request = 0;

    AVMessage msg;
    msg_init_msg(&msg);
    msg.what = FFP_MSG_FLUSH;
    msg_queue_put_private(q, &msg);
}

int msg_queue_get(MessageQueue *q, AVMessage *msg, int block)
{
    std::unique_lock<std::mutex> lock(q->mutex);

    for (;;)
    {
        if (q->abort_request)
        {
            return -1;
        }

        if (!q->messages.empty())
        {
            *msg = q->messages.front();
            q->messages.pop_front();
            sync_queue_stats(q);
            return 1;
        }

        if (!block)
        {
            return 0;
        }

        q->cond.wait(lock, [q]()
                     { return q->abort_request || !q->messages.empty(); });
    }
}

void msg_queue_remove(MessageQueue *q, int what)
{
    std::lock_guard<std::mutex> lock(q->mutex);

    if (q->abort_request || q->messages.empty())
    {
        return;
    }

    auto it = q->messages.begin();
    while (it != q->messages.end())
    {
        if (it->what == what)
        {
            free_message_contents(&(*it));
            it = q->messages.erase(it);
            ++q->recycle_count;
            continue;
        }
        ++it;
    }

    sync_queue_stats(q);
}
