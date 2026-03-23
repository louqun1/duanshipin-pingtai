# PlayerController 播放暂停打通实现笔记

## 1. 文档目的

这份文档记录这一次把“前端播放/暂停按钮”真正打通到后端 `ijkPlayer` 的实现思路，重点说明三件事：

- 之前为什么会出现“前端显示已暂停，但后端其实没有收到暂停命令”
- 这次为什么不只是补一行 `pause()`，而是顺手把状态流也一起拉直
- 后面继续接 `seek`、`buffering`、`completed` 这类能力时，应该沿着什么原则继续演进

本次改动主要集中在这些文件里：

- [VideoPlayerWindow.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/widgets/VideoPlayerWindow.cpp)
- [PlayerController.hpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/playercontroller/service/PlayerController.hpp)
- [PlayerController.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/playercontroller/service/PlayerController.cpp)
- [IjkMediaPlayer.hpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/playerEngine/IjkMediaPlayer.hpp)
- [IjkMediaPlayer.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/playerEngine/IjkMediaPlayer.cpp)

## 2. 这次问题的真实根因

这次问题表面上看是“暂停没打通”，但真正的根因其实分成两层。

### 2.1 按钮点击其实已经到了控制器

前端按钮并不是完全没接线。

在 [VideoPlayerWindow.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/widgets/VideoPlayerWindow.cpp) 里，`playButton_` 的点击已经连接到了：

- `PlayerController::requestTogglePlayback()`

也就是说，链路里“前端按钮点下去”这一步其实已经成立了。

### 2.2 控制器只改了 UI 状态，没有真正调用播放器

真正的问题出在 [PlayerController.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/playercontroller/service/PlayerController.cpp)。

修改之前：

- `requestPlay()` 只做 `updatePlaybackState(Playing, ...)`
- `requestPause()` 只做 `updatePlaybackState(Paused, ...)`
- `requestTogglePlayback()` 又额外重复修改一次 `Playing/Paused`

也就是说，控制器做的是“把界面状态切过去”，而不是“把命令发给 `IjkMediaPlayer`”。

所以就会出现一种假象：

1. 用户点击暂停
2. `PlayerController` 把自己的状态改成 `Paused`
3. `VideoPlayerWindow` 收到 `playbackStateChanged(Paused, ...)`
4. 前端按钮文字变成 `Resume`
5. 但底层 `ijkPlayer` 根本没有执行 `pause()`

这就是这次问题最核心的断点。

## 3. 我这次修复时的核心思想

这次修复的核心思想只有一句话：

`UI` 只能发命令，不能伪造播放结果；真正的播放状态应该由播放器回调反推回来。

这句话拆开看，其实有三个设计原则。

### 3.1 前端发的是命令，不是结果

前端点击按钮表达的是：

- “我想播放”
- “我想暂停”
- “我想停止”

它表达的不是：

- “播放器现在已经在播放了”
- “播放器现在已经暂停了”

所以 `requestPlay()` / `requestPause()` 这种函数的职责，应该是把命令下发到底层播放器，而不是直接把控制器状态改成最终结果。

### 3.2 播放状态要以底层播放器的真实回调为准

真正可信的状态变化来源不是按钮点击，而是 `IjkMediaPlayer` 发回来的事件：

- `Prepared`
- `Playing`
- `Paused`
- `Stopped`
- `ErrorOccurred`

也就是说，按钮只能表达意图，事件才表达事实。

如果把这两件事混在一起，后面就很容易出现这些问题：

- UI 已经显示 `Paused`，但音频还在播
- UI 已经显示 `Playing`，但 `start()` 实际执行失败
- `Prepared`、`Buffering`、`Paused` 之间相互覆盖，状态来源越来越乱

### 3.3 PlayerController 应该是唯一的状态协调层

这次我刻意把职责收口到 `PlayerController`：

- 上面接收 `VideoPlayerWindow` 发来的高层命令
- 下面调用 `IjkMediaPlayer`
- 再把底层事件翻译成前端可消费的高层状态

所以 `PlayerController` 不应该既自己“脑补状态”，又再去订阅播放器状态。

更稳的做法是：

- 命令往下走
- 事件往上走
- 最终状态由 `PlayerController` 统一发布

## 4. 这次修复后的播放/暂停链路

现在这条链路应该是这样的。

### 4.1 打开媒体

1. `VideoPlayerWindow::showSelectedVideo(...)`
2. 调用 `PlayerController::openMedia(...)`
3. `PlayerController` 创建或复用 `IjkMediaPlayer`
4. 调用 `setDataSource(...)`
5. 调用 `prepareAsync()`
6. 等待 `IjkMediaPlayer` 回调 `Prepared`
7. `PlayerController` 再发布 `PlaybackState::Prepared`
8. `VideoPlayerWindow` 更新按钮和提示文案

这里的关键点是：

`openMedia()` 不应该在还没收到播放器回调时，就提前伪造 `Prepared`。

### 4.2 点击播放

1. 前端按钮点击
2. `VideoPlayerWindow` 调用 `PlayerController::requestTogglePlayback()`
3. `PlayerController` 判断当前状态不是 `Playing`
4. 转去执行 `requestPlay()`
5. `requestPlay()` 真正调用 `ijkPlayer_->start()`
6. `IjkMediaPlayer` 成功后抛出 `PlayerEvent::Playing`
7. `PlayerController` 收到事件并更新为 `PlaybackState::Playing`
8. 前端按钮切成 `Pause`

### 4.3 点击暂停

1. 前端按钮点击
2. `VideoPlayerWindow` 调用 `PlayerController::requestTogglePlayback()`
3. `PlayerController` 判断当前状态是 `Playing`
4. 转去执行 `requestPause()`
5. `requestPause()` 真正调用 `ijkPlayer_->pause()`
6. `IjkMediaPlayer` 成功后抛出 `PlayerEvent::Paused`
7. `PlayerController` 收到事件并更新为 `PlaybackState::Paused`
8. 前端按钮切成 `Resume`

这条链路里最重要的一点是：

暂停状态不是按钮自己改出来的，而是 `ijkPlayer` 执行完 `pause()` 后再回推上来的。

## 5. 这次代码上具体做了什么

### 5.1 requestPlay/requestPause/requestStop 真正下发到底层播放器

这次在 [PlayerController.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/playercontroller/service/PlayerController.cpp) 里把下面三件事补上了：

- `requestPlay()` 调用 `ijkPlayer_->start()`
- `requestPause()` 调用 `ijkPlayer_->pause()`
- `requestStop()` 调用 `ijkPlayer_->stop()`

同时保留了最基本的失败保护：

- 没有媒体时直接报错
- 没有 `ijkPlayer` 实例时进入 `Error`
- 如果底层返回非 0，则不假装成功

### 5.2 requestTogglePlayback 不再重复伪造 Playing/Paused

这次也顺手去掉了 `requestTogglePlayback()` 里额外的状态伪造。

修改之前它会这样做：

1. 调 `requestPause()` 或 `requestPlay()`
2. 无论底层是否成功，再手动 `updatePlaybackState(...)` 一次

这相当于把“命令入口”和“状态结论”写在了同一个地方，后面很容易打架。

现在它只负责：

- 判断当前是否处于 `Playing`
- 决定往 `requestPause()` 还是 `requestPlay()` 分发

至于最终是 `Playing` 还是 `Paused`，统一由播放器事件驱动。

### 5.3 openMedia 不再提前把状态伪造成 Prepared

之前 `openMedia()` 在发起 `prepareAsync()` 后，会直接进入：

- `PlaybackState::Prepared`

这在概念上其实是不准确的，因为这时底层播放器还没有真正回调 `Prepared`。

这次改完后：

- `openMedia()` 只负责进入 `Opening`
- `Prepared` 要等 `IjkMediaPlayer` 事件回推

这能保证前端看到的“可播放”状态和底层真实状态是一致的。

### 5.4 handlePlayerEvent 开始真正翻译底层事件

这次还把 [PlayerController.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/playercontroller/service/PlayerController.cpp) 里的 `handlePlayerEvent(...)` 从“只打印日志”推进成了“真正更新状态”。

现在它已经能把这些事件翻译成控制器状态：

- `OpenInputStarted` -> `Opening`
- `Prepared` -> `Prepared`
- `Playing` -> `Playing`
- `Paused` -> `Paused`
- `Stopped` -> `Stopped`
- `PlaybackFinished` -> `Stopped`
- `ErrorOccurred` -> `Error`

这里我用了异步回投到 Qt 对象线程的方式去更新状态，目的是避免底层消息线程直接改 UI 侧状态，减少线程上下文不一致的问题。

## 6. 为什么这次我不建议只补一行 pause()

从表面看，这次问题像是只要在 `requestPause()` 里补一行：

- `ijkPlayer_->pause()`

但如果只补这一行，另外两个问题还会继续存在：

- `requestTogglePlayback()` 仍然会手动伪造 `Paused/Playing`
- `openMedia()` 仍然会在底层未准备完成时提前进入 `Prepared`

那样虽然“暂停命令发下去了”，但整体状态流依然是乱的。

所以这次我顺手一起修了两件事：

- 把命令真正打到底层
- 把状态真正交给底层事件回推

这样这条链路后面才有继续扩展的基础。

## 7. 我这次想守住的边界

这次改动虽然不大，但我其实想守住一条很重要的边界：

不要让 `VideoPlayerWindow` 和 `IjkMediaPlayer` 直接耦合。

也就是说：

- `VideoPlayerWindow` 不直接调用 `start/pause/stop`
- `VideoPlayerWindow` 不直接理解 `PlayerEvent`
- `VideoPlayerWindow` 只订阅 `PlayerController::playbackStateChanged(...)`

这层隔离的好处很直接：

- 以后底层从 `IjkMediaPlayer` 换成别的实现，前端不需要重写
- 后面如果加入 `Seeking`、`Buffering`、`Completed`，也是先落在控制器，不会把窗口逻辑越搅越复杂
- 状态问题可以统一在 `PlayerController` 排查，而不是分散在 UI 和底层两个方向

## 8. 对后续演进的建议

这次播放/暂停打通以后，后面如果继续完善播放器，我建议沿着同一套思路继续做。

### 8.1 继续扩展高层状态，而不是让 UI 直接看底层消息

后面可以继续补这些高层状态：

- `Seeking`
- `Buffering`
- `Completed`

但原则仍然一样：

- 底层消息先进入 `PlayerController`
- `PlayerController` 翻译成 UI 能理解的状态
- `VideoPlayerWindow` 只消费翻译后的结果

### 8.2 把“命令成功”和“状态变化”彻底分开

后面无论是 `seek`、`setRate`、`setVolume`，都建议继续遵循下面这个模型：

1. UI 发命令
2. 控制器调用底层
3. 底层回事件
4. 控制器更新统一状态
5. UI 只响应最终状态

不要再回到“点击按钮时先把界面切过去”的写法。

### 8.3 把 PlayerController 当成播放器协调层来维护

如果后面播放器功能继续变多，`PlayerController` 最适合承担的角色仍然是：

- 命令入口
- 状态机中心
- 事件翻译层
- UI 与底层播放器之间的边界层

只要这个边界守住，后面功能再加深，复杂度也还在可控范围里。

## 9. 这次实现想表达的结论

这次修复表面上解决的是“暂停命令没有传到后端”，但更重要的收获其实是把这条链路的控制关系理顺了：

- `VideoPlayerWindow` 负责发命令和显示结果
- `PlayerController` 负责协调命令、状态和事件
- `IjkMediaPlayer` 负责执行真实播放动作并回推事件

对应到一句最简短的话就是：

不要让前端假装播放器已经暂停，应该让播放器在真的暂停之后，再通知前端它已经暂停了。
