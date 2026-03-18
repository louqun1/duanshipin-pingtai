# PlayerEngine Refactor Roadmap

## 1. 这份文档解决什么问题

这份文档只聚焦一件事：

如何把现在的 `playerEngine` 从“能跑但职责缠在一起”，一步一步重构成“边界清楚、可以继续扩展”的播放器内核。

这份文档默认你前端架构已经搭好，`HomePage -> MainWindow -> VideoPlayerWindow -> PlayerController` 这条链路不再是本次重构重点。  
这次只处理下面这些文件对应的播放器内核层：

- `backend/playerEngine/FFplayer.hpp`
- `backend/playerEngine/FFplayer.cpp`
- `backend/playerEngine/IjkMediaPlayer.hpp`
- `backend/playerEngine/IjkMediaPlayer.cpp`
- `backend/playerEngine/FFMessage.hpp`
- `backend/playerEngine/FFMessageQueue.hpp`

---

## 2. 先说结论：你现在最该怎么想

你现在的核心问题，不是“FFmpeg 代码太多”，而是“播放器内核缺少稳定的中间层”。

目前大概是这样：

- `FFPlayer`
  - 同时负责解复用、解码、音频输出、视频刷新、时钟同步、倍速、截图、消息通知、线程生命周期
- `IjkMediaPlayer`
  - 已经开始做包装层，但还没有完全成为真正的播放器会话层
- `PlayerController`
  - 前端控制器已经搭好了，但还没有建立在一个稳定的 engine contract 之上

所以这次重构的核心目标不是“立刻把 `FFPlayer` 拆成很多 cpp”，而是先建立清楚的层次：

- `PlayerController`
  - 只处理高层命令和高层状态
- `IjkMediaPlayer`
  - 只处理一次播放会话的生命周期、底层消息翻译、状态维护
- `FFPlayer`
  - 只处理底层播放内核能力

你要先把边界理顺，再拆内部。

---

## 3. 最终想要的结构

建议你把播放器链路收敛成下面这样：

`UI -> PlayerController -> IjkMediaPlayer -> FFPlayer`

每层职责固定如下。

### 3.1 PlayerController 负责什么

- 接收 UI 命令
- 维护应用层播放状态
- 订阅播放器高层事件
- 把高层事件转成 UI 能理解的信号

它不应该直接知道：

- `AVPacket`
- `AVFrame`
- `SDL`
- `FFP_MSG_*`
- 解码线程和时钟

### 3.2 IjkMediaPlayer 负责什么

这一层不要再把它仅仅理解成“模仿 Android 名字的壳”。

在你这个项目里，它最适合承担的是：

- 一次播放会话的入口
- 调用 `FFPlayer`
- 跑 message loop
- 接收底层消息
- 把底层消息翻译成高层事件
- 维护底层播放器状态

它就是你现在最需要的“中间层”。

### 3.3 FFPlayer 负责什么

`FFPlayer` 只负责底层内核：

- 打开媒体
- read thread
- packet queue / frame queue
- audio decode / video decode
- audio output
- video refresh
- AV sync
- screenshot / rate / volume 这些底层能力

它不应该直接承担应用层语义，比如：

- “当前窗口提示文案是什么”
- “UI 是不是在 Loading”
- “播放按钮该显示 Pause 还是 Resume”

---

## 4. 这次重构一定要守住的 6 条原则

### 原则 1：UI 不直接消费 `FFP_MSG_*`

`FFP_MSG_PREPARED`、`FFP_MSG_SEEK_COMPLETE`、`FFP_MSG_PLAY_FNISH` 这些都属于内核消息，不应该直接暴露给 UI。

UI 只应该看到高层事件，比如：

- `Prepared`
- `Playing`
- `Paused`
- `SeekCompleted`
- `PlaybackFinished`
- `ErrorOccurred`

### 原则 2：PlayerController 不直接碰 FFmpeg/SDL 类型

如果某段代码里同时出现：

- `PlaybackState`
- `AVFrame`
- `QWidget`
- `FFP_MSG_*`

那这个边界大概率已经乱了。

### 原则 3：命令和事件要分开理解

命令是上层发给播放器的：

- `Open`
- `Play`
- `Pause`
- `Stop`
- `Seek`
- `SetVolume`
- `SetRate`

事件是播放器回给上层的：

- `Prepared`
- `FirstFrameReady`
- `Playing`
- `Paused`
- `BufferingStarted`
- `SeekCompleted`
- `PlaybackFinished`
- `ErrorOccurred`

不要在脑子里把这两类东西混成一个概念。

### 原则 4：状态权威最多只能有两层

建议只保留：

- 底层播放器状态：`IjkMediaPlayer`
- 应用层展示状态：`PlayerController`

不要让 `FFPlayer` 再变成第三套“面向业务的状态机”。

### 原则 5：先稳接口，再拆内部

如果接口还在变，就不要急着拆很多底层模块。  
先把 `IjkMediaPlayer <-> PlayerController` 的契约收紧。

### 原则 6：每一步都只改一个维度

不要一口气同时做：

- 状态机重写
- 消息循环迁移
- `FFPlayer` 大拆分
- 渲染方式重做

这样最容易把自己绕进去。

---

## 5. 先定义清楚：这次重构的阶段顺序

这次最推荐的顺序是：

1. 先收口 `IjkMediaPlayer` 的职责
2. 再统一“命令 / 事件 / 状态”的边界
3. 再把 `PlayerController` 改成基于真实事件驱动
4. 最后才拆 `FFPlayer`

顺序不要反。

---

## 6. 第一阶段：先把 IjkMediaPlayer 变成真正的中间层

### 6.1 这一阶段的目标

让 `IjkMediaPlayer` 成为 `playerEngine` 的唯一公开入口。

意思是：

- `PlayerController` 以后只和 `IjkMediaPlayer` 打交道
- `PlayerController` 不直接知道 `FFPlayer`
- `FFPlayer` 只被 `IjkMediaPlayer` 持有

### 6.2 这一阶段你应该做什么

你先不要拆 `FFPlayer`，先只做这几件事：

1. 梳理 `IjkMediaPlayer` 对外 API
2. 明确哪些是公开命令
3. 明确哪些是内部辅助方法
4. 明确 message loop 的拥有者就是 `IjkMediaPlayer`

建议 `IjkMediaPlayer` 对外只保留这一类接口：

- `create`
- `destroy`
- `setDataSource`
- `prepareAsync`
- `start`
- `pause`
- `stop`
- `seekTo`
- `setVolume`
- `setRate`
- `getCurrentPosition`
- `getDuration`
- `setEventCallback`
- `setVideoFrameCallback`

这里最重要的变化不是名字，而是职责含义：

- 对外是“播放器能力”
- 对内才是 `FFPlayer` 的细节

### 6.3 这一阶段不要做什么

- 不要改 UI
- 不要先改 `VideoPlayerWindow`
- 不要先碰 `QWidget`
- 不要先拆 `FFPlayer` 的 read/audio/video 逻辑

### 6.4 完成标准

做到下面这几条，就算第一阶段完成：

- 上层不直接依赖 `FFPlayer`
- `IjkMediaPlayer` 成为唯一 engine facade
- 你能用一句话讲清楚每个公开接口的职责

---

## 7. 第二阶段：把“命令”和“事件”真正分开

### 7.1 你现在的问题

你现在的 `IjkMediaPlayer.cpp` 已经开始把：

- `FFP_REQ_*`
- `FFP_MSG_*`

都放到同一个理解空间里处理了。

这会导致一个长期问题：

你很难区分现在是在“向播放器下命令”，还是“播放器正在回报状态”。

### 7.2 正确的脑图

你应该在 `playerEngine` 里建立两套清晰概念。

第一套：命令

- `Open`
- `Prepare`
- `Play`
- `Pause`
- `Stop`
- `Seek`
- `SetVolume`
- `SetRate`
- `Screenshot`

第二套：事件

- `OpenInputStarted`
- `Prepared`
- `FirstFrameReady`
- `Playing`
- `Paused`
- `Stopped`
- `SeekCompleted`
- `PlaybackFinished`
- `BufferingStarted`
- `BufferingEnded`
- `ErrorOccurred`
- `ScreenshotCompleted`

### 7.3 这一阶段你应该怎么做

建议在 `IjkMediaPlayer` 层做一层“翻译表”：

- 从 `FFP_MSG_*`
- 翻译成高层 `PlayerEvent`

比如：

- `FFP_MSG_PREPARED` -> `Prepared`
- `FFP_MSG_PLAYBACK_STATE_CHANGED` -> `StateChanged`
- `FFP_MSG_SEEK_COMPLETE` -> `SeekCompleted`
- `FFP_MSG_PLAY_FNISH` -> `PlaybackFinished`
- `FFP_MSG_ERROR` -> `ErrorOccurred`

这里最关键的点是：

`PlayerController` 以后应该订阅的是 `PlayerEvent`，不是 `FFP_MSG_*`。

### 7.4 完成标准

做到下面这几条，就算第二阶段完成：

- 你可以列出完整命令表
- 你可以列出完整事件表
- `FFP_MSG_*` 不再直接冒到控制器/UI 层

---

## 8. 第三阶段：把状态机固定下来

### 8.1 为什么必须先有状态机

播放器最容易乱，不是因为线程多，而是因为没有明确状态机。

没有状态机时，代码会慢慢变成：

- 这里 if 一下
- 那里再补一个标记位
- seek 之后再加一个特判
- pause 和 buffering 再互相覆盖

最后就会越来越难维护。

### 8.2 建议你保留的最小状态集

建议应用层最少保留这些状态：

- `Idle`
- `Opening`
- `Prepared`
- `Playing`
- `Paused`
- `Seeking`
- `Buffering`
- `Stopped`
- `Completed`
- `Error`

### 8.3 谁维护哪一层状态

建议这样分：

- `IjkMediaPlayer`
  - 维护底层播放运行态
- `PlayerController`
  - 维护对 UI 暴露的应用层状态

### 8.4 你应该先画出这张表

在动代码之前，先把这类转移表写清楚：

- `OpenMedia`: `Idle -> Opening`
- 收到 `Prepared`: `Opening -> Prepared`
- 收到 `Play`: `Prepared/Paused/Completed -> Playing`
- 收到 `Pause`: `Playing -> Paused`
- 收到 `Seek`: `Playing/Paused -> Seeking`
- 收到 `SeekCompleted`: `Seeking -> Paused` 或 `Playing`
- 收到 `PlaybackFinished`: `Playing -> Completed`
- 收到 `ErrorOccurred`: `Any -> Error`

这里不要追求一开始就完美，先追求“任何状态变化都能解释得通”。

### 8.5 完成标准

- 每个命令进入时，都知道它在当前状态下是否合法
- 每个事件回来时，都知道它应该把状态推进到哪里

---

## 9. 第四阶段：把 message loop 放回播放器会话内部

### 9.1 你现在应该怎么理解 message loop

message loop 不是 UI 的一部分。  
它属于播放器会话本身。

也就是说，未来应该是：

- `IjkMediaPlayer` 自己跑消息循环
- 自己读取 `FFPlayer` 的消息
- 自己做翻译
- 再把高层事件抛给上层

### 9.2 这一阶段应该怎么做

你要把 message loop 理顺成下面这样：

1. 上层发命令给 `IjkMediaPlayer`
2. `IjkMediaPlayer` 调 `FFPlayer`
3. `FFPlayer` 产生底层消息
4. `IjkMediaPlayer` 读取底层消息
5. `IjkMediaPlayer` 翻译成高层事件
6. `PlayerController` 收到高层事件后更新状态

### 9.3 这一阶段特别要避免什么

避免下面这种结构继续膨胀：

- UI 拥有 message loop
- 控制器自己解析 `FFP_MSG_*`
- 底层消息没有统一翻译口

### 9.4 完成标准

- message loop 的拥有者是播放器层，不是 UI
- 高层只订阅事件，不直接解析底层消息

---

## 10. 第五阶段：处理视频渲染边界

### 10.1 这里最容易混乱

你现在已经有 `video_refresh_callback_` 这个出口，这很好。  
但这里一定要想清楚：

播放器内核只负责“产出帧”，不要直接负责“窗口怎么显示”。

### 10.2 正确的边界

建议这样理解：

- `FFPlayer`
  - 产生 `Frame`
- `IjkMediaPlayer`
  - 把 `Frame` 作为回调事件向上抛
- 更上层的渲染模块
  - 决定怎么把帧显示到 `playerSurface_`

也就是说：

`playerEngine` 可以知道“有一帧到了”，  
但不应该知道“这帧该怎么塞进 Qt 某个控件”。

### 10.3 这一阶段先不要急着做什么

如果你现在还没准备好 Qt 渲染方案，就先把回调出口稳定住，不要急着把 `QWidget` 混进 engine。

---

## 11. 第六阶段：最后再拆 FFPlayer 内部

### 11.1 为什么这一步要放最后

因为 `FFPlayer` 现在的问题，不只是代码长，而是职责边界不清。  
边界没理顺前就拆文件，很容易拆成更多“名字不同但耦合还在”的类。

### 11.2 等前面稳定后，再按职责拆

等 `IjkMediaPlayer` 和 `PlayerController` 的契约稳定后，你再考虑把 `FFPlayer` 内部按职责拆成几个模块。

建议优先按下面这几个方向拆，而不是按按钮功能拆：

- `DemuxReader`
  - 负责 `read_thread`
- `AudioPipeline`
  - 负责音频解码、重采样、SDL 音频输出
- `VideoPipeline`
  - 负责视频解码、帧队列、刷新节奏
- `ClockSync`
  - 负责音视频时钟和同步策略
- `PlaybackFeatures`
  - 负责 seek、倍速、截图、音量这些通用能力

### 11.3 拆分顺序建议

建议这样拆：

1. 先抽“纯逻辑”模块，比如 clock/sync
2. 再抽 audio pipeline
3. 再抽 video pipeline
4. 最后抽 read/demux

不要一开始就大拆线程和队列，风险最高。

---

## 12. 你下一步最实际的落地顺序

如果你现在就准备开始做，建议按这个顺序推进。

### 第 1 步

先写一张你自己的 engine contract 草表：

- `IjkMediaPlayer` 对外公开哪些命令
- 往上抛哪些高层事件
- 状态机有哪些状态

先不用写代码，先把表列出来。

### 第 2 步

收紧 `IjkMediaPlayer` 的定位：

- 它是播放器会话入口
- 它拥有 message loop
- 它翻译消息
- 它维护底层运行态

### 第 3 步

让 `PlayerController` 只根据高层事件更新状态，去掉“伪造 Prepared/Playing/Paused”的写法。

### 第 4 步

当高层事件流稳定后，再决定：

- 是否把 `IjkMediaPlayer` 改名成 `PlayerSession`
- 或者保留 `IjkMediaPlayer` 名字，但让它承担 session 角色

### 第 5 步

最后再动 `FFPlayer` 内部结构。

---

## 13. 这一轮重构你暂时不要做的事

为了避免把自己拖进泥潭，下面这些事建议先不要做：

- 不要同时重写 Qt 渲染
- 不要一边重构一边加评论、弹幕、推荐视频这些功能
- 不要先追求多实例播放器
- 不要先做硬解码切换
- 不要先做完整缓存体系
- 不要在 `FFPlayer` 里继续叠加 UI 语义

---

## 14. 判断自己有没有走偏的检查表

如果后面你写着写着有点乱了，就用这张表检查。

### 正常方向

- `PlayerController` 只看高层事件
- `IjkMediaPlayer` 只做会话控制和消息翻译
- `FFPlayer` 只做底层播放内核
- UI 不理解 `FFP_MSG_*`
- `QWidget` 不出现在 engine 核心里

### 走偏信号

- `PlayerController` 开始解析 `FFP_MSG_*`
- `FFPlayer` 开始知道 Qt 控件
- `IjkMediaPlayer` 又开始直接承担 UI 语义
- 一个状态变化要改 3 个地方还说不清谁是权威
- seek、pause、buffering 互相覆盖，越来越多标记位

---

## 15. 最后一句总结

这次 `playerEngine` 重构的关键，不是“把 `FFPlayer` 拆碎”，而是：

先让 `IjkMediaPlayer` 成为稳定的播放器会话层，  
再让 `PlayerController` 只基于高层事件工作，  
最后才去清理 `FFPlayer` 内部。

顺序一旦对了，你后面会越写越顺。  
顺序一旦反了，你会一直在底层细节里打转。
