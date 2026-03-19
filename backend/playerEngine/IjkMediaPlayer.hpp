#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <thread>

#include "FFPlayer.hpp"
#include "FFMessageQueue.hpp"

namespace media {

#define MP_STATE_IDLE               0
#define MP_STATE_INITIALIZED        1
#define MP_STATE_ASYNC_PREPARING    2
#define MP_STATE_PREPARED           3
#define MP_STATE_STARTED            4
#define MP_STATE_PAUSED             5
#define MP_STATE_COMPLETED          6
#define MP_STATE_STOPPED            7
#define MP_STATE_ERROR              8
#define MP_STATE_SEEKING            9
#define MP_STATE_BUFFERING          10
#define MP_STATE_END                11

enum class PlayerEvent {
    Prepared,
    Playing,
    Paused,
    Stopped,
    SeekCompleted,
    PlaybackFinished,
    BufferingStarted,
    BufferingEnded,
    ErrorOccurred,
    StateChanged,
    VideoFrameReady,
    OpenInputStarted,
    ScreenshotCompleted
};

class IjkMediaPlayer {
public:
    IjkMediaPlayer();
    ~IjkMediaPlayer();

    int create();
    int destroy();

    int setDataSource(const char *url);
    int prepareAsync();

    int start();
    int pause();
    int stop();
    int seekTo(long msec);
    int screenshot(const char *file_path);

    int getState() const;
    long getCurrentPosition();
    long getDuration();

    void setPlaybackVolume(int volume);
    void setPlaybackRate(float rate);
    float getPlaybackRate();

    void setEventCallback(std::function<void(PlayerEvent, int, void*)> callback);
    void setVideoFrameCallback(std::function<int(const Frame*)> callback);

private:
    void messageLoop();
    void handleMessage(AVMessage *msg);
    void changeState(int new_state);
    void notifyEvent(PlayerEvent event, int arg1 = 0, void *arg2 = nullptr);
    int handleVideoFrame(const Frame *frame);
    void bindVideoFrameCallbackLocked();
    void resetSessionFlagsLocked();
    int resolvePostSeekStateLocked() const;
    int resolvePostBufferingStateLocked() const;
    bool isStartAllowedLocked() const;
    bool isPauseAllowedLocked() const;
    bool isSeekAllowedLocked() const;

private:
    mutable std::mutex mutex_;
    FFPlayer *ffplayer_ = nullptr;
    std::thread msg_thread_;
    std::atomic<bool> msg_thread_running_{false};
    char *data_source_ = nullptr;// 当前媒体资源地址
    int mp_state_ = MP_STATE_IDLE;

    std::function<void(PlayerEvent, int, void*)> event_callback_;
    std::function<int(const Frame*)> video_frame_callback_;
    bool pending_resume_after_seek_ = false;    //标记在 seek 操作完成后是否需要自动恢复播放
    int state_before_seek_ = MP_STATE_IDLE;     //seek前的状态
    int state_before_buffering_ = MP_STATE_IDLE; //buffering->缓冲前的状态
    bool first_video_frame_dispatched_ = false;
};

} // namespace media
