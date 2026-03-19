#pragma once

#include <array>
#include <condition_variable>
#include <deque>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <mutex>
#include <signal.h>
#include <stdint.h>

extern "C" {
#include "libavcodec/avfft.h"
#include "libavdevice/avdevice.h"
#include "libavformat/avformat.h"
#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/dict.h"
#include "libavutil/eval.h"
#include "libavutil/imgutils.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/pixdesc.h"
#include "libavutil/samplefmt.h"
#include "libavutil/time.h"
#include "libswresample/swresample.h"
#include "libswscale/swscale.h"
}

#include <SDL.h>
#include <SDL_thread.h>

#include <assert.h>

#include "IjksdlTimer.hpp"

#define MAX_QUEUE_SIZE (15 * 1024 * 1024)
#define MIN_FRAMES 25
#define EXTERNAL_CLOCK_MIN_FRAMES 2
#define EXTERNAL_CLOCK_MAX_FRAMES 10

#define SDL_VOLUME_STEP (0.75)
#define AV_SYNC_THRESHOLD_MIN 0.04
#define AV_SYNC_THRESHOLD_MAX 0.1
#define AV_SYNC_FRAMEDUP_THRESHOLD 0.1
#define AV_NOSYNC_THRESHOLD 10.0

typedef struct FFTrackCacheStatistic
{
    int64_t duration;
    int64_t bytes;
    int64_t packets;
} FFTrackCacheStatistic;

typedef struct FFStatistic
{
    int64_t vdec_type;
    float vfps;
    float vdps;
    float avdelay;
    float avdiff;
    int64_t bit_rate;
    FFTrackCacheStatistic video_cache;
    FFTrackCacheStatistic audio_cache;
    int64_t buf_backwards;
    int64_t buf_forwards;
    int64_t buf_capacity;
    SDL_SpeedSampler2 tcp_read_sampler;
    int64_t latest_seek_load_duration;
    int64_t byte_count;
    int64_t cache_physical_pos;
    int64_t cache_file_forwards;
    int64_t cache_file_pos;
    int64_t cache_count_bytes;
    int64_t logical_file_size;
    int drop_frame_count;
    int decode_frame_count;
    float drop_frame_rate;
} FFStatistic;

enum RET_CODE
{
    RET_ERR_UNKNOWN = -2,
    RET_FAIL = -1,
    RET_OK = 0,
    RET_ERR_OPEN_FILE,
    RET_ERR_NOT_SUPPORT,
    RET_ERR_OUTOFMEMORY,
    RET_ERR_STACKOVERFLOW,
    RET_ERR_NULLREFERENCE,
    RET_ERR_ARGUMENTOUTOFRANGE,
    RET_ERR_PARAMISMATCH,
    RET_ERR_MISMATCH_CODE,
    RET_ERR_EAGAIN,
    RET_ERR_EOF
};

typedef struct MyAVPacketList {
    AVPacket pkt;
    struct MyAVPacketList *next;
    int serial;
} MyAVPacketList;

typedef struct PacketQueue {
    std::deque<MyAVPacketList> packets;
    int nb_packets = 0;
    int size = 0;
    int64_t duration = 0;
    int abort_request = 1;// 用户退出请求标志
    int serial = 0;
    std::mutex mutex;
    std::condition_variable cond;
} PacketQueue;

#define VIDEO_PICTURE_QUEUE_SIZE 3
#define VIDEO_PICTURE_QUEUE_SIZE_MIN (3)
#define VIDEO_PICTURE_QUEUE_SIZE_MAX (16)
#define VIDEO_PICTURE_QUEUE_SIZE_DEFAULT (VIDEO_PICTURE_QUEUE_SIZE_MIN)
#define SUBPICTURE_QUEUE_SIZE 16
#define SAMPLE_QUEUE_SIZE 9
#define FRAME_QUEUE_SIZE FFMAX(SAMPLE_QUEUE_SIZE, FFMAX(VIDEO_PICTURE_QUEUE_SIZE, SUBPICTURE_QUEUE_SIZE))

typedef struct AudioParams {
    int freq = 0;
    int channels = 0;
    AVChannelLayout channel_layout = {};
    enum AVSampleFormat fmt = AV_SAMPLE_FMT_NONE;
    int frame_size = 0;
    int bytes_per_sec = 0;
} AudioParams;

typedef struct Frame {
    AVFrame *frame;
    int serial;
    double pts;
    double duration;
    int64_t pos;
    int width;
    int height;
    int format;
    AVRational sar;
    int uploaded;
    int flip_v;
} Frame;

typedef struct FrameQueue {
    std::array<Frame, FRAME_QUEUE_SIZE> queue{};
    int rindex = 0;
    int windex = 0;
    int size = 0;
    int max_size = 0;
    int keep_last = 0;
    int rindex_shown = 0;
    std::mutex mutex;
    std::condition_variable cond;
    PacketQueue *pktq = nullptr;
} FrameQueue;

typedef struct Clock {
    double pts;
    double pts_drift;
    double last_updated;
    double speed;
    int serial;
    int paused;
    int *queue_serial;
} Clock;

enum {
    AV_SYNC_UNKNOW_MASTER = -1,
    AV_SYNC_AUDIO_MASTER,
    AV_SYNC_VIDEO_MASTER,
};

#define fftime_to_milliseconds(ts) (av_rescale(ts, 1000, AV_TIME_BASE))
#define milliseconds_to_fftime(ms) (av_rescale(ms, AV_TIME_BASE, 1000))

extern AVPacket flush_pkt;

int packet_queue_put(PacketQueue *q, AVPacket *pkt);
int packet_queue_put_nullpacket(PacketQueue *q, int stream_index);
int packet_queue_init(PacketQueue *q);
void packet_queue_flush(PacketQueue *q);
void packet_queue_destroy(PacketQueue *q);
void packet_queue_abort(PacketQueue *q);
void packet_queue_start(PacketQueue *q);
int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block, int *serial);
double packet_queue_cache_duration(PacketQueue *q, AVRational time_base, double packet_duration);

int frame_queue_init(FrameQueue *f, PacketQueue *pktq, int max_size, int keep_last);
void frame_queue_destory(FrameQueue *f);
void frame_queue_signal(FrameQueue *f);
Frame *frame_queue_peek(FrameQueue *f);
Frame *frame_queue_peek_next(FrameQueue *f);
Frame *frame_queue_peek_last(FrameQueue *f);
Frame *frame_queue_peek_writable(FrameQueue *f);
Frame *frame_queue_peek_readable(FrameQueue *f);
void frame_queue_push(FrameQueue *f);
void frame_queue_next(FrameQueue *f);
int frame_queue_nb_remaining(FrameQueue *f);
int64_t frame_queue_last_pos(FrameQueue *f);

double get_clock(Clock *c);
void set_clock_at(Clock *c, double pts, int serial, double time);
void set_clock(Clock *c, double pts, int serial);
void init_clock(Clock *c, int *queue_serial);

void ffp_reset_statistic(FFStatistic *dcc);
