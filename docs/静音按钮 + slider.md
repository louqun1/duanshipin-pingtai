# Player 音量控制实现说明

## 1. 这份文档记录什么

这份文档记录本次播放器音量控制接入时，我做了什么，以及我当时的设计思路。

这次的目标不是单纯“加一个 slider”，而是把下面这几件事一起打通：

- UI 能直接调节音量
- UI 能一键静音和恢复
- 音量值能真正传到底层播放器
- 播放器重建后仍然保留当前音量设置

本次改动主要集中在这些文件：

- `backend/playercontroller/service/PlayerController.hpp`
- `backend/playercontroller/service/PlayerController.cpp`
- `frontend/widgets/VideoPlayerWindow.hpp`
- `frontend/widgets/VideoPlayerWindow.cpp`

## 2. 我这次实际做了什么

### 2.1 在 `PlayerController` 增加音量控制接口

这次我没有让 `VideoPlayerWindow` 直接调用 `IjkMediaPlayer::setPlaybackVolume()`，而是继续把音量控制收口到 `PlayerController`。

新增的内容包括：

- `requestSetVolume(int volume)`
- `requestToggleMute()`
- `playbackVolumeChanged(int volume, bool muted)`
- `updatePlaybackVolume(...)`
- `applyPlaybackVolume()`

这意味着音量控制也进入了和播放、暂停、seek 一样的架构路径：

- UI 发命令
- `PlayerController` 协调
- 控制器把结果同步回 UI

### 2.2 在窗口里加入了“静音按钮 + 音量 slider + 百分比”

我最终没有只放一个 slider，而是做成了一个组合控件：

- 一个静音按钮
- 一个横向音量 slider
- 一个显示当前百分比的文本

这样做的效果是：

- 想快速静音时，一下就能点
- 想精细调节时，可以拖 slider
- 当前音量一眼就能看出来

从交互上讲，这比“只有一个 slider”更完整。

### 2.3 把音量值真正传到了底层播放器

这次不是只改了界面显示。

现在的链路是：

1. 用户拖动音量 slider
2. `VideoPlayerWindow` 调用 `PlayerController::requestSetVolume(...)`
3. `PlayerController` 调用 `IjkMediaPlayer::setPlaybackVolume(...)`
4. `IjkMediaPlayer` 再把音量传给底层 `FFPlayer`

也就是说，现在 UI 的音量操作会真正影响实际播放声音。

### 2.4 增加了“恢复到上一次非零音量”的逻辑

静音按钮不是简单地把音量设成 0，然后再点一次回到固定值 50。

我额外保存了一个 `lastNonZeroVolume_`，用于记录最近一次非零音量。

这样交互会更自然：

- 当前是 72，点静音 -> 变成 0
- 再点一次 -> 恢复到 72

这比“静音后恢复成写死值”更符合播放器用户习惯。

## 3. 我这次的核心思路

## 3.1 不让 UI 直接操作底层播放器

这次的思路和前面做 seek 时是一致的：

- `VideoPlayerWindow` 不直接碰底层播放器对象
- `PlayerController` 继续做中间协调层

我刻意没有在窗口里直接写：

- `ijkPlayer_->setPlaybackVolume(...)`

因为一旦 UI 直接碰底层，后面会带来几个问题：

- UI 和播放器实现耦合变深
- 音量状态分散在多个地方
- 以后如果播放器实现变化，窗口逻辑也得跟着改

所以我这次继续守住原来的边界，让窗口只表达“我要改音量”，真正的执行和同步都放在控制器。

## 3.2 音量本质上也是播放器状态的一部分

虽然音量不像 `Playing / Paused / Seeking` 那样是主状态，但它仍然属于播放器会话的一部分。

所以这次我没有把它当成“一次性 UI 临时值”，而是当成播放器控制状态来处理：

- 控制器里保存 `playbackVolume_`
- 控制器里保存 `lastNonZeroVolume_`
- 当 `IjkMediaPlayer` 被创建或重新打开媒体时，再把当前音量重新下发到底层

这一步很重要，因为否则很容易出现这种问题：

- UI 上显示 20%
- 但底层播放器刚重建，音量其实已经回到默认值

我这次想避免的就是这种“界面显示值”和真实播放值不一致”的情况。

## 3.3 选择“静音按钮 + slider”而不是只放 slider

用户一开始说“也可以用 slider，或者更好的”，我这里理解成：

不只是能调音量，还要在常用场景下更顺手。

所以我没有停在最基础的方案，而是做了一个稍微完整一点的组合：

- slider 负责精调
- 静音按钮负责快速切换
- 百分比负责明确反馈

如果只用 slider，会有两个问题：

- 想临时静音时，不方便
- 看不出当前数值，只能大概猜位置

所以这次的组合控件，是我觉得在当前界面里最自然的一版基础实现。

## 4. 现在这条音量链路是什么样的

### 4.1 调整音量

1. 用户拖动 `volumeSlider_`
2. `VideoPlayerWindow` 发出 `requestSetVolume(volume)`
3. `PlayerController` 更新内部音量状态
4. `PlayerController` 调用 `applyPlaybackVolume()`
5. `IjkMediaPlayer::setPlaybackVolume(...)` 把值传到底层播放器
6. 控制器发出 `playbackVolumeChanged(volume, muted)`
7. UI 更新按钮文本和百分比

### 4.2 静音 / 取消静音

1. 用户点击 `muteButton_`
2. `VideoPlayerWindow` 发出 `requestToggleMute()`
3. `PlayerController` 判断当前是否为 0
4. 如果当前大于 0，则保存到 `lastNonZeroVolume_` 后置 0
5. 如果当前等于 0，则恢复到上一次非零音量
6. 控制器再把结果同步到 UI

## 5. 这次实现里我特别在意的点

### 5.1 音量设置要能跨播放器实例保留

这次我在 `ensureIjkPlayerCreated()` 和 `openMediaWithIjkPlayer()` 里都补了 `applyPlaybackVolume()`。

目的就是保证：

- 新建播放器时，音量立即生效
- 切换媒体时，音量不会悄悄回到默认值

这不是 UI 细节，而是播放器一致性问题。

### 5.2 静音和音量值不要互相打架

很多播放器做静音时会单独再维护一个 `muted` 布尔值。

这次我没有额外再搞一套复杂状态，而是采用了更直接的规则：

- `volume == 0` 就认为当前静音
- `lastNonZeroVolume_` 用来支持恢复

这样做的优点是：

- 逻辑简单
- UI 判定直观
- 不容易出现“muted=true 但 volume=30”这种不一致状态

### 5.3 UI 展示必须由控制器回推，不靠窗口自己猜

我这次也没有让窗口在按钮点击后自己先改文本，而是等 `playbackVolumeChanged(...)` 回来再统一更新：

- 按钮文案
- slider 值
- 百分比文本

这样能避免窗口本地值和控制器值分叉。

## 6. 当前实现的局限与后续建议

### 6.1 目前按钮文本还是文字，不是图标

现在为了快速落地和避免额外资源依赖，按钮文字是：

- `Muted`
- `Vol Low`
- `Vol Mid`
- `Vol High`

这已经够用，但如果后面继续打磨体验，我建议下一步改成：

- 真正的扬声器图标
- 静音时换成带斜杠图标

这样识别成本更低。

### 6.2 目前没有做悬浮弹出式音量条

这次我做的是常驻底部控制栏。

这是当前窗口里最稳、最直接的方案。后面如果想做得更轻，可以考虑：

- 只保留一个音量按钮
- hover 或点击后弹出竖向音量条

但那样会带来更多交互和状态处理，所以我这次没有优先走那条路。

### 6.3 还没有把音量写入持久化配置

这次音量可以在当前播放器会话内保持，但还没有写入用户配置。

如果后面要做得更完整，可以再补：

- 应用重启后保留上次音量
- 不同用户保留各自音量偏好

## 7. 这次实现想表达的结论

这次表面上是在做“音量控件”，但本质上我在做的是：

- 把音量控制纳入 `PlayerController` 的统一协调范围
- 让 UI 既能快速静音，也能精细调节
- 保证界面显示值和底层真实音量值保持一致

如果用一句话概括这次的思路，就是：

音量不应该只是一个窗口上的 slider，它应该是播放器控制状态的一部分，并且由控制器统一管理。

## 8. 本次结果

本次改动完成后，已经具备这些能力：

- 可以通过 slider 调节音量
- 可以通过按钮快速静音/恢复
- 恢复时会回到上一次非零音量
- 当前音量会用百分比显示出来
- 音量值会真正传到底层播放器
- 播放器重建或重新打开媒体后，当前音量仍会继续生效

另外，这次改动已经做过一次本地增量编译验证，构建通过。
