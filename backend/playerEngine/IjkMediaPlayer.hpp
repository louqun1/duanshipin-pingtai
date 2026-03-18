#pragma once
#include  <mutex>
#include <thread>
#include <functional>
#include "ff_ffplay_def.hpp"
#include "FFPlayer.hpp"
#include "FFMessageQueue.hpp"

/*-
 MPST_CHECK_NOT_RET(mp->mp_state, MP_STATE_IDLE);
 MPST_CHECK_NOT_RET(mp->mp_state, MP_STATE_INITIALIZED);
 MPST_CHECK_NOT_RET(mp->mp_state, MP_STATE_ASYNC_PREPARING);
 MPST_CHECK_NOT_RET(mp->mp_state, MP_STATE_PREPARED);
 MPST_CHECK_NOT_RET(mp->mp_state, MP_STATE_STARTED);
 MPST_CHECK_NOT_RET(mp->mp_state, MP_STATE_PAUSED);
 MPST_CHECK_NOT_RET(mp->mp_state, MP_STATE_COMPLETED);
 MPST_CHECK_NOT_RET(mp->mp_state, MP_STATE_STOPPED);
 MPST_CHECK_NOT_RET(mp->mp_state, MP_STATE_ERROR);
 MPST_CHECK_NOT_RET(mp->mp_state, MP_STATE_END);
 */

/*-
 * ijkmp_set_data_source()  -> MP_STATE_INITIALIZED
 *
 * ijkmp_reset              -> self
 * ijkmp_release            -> MP_STATE_END
 */
#define MP_STATE_IDLE               0

/*-
 * ijkmp_prepare_async()    -> MP_STATE_ASYNC_PREPARING
 *
 * ijkmp_reset              -> MP_STATE_IDLE
 * ijkmp_release            -> MP_STATE_END
 */
#define MP_STATE_INITIALIZED        1

/*-
 *                   ...    -> MP_STATE_PREPARED
 *                   ...    -> MP_STATE_ERROR
 *
 * ijkmp_reset              -> MP_STATE_IDLE
 * ijkmp_release            -> MP_STATE_END
 */
#define MP_STATE_ASYNC_PREPARING    2

/*-
 * ijkmp_seek_to()          -> self
 * ijkmp_start()            -> MP_STATE_STARTED
 *
 * ijkmp_reset              -> MP_STATE_IDLE
 * ijkmp_release            -> MP_STATE_END
 */
#define MP_STATE_PREPARED           3

/*-
 * ijkmp_seek_to()          -> self
 * ijkmp_start()            -> self
 * ijkmp_pause()            -> MP_STATE_PAUSED
 * ijkmp_stop()             -> MP_STATE_STOPPED
 *                   ...    -> MP_STATE_COMPLETED
 *                   ...    -> MP_STATE_ERROR
 *
 * ijkmp_reset              -> MP_STATE_IDLE
 * ijkmp_release            -> MP_STATE_END
 */
#define MP_STATE_STARTED            4

/*-
 * ijkmp_seek_to()          -> self
 * ijkmp_start()            -> MP_STATE_STARTED
 * ijkmp_pause()            -> self
 * ijkmp_stop()             -> MP_STATE_STOPPED
 *
 * ijkmp_reset              -> MP_STATE_IDLE
 * ijkmp_release            -> MP_STATE_END
 */
#define MP_STATE_PAUSED             5

/*-
 * ijkmp_seek_to()          -> self
 * ijkmp_start()            -> MP_STATE_STARTED (from beginning)
 * ijkmp_pause()            -> self
 * ijkmp_stop()             -> MP_STATE_STOPPED
 *
 * ijkmp_reset              -> MP_STATE_IDLE
 * ijkmp_release            -> MP_STATE_END
 */
#define MP_STATE_COMPLETED          6

/*-
 * ijkmp_stop()             -> self
 * ijkmp_prepare_async()    -> MP_STATE_ASYNC_PREPARING
 *
 * ijkmp_reset              -> MP_STATE_IDLE
 * ijkmp_release            -> MP_STATE_END
 */
#define MP_STATE_STOPPED            7

/*-
 * ijkmp_reset              -> MP_STATE_IDLE
 * ijkmp_release            -> MP_STATE_END
 */
#define MP_STATE_ERROR              8

/*-
 * ijkmp_release            -> self
 */
#define MP_STATE_END                9

#define  MP_SEEK_STEP            10  // 快退快进步长10秒


class IjkMediaPlayer
{
public:
    IjkMediaPlayer();
    ~IjkMediaPlayer();
    int ijkmp_create(std::function<int(void *)> msg_loop);
    int ijkmp_destroy();
    //设置要播放的url
    int ijkmp_set_data_source(const char *url);
    //准备播放
    int ijkmp_prepare_async();
    //触发播放
    int ijkmp_start();
    //停止
    int ijkmp_stop();
    //暂停
    int ijkmp_pause();
    //seek到指定位置
    int ijkmp_seek_to(long msec);
    //快进
    int ijkmp_forward_to(long incr);
    //快退
    int ijkmp_back_to(long incr);
    //截屏
    int ijkmp_screenshot(char *file_path);
    //获取播放状态
    int ijkmp_get_state();
    //是否在播放中
    bool ijkmp_is_playing();
    //当前播放位置
    long ijkmp_get_current_position();
    //总长度
    long ijkmp_get_duration();
    //已经播放的长度
    long ijkmp_get_playable_duration();
    //设置循环播放
    void ijkmp_set_loop(int loop);
    //获取是否循环播放
    int ijkmp_get_loop();
    //读取消息
    int ijkmp_get_msg(AVMessage *msg, int block);
    //设置音量
    void ijkmp_set_playback_volume(int volume);
    //循环消息处理
    int ijkmp_msg_loop(void *arg);
    //设置播放速率
    void ijkmp_set_playback_rate(float rate);
    //获取播放速率
    float ijkmp_get_playback_rate();
    //
    void AddVideoRefreshCallback(std::function<int(const Frame *)>callback);
    //获取状态值
    int64_t ijkmp_get_property_int64(int id, int64_t default_value);
    //
    void ijkmp_change_state_l(int new_state);
private:
    // 互斥量
    std::mutex mutex_;
    // 真正的播放器
    FFPlayer *ffplayer_ = NULL;
    //函数指针, 指向创建的message_loop，即消息循环函数
    //    int (*msg_loop)(void*);
    std::function<int(void *)> msg_loop_ = NULL; // ui处理消息的循环
    //消息机制线程
    std::thread *msg_thread_; // 执行msg_loop
    //    SDL_Thread _msg_thread;
    //字符串，就是一个播放url
    char *data_source_;
    //播放器状态，例如prepared,resumed,error,completed等
    int mp_state_;  // 播放状态

    int seek_req = 0;
    long seek_msec = 0;

    // 截屏请求
    char *file_path_ = NULL;
};
