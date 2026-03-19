#include "IjkMediaPlayer.hpp"

#include <cstring>
#include <utility>

#include <spdlog/spdlog.h>

#include "FFMessage.hpp"

namespace media
{

    IjkMediaPlayer::IjkMediaPlayer()
    {
        spdlog::info("IjkMediaPlayer()");
    }

    IjkMediaPlayer::~IjkMediaPlayer()
    {
        spdlog::info("~IjkMediaPlayer()");
        destroy();
    }

    int IjkMediaPlayer::create()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (ffplayer_)
            {
                return 0;
            }

            ffplayer_ = new FFPlayer();
            if (!ffplayer_)
            {
                spdlog::error("new FFPlayer() failed");
                return -1;
            }

            const int ret = ffplayer_->ffp_create();
            if (ret < 0)
            {
                delete ffplayer_;
                ffplayer_ = nullptr;
                return -1;
            }

            bindVideoFrameCallbackLocked();
            resetSessionFlagsLocked();
            msg_thread_running_ = true;
            msg_thread_ = std::thread(&IjkMediaPlayer::messageLoop, this);
        }

        changeState(MP_STATE_INITIALIZED);
        return 0;
    }

    int IjkMediaPlayer::destroy()
    {
        FFPlayer *player = nullptr;
        char *data_source = nullptr;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            player = ffplayer_;
            if (player && msg_thread_running_)
            {
                msg_thread_running_ = false;
                msg_queue_abort(&player->msg_queue_);
            }
        }

        if (msg_thread_.joinable())
        {
            msg_thread_.join();
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            player = ffplayer_;
            ffplayer_ = nullptr;

            data_source = data_source_;
            data_source_ = nullptr;

            mp_state_ = MP_STATE_IDLE;
            resetSessionFlagsLocked();
        }

        if (player)
        {
            player->ffp_destroy();
            delete player;
        }

        if (data_source)
        {
            free(data_source);
        }

        return 0;
    }

    int IjkMediaPlayer::setDataSource(const char *url)
    {
        if (!url)
        {
            return -1;
        }

        bool should_enter_initialized = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (mp_state_ != MP_STATE_IDLE && mp_state_ != MP_STATE_INITIALIZED)
            {
                return -1;
            }

            if (data_source_)
            {
                free(data_source_);
            }

            data_source_ = strdup(url);
            if (!data_source_)
            {
                return -1;
            }

            resetSessionFlagsLocked();
            should_enter_initialized = (ffplayer_ != nullptr);
        }

        if (should_enter_initialized)
        {
            changeState(MP_STATE_INITIALIZED);
        }

        return 0;
    }

    int IjkMediaPlayer::prepareAsync()
    {
        int ret = -1;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!ffplayer_ || !data_source_ || mp_state_ != MP_STATE_INITIALIZED)
            {
                return -1;
            }

            resetSessionFlagsLocked();
            msg_queue_start(&ffplayer_->msg_queue_);
            mp_state_ = MP_STATE_ASYNC_PREPARING;
            ret = ffplayer_->ffp_prepare_async_l(data_source_);
            if (ret < 0)
            {
                mp_state_ = MP_STATE_ERROR;
            }
        }

        if (ret < 0)
        {
            notifyEvent(PlayerEvent::StateChanged, MP_STATE_ERROR, nullptr);
            notifyEvent(PlayerEvent::ErrorOccurred, ret, nullptr);
            return -1;
        }

        notifyEvent(PlayerEvent::StateChanged, MP_STATE_ASYNC_PREPARING, nullptr);
        return 0;
    }

    int IjkMediaPlayer::start()
    {
        int ret = -1;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!ffplayer_ || !isStartAllowedLocked())
            {
                return -1;
            }

            ret = ffplayer_->ffp_start_l();
            if (ret == 0)
            {
                pending_resume_after_seek_ = true;
                state_before_buffering_ = MP_STATE_STARTED;
            }
        }

        if (ret == 0)
        {
            changeState(MP_STATE_STARTED);
            notifyEvent(PlayerEvent::Playing);
        }

        return ret;
    }

    int IjkMediaPlayer::pause()
    {
        int ret = -1;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!ffplayer_ || !isPauseAllowedLocked())
            {
                return -1;
            }

            ret = ffplayer_->ffp_pause_l();
            if (ret == 0)
            {
                pending_resume_after_seek_ = false;
            }
        }

        if (ret == 0)
        {
            changeState(MP_STATE_PAUSED);
            notifyEvent(PlayerEvent::Paused);
        }

        return ret;
    }

    int IjkMediaPlayer::stop()
    {
        int ret = -1;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!ffplayer_ || mp_state_ == MP_STATE_IDLE || mp_state_ == MP_STATE_STOPPED)
            {
                return -1;
            }

            ret = ffplayer_->ffp_stop_l();
            if (ret == 0)
            {
                pending_resume_after_seek_ = false;
            }
        }

        if (ret == 0)
        {
            changeState(MP_STATE_STOPPED);
            notifyEvent(PlayerEvent::Stopped);
        }

        return ret;
    }

    int IjkMediaPlayer::seekTo(long msec)
    {
        if (msec < 0)
        {
            return -1;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!ffplayer_ || !isSeekAllowedLocked())
            {
                return -1;
            }

            state_before_seek_ = mp_state_;
            if (mp_state_ == MP_STATE_BUFFERING)
            {
                state_before_seek_ = state_before_buffering_;
            }
            pending_resume_after_seek_ = (state_before_seek_ == MP_STATE_STARTED ||
                                          state_before_seek_ == MP_STATE_BUFFERING);

            ffplayer_->ffp_seek_to_l(msec);
            mp_state_ = MP_STATE_SEEKING;
        }

        notifyEvent(PlayerEvent::StateChanged, MP_STATE_SEEKING, nullptr);
        return 0;
    }

    int IjkMediaPlayer::screenshot(const char *file_path)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ffplayer_ || !file_path)
        {
            return -1;
        }

        return ffplayer_->ffp_screenshot_l(const_cast<char *>(file_path));
    }

    int IjkMediaPlayer::getState() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return mp_state_;
    }

    long IjkMediaPlayer::getCurrentPosition()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ffplayer_)
        {
            return 0;
        }

        return ffplayer_->ffp_get_current_position_l();
    }

    long IjkMediaPlayer::getDuration()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ffplayer_)
        {
            return 0;
        }

        return ffplayer_->ffp_get_duration_l();
    }

    void IjkMediaPlayer::setPlaybackVolume(int volume)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (ffplayer_)
        {
            ffplayer_->ffp_set_playback_volume(volume);
        }
    }

    void IjkMediaPlayer::setPlaybackRate(float rate)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (ffplayer_)
        {
            ffplayer_->ffp_set_playback_rate(rate);
        }
    }

    float IjkMediaPlayer::getPlaybackRate()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ffplayer_)
        {
            return 1.0f;
        }

        return ffplayer_->ffp_get_playback_rate();
    }

    void IjkMediaPlayer::setEventCallback(std::function<void(PlayerEvent, int, void *)> callback)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        event_callback_ = std::move(callback);
    }

    void IjkMediaPlayer::setVideoFrameCallback(std::function<int(const Frame *)> callback)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        video_frame_callback_ = std::move(callback);
        bindVideoFrameCallbackLocked();
    }

    void IjkMediaPlayer::messageLoop()
    {
        spdlog::info("IjkMediaPlayer message loop started");

        while (msg_thread_running_)
        {
            FFPlayer *player = nullptr;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!msg_thread_running_ || !ffplayer_)
                {
                    break;
                }
                player = ffplayer_;
            }

            AVMessage msg;
            const int ret = msg_queue_get(&player->msg_queue_, &msg, 1);
            if (!msg_thread_running_)
            {
                break;
            }
            if (ret <= 0)
            {
                continue;
            }

            handleMessage(&msg);
            msg_free_res(&msg);
        }

        spdlog::info("IjkMediaPlayer message loop ended");
    }

    void IjkMediaPlayer::handleMessage(AVMessage *msg)
    {
        const int arg1 = msg->arg1;
        void *arg2 = msg->obj;

        switch (msg->what)
        {
        case FFP_MSG_OPEN_INPUT:
            notifyEvent(PlayerEvent::OpenInputStarted);
            return;

        case FFP_MSG_PREPARED:
            changeState(MP_STATE_PREPARED);
            notifyEvent(PlayerEvent::Prepared);
            return;

        case FFP_MSG_SEEK_COMPLETE:
        {
            int post_seek_state = MP_STATE_PAUSED;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                post_seek_state = resolvePostSeekStateLocked();
                state_before_seek_ = MP_STATE_IDLE;
            }

            changeState(post_seek_state);
            notifyEvent(PlayerEvent::SeekCompleted, arg1, arg2);
            if (post_seek_state == MP_STATE_STARTED)
            {
                notifyEvent(PlayerEvent::Playing);
            }
            else if (post_seek_state == MP_STATE_PAUSED)
            {
                notifyEvent(PlayerEvent::Paused);
            }
            return;
        }

        case FFP_MSG_PLAY_FNISH:
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pending_resume_after_seek_ = false;
        }
            changeState(MP_STATE_COMPLETED);
            notifyEvent(PlayerEvent::PlaybackFinished, arg1, arg2);
            return;

        case FFP_MSG_ERROR:
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pending_resume_after_seek_ = false;
        }
            changeState(MP_STATE_ERROR);
            notifyEvent(PlayerEvent::ErrorOccurred, arg1, arg2);
            return;

        case FFP_MSG_BUFFERING_START:
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (mp_state_ != MP_STATE_BUFFERING)
            {
                state_before_buffering_ = mp_state_;
                mp_state_ = MP_STATE_BUFFERING;
            }
        }
            notifyEvent(PlayerEvent::StateChanged, MP_STATE_BUFFERING, nullptr);
            notifyEvent(PlayerEvent::BufferingStarted, arg1, arg2);
            return;

        case FFP_MSG_BUFFERING_END:
        {
            int next_state = MP_STATE_STARTED;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                next_state = resolvePostBufferingStateLocked();
                state_before_buffering_ = MP_STATE_IDLE;
            }
            changeState(next_state);
            notifyEvent(PlayerEvent::BufferingEnded, arg1, arg2);
            return;
        }

        case FFP_MSG_SCREENSHOT_COMPLETE:
            notifyEvent(PlayerEvent::ScreenshotCompleted, arg1, arg2);
            return;

        case FFP_MSG_PLAYBACK_STATE_CHANGED:
            notifyEvent(PlayerEvent::StateChanged, getState(), nullptr);
            return;

        default:
            spdlog::info("Unhandled message: {}", msg->what);
            return;
        }
    }

    void IjkMediaPlayer::changeState(int new_state)
    {
        bool changed = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (mp_state_ != new_state)
            {
                mp_state_ = new_state;
                changed = true;
            }
        }

        if (changed)
        {
            notifyEvent(PlayerEvent::StateChanged, new_state, nullptr);
        }
    }

    void IjkMediaPlayer::notifyEvent(PlayerEvent event, int arg1, void *arg2)
    {
        std::function<void(PlayerEvent, int, void *)> callback;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            callback = event_callback_;
        }

        if (callback)
        {
            callback(event, arg1, arg2);
        }
    }

    int IjkMediaPlayer::handleVideoFrame(const Frame *frame)
    {
        std::function<int(const Frame *)> callback;
        bool should_notify_first_frame = false;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            callback = video_frame_callback_;
            if (!first_video_frame_dispatched_)
            {
                first_video_frame_dispatched_ = true;
                should_notify_first_frame = true;
            }
        }

        if (should_notify_first_frame)
        {
            notifyEvent(PlayerEvent::VideoFrameReady, 0, const_cast<Frame *>(frame));
        }

        if (callback)
        {
            return callback(frame);
        }

        return 0;
    }

    void IjkMediaPlayer::bindVideoFrameCallbackLocked()
    {
        if (!ffplayer_)
        {
            return;
        }

        ffplayer_->AddVideoRefreshCallback([this](const Frame *frame)
                                           { return handleVideoFrame(frame); });
    }

    void IjkMediaPlayer::resetSessionFlagsLocked()
    {
        pending_resume_after_seek_ = false;
        state_before_seek_ = MP_STATE_IDLE;
        state_before_buffering_ = MP_STATE_IDLE;
        first_video_frame_dispatched_ = false;
    }

    int IjkMediaPlayer::resolvePostSeekStateLocked() const
    {
        if (pending_resume_after_seek_)
        {
            return MP_STATE_STARTED;
        }

        if (state_before_seek_ == MP_STATE_STARTED || state_before_seek_ == MP_STATE_BUFFERING)
        {
            return MP_STATE_STARTED;
        }

        if (state_before_seek_ == MP_STATE_PAUSED || state_before_seek_ == MP_STATE_COMPLETED)
        {
            return MP_STATE_PAUSED;
        }

        if (state_before_seek_ == MP_STATE_PREPARED)
        {
            return MP_STATE_PREPARED;
        }

        return MP_STATE_PAUSED;
    }

    int IjkMediaPlayer::resolvePostBufferingStateLocked() const
    {
        if (state_before_buffering_ == MP_STATE_IDLE || state_before_buffering_ == MP_STATE_BUFFERING)
        {
            return pending_resume_after_seek_ ? MP_STATE_STARTED : MP_STATE_PAUSED;
        }

        return state_before_buffering_;
    }

    bool IjkMediaPlayer::isStartAllowedLocked() const
    {
        return mp_state_ == MP_STATE_PREPARED ||
               mp_state_ == MP_STATE_PAUSED ||
               mp_state_ == MP_STATE_COMPLETED;
    }

    bool IjkMediaPlayer::isPauseAllowedLocked() const
    {
        return mp_state_ == MP_STATE_STARTED ||
               mp_state_ == MP_STATE_BUFFERING;
    }

    bool IjkMediaPlayer::isSeekAllowedLocked() const
    {
        return mp_state_ == MP_STATE_PREPARED ||
               mp_state_ == MP_STATE_STARTED ||
               mp_state_ == MP_STATE_PAUSED ||
               mp_state_ == MP_STATE_COMPLETED ||
               mp_state_ == MP_STATE_BUFFERING;
    }

} // namespace media
