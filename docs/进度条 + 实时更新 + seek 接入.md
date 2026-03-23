# Player Seek 与进度条实现说明

## 1. 这份文档记录什么

这份文档记录本次播放器接入下面三项能力时，我实际做了什么，以及我当时的设计思路：

- 在 `VideoPlayerWindow` 里加入类似 `slider` 的进度条
- 让进度条随播放器当前位置实时更新
- 用户拖动进度条后，真正调用到底层 `seek`

本次改动主要集中在这些文件：

- `backend/playercontroller/service/PlayerController.hpp`
- `backend/playercontroller/service/PlayerController.cpp`
- `frontend/widgets/VideoPlayerWindow.hpp`
- `frontend/widgets/VideoPlayerWindow.cpp`

## 2. 我这次实际做了什么

### 2.1 在 `PlayerController` 增加了“进度同步层”

我没有让 `VideoPlayerWindow` 直接去轮询 `IjkMediaPlayer`，而是把进度同步统一放进了 `PlayerController`。

这次新增了几项内容：

- `playbackProgressChanged(qint64 positionMs, qint64 durationMs)` 信号
- `requestSeek(int positionMs)` 槽函数
- `syncPlaybackProgress()` 定时同步函数
- `updatePlaybackProgress(...)` 统一发出进度更新
- `QTimer` 每 250ms 轮询一次当前播放位置和总时长

这样做完以后，`PlayerController` 不只是负责播放/暂停命令，也开始负责“把底层进度翻译成 UI 能直接消费的高层数据”。

### 2.2 在 `VideoPlayerWindow` 增加了进度条和时间显示

UI 侧新增了这些元素：

- 一个水平 `QSlider`
- 左侧当前时间文本
- 右侧总时长文本

同时把控制器发上来的 `playbackProgressChanged(...)` 接到了窗口里，用来：

- 更新 slider 的最大值和当前位置
- 更新 `00:00 / 00:00` 这种时间显示
- 根据当前状态决定进度条是否可拖动

### 2.3 把“拖动进度条”真正打通成 seek 命令

这次不是只做了界面展示，而是把拖动后的行为接通到底层：

1. 用户按下 slider，进入拖动状态
2. 用户移动 slider 时，只更新本地显示时间
3. 用户松开 slider 时，调用 `PlayerController::requestSeek(...)`
4. `PlayerController` 再调用 `ijkPlayer_->seekTo(...)`

也就是说，这次 slider 不只是“显示进度”，已经是一个真正可交互的 seek 控件。

## 3. 我这次的核心思路

### 3.1 不让 UI 直接碰底层播放器

我刻意没有在 `VideoPlayerWindow` 里直接写：

- `ijkPlayer_->getCurrentPosition()`
- `ijkPlayer_->getDuration()`
- `ijkPlayer_->seekTo(...)`

原因很简单：

UI 的职责应该是展示和收集用户意图，不应该直接管理播放器底层细节。

如果窗口直接依赖 `IjkMediaPlayer`，后面会很快出现这些问题：

- UI 和播放器耦合过深
- 以后换播放器实现时，窗口代码也要一起改
- 状态和进度逻辑分散在 UI 和控制器两边，不好排查

所以这次我继续守住之前那条边界：

- `VideoPlayerWindow` 只负责发出“我要 seek 到这里”
- `PlayerController` 负责把命令下发到底层，并把结果回推回来

### 3.2 把“命令”和“状态同步”分开

这次我把两类东西分得比较清楚：

第一类是命令：

- `requestPlay()`
- `requestPause()`
- `requestSeek(positionMs)`

第二类是同步结果：

- `playbackStateChanged(...)`
- `playbackProgressChanged(positionMs, durationMs)`

这样做的好处是：

- UI 点击、拖动时，只表达意图
- 真正的播放器状态和进度，仍然以控制器同步回来的结果为准

这个思路和前面“Prepared 不应该自动播放”的修正是一致的，本质上都是在防止 UI 自己脑补底层状态。

### 3.3 进度条拖动时，不能被后台轮询抢回去

这是这次实现里最需要注意的一个细节。

因为我加了一个 250ms 的定时器去持续同步进度，所以如果用户正在拖动 slider，后台轮询还在不断上报当前位置，就会出现一个很差的体验：

- 用户往后拖
- slider 又被播放器当前时间顶回来
- 看起来像拖不动

所以我加了两个保护变量：

- `isSliderScrubbing_`
- `seekInFlight_`

它们分别解决两个阶段的问题：

1. 用户正在拖动但还没松手时，UI 优先显示用户手上的值，不用后台值覆盖
2. 用户刚松手、seek 还没真正完成时，先临时显示目标位置，等 seek 完成后再恢复正常同步

这一步是为了让交互体验稳定，不然功能虽然“能用”，但手感会很差。

## 4. 现在这条调用链是什么样的

### 4.1 实时进度更新链路

1. `PlayerController` 内部 `QTimer` 定时触发
2. 调用 `IjkMediaPlayer::getCurrentPosition()`
3. 调用 `IjkMediaPlayer::getDuration()`
4. `PlayerController` 发出 `playbackProgressChanged(positionMs, durationMs)`
5. `VideoPlayerWindow` 收到信号后更新 slider 和时间文本

### 4.2 用户拖动 seek 链路

1. 用户按下 slider
2. `VideoPlayerWindow` 标记当前处于拖动态
3. 用户移动 slider，窗口只更新显示时间
4. 用户松开 slider
5. `VideoPlayerWindow` 调用 `PlayerController::requestSeek(positionMs)`
6. `PlayerController` 调用 `ijkPlayer_->seekTo(...)`
7. 底层完成 seek 后，通过现有播放器事件继续恢复位置同步

## 5. 这次我为什么选“轮询进度”，而不是先做事件驱动进度

理论上，播放器最理想的做法是底层主动上报 `PositionUpdated` 事件。

但这次我没有继续往 `FFPlayer/IjkMediaPlayer` 里扩一整套“定时位置事件”消息，而是先在 `PlayerController` 层做轮询，原因有三个：

### 5.1 现有底层已经有获取位置和时长的接口

当前已经有：

- `getCurrentPosition()`
- `getDuration()`

所以从控制器层拉一层定时同步，能最短路径把功能先落地。

### 5.2 这次的目标是先把 UI 交互打通

这次最重要的是让用户能够：

- 看到进度
- 拖动进度
- 真正 seek

轮询方案足够满足这三个目标，而且改动面可控。

### 5.3 后面仍然可以平滑升级成事件驱动

我这次把进度同步入口收口到了 `PlayerController::syncPlaybackProgress()` 和 `playbackProgressChanged(...)`。

这意味着后面如果底层真的补了位置更新事件，UI 层几乎不用重写，只需要把控制器的数据来源从“轮询”换成“底层事件”即可。

所以这次的方案不是死路，而是一个可演进的中间态。

## 6. 这次改动里我特别想守住的边界

这次虽然做的是 seek 和 slider，但我其实更在意架构边界不要被破坏。

我想守住的边界是：

- `VideoPlayerWindow` 不直接依赖底层播放器对象
- `PlayerController` 继续作为 UI 和播放器之间的协调层
- 底层能力先在控制器里翻译，再喂给 UI

因为一旦 UI 直接开始：

- 读底层位置
- 直接调用底层 seek
- 自己处理底层线程/消息

后面这个窗口会越来越像“第二个播放器控制器”，职责会变乱。

## 7. 当前实现的局限与后续建议

这次功能已经打通，但还有一些可以继续优化的点：

### 7.1 当前位置仍然是轮询来的

现在是每 250ms 更新一次，已经能满足基础交互，但它不是最细粒度、最省资源的方案。

后面可以考虑：

- 在 `IjkMediaPlayer` 层补 `PositionUpdated` 事件
- 用底层事件代替控制器轮询

### 7.2 目前是“拖动松手后 seek”

现在的交互是：

- 按住拖
- 松手才执行 seek

这是最稳的基础版本。后面如果要做得更像成熟播放器，还可以补：

- 点击进度条任意位置直接跳转
- 拖动时实时预览目标时间
- seek 过程中显示“正在定位”

### 7.3 可以继续补高层状态

这次进度条已经接好了，后面比较自然的下一步是补更完整的高层状态：

- `Seeking`
- `Buffering`
- `Completed`

这样 UI 对播放器过程的表达会更完整。

## 8. 这次实现想表达的结论

这次表面上是在做“一个进度条”，但本质上我做的是三件事：

- 给播放器补了一个统一的进度同步出口
- 给 UI 补了一个真正可交互的 seek 输入入口
- 继续把播放器逻辑收口在 `PlayerController`，而不是散到窗口层

如果用一句话概括这次的思路，就是：

不要让进度条自己去管理播放器；应该让控制器统一管理播放器进度，再把结果和命令分别连接给 UI。

## 9. 本次结果

本次改动完成后，已经具备这些能力：

- 打开视频后，进度条会跟随当前位置实时更新
- 窗口会显示当前播放时间和总时长
- 用户拖动并松开进度条后，会真正触发底层 seek
- 拖动过程中不会被后台实时更新顶回去

另外，这次改动已经做过一次本地增量编译验证，构建通过。
