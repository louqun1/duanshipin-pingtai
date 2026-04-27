#define SONIC_IMPLEMENTATION
#include "sonic.hpp"
#include "FFPlayer.hpp"
#include <cstdio>
#include <cmath>
#include <string.h>
#include "FFMessage.hpp"
// #include "screenshot.h"

#include "spdlog/spdlog.h"
/* Minimum SDL audio buffer size, in samples. */
#define SDL_AUDIO_MIN_BUFFER_SIZE 512
/* Calculate actual buffer size keeping in mind not cause too frequent audio callbacks */
#define SDL_AUDIO_MAX_CALLBACKS_PER_SEC 30
int infinite_buffer = 0;
static int decoder_reorder_pts = -1;
/*decoder_reorder_pts 决定解码后帧的 PTS 从哪来：
                                                    自动估算？               -1
                                                    直接拿包的 DTS？          0
                                                    还是完全相信解码器？        1
*/
static int seek_by_bytes = -1;
void print_error(const char *filename, int err)
{
    char errbuf[128];
    const char *errbuf_ptr = errbuf;
    if (av_strerror(err, errbuf, sizeof(errbuf)) < 0)
    {
        errbuf_ptr = strerror(AVUNERROR(err));
    }
    av_log(NULL, AV_LOG_ERROR, "%s: %s\n", filename, errbuf_ptr);
}

namespace
{

void describe_audio_layout(const AVChannelLayout &layout, char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0)
    {
        return;
    }

    buffer[0] = '\0';
    if (layout.nb_channels <= 0)
    {
        std::snprintf(buffer, buffer_size, "0 channels");
        return;
    }

    if (av_channel_layout_describe(&layout, buffer, buffer_size) < 0)
    {
        std::snprintf(buffer, buffer_size, "%d channels", layout.nb_channels);
    }
}

void reset_audio_params(AudioParams *params)
{
    if (!params)
    {
        return;
    }

    av_channel_layout_uninit(&params->channel_layout);
    *params = AudioParams{};
}

void copy_audio_params(AudioParams *dst, const AudioParams &src)
{
    if (!dst)
    {
        return;
    }

    reset_audio_params(dst);
    dst->freq = src.freq;
    dst->channels = src.channels;
    dst->fmt = src.fmt;
    dst->frame_size = src.frame_size;
    dst->bytes_per_sec = src.bytes_per_sec;

    if (src.channel_layout.nb_channels > 0)
    {
        if (av_channel_layout_copy(&dst->channel_layout, &src.channel_layout) < 0)
        {
            av_channel_layout_default(&dst->channel_layout, src.channel_layout.nb_channels);
        }
    }
    else if (src.channels > 0)
    {
        av_channel_layout_default(&dst->channel_layout, src.channels);
    }
}

bool normalize_audio_frame_metadata(const AVCodecContext *avctx, AVFrame *frame)
{
    if (!avctx || !frame)
    {
        return false;
    }

    if (frame->format == AV_SAMPLE_FMT_NONE && avctx->sample_fmt != AV_SAMPLE_FMT_NONE)
    {
        frame->format = avctx->sample_fmt;
    }

    if (frame->sample_rate <= 0 && avctx->sample_rate > 0)
    {
        frame->sample_rate = avctx->sample_rate;
    }

    if (frame->ch_layout.nb_channels <= 0 && avctx->ch_layout.nb_channels > 0)
    {
        av_channel_layout_uninit(&frame->ch_layout);
        if (av_channel_layout_copy(&frame->ch_layout, &avctx->ch_layout) < 0)
        {
            av_channel_layout_default(&frame->ch_layout, avctx->ch_layout.nb_channels);
        }
    }

    return frame->format != AV_SAMPLE_FMT_NONE &&
           frame->sample_rate > 0 &&
           frame->ch_layout.nb_channels > 0;
}

const char *safe_sample_fmt_name(AVSampleFormat fmt)
{
    const char *name = av_get_sample_fmt_name(fmt);
    return name ? name : "unknown";
}

const char *safe_pix_fmt_name(AVPixelFormat fmt)
{
    const char *name = av_get_pix_fmt_name(fmt);
    return name ? name : "unknown";
}

} // namespace

int FFPlayer::ffp_create()
{
    msg_queue_init(&msg_queue_);
    return 0;
}

void FFPlayer::ffp_destroy()
{
    stream_close();
    // 销毁消息队列
    msg_queue_destroy(&msg_queue_);
}

int FFPlayer::ffp_prepare_async_l(char *file_name)
{
    // 保存文件名
    spdlog::info("[ffplayer] prepare_async requested url={}", file_name ? file_name : "<null>");
    input_filename_ = strdup(file_name);
    int reval = stream_open(file_name);
    return reval;
}

int FFPlayer::ffp_start_l()
{
    // 触发播放
    spdlog::info("[ffplayer] start playback");
    toggle_pause(0);
    return 0;
}

int FFPlayer::ffp_stop_l()
{
    spdlog::info("[ffplayer] stop playback");
    abort_request.store(1);       // 请求退出
    msg_queue_abort(&msg_queue_); // 禁止再插入消息
    return 0;
}

int FFPlayer::stream_open(const char *file_name)
{
    logged_first_audio_packet_ = false;
    logged_first_video_packet_ = false;
    logged_demux_backpressure_ = false;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
    {
        av_log(NULL, AV_LOG_FATAL, "Could not initialize SDL - %s\n", SDL_GetError());
        av_log(NULL, AV_LOG_FATAL, "(Did you set the DISPLAY variable?)\n");
        return -1;
    }
    // 初始化Frame帧队列
    if (frame_queue_init(&pictq, &videoq, VIDEO_PICTURE_QUEUE_SIZE_DEFAULT, 1) < 0)
    {
        goto fail;
    }
    // 要注意最后一个值设置为1的重要性
    if (frame_queue_init(&sampq, &audioq, SAMPLE_QUEUE_SIZE, 1) < 0)
    {
        goto fail;
    }
    // 初始化Packet队列
    if (packet_queue_init(&videoq) < 0 || packet_queue_init(&audioq) < 0)
    {
        goto fail;
    }
    // 初始化时钟 时钟序列->queue_serial，实际上指向的是videoq.serial
    init_clock(&vidclk, &videoq.serial);
    init_clock(&audclk, &audioq.serial);
    audio_clock_serial = -1;
    // prepareAsync only prepares streams and buffers. start() is the point that should
    // actually resume A/V clocks and output.
    pause_req.store(1);
    paused.store(1);
    auto_resume.store(0);
    buffering_on.store(0);
    step.store(0);
    audclk.paused = 1;
    vidclk.paused = 1;
    // 初始化音量等
    spdlog::info("[ffplayer] stream_open initialized url={}", file_name ? file_name : "<null>");

    // 创建解复用器读数据线程read_thread
    read_thread_ = new std::thread(&FFPlayer::read_thread, this);
    video_refresh_thread_ = new std::thread(&FFPlayer::video_refresh_thread, this);
    return 0;
fail:
    stream_close();
    return -1;
}

void FFPlayer::stream_close()
{
    spdlog::info("[ffplayer] stream_close begin");
    abort_request.store(1);
    if (read_thread_ && read_thread_->joinable())
    {
        read_thread_->join(); // 等待线程退出
    }
    /* close each stream */
    if (audio_stream >= 0)
    {
        stream_component_close(audio_stream);
    }
    if (video_stream >= 0)
    {
        stream_component_close(video_stream);
    }
    // 关闭解复用器 avformat_close_input(&ic);
    packet_queue_destroy(&audioq);
    packet_queue_destroy(&videoq);

    frame_queue_destory(&pictq);
    frame_queue_destory(&sampq);
    if (input_filename_)
    {
        free(input_filename_);
        input_filename_ = NULL;
    }
    spdlog::info("[ffplayer] stream_close finished");
}

int FFPlayer::stream_component_open(int stream_index)
{
    AVCodecContext *avctx;
    AVCodec *codec;
    int sample_rate;
    int nb_channels;
    AVChannelLayout channel_layout;
    int ret = 0;
    if ((stream_index < 0) || (stream_index >= ic->nb_streams))
    {
        return -1;
    }

    avctx = avcodec_alloc_context3(NULL);
    if (!avctx)
    {
        return AVERROR(ENOMEM);
    }
    /* 将码流中的编解码器信息拷贝到新分配的编解码器上下文结构体 */
    ret = avcodec_parameters_to_context(avctx, ic->streams[stream_index]->codecpar);
    if (ret < 0)
    {
        spdlog::error("[ffplayer] avcodec_parameters_to_context failed stream_index={} ret={}", stream_index, ret);
        goto fail;
    }
    avctx->pkt_timebase = ic->streams[stream_index]->time_base;
    /* 根据codec_id查找解码器 */
    codec = (AVCodec *)avcodec_find_decoder(avctx->codec_id);
    if (!codec)
    {
        av_log(NULL, AV_LOG_WARNING,
               "No decoder could be found for codec %s\n", avcodec_get_name(avctx->codec_id));
        ret = AVERROR(EINVAL);
        goto fail;
    }
    if ((ret = avcodec_open2(avctx, codec, NULL)) < 0)
    {
        spdlog::error("avcodec_open2 failed, ret={}", ret);
        goto fail;
    }
    switch (avctx->codec_type)
    {
    case AVMEDIA_TYPE_AUDIO:
        // 从avctx(即AVCodecContext)中获取音频格式参数
        sample_rate = avctx->sample_rate;
        
        nb_channels = avctx->ch_layout.nb_channels;
        
        channel_layout = avctx->ch_layout;
         // 通道布局
        // 调用audio_open打开sdl音频输出，实际打开的设备参数保存在audio_tgt，返回值表示输出设备的缓冲区大小
        if ((ret = audio_open(channel_layout, nb_channels, sample_rate, &audio_tgt)) < 0)
        {
            spdlog::error("audio_open failed, ret={}", ret);
            goto fail;
        }
        audio_hw_buf_size = ret;
        copy_audio_params(&audio_src, audio_tgt); // 暂且将数据源参数等同于目标输出参数
        audio_buf_size = 0;
        audio_buf_index = 0;
        audio_stream = stream_index;
        audio_st = ic->streams[stream_index]; // 获取audio的stream指针
        {
            char layout_desc[64];
            describe_audio_layout(channel_layout, layout_desc, sizeof(layout_desc));
            spdlog::info(
                "[ffplayer] opened audio stream index={} codec={} sample_rate={} channels={} layout={} time_base={}/{}",
                stream_index,
                avcodec_get_name(avctx->codec_id),
                sample_rate,
                nb_channels,
                layout_desc,
                audio_st->time_base.num,
                audio_st->time_base.den);
        }
        // 初始化ffplay封装的音频解码器, 并将解码器上下文 avctx和Decoder绑定
        auddec.decoder_init(avctx, &audioq);
        auddec.decoder_start(AVMEDIA_TYPE_AUDIO, "audio_thread", this);
        SDL_PauseAudio(0);
        break;
    case AVMEDIA_TYPE_VIDEO:
        video_stream = stream_index;
        video_st = ic->streams[stream_index];
        {
            const AVRational guessed_frame_rate = av_guess_frame_rate(ic, video_st, NULL);
            const double guessed_fps =
                guessed_frame_rate.num > 0 && guessed_frame_rate.den > 0 ? av_q2d(guessed_frame_rate) : 0.0;
            spdlog::info(
                "[ffplayer] opened video stream index={} codec={} resolution={}x{} pix_fmt={} time_base={}/{} fps={:.3f}",
                stream_index,
                avcodec_get_name(avctx->codec_id),
                avctx->width,
                avctx->height,
                safe_pix_fmt_name(static_cast<AVPixelFormat>(avctx->pix_fmt)),
                video_st->time_base.num,
                video_st->time_base.den,
                guessed_fps);
        }
        viddec.decoder_init(avctx, &videoq);
        if ((ret = viddec.decoder_start(AVMEDIA_TYPE_VIDEO, "video_decoder", this)) < 0)
        {
            goto out;
        }
        break;
    default:
        break;
    }
    goto out;
fail:
    avcodec_free_context(&avctx);
out:
    return ret;
}

void FFPlayer::stream_component_close(int stream_index)
{
    AVCodecParameters *codecpar; // 编解码器参数
    if (stream_index < 0 || stream_index >= ic->nb_streams)
    {
        return;
    }
    codecpar = ic->streams[stream_index]->codecpar;
    switch (codecpar->codec_type)
    { // 流类型
    case AVMEDIA_TYPE_AUDIO:
        // 请求终止解码器线程
        auddec.decoder_abort(&sampq);
        audio_close();
        auddec.decoder_destroy();
        swr_free(&swr_ctx);
        av_freep(&audio_buf1);
        audio_buf1_size = 0;
        audio_buf = NULL;
        break;
    case AVMEDIA_TYPE_VIDEO:
        if (video_refresh_thread_ && video_refresh_thread_->joinable())
        {
            video_refresh_thread_->join();
        }
        viddec.decoder_abort(&pictq);
        viddec.decoder_destroy();
        break;
    default:
        break;
    }
    //    ic->streams[stream_index]->discard = AVDISCARD_ALL;  // 这个又有什么用?
    switch (codecpar->codec_type)
    {
    case AVMEDIA_TYPE_AUDIO:
        audio_st = NULL;
        audio_stream = -1;
        break;
    case AVMEDIA_TYPE_VIDEO:
        video_st = NULL;
        video_stream = -1;
        break;
    default:
        break;
    }
}
static int audio_decode_frame(FFPlayer *is)
{
    int data_size, resampled_data_size;
    AVChannelLayout dec_channel_layout = AV_CHANNEL_LAYOUT_STEREO;
    int wanted_nb_samples;
    Frame *af;
    int ret = 0;
    if (is->paused.load())
    {
        return -1;
    }
    // 读取一帧数据
    do
    {
        if (!(af = frame_queue_peek_readable(&is->sampq)))
        {
            return -1;
        }
        frame_queue_next(&is->sampq);
        if (af->serial != is->audioq.serial)
        {
            continue;
        }
        if (!af->frame)
        {
            spdlog::warn("skip audio frame: frame pointer is null");
            continue;
        }
        if (!normalize_audio_frame_metadata(is->auddec.avctx_, af->frame))
        {
            spdlog::warn("skip audio frame: invalid metadata nb_samples={}, format={}, sample_rate={}, channels={}",
                         static_cast<int>(af->frame->nb_samples),
                         static_cast<int>(af->frame->format),
                         static_cast<int>(af->frame->sample_rate),
                         static_cast<int>(af->frame->ch_layout.nb_channels));
            continue;
        }
        if (af->frame->nb_samples <= 0 || !af->frame->extended_data || !af->frame->data[0])
        {
            spdlog::warn("skip audio frame: empty samples nb_samples={}, data0={}",
                         static_cast<int>(af->frame->nb_samples),
                         static_cast<const void *>(af->frame->data[0]));
            continue;
        }
        break;
    } while (1);
    // spdlog::info("audio.nb_samples={}, channels={}, channel_layout={}, format={}, sample_rate={}",
    //              static_cast<int>(af->frame->nb_samples), static_cast<int>(af->frame->ch_layout.nb_channels), layout_buf,
    //              static_cast<int>(af->frame->format), static_cast<int>(af->frame->sample_rate));
    // spdlog::info("audio frame data pointers: data[0]={}, data[1]={}",
    //              static_cast<const void *>(af->frame->data[0]),
    //              static_cast<const void *>(af->frame->data[1]));

    // 根据frame中指定的音频参数获取缓冲区的大小 af->frame->channels * af->frame->nb_samples * 2
    data_size = av_samples_get_buffer_size(NULL, af->frame->ch_layout.nb_channels,
                                           af->frame->nb_samples,
                                           (enum AVSampleFormat)af->frame->format, 1);
    if (data_size < 0)
    {
        spdlog::error("av_samples_get_buffer_size failed for audio frame");
        return -1;
    }
    // 获取声道布局
    // Use AVChannelLayout API for FFmpeg >= 6.0
    if (af->frame->ch_layout.nb_channels > 0)
    {
        av_channel_layout_uninit(&dec_channel_layout);
        if (av_channel_layout_copy(&dec_channel_layout, &af->frame->ch_layout) < 0)
        {
            av_channel_layout_default(&dec_channel_layout, af->frame->ch_layout.nb_channels);
        }
    }
    else
    {
        av_channel_layout_default(&dec_channel_layout, af->frame->ch_layout.nb_channels > 0 ? af->frame->ch_layout.nb_channels : 2);
    }
    if (dec_channel_layout.nb_channels <= 0)
    {
        char layout_desc[64];
        av_channel_layout_describe(&af->frame->ch_layout, layout_desc, sizeof(layout_desc));
        spdlog::warn("[ffplayer] invalid decoded audio layout='{}' channels={}", layout_desc, af->frame->ch_layout.nb_channels);
        av_channel_layout_default(&dec_channel_layout, 2); // 默认设置为立体声（2通道）
        return -1;                                         // 异常情况
    }
    wanted_nb_samples = af->frame->nb_samples;

    if (af->frame->format != is->audio_src.fmt ||
        av_channel_layout_compare(&dec_channel_layout, &is->audio_src.channel_layout) != 0 ||
        af->frame->sample_rate != is->audio_src.freq ||
        (wanted_nb_samples != af->frame->nb_samples && !is->swr_ctx))
    {
        swr_free(&is->swr_ctx);
        // 假设 is->audio_tgt.channel_layout 现在是 AVChannelLayout 类型（如果不是，也需要升级）
        SwrContext *swr_ctx = NULL;
        int ret = swr_alloc_set_opts2(&swr_ctx,
                                      &is->audio_tgt.channel_layout, // 输出布局（需升级为 AVChannelLayout）
                                      is->audio_tgt.fmt,
                                      is->audio_tgt.freq,
                                      &dec_channel_layout, // 输入布局（AVChannelLayout*）
                                      (enum AVSampleFormat)af->frame->format,
                                      af->frame->sample_rate,
                                      0, NULL);
        //打印swr_alloc_set_opts2参数
        // 检查参数有效性并打印详细信息
        bool param_error = false;
        if (is->audio_tgt.channel_layout.nb_channels <= 0) {
            spdlog::error("Invalid output channel_layout.nb_channels: {}", is->audio_tgt.channel_layout.nb_channels);
            param_error = true;
        }
        if (is->audio_tgt.fmt == AV_SAMPLE_FMT_NONE) {
            spdlog::error("Invalid output sample format: {}", static_cast<int>(is->audio_tgt.fmt));
            param_error = true;
        }
        if (is->audio_tgt.freq <= 0) {
            spdlog::error("Invalid output sample rate: {}", is->audio_tgt.freq);
            param_error = true;
        }
        if (dec_channel_layout.nb_channels <= 0) {
            spdlog::error("Invalid input channel_layout.nb_channels: {}", dec_channel_layout.nb_channels);
            param_error = true;
        }
        if ((enum AVSampleFormat)af->frame->format == AV_SAMPLE_FMT_NONE) {
            spdlog::error("Invalid input sample format: {}", static_cast<int>(af->frame->format));
            param_error = true;
        }
        if (af->frame->sample_rate <= 0) {
            spdlog::error("Invalid input sample rate: {}", af->frame->sample_rate);
            param_error = true;
        }
        if (!param_error) {
            spdlog::info(
                "[ffplayer] reconfigure audio resampler input={}Hz/{}ch/{} output={}Hz/{}ch/{}",
                static_cast<int>(af->frame->sample_rate),
                static_cast<int>(dec_channel_layout.nb_channels),
                safe_sample_fmt_name(static_cast<AVSampleFormat>(af->frame->format)),
                static_cast<int>(is->audio_tgt.freq),
                static_cast<int>(is->audio_tgt.channel_layout.nb_channels),
                safe_sample_fmt_name(is->audio_tgt.fmt));
        }
        if (ret < 0)
        {
            // 错误处理
        }
        is->swr_ctx = swr_ctx;

        ret = 0;
        if (!is->swr_ctx || (ret = swr_init(is->swr_ctx)) < 0)
        {
            char errstr[256] = {0};
            av_strerror(ret, errstr, sizeof(errstr));
            spdlog::error("swr_init failed: {}", errstr);
            sprintf(errstr, "Cannot create sample rate converter for conversion of %d Hz %s %d channels to %d Hz %s %d channels!\n",
                    af->frame->sample_rate, av_get_sample_fmt_name((enum AVSampleFormat)af->frame->format), af->frame->ch_layout.nb_channels,
                    is->audio_tgt.freq, av_get_sample_fmt_name(is->audio_tgt.fmt), is->audio_tgt.channels);
            spdlog::error("{}", errstr);
            swr_free(&is->swr_ctx);
            ret = -1;
            goto fail;
        }
        is->audio_src.channels = af->frame->ch_layout.nb_channels;
        is->audio_src.freq = af->frame->sample_rate;
        is->audio_src.fmt = (enum AVSampleFormat)af->frame->format;
        is->audio_src.frame_size = data_size;
        is->audio_src.bytes_per_sec = av_samples_get_buffer_size(NULL,
                                                                 is->audio_src.channels,
                                                                 is->audio_src.freq,
                                                                 is->audio_src.fmt,
                                                                 1);
        av_channel_layout_uninit(&is->audio_src.channel_layout);
        if (av_channel_layout_copy(&is->audio_src.channel_layout, &dec_channel_layout) < 0)
        {
            av_channel_layout_default(&is->audio_src.channel_layout, dec_channel_layout.nb_channels);
        }
    }
    if (is->swr_ctx)
    {
        // 重采样输入参数1：输入音频样本数是af->frame->nb_samples
        // 重采样输入参数2：输入音频缓冲区
        const uint8_t **in = (const uint8_t **)af->frame->extended_data; // data[0] data[1]
        // 重采样输出参数1：输出音频缓冲区
        uint8_t **out = &is->audio_buf1; // 真正分配缓存audio_buf1，指向是用audio_buf
        // 重采样输出参数2：输出音频缓冲区尺寸， 高采样率往低采样率转换时得到更少的样本数量，比如 96k->48k, wanted_nb_samples=1024
        // 则wanted_nb_samples * audio_tgt.freq / af->frame->sample_rate 为1024*48000/96000 = 512
        // +256 的目的是重采样内部是有一定的缓存，就存在上一次的重采样还缓存数据和这一次重采样一起输出的情况，所以目的是多分配输出buffer
        int out_count = (int64_t)wanted_nb_samples * is->audio_tgt.freq / af->frame->sample_rate + 256;
        // 计算对应的样本数 对应的采样格式 以及通道数，需要多少buffer空间
        int out_size = av_samples_get_buffer_size(NULL, is->audio_tgt.channels,
                                                  out_count, is->audio_tgt.fmt, 0);
        int len2 = 0;
        if (out_size < 0)
        {
            av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size() failed\n");
            ret = -1;
            goto fail;
        }
        // if(audio_buf1_size < out_size) {重新分配out_size大小的缓存给audio_buf1, 并将audio_buf1_size设置为out_size }
        av_fast_malloc(&is->audio_buf1, &is->audio_buf1_size, out_size);
        if (!is->audio_buf1)
        {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        // 音频重采样：len2返回值是重采样后得到的音频数据中单个声道的样本数
        len2 = swr_convert(is->swr_ctx, out, out_count, in, af->frame->nb_samples);
        if (len2 < 0)
        {
            av_log(NULL, AV_LOG_ERROR, "swr_convert() failed\n");
            ret = -1;
            goto fail;
        }
        if (len2 == out_count)
        { // 这里的意思是我已经多分配了buffer，实际输出的样本数不应该超过我多分配的数量
            av_log(NULL, AV_LOG_WARNING, "audio buffer is probably too small\n");
            if (swr_init(is->swr_ctx) < 0)
            {
                swr_free(&is->swr_ctx);
            }
        }
        // 重采样返回的一帧音频数据大小(以字节为单位)
        is->audio_buf = is->audio_buf1;
        resampled_data_size = len2 * is->audio_tgt.channels * av_get_bytes_per_sample(is->audio_tgt.fmt);
    }
    else
    {
        // 未经重采样，则将指针指向frame中的音频数据
        is->audio_buf = af->frame->data[0];
        resampled_data_size = data_size;
    }
    if (!std::isnan(af->pts))
    { // 别看错了  这个不是nan  下面的才是！！！
        is->audio_clock = af->pts + (double)af->frame->nb_samples / af->frame->sample_rate;
    }
    else
    {
        is->audio_clock = NAN;
    }
    is->audio_clock_serial = af->serial; // 保存当前解码帧的serial
    ret = resampled_data_size;
fail:
    av_channel_layout_uninit(&dec_channel_layout);
    return ret;
}
static void sdl_audio_callback(void *opaque, Uint8 *stream, int len)
{
    // 2ch 2字节 1024 = 4096 -> 回调每次读取2帧数据
    FFPlayer *is = (FFPlayer *)opaque;
    int audio_size, len1;
    is->audio_callback_time.store(av_gettime_relative());
    while (len > 0)
    { // 循环读取，直到读取到足够的数据
        /* (1)如果audio_buf_index < audio_buf_size则说明上次拷贝还剩余一些数据，
         * 先拷贝到stream再调用audio_decode_frame
         * (2)如果audio_buf消耗完了，则调用audio_decode_frame重新填充audio_buf
         */
        if (is->audio_buf_index >= is->audio_buf_size)
        {
            audio_size = audio_decode_frame(is);
            //                        LOG(INFO) << "  audio_size: " << audio_size;
            if (audio_size < 0)
            {
                is->audio_buf = NULL;
                is->audio_buf_size = is->audio_tgt.frame_size > 0
                                         ? (SDL_AUDIO_MIN_BUFFER_SIZE / is->audio_tgt.frame_size) * is->audio_tgt.frame_size
                                         : 0;
                is->audio_no_data.store(1);
                if (is->eof.load())
                {
                    // 如果文件以及读取完毕，此时应该判断是否还有数据可以读取，如果没有就该发送通知ui停止播放
                    is->check_play_finish();
                }
            }
            else
            {
                is->audio_buf_size = audio_size;
                is->audio_no_data.store(0);
            }
            is->audio_buf_index = 0;
            // 2是否要做变速 已写
            if (is->ffp_get_playback_rate_change())
            {
                is->ffp_set_playback_rate_change(0);
                // 初始化
                if (is->audio_speed_convert)
                {
                    // 先释放
                    sonicDestroyStream(is->audio_speed_convert);
                }
                // 再创建
                is->audio_speed_convert = sonicCreateStream(is->get_target_frequency(),
                                                            is->get_target_channels());
                // 设置变速系数
                sonicSetSpeed(is->audio_speed_convert, is->ffp_get_playback_rate());
                sonicSetPitch(is->audio_speed_convert, 1.0); // 音高
                sonicSetRate(is->audio_speed_convert, 1.0);
            }
            if (!is->is_normal_playback_rate() && is->audio_buf)
            {
                // 非normal 则需要修改  audio_buf_index audio_buf_size audio_buf
                /*缓冲区可存储 多少 个单声道样本*/
                int actual_out_samples = is->audio_buf_size /
                                         (is->audio_tgt.channels * av_get_bytes_per_sample(is->audio_tgt.fmt));
                // 计算处理后的点数
                int out_ret = 0;
                int out_size = 0;
                int num_samples = 0;
                int sonic_samples = 0;
                if (is->audio_tgt.fmt == AV_SAMPLE_FMT_FLT)
                {
                    out_ret = sonicWriteFloatToStream(is->audio_speed_convert,
                                                      (float *)is->audio_buf,
                                                      actual_out_samples);
                }
                else if (is->audio_tgt.fmt == AV_SAMPLE_FMT_S16)
                {
                    out_ret = sonicWriteShortToStream(is->audio_speed_convert,
                                                      (short *)is->audio_buf,
                                                      actual_out_samples);
                }
                else
                {
                    av_log(NULL, AV_LOG_ERROR, "sonic unspport ......\n");
                }
                num_samples = sonicSamplesAvailable(is->audio_speed_convert);
                // 2通道  目前只支持2通道的
                out_size = (num_samples)*av_get_bytes_per_sample(is->audio_tgt.fmt) * is->audio_tgt.channels;
                av_fast_malloc(&is->audio_buf1, &is->audio_buf1_size, out_size);
                if (out_ret)
                {
                    // 从流中读取处理好的数据
                    if (is->audio_tgt.fmt == AV_SAMPLE_FMT_FLT)
                    {
                        sonic_samples = sonicReadFloatFromStream(is->audio_speed_convert,
                                                                 (float *)is->audio_buf1,
                                                                 num_samples);
                    }
                    else if (is->audio_tgt.fmt == AV_SAMPLE_FMT_S16)
                    {
                        sonic_samples = sonicReadShortFromStream(is->audio_speed_convert,
                                                                 (short *)is->audio_buf1,
                                                                 num_samples);
                    }
                    else
                    {
                        spdlog::error("sonic unsport fmt: {}", av_get_sample_fmt_name(is->audio_tgt.fmt));
                    }
                    is->audio_buf = is->audio_buf1;
                    is->audio_buf_size = sonic_samples * is->audio_tgt.channels * av_get_bytes_per_sample(is->audio_tgt.fmt);
                    is->audio_buf_index = 0;
                }
            }
            // 变速结束
        }
        if (is->audio_buf_size == 0)
        {
            continue;
        }
        // 根据缓冲区剩余大小量力而行
        len1 = is->audio_buf_size - is->audio_buf_index;
        if (len1 > len)
        {
            len1 = len;
        }
        if (is->audio_buf && is->audio_volume.load() == SDL_MIX_MAXVOLUME)
        { // 暂时默认 128
            memcpy(stream, (uint8_t *)is->audio_buf + is->audio_buf_index, len1);
        }
        else
        {
            memset(stream, 0, len1); // 先静音 防止噪声 再变换声音
            if (is->audio_buf)
            {
                SDL_MixAudio(stream, (uint8_t *)is->audio_buf + is->audio_buf_index, len1, is->audio_volume.load());
            }
        }
        /* 更新audio_buf_index，指向audio_buf中未被拷贝到stream的数据（剩余数据）的起始位置 */
        len -= len1;
        stream += len1;
        is->audio_buf_index += len1;
    }
    is->audio_write_buf_size = is->audio_buf_size - is->audio_buf_index;
    /* Let's assume the audio driver that is used by SDL has two periods. */
    if (!std::isnan(is->audio_clock))
    {
        double audio_clock = is->audio_clock / is->ffp_get_playback_rate();
        set_clock_at(&is->audclk,
                     audio_clock - (double)(2 * is->audio_hw_buf_size + is->audio_write_buf_size) / is->audio_tgt.bytes_per_sec,
                     is->audio_clock_serial,
                     is->audio_callback_time.load() / 1000000.0);
        //        LOG(INFO) << "audio_clock->pts = " << is->audclk.pts;
    }
}
int FFPlayer::audio_open(AVChannelLayout wanted_channel_layout, int wanted_nb_channels, int wanted_sample_rate, AudioParams *audio_hw_params)
{
    // spdlog::info("audio_open: wanted_nb_channels={}, wanted_sample_rate={}", wanted_nb_channels, wanted_sample_rate);
    SDL_AudioSpec wanted_spec;             // 音频参数设置SDL_AudioSpec
    wanted_spec.freq = wanted_sample_rate; // 采样率
    wanted_spec.format = AUDIO_S16SYS;     // 采样点格式
    wanted_spec.channels = wanted_nb_channels;
    wanted_spec.silence = 0;
    wanted_spec.samples = 2048; // 23.2ms -> 46.4ms 每次读取的采样数量，多久产生一次回调和 samples
    wanted_spec.callback = sdl_audio_callback;
    wanted_spec.userdata = this;
    //    SDL_OpenAudioDevice
    // 打开音频设备
    const char *driver = SDL_GetCurrentAudioDriver();
    if (SDL_OpenAudio(&wanted_spec, NULL) != 0)
    {
        spdlog::error("Failed to open audio device, err: {}", SDL_GetError());
        return -1;
    }
    // wanted_spec是期望的参数，spec是实际的参数，wanted_spec和spec都是SDL中的结构。
    // 此处audio_hw_params是FFmpeg中的参数，输出参数供上级函数使用
    // audio_hw_params保存的参数，就是在做重采样的时候要转成的格式。
    audio_hw_params->fmt = AV_SAMPLE_FMT_S16;
    audio_hw_params->freq = wanted_spec.freq;
    audio_hw_params->channels = wanted_spec.channels;

    // --- 处理通道布局 ---
    // 1. 先清理目标布局（防止内存泄漏）
    av_channel_layout_uninit(&audio_hw_params->channel_layout);
    if ((wanted_channel_layout.nb_channels) > 0)
    {
        av_channel_layout_copy(&audio_hw_params->channel_layout, &wanted_channel_layout);;
    } else {
        // 使用 wanted_spec.channels 创建默认布局
        av_channel_layout_default(&audio_hw_params->channel_layout, wanted_spec.channels);
        spdlog::warn("使用 wanted_spec.channels 创建默认布局 : {}", wanted_nb_channels);
    }
    /* audio_hw_params->frame_size这里只是计算一个采样点占用的字节数 */
    audio_hw_params->frame_size = av_samples_get_buffer_size(NULL, audio_hw_params->channels,
                                                             1,
                                                             audio_hw_params->fmt, 1);
    audio_hw_params->bytes_per_sec = av_samples_get_buffer_size(NULL, audio_hw_params->channels,
                                                                audio_hw_params->freq,
                                                                audio_hw_params->fmt, 1);
    if (audio_hw_params->bytes_per_sec <= 0 || audio_hw_params->frame_size <= 0)
    {
        av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size failed\n");
        return -1;
    }
    // 比如2帧数据，一帧就是1024个采样点， 1024*2*2 * 2 = 8192字节
    spdlog::info(
        "[ffplayer] audio device opened driver={} sample_rate={} channels={} samples={} fmt={} hw_buf_size={} frame_size={} bytes_per_sec={}",
        driver ? driver : "unknown",
        wanted_spec.freq,
        static_cast<int>(wanted_spec.channels),
        static_cast<int>(wanted_spec.samples),
        safe_sample_fmt_name(audio_hw_params->fmt),
        static_cast<int>(wanted_spec.size),
        static_cast<int>(audio_hw_params->frame_size),
        static_cast<int>(audio_hw_params->bytes_per_sec));
    return wanted_spec.size; /* SDL内部缓存的数据字节, samples * channels *byte_per_sample */
}

void FFPlayer::audio_close()
{
    SDL_CloseAudio(); // SDL_CloseAudioDevice
}

long FFPlayer::ffp_get_duration_l()
{
    if (!ic)
    {
        return 0;
    }
    int64_t duration = fftime_to_milliseconds(ic->duration);
    if (duration < 0)
    {
        return 0;
    }
    return (long)duration;
}

long FFPlayer::ffp_get_current_position_l()
{
    if (!ic)
    {
        return 0;
    }
    int64_t start_time = ic->start_time; // 起始时间 一般为0
    int64_t start_diff = 0;
    if (start_time > 0 && start_time != AV_NOPTS_VALUE)
    {
        start_diff = fftime_to_milliseconds(start_time); // 返回只需ms这个级别的
    }
    int64_t pos = 0;
    double pos_clock = get_master_clock(); // 获取当前时钟
    if (std::isnan(pos_clock))
    {
        pos = fftime_to_milliseconds(seek_pos);
    }
    else
    {
        pos = pos_clock * 1000; // 转成msg
    }
    if (pos < 0 || pos < start_diff)
    {
        return 0;
    }
    int64_t adjust_pos = pos - start_diff;
    return (long)adjust_pos * pf_playback_rate; // 变速的系数
}

int FFPlayer::get_target_frequency()
{
    return audio_tgt.freq;
}

int FFPlayer::get_target_channels()
{
    return audio_tgt.channels;
}

void FFPlayer::ffp_set_playback_rate(float rate)
{
    pf_playback_rate = rate;
    pf_playback_rate_changed = 1;
}

void FFPlayer::check_play_finish()
{
    if (eof.load() == 1)
    {
        if (audio_stream >= 0 && video_stream >= 0)
        {
            if (audio_no_data.load() == 1 && video_no_data.load() == 1)
            {
                // 发送停止
                ffp_notify_msg1(this, FFP_MSG_PLAY_FNISH);
            }
            return;
        }
        if (audio_stream >= 0)
        {
            if (audio_no_data.load() == 1)
            {
                ffp_notify_msg1(this, FFP_MSG_PLAY_FNISH);
            }
            return;
        }
        if (video_stream >= 0)
        {
            if (video_no_data.load() == 1)
            {
                ffp_notify_msg1(this, FFP_MSG_PLAY_FNISH);
            }
            return;
        }
    }
}

int FFPlayer::stream_has_enough_packets(AVStream *st, int stream_id, PacketQueue *queue)
{
    return stream_id < 0 ||
           queue->abort_request ||
           (st->disposition & AV_DISPOSITION_ATTACHED_PIC) ||
           (queue->nb_packets.load() > MIN_FRAMES &&
            (!queue->duration.load() || av_q2d(st->time_base) * queue->duration.load() > 1.0));
}
static int is_realtime(AVFormatContext *s)
{
    if (!strcmp(s->iformat->name, "rtp") || !strcmp(s->iformat->name, "rtsp") || !strcmp(s->iformat->name, "sdp") || !strcmp(s->iformat->name, "rtmp"))
    {
        return 1;
    }
    if (s->pb && (!strncmp(s->url, "rtp:", 4) || !strncmp(s->url, "udp:", 4)))
    {
        return 1;
    }
    return 0;
}
int FFPlayer::read_thread()
{
    int err, ret;
    int st_index[AVMEDIA_TYPE_NB]; // AVMEDIA_TYPE_VIDEO/ AVMEDIA_TYPE_AUDIO 等，用来保存stream index
    AVPacket pkt1;
    AVPacket *pkt = &pkt1;
    // 初始化为-1,如果一直为-1说明没相应steam
    memset(st_index, -1, sizeof(st_index));
    video_stream = -1;
    audio_stream = -1;
    eof.store(0);
    // 1. 创建上下文结构体，这个结构体是最上层的结构体，表示输入上下文
    ic = avformat_alloc_context();
    if (!ic)
    {
        av_log(NULL, AV_LOG_FATAL, "Could not allocate context.\n");
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    /* 2.打开文件，主要是探测协议类型，如果是网络文件则创建网络链接等 */
    err = avformat_open_input(&ic, input_filename_, nullptr, nullptr);
    if (err < 0)
    {
        print_error(input_filename_, err);
        ret = -1;
        goto fail;
    }
    ffp_notify_msg1(this, FFP_MSG_OPEN_INPUT);
    spdlog::info("[ffplayer] open_input ok url={} format={}", input_filename_, ic->iformat ? ic->iformat->name : "unknown");
    if (seek_by_bytes < 0)
    {
        // 如果该文件的时间戳可能不连续，并且它不是 OGG 格式，就启用“按字节偏移进行 seek”；否则用普通时间戳 seek。
        seek_by_bytes = !!(ic->iformat->flags & AVFMT_TS_DISCONT) && strcmp("ogg", ic->iformat->name);
    }
    err = avformat_find_stream_info(ic, NULL);
    if (err < 0)
    {
        av_log(NULL, AV_LOG_WARNING,
               "%s: could not find codec parameters\n", input_filename_);
        ret = -1;
        goto fail;
    }
    ffp_notify_msg1(this, FFP_MSG_FIND_STREAM_INFO);
    realtime = is_realtime(ic);
    spdlog::info(
        "[ffplayer] stream_info ok streams={} duration_ms={} bit_rate={} realtime={}",
        static_cast<int>(ic->nb_streams),
        ic->duration != AV_NOPTS_VALUE ? fftime_to_milliseconds(ic->duration) : -1,
        static_cast<long long>(ic->bit_rate),
        realtime);
    av_dump_format(ic, 0, input_filename_, 0);
    // 3.利用av_find_best_stream选择流，
    st_index[AVMEDIA_TYPE_VIDEO] =
        av_find_best_stream(ic, AVMEDIA_TYPE_VIDEO,
                            st_index[AVMEDIA_TYPE_VIDEO], -1, NULL, 0);
    st_index[AVMEDIA_TYPE_AUDIO] =
        av_find_best_stream(ic, AVMEDIA_TYPE_AUDIO,
                            st_index[AVMEDIA_TYPE_AUDIO], st_index[AVMEDIA_TYPE_VIDEO],
                            NULL, 0);
    /* 4. 打开视频、音频解码器。在此会打开相应解码器，并创建相应的解码线程。 */
    if (st_index[AVMEDIA_TYPE_AUDIO] >= 0)
    {
        stream_component_open(st_index[AVMEDIA_TYPE_AUDIO]);
    }
    ret = -1;
    if (st_index[AVMEDIA_TYPE_VIDEO] >= 0)
    {
        ret = stream_component_open(st_index[AVMEDIA_TYPE_VIDEO]);
    }
    ffp_notify_msg1(this, FFP_MSG_COMPONENT_OPEN);
    if (video_stream < 0 && audio_stream < 0)
    {
        av_log(NULL, AV_LOG_FATAL, "Failed to open file '%s' or configure filtergraph\n",
               input_filename_);
        ret = -1;
        goto fail;
    }
    ffp_notify_msg1(this, FFP_MSG_PREPARED);
    spdlog::info("[ffplayer] prepared audio_stream={} video_stream={}", audio_stream, video_stream);
    while (1)
    {
        if (abort_request.load())
        {
            break;
        }
        if (seek_req)
        {
            // seek的位置
            int64_t seek_target = seek_pos;
            int64_t seek_min = seek_rel > 0 ? seek_target - seek_rel + 2 : INT64_MIN;
            int64_t seek_max = seek_rel < 0 ? seek_target - seek_rel - 2 : INT64_MAX;
            ret = avformat_seek_file(ic, -1, seek_min, seek_target, seek_max, seek_flags);
            if (ret < 0)
            {
                spdlog::error("[ffplayer] seek failed target_ms={} ret={}", seek_target / 1000, ret);
                av_log(NULL, AV_LOG_ERROR,
                       "%s: error while seeking\n", ic->url); // ic->filename 已经弃用
            }
            else
            {
                if (audio_stream >= 0)
                {
                    packet_queue_flush(&audioq);
                    packet_queue_put(&audioq, &flush_pkt);
                }
                if (video_stream >= 0)
                {
                    packet_queue_flush(&videoq);
                    packet_queue_put(&videoq, &flush_pkt);
                }
                spdlog::info("[ffplayer] seek completed target_ms={} and queues flushed", seek_target / 1000);
            }
            seek_req = 0;
            eof.store(0);
            ffp_notify_msg1(this, FFP_MSG_SEEK_COMPLETE);
        }
        if (infinite_buffer < 1 &&
            (audioq.size.load() + videoq.size.load() > MAX_QUEUE_SIZE || (stream_has_enough_packets(audio_st, audio_stream, &audioq) &&
                                                            stream_has_enough_packets(video_st, video_stream, &videoq))))
        {
            if (!logged_demux_backpressure_)
            {
                logged_demux_backpressure_ = true;
                spdlog::debug(
                    "[ffplayer] demux backpressure audioq_bytes={} videoq_bytes={} audioq_packets={} videoq_packets={}",
                    audioq.size.load(),
                    videoq.size.load(),
                    audioq.nb_packets.load(),
                    videoq.nb_packets.load());
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        logged_demux_backpressure_ = false;
        ret = av_read_frame(ic, pkt);
        if (ret < 0)
        { // 出错或者已经读取完毕了
            if ((ret == AVERROR_EOF || avio_feof(ic->pb)) && !eof.load())
            { // 读取完毕了
                if (video_stream >= 0)
                {
                    packet_queue_put_nullpacket(&videoq, video_stream);
                }
                if (audio_stream >= 0)
                {
                    packet_queue_put_nullpacket(&audioq, audio_stream);
                }
                eof.store(1);
                spdlog::info("[ffplayer] demux reached eof and queued null packets");
            }
            if (ic->pb && ic->pb->error)
            { // io异常 // 退出循环
                spdlog::error("[ffplayer] av_read_frame aborted due to io error={}", ic->pb->error);
                break;
            }
            if (ret != AVERROR_EOF)
            {
                spdlog::debug("[ffplayer] av_read_frame returned ret={} eof={}", ret, eof.load());
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10)); // 读取完数据了，这里可以使用timeout的方式休眠等待下一步的检测
            continue;
        }
        else
        {
            eof.store(0);
        }
        if (pkt->stream_index == audio_stream)
        {
            if (!logged_first_audio_packet_)
            {
                logged_first_audio_packet_ = true;
                spdlog::info("[ffplayer] first audio packet pts={} dts={} size={}", pkt->pts, pkt->dts, pkt->size);
            }
            packet_queue_put(&audioq, pkt);
        }
        else if (pkt->stream_index == video_stream)
        {
            if (!logged_first_video_packet_)
            {
                logged_first_video_packet_ = true;
                spdlog::info("[ffplayer] first video packet pts={} dts={} size={}", pkt->pts, pkt->dts, pkt->size);
            }
            packet_queue_put(&videoq, pkt);
        }
        else
        {
            av_packet_unref(pkt);
        }
    }
    spdlog::info(
        "[ffplayer] read_thread exit abort={} eof={} audioq_packets={} videoq_packets={}",
        abort_request.load(),
        eof.load(),
        audioq.nb_packets.load(),
        videoq.nb_packets.load());
fail:
    return 0;
}
#define REFRESH_RATE 0.01 // 每帧休眠10ms

int FFPlayer::video_refresh_thread()
{
    double remaining_time = 0.0;
    while (!abort_request.load())
    {
        if (remaining_time > 0.0)
        {
            av_usleep((int)(int64_t)(remaining_time * 1000000.0));
        }
        remaining_time = REFRESH_RATE;
        video_refresh(&remaining_time);
    }
    spdlog::debug("[ffplayer] video_refresh_thread exited");
    return 0;
}

void FFPlayer::video_refresh(double *remaining_time)
{
    Frame *vp = nullptr, *lastvp = nullptr;
    //     目前我们先是只有队列里面有视频帧可以播放，就先播放出来
    //     判断有没有视频画面
    if (video_st)
    {
    retry:
        if (frame_queue_nb_remaining(&pictq) == 0)
        {
            video_no_data.store(1); // 没数据可读
            if (eof.load() == 1)
            { // eof在read_thread 来判断是否包读取完毕
                check_play_finish();
            }
        }
        else
        {
            video_no_data.store(0);
            double last_duration, duration, delay;
            lastvp = frame_queue_peek_last(&pictq);
            // 截屏
            screenshot(lastvp->frame);
            //
            vp = frame_queue_peek(&pictq);
            if (vp->serial != videoq.serial)
            {
                frame_queue_next(&pictq);
                goto retry;
            }
            if (lastvp->serial != vp->serial)
            {
                frame_timer = av_gettime_relative() / 1000000;
            }
            if (paused.load())
            {
                goto display;
            }

            last_duration = vp_duration(lastvp, vp);
            delay = compute_target_delay(last_duration);
            double time = av_gettime_relative() / 1000000.0;
            if (time < frame_timer + delay)
            {
                *remaining_time = FFMIN(frame_timer + delay - time, *remaining_time);
                goto display;
            }
            frame_timer += delay;
            if (delay > 0 && time - frame_timer > AV_SYNC_THRESHOLD_MAX)
            {
                frame_timer = time;
            }
            {
                std::lock_guard<std::mutex> lock(pictq.mutex);
                if (!std::isnan(vp->pts))
                {
                    update_video_pts(vp->pts, vp->pos, vp->serial);
                }
            }

            if (frame_queue_nb_remaining(&pictq) > 1)
            {
                Frame *nextvp = frame_queue_peek_next(&pictq);
                duration = vp_duration(vp, nextvp);
                if (!step && (framedrop > 0 || (framedrop && get_master_sync_type() != AV_SYNC_VIDEO_MASTER)) && time > frame_timer + duration)
                { // time 实际时间  frame_timer + duration  理想下一帧播放时间
                    frame_drops_late.fetch_add(1);
                    frame_queue_next(&pictq);
                    goto retry;
                }
            }
            //            LOG(INFO) << "FFPlayer::video_refresh vp->pts" << vp->pts
            //                      << ", audclk->pts " << audclk.pts;
            frame_queue_next(&pictq);
            force_refresh.store(1);
        }
    display:
        if (force_refresh.load() && pictq.rindex_shown)
        {
            if (vp)
            {
                if (video_refresh_callback_)
                {
                    video_refresh_callback_(vp);
                }
            }
        }
    }
    force_refresh.store(0);
}

double FFPlayer::vp_duration(Frame *vp, Frame *nextvp)
{
    if (vp->serial == nextvp->serial)
    {
        double duration = nextvp->pts - vp->pts;
        if (std::isnan(duration) || duration <= 0 || duration > max_frame_duration)
        {
            return vp->duration / pf_playback_rate;
        }
        else
        {
            return duration / pf_playback_rate;
        }
    }
    else
    {
        return 0.0;
    }
}

double FFPlayer::compute_target_delay(double delay)
{
    double sync_threshold, diff = 0;
    if (get_master_sync_type() != AV_SYNC_VIDEO_MASTER)
    {
        diff = get_clock(&vidclk) - get_master_clock();
        //  为什么要选delay   判断延迟  是否大于一帧； 取较小者  防止同步阈值过大
        sync_threshold = FFMAX(AV_SYNC_THRESHOLD_MIN, FFMIN(AV_SYNC_THRESHOLD_MAX, delay));
        if (!std::isnan(diff) && fabs(diff) < max_frame_duration)
        {
            // 视频太快  帧间隔足够长 就放慢视频速度
            if (diff <= -sync_threshold)
            {
                delay = FFMAX(0, delay + diff);
                // 帧间隔比较短  不够帧间隔最小阈值  就直接翻倍处理
            }
            else if (diff >= sync_threshold && delay > AV_SYNC_FRAMEDUP_THRESHOLD)
            {
                delay += diff;
            }
            else if (diff >= sync_threshold)
            {
                delay *= 2;
            }
        }
    }
    return delay;
}

void FFPlayer::update_video_pts(double pts, int64_t pos, int serial)
{
    set_clock(&vidclk, pts / pf_playback_rate, serial);
}

void FFPlayer::AddVideoRefreshCallback(std::function<int(const Frame *)> callback)
{
    video_refresh_callback_ = callback;
}

int FFPlayer::get_master_sync_type()
{
    if (av_sync_type == AV_SYNC_VIDEO_MASTER)
    {
        if (video_st)
        {
            return AV_SYNC_VIDEO_MASTER;
        }
        else
        {
            return AV_SYNC_AUDIO_MASTER; /* 如果没有视频成分则使用 audio master */
        }
    }
    else if (av_sync_type == AV_SYNC_AUDIO_MASTER)
    {
        if (audio_st)
        {
            return AV_SYNC_AUDIO_MASTER;
        }
        else if (video_st)
        {
            return AV_SYNC_VIDEO_MASTER;
        }
        else
        {
            return AV_SYNC_UNKNOW_MASTER;
        }
    }
    else
    {
        return AV_SYNC_AUDIO_MASTER;
    }
}

double FFPlayer::get_master_clock()
{
    double val;
    switch (get_master_sync_type())
    {
    case AV_SYNC_AUDIO_MASTER:
        val = get_clock(&audclk);
        break;
    case AV_SYNC_VIDEO_MASTER:
        val = get_clock(&vidclk);
        break;
    default:
        val = get_clock(&audclk);
        break;
    }
    return val;
}

int FFPlayer::ffp_screenshot_l(char *screen_path)
{
    // 存在视频的情况下才能截屏
    if (video_st && !req_screenshot_)
    {
        if (screen_path_)
        {
            free(screen_path_);
            screen_path_ = NULL;
        }
        screen_path_ = strdup(screen_path);
        req_screenshot_ = true;
    }
    return 0;
}

void FFPlayer::screenshot(AVFrame *frame)
{
    // if(req_screenshot_){
    //     ScreenShot shot;
    //     int ret = -1;
    //     if(frame){
    //         ret = shot.SaveJpeg(frame, screen_path_, 70);
    //     }
    //     //如果正常则ret = 0; 异常则为 < 0
    //     ffp_notify_msg4(this, FFP_MSG_SCREENSHOT_COMPLETE, ret, 0, screen_path_, strlen(screen_path_) + 1);
    //     // 截屏完毕后允许再次截屏
    //     req_screenshot_ =  false;
    // }
}

float FFPlayer::ffp_get_playback_rate()
{
    return pf_playback_rate;
}

bool FFPlayer::is_normal_playback_rate()
{
    if (pf_playback_rate > 0.99 && pf_playback_rate < 1.01)
    {
        return true;
    }
    else
        return false;
}

int FFPlayer::ffp_get_playback_rate_change()
{
    return pf_playback_rate_changed;
}

void FFPlayer::ffp_set_playback_rate_change(int change)
{
    pf_playback_rate_changed = change;
}

void FFPlayer::ffp_set_playback_volume(int value)
{
    value = av_clip(value, 0, 100); // 将value 的值限制在 0 到 100 的范围内
    value = av_clip(SDL_MIX_MAXVOLUME * value / 100, 0, SDL_MIX_MAXVOLUME);
    audio_volume.store(value);
    // spdlog::info("audio_volume: {}", audio_volume.load());
}

int FFPlayer::ffp_pause_l() // 暂停的请求
{
    toggle_pause(1);
    return 0;
}

void FFPlayer::toggle_pause(int pause_on)
{
    toggle_pause_l(pause_on);
}

void FFPlayer::toggle_pause_l(int pause_on)
{
    if (pause_req.load() && !pause_on)
    { // 播放时候改时间戳 这里的是暂停时候的pts
        set_clock(&vidclk, get_clock(&vidclk), vidclk.serial);
        set_clock(&audclk, get_clock(&audclk), audclk.serial);
    }
    pause_req.store(pause_on);
    auto_resume.store(!pause_on);
    stream_update_pause_l();
    step.store(0);
}

void FFPlayer::stream_update_pause_l()
{
    if (!step.load() && (pause_req.load() || buffering_on.load()))
    {
        stream_toggle_pause_l(1);
    }
    else
    {
        stream_toggle_pause_l(0);
    }
}

void FFPlayer::stream_toggle_pause_l(int pause_on)
{
    if (paused && !pause_on)
    {                                                                           // 播放时候设置时间戳
        frame_timer += av_gettime_relative() / 1000000.0 - vidclk.last_updated; // 当前time - 上一次pts  就是暂停到恢复的duration
        set_clock(&vidclk, get_clock(&vidclk), vidclk.serial);
        set_clock(&audclk, get_clock(&audclk), audclk.serial);
    }
    else
    {
    }
    if (step.load() && (pause_req.load() || buffering_on.load()))
    {
        paused.store(pause_on);
        vidclk.paused = pause_on;
    }
    else
    {
        paused.store(pause_on);
        audclk.paused = pause_on;
        vidclk.paused = pause_on;
        //        SDL_AoutPauseAudio(ffp->aout, pause_on);
    }
}

int FFPlayer::ffp_seek_to_l(long msec)
{
    int64_t start_time = 0;
    int64_t seep_pos = milliseconds_to_fftime(msec); // 转成微秒
    int64_t duration = milliseconds_to_fftime(ffp_get_duration_l());
    if (duration > 0 && seep_pos >= duration)
    {
        ffp_notify_msg1(this, FFP_MSG_SEEK_COMPLETE);
        return 0;
    }
    spdlog::info("[ffplayer] seek request target_ms={} duration_ms={}", seep_pos / 1000, duration / 1000);
    stream_seek(seep_pos, 0, 0);
    return 1;
}

void FFPlayer::stream_seek(int64_t pos, int64_t rel, int seek_by_bytes)
{
    if (!seek_req)
    {
        seek_pos = pos;
        seek_rel = rel;
        seek_flags &= ~AVSEEK_FLAG_BYTE;
        if (seek_by_bytes)
        {
            seek_flags |= AVSEEK_FLAG_BYTE;
        }
        seek_req = 1;
        //        SDL_CondSignal( continue_read_thread);
    }
}

FFPlayer::FFPlayer()
{
}

Decoder::Decoder() : pkt_{}//在每次重新使用 pkt_ 前调用 av_packet_unref(&pkt_)
{
}

Decoder::~Decoder()
{
}

void Decoder::decoder_init(AVCodecContext *avctx, PacketQueue *queue)
{
    avctx_ = avctx;
    queue_ = queue;
    pkt_serial_ = -1;
    finished_ = 0;
    packet_pending_ = 0;
    start_pts = AV_NOPTS_VALUE;
    start_pts_tb = AVRational{0, 1};
    next_pts = AV_NOPTS_VALUE;
    next_pts_tb = AVRational{0, 1};
    av_packet_unref(&pkt_);
}

int Decoder::decoder_start(AVMediaType codec_type, const char *thread_name, void *arg)
{
    // 启用包队列
    packet_queue_start(queue_);
    // 创建线程
    if (AVMEDIA_TYPE_VIDEO == codec_type)
    {
        decoder_thread_ = new std::thread(&Decoder::video_thread, this, arg);
    }
    else if (AVMEDIA_TYPE_AUDIO == codec_type)
    {
        decoder_thread_ = new std::thread(&Decoder::audio_thread, this, arg);
    }
    else
    {
        return -1;
    }
    return 0;
}

void Decoder::decoder_abort(FrameQueue *fq)
{
    // spdlog::info("decoder_abort start");
    packet_queue_abort(queue_); // 请求退出包队列
    frame_queue_signal(fq);     // 唤醒阻塞的帧队列
    if (decoder_thread_ && decoder_thread_->joinable())
    {
        decoder_thread_->join(); // 等待解码线程退出
        delete decoder_thread_;
        decoder_thread_ = NULL;
    }
    packet_queue_flush(queue_); // 情况packet队列，并释放数据
}

void Decoder::decoder_destroy()
{
    av_packet_unref(&pkt_);
    avcodec_free_context(&avctx_);
}
// 返回值-1: 请求退出
//       0: 解码已经结束了，不再有数据可以读取
//       1: 获取到解码后的frame
int Decoder::decoder_decode_frame(AVFrame *frame)
{
    int ret = AVERROR(EAGAIN);
    for (;;)
    {
        AVPacket pkt;
        // 1. 流连续情况下获取解码后的帧
        if (queue_->serial == pkt_serial_)
        {
            do
            {
                if (queue_->abort_request)
                {
                    return -1;
                }
                switch (avctx_->codec_type)
                {
                case AVMEDIA_TYPE_VIDEO:
                    ret = avcodec_receive_frame(avctx_, frame);
                    if (ret >= 0)
                    {
                        if (decoder_reorder_pts == -1)
                        {
                            frame->pts = frame->best_effort_timestamp;
                        }
                        else if (!decoder_reorder_pts)
                        {
                            frame->pts = frame->pkt_dts;
                        }
                        //                        LOG(INFO) << "video frame pts:" <<  frame->pts << ", dts:" << frame->pkt_dts;
                    }
                    break;
                case AVMEDIA_TYPE_AUDIO:
                    ret = avcodec_receive_frame(avctx_, frame);
                    if (ret >= 0)
                    {
                        normalize_audio_frame_metadata(avctx_, frame);
                        AVRational tb = {1, frame->sample_rate > 0 ? frame->sample_rate : 1};
                        if (frame->pts != AV_NOPTS_VALUE)
                        {
                            // 如果frame->pts正常则先将其从pkt_timebase转成{1, frame->sample_rate}
                            // pkt_timebase实质就是stream->time_base
                            frame->pts = av_rescale_q(frame->pts, avctx_->pkt_timebase, tb);
                        }
                        else if (next_pts != AV_NOPTS_VALUE)
                        {
                            // 如果frame->pts不正常则使用上一帧更新的next_pts和next_pts_tb
                            // 转成{1, frame->sample_rate}
                            frame->pts = av_rescale_q(next_pts, next_pts_tb, tb);
                        }
                        if (frame->pts != AV_NOPTS_VALUE)
                        {
                            next_pts = frame->pts + frame->nb_samples;
                            next_pts_tb = tb;
                        }
                    }
                    break;
                }
                // 1.3. 检查解码是否已经结束，解码结束返回0
                if (ret == AVERROR_EOF)
                {
                    finished_ = pkt_serial_;
                    spdlog::debug("[ffplayer] decoder reached eof pkt_serial={}", pkt_serial_);
                    avcodec_flush_buffers(avctx_);
                    return 0;
                }
                // 1.4. 正常解码返回1
                if (ret >= 0)
                {
                    return 1;
                }
            } while (ret != AVERROR(EAGAIN)); // 1.5 没帧可读时ret返回EAGIN，需要继续送packet
        }
        // 2 获取一个packet，如果播放序列不一致(数据不连续)则过滤掉“过时”的packet
        do
        {
            if (packet_pending_)
            {
                av_packet_move_ref(&pkt, &pkt_);
                packet_pending_ = 0;
            }
            else
            {
                if (packet_queue_get(queue_, &pkt, 1, &pkt_serial_) < 0)
                {
                    return -1;
                }
            }
            if (queue_->serial != pkt_serial_)
            {
                spdlog::debug("[ffplayer] discard packet from old serial queue_serial={} pkt_serial={}", queue_->serial, pkt_serial_);
                av_packet_unref(&pkt); // fixed me? 释放要过滤的packet
            }
        } while (queue_->serial != pkt_serial_);
        // 3 将packet送入解码器
        if (pkt.data == flush_pkt.data)
        { // when seeking or when switching to a different stream
            avcodec_flush_buffers(avctx_);
            finished_ = 0;
            next_pts = start_pts;
            next_pts_tb = start_pts_tb;
        }
        else
        {
            if (avctx_->codec_type == AVMEDIA_TYPE_SUBTITLE)
            {
                //                int got_frame = 0;
                //                ret = avcodec_decode_subtitle2(avctx_, sub, &got_frame, &pkt);
                //                if (ret < 0) {
                //                    ret = AVERROR(EAGAIN);
                //                } else {
                //                    if (got_frame && !pkt.data) {
                //                        packet_pending = 1;
                //                        av_packet_move_ref(&pkt, &pkt);
                //                    }
                //                    ret = got_frame ? 0 : (pkt.data ? AVERROR(EAGAIN) : AVERROR_EOF);
                //                }
            }
            else
            {
                if (avcodec_send_packet(avctx_, &pkt) == AVERROR(EAGAIN))
                {
                    //                    av_log(avctx, AV_LOG_ERROR, "Receive_frame and send_packet both returned EAGAIN, which is an API violation.\n");
                    packet_pending_ = 1;
                    av_packet_move_ref(&pkt_, &pkt);
                }
            }
            av_packet_unref(&pkt); // 一定要去释放音视频数据
        }
    }
}

int Decoder::get_video_frame(AVFrame *frame)
{
    int got_picture;
    if ((got_picture = decoder_decode_frame(frame)) < 0)
    {
        return -1;
    }
    if (got_picture)
    {
        // 2. 分析获取到的该帧是否要drop掉, 该机制的目的是在放入帧队列前先drop掉过时的视频帧
        //        frame->sample_aspect_ratio = av_guess_sample_aspect_ratio(ic, video_st, frame);
    }
    return got_picture;
}

int Decoder::queue_picture(FrameQueue *fq, AVFrame *src_frame, double pts, double duration, int64_t pos, int serial)
{
    Frame *vp;
    if (!(vp = frame_queue_peek_writable(fq)))
    { // 检测队列是否有可写空间
        // 队列没满 才有可写入的帧   队列满了 就没有可写入的帧了 就会陷入等待 这个步骤就是来看 队列是否还有能写入新帧的空间
        return -1; // 请求退出则返回-1
    }
    // 执行到这步说已经获取到了可写入的Frame
    //    vp->sar = src_frame->sample_aspect_ratio;
    //    vp->uploaded = 0;
    vp->width = src_frame->width;
    vp->height = src_frame->height;
    vp->format = src_frame->format;
    vp->pts = pts;
    vp->duration = duration;
    vp->pos = pos;
    vp->serial = serial;                     // 设置serial
    av_frame_move_ref(vp->frame, src_frame); // 将src中所有数据转移到dst中，并复位src。
                                             //    std::cout << "Decoder::queue_picture " << "vp->pts:" << vp->pts << std::endl;
    frame_queue_push(fq);                    // 更新写索引位置
    return 0;
}

int Decoder::audio_thread(void *arg)
{
    spdlog::debug("[ffplayer] audio decoder thread started");
    FFPlayer *is = (FFPlayer *)arg;
    AVFrame *frame = av_frame_alloc(); // 分配解码帧
    Frame *af;
    int got_frame = 0; // 判断是否读取到帧  并不是真正读取了一帧
    AVRational tb;
    int ret = 0;
    if (!frame)
    {
        return AVERROR(ENOMEM);
    }
    do
    {
        // 获取缓存情况

        // 1.从解码器读取解码帧
        if ((got_frame = decoder_decode_frame(frame)) < 0)
        {
            goto the_end;
        }
        if (got_frame)
        {
            if (!normalize_audio_frame_metadata(is->auddec.avctx_, frame) ||
                frame->nb_samples <= 0 ||
                !frame->extended_data ||
                !frame->data[0])
            {
                spdlog::warn("drop decoded audio frame: nb_samples={}, format={}, sample_rate={}, channels={}, data0={}",
                             static_cast<int>(frame->nb_samples),
                             static_cast<int>(frame->format),
                             static_cast<int>(frame->sample_rate),
                             static_cast<int>(frame->ch_layout.nb_channels),
                             static_cast<const void *>(frame->data[0]));
                av_frame_unref(frame);
                continue;
            }
            tb.num = 1;
            tb.den = frame->sample_rate;
            //从音频帧队列获取一个可写的Frame af
            if (!(af = frame_queue_peek_writable(&is->sampq)))
            {
                goto the_end;
            }
            // 3. 设置Frame并放入FrameQueue
            af->pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb); // 转换时间戳
            af->pos = frame->pkt_pos;
            af->serial = is->auddec.pkt_serial_;
            AVRational temp_a;
            temp_a.num = frame->nb_samples;
            temp_a.den = frame->sample_rate;
            af->duration = av_q2d(temp_a);
            av_frame_move_ref(af->frame, frame);
            frame_queue_push(&is->sampq);
        }
    } while (ret >= 0 || ret == AVERROR(EAGAIN) || ret == AVERROR_EOF);
the_end:
    spdlog::debug("[ffplayer] audio decoder thread exited ret={}", ret);
    av_frame_free(&frame);
    return ret;
}

int Decoder::video_thread(void *arg)
{
    spdlog::debug("[ffplayer] video decoder thread started");
    FFPlayer *is = (FFPlayer *)arg;
    AVFrame *frame = av_frame_alloc(); // 分配解码帧
    double pts, duration;
    int ret;
    AVRational tb = is->video_st->time_base;
    AVRational frame_rate = av_guess_frame_rate(is->ic, is->video_st, NULL);
    if (!frame)
    {
        return AVERROR(ENOMEM);
    }
    for (;;)
    { // 循环取出视频解码的帧数据
        // 统计视频packet缓存 待写

        // 获取解码后的视频帧
        ret = get_video_frame(frame);
        //        std::cout << "ret = " << ret << std::endl;
        if (ret < 0)
        {
            goto the_end;
        }
        if (!ret)
        { // ret == 0  解码正常结束
            continue;
        }
        //        LOG(INFO) << avctx_->codec->name << " packet size: " << queue_->size << " frame size: " << is->pictq.size << ", pts: " << frame->pts ;
        AVRational temp_a;
        temp_a.num = frame_rate.den;
        temp_a.den = frame_rate.num;
        duration = (frame_rate.num && frame_rate.den ? av_q2d(temp_a) : 0);
        //        duration = (frame_rate.num && frame_rate.den ? av_q2d((AVRational) {
        //            frame_rate.den, frame_rate.num
        //        }) : 0);
        pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb);
        // 5 将解码后的视频帧插入队列
        ret = queue_picture(&is->pictq, frame, pts, duration, frame->pkt_pos, is->viddec.pkt_serial_);
        // 6 释放frame对应的数据
        av_frame_unref(frame); // frame 指针本身 仍然有效，可以再次被复用
        if (ret < 0)
        {
            goto the_end;
        }
    }
the_end:
    spdlog::debug("[ffplayer] video decoder thread exited ret={}", ret);
    av_frame_free(&frame); // frame 指针失效 为NULL
    return 0;
}
