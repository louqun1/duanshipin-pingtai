#pragma once
#include <thread>
#include <functional>
#include "FFMessageQueue.hpp"
#include "ff_ffplay_def.hpp"
extern "C"
{
#include "libavcodec/avcodec.h"
}

struct sonicStreamStruct;

class Decoder
{
public:
    int packet_pending_ = 0; // 是否有正在等待处理的包
    AVPacket pkt_;
    PacketQueue *queue_;
    AVCodecContext *avctx_;
    int pkt_serial_ = -1;
    int finished_ = 0;
    std::thread *decoder_thread_ = NULL;

    int64_t start_pts = AV_NOPTS_VALUE;
    AVRational start_pts_tb = {0, 1};
    int64_t next_pts = AV_NOPTS_VALUE;
    AVRational next_pts_tb = {0, 1};
    Decoder();
    ~Decoder();
    void decoder_init(AVCodecContext *avctx, PacketQueue *queue);
    // 创建和启动线程
    int decoder_start(enum AVMediaType codec_type, const char *thread_name, void *arg);
    // 停止线程
    void decoder_abort(FrameQueue *fq);
    void decoder_destroy();
    int decoder_decode_frame(AVFrame *frame);
    int get_video_frame(AVFrame *frame);
    // 写入一帧
    int queue_picture(FrameQueue *fq, AVFrame *src_frame, double pts,
                      double duration, int64_t pos, int serial);
    int audio_thread(void *arg);
    int video_thread(void *arg);
};
// namespace media
// {

    class FFPlayer
    {
    public:
        FFPlayer();
        int ffp_create();
        void ffp_destroy();
        int ffp_prepare_async_l(char *file_name);
        // 播放控制
        int ffp_start_l();
        int ffp_stop_l();
        int stream_open(const char *file_name);
        void stream_close();
        // 打开指定stream对应解码器、创建解码线程、以及初始化对应的输出
        int stream_component_open(int stream_index);
        void stream_component_close(int stream_index);
        int audio_open(AVChannelLayout wanted_channel_layout,
                       int wanted_nb_channels, int wanted_sample_rate,
                       struct AudioParams *audio_hw_params);
        void audio_close();
        // 获取播放时长
        long ffp_get_duration_l();
        long ffp_get_current_position_l();
        // 变速相关
        int get_target_frequency();
        int get_target_channels();
        void ffp_set_playback_rate(float rate);
        float ffp_get_playback_rate();
        bool is_normal_playback_rate();
        int ffp_get_playback_rate_change();
        void ffp_set_playback_rate_change(int change);
        // 音量相关
        void ffp_set_playback_volume(int value);
        // 暂停恢复
        int ffp_pause_l();
        void toggle_pause(int pause_on);
        void toggle_pause_l(int pause_on);
        void stream_update_pause_l();
        void stream_toggle_pause_l(int pause_on);
        // 截屏相关
        int ffp_screenshot_l(char *screen_path);
        void screenshot(AVFrame *frame);
        // seek相关
        int ffp_seek_to_l(long msec);
        void stream_seek(int64_t pos, int64_t rel, int seek_by_bytes);
        // 播放完毕相关判断 1. av_read_frame返回eof; 2. audio没有数据可以输出; 3.video没有数据可以输出
        void check_play_finish(); // 如果已经结束则通知ui调用停止函数
        // 供外包获取信息
        MessageQueue msg_queue_;
        char *input_filename_;
        int realtime = 0; // 判断流是否到达可进一步处理要求
        int stream_has_enough_packets(AVStream *st, int stream_id, PacketQueue *queue);
        int read_thread();
        std::thread *read_thread_;

        int video_refresh_thread();
        void video_refresh(double *remaining_time);
        double vp_duration(Frame *vp, Frame *nextvp);
        double compute_target_delay(double delay);
        void update_video_pts(double pts, int64_t pos, int serial);
        // 视频画面输出相关
        std::thread *video_refresh_thread_ = NULL;

        std::function<int(const Frame *)> video_refresh_callback_ = NULL;
        void AddVideoRefreshCallback(std::function<int(const Frame *)> callback);

        int get_master_sync_type();
        double get_master_clock();
        int av_sync_type = AV_SYNC_AUDIO_MASTER; // 音视频同步类型, 默认audio master
        Clock audclk;                            // 音频时钟
        Clock vidclk;                            // 视频时钟
        //    Clock	extclk;

        double audio_clock = 0; // 当前音频帧的PTS+当前帧Duration
        int audio_clock_serial; // 播放序列，seek可改变此值, 解码后保存
        std::atomic<int64_t> audio_callback_time{0};
        // 帧队列
        FrameQueue pictq;
        FrameQueue sampq;

        // 包队列
        PacketQueue audioq;
        PacketQueue videoq;
        std::atomic<int> abort_request{0};

        AVStream *audio_st = NULL;
        AVStream *video_st = NULL;
        std::atomic<int> force_refresh{0};
        double frame_timer = 0;

        int audio_stream = -1;
        int video_stream = -1;

        Decoder auddec;
        Decoder viddec;

        std::atomic<int> eof{0};
        std::atomic<int> audio_no_data{0};
        std::atomic<int> video_no_data{0};
        AVFormatContext *ic = NULL; // 多媒体文件元数据

        std::atomic<int> paused{0};
        // 音频输出相关
        struct AudioParams audio_src; // 保存最新解码的音频参数
        struct AudioParams audio_tgt; // 保存SDL音频输出需要的参数  默认初始化在ff_ffplay_def.hpp (line 124)
        struct SwrContext *swr_ctx = NULL;
        int audio_hw_buf_size = 0; // SDL音频缓冲区的大小(字节为单位)
        // 指向待播放的一帧音频数据，指向的数据区将被拷入SDL音频缓冲区。若经过重采样则指向audio_buf1，
        // 否则指向frame中的音频
        uint8_t *audio_buf = NULL;        // 指向需要重采样的数据
        uint8_t *audio_buf1 = NULL;       // 指向重采样后的数据
        unsigned int audio_buf_size = 0;  // 待播放的一帧音频数据(audio_buf指向)的大小
        unsigned int audio_buf1_size = 0; // 申请到的音频缓冲区audio_buf1的实际尺寸
        int audio_buf_index = 0;          // 更新拷贝位置 当前音频帧中已拷入SDL音频缓冲区
        int audio_write_buf_size = 0;
        std::atomic<int> audio_volume{50};
        int startup_volume = 50;
        // seek 相关
        int64_t seek_req = 0; // 请求seek位置
        int64_t seek_rel = 0; // 偏移量
        std::atomic<int64_t> seek_flags{0};
        int64_t seek_pos = 0; // seek后的位置
        // 截屏相关
        bool req_screenshot_ = false;
        char *screen_path_ = NULL;
        // 单步运行
        std::atomic<int> step{0};
        std::atomic<int> framedrop{1};
        std::atomic<int> frame_drops_late{0};

        std::atomic<int> pause_req{0};
        std::atomic<int> auto_resume{0};
        std::atomic<int> buffering_on{0};
        // 变速相关
        float pf_playback_rate = 1.0;     // 播放速率
        int pf_playback_rate_changed = 0; // 播放速率改变
        sonicStreamStruct *audio_speed_convert = nullptr;
        int max_frame_duration = 3600;
        // 统计相关的操作
    };
    inline static void ffp_notify_msg1(FFPlayer *ffp, int what)
    {
        msg_queue_put_simple3(&ffp->msg_queue_, what, 0, 0);
    }
    inline static void ffp_notify_msg2(FFPlayer *ffp, int what, int arg1)
    {
        msg_queue_put_simple3(&ffp->msg_queue_, what, arg1, 0);
    }
    inline static void ffp_notify_msg3(FFPlayer *ffp, int what, int arg1, int arg2)
    {
        msg_queue_put_simple3(&ffp->msg_queue_, what, arg1, arg2);
    }
    inline static void ffp_notify_msg4(FFPlayer *ffp, int what, int arg1, int arg2, void *obj, int obj_len)
    {
        msg_queue_put_simple4(&ffp->msg_queue_, what, arg1, arg2, obj, obj_len);
    }
    inline static void ffp_remove_msg(FFPlayer *ffp, int what)
    {
        msg_queue_remove(&ffp->msg_queue_, what);
    }
// }
