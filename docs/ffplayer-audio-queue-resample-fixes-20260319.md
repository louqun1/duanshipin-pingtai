# FFPlayer 音频队列与重采样修复说明

## 背景

`FFplayer.cpp` 在正常播放、未暂停、未 seek 的情况下出现了以下异常现象：

- 理论上只应创建一次音频重采样器，但实际会重复重采样多次
- 多次重采样中有几次输入参数明显无效
- 加一条 `spdlog::info(...)` 后，现象会变化，说明问题具有明显时序敏感性

典型日志如下：

```text
audio.nb_samples=0, channels=0, channel_layout=0 channels, format=-1, sample_rate=0
audio frame data pointers: data[0]=0x0, data[1]=0x0
Invalid input sample format: -1
Invalid input sample rate: 0
swr_init failed: Invalid argument
```

## 根因分析

### 1. 音频帧队列可读条件错误

`frame_queue_peek_readable()` 原先只判断：

```cpp
f->size > 0
```

但 `sampq` 使用了 `keep_last = 1`。  
在这种模式下，真正可读的帧数应该是：

```cpp
f->size - f->rindex_shown
```

否则会把“上一帧已经显示过，但下一帧其实还没写入”的空槽位提前当作可读帧返回。  
这正是日志里出现：

- `nb_samples = 0`
- `format = -1`
- `sample_rate = 0`
- `data[0] = nullptr`

的直接原因。

### 2. 音频帧元数据缺乏兜底

解码后的 `AVFrame` 如果缺失：

- `format`
- `sample_rate`
- `ch_layout`

原逻辑会直接继续向下走，把坏值喂给 `swr_alloc_set_opts2()` 和 `swr_init()`。  
一旦取到空帧或元数据不完整帧，就会出现 `Invalid argument`。

### 3. 音频参数结构体复制方式不安全

`audio_src = audio_tgt` 会对 `AVChannelLayout` 做浅拷贝。  
这类结构体应显式 `uninit/copy`，否则容易埋下布局状态不一致的问题。

### 4. 若干高频共享标志存在时序敏感读写

这批问题虽然不一定每次都直接触发崩溃，但会放大“加日志后行为改变”的概率。  
本次主要把以下共享状态收敛为原子读写：

- `abort_request`
- `eof`
- `audio_no_data`
- `video_no_data`
- `paused`
- `force_refresh`
- `audio_volume`
- `audio_callback_time`
- `step`
- `framedrop`
- `frame_drops_late`
- `pause_req`
- `auto_resume`
- `buffering_on`

同时把 `PacketQueue` 的统计字段改为原子类型：

- `nb_packets`
- `size`
- `duration`

## 本次修复内容

### 队列修复

修改 `backend/playerEngine/ff_ffplay_def.cpp`：

- `frame_queue_peek_readable()` 改为判断 `(f->size - f->rindex_shown) > 0`
- `PacketQueue` 的统计字段改为原子更新，避免日志和判断读到撕裂值

### 音频元数据修复

修改 `backend/playerEngine/FFplayer.cpp`：

- 新增 `normalize_audio_frame_metadata()`，当 `AVFrame` 元数据缺失时，使用 `AVCodecContext` 做兜底
- 新增 `describe_audio_layout()`，避免无效 layout 描述时输出混乱
- 在 `Decoder::decoder_decode_frame()` 的音频分支里，先补齐音频元数据，再处理时间戳
- 在 `Decoder::audio_thread()` 里，发现无效音频帧直接丢弃，不再入 `sampq`
- 在 `audio_decode_frame()` 里，对从 `sampq` 取出的帧再次做校验；空帧、坏帧直接跳过

### 重采样器修复

修改 `backend/playerEngine/FFplayer.cpp`：

- 只有在输入参数真的变化、或者 `swr_ctx` 尚未创建时，才重新创建 `swr`
- `audio_src.channel_layout` 改为显式 `av_channel_layout_copy()`
- 新增 `frame_size/bytes_per_sec` 的同步更新
- `dec_channel_layout` 在函数退出前显式 `uninit`

### 共享状态修复

修改 `backend/playerEngine/FFplayer.hpp` 与 `backend/playerEngine/FFplayer.cpp`：

- 将多处高频共享标志改成 `std::atomic`
- 对应调用点改为 `load()/store()/fetch_add()`
- SDL 音频回调中增加了 `frame_size == 0` 的保护，避免错误路径下除零

## 为什么“加一条 spdlog 就变正常一些”

因为原问题本质上是时序敏感的。

在未修复前：

1. 音频消费者线程会过早从 `sampq` 读取“尚未写满的槽位”
2. 这个槽位里的 `AVFrame` 看起来就是空帧
3. 加日志后，线程切换顺序被轻微改变
4. 生产者更早写入下一帧，消费者偶尔就碰巧拿到了正确帧

所以日志不是修复，只是改变了复现概率。

## 验证结果

本次修改后已完成以下构建验证：

- `ServoBackend`
- `QtFrontend`

构建通过时间：2026-03-19

## 建议的回归观察点

建议重新关注以下日志是否仍出现：

- `audio.nb_samples=0`
- `format=-1`
- `sample_rate=0`
- `data[0]=0x0`
- `swr_init failed: Invalid argument`

如果这些仍出现，再继续排查媒体源本身、解码器输出、以及 seek/flush 场景。
