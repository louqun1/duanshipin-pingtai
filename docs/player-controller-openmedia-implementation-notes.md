# PlayerController OpenMedia 实现笔记

## 1. 文档目的

这份文档记录这一次把短视频播放链路从首页卡片点击正式接到 `PlayerController` 的实现过程，重点说明三件事：

- 为什么这次先只打通到 `PlayerController`，而不急着继续接 `ijkPlayer` 和 `ffplay`
- `HomePage`、`MainWindow`、`VideoPlayerWindow`、`PlayerController` 在这条链路里各自承担什么职责
- 现在真实生效的事件流是什么样，后面要接 `ijkPlayer` 应该从哪里继续落地

这次实现主要分布在这些文件里：

- [AppBootstrap.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/app/bootstrap/AppBootstrap.cpp)
- [MainWindow.hpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/widgets/MainWindow.hpp)
- [MainWindow.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/widgets/MainWindow.cpp)
- [VideoPlayerWindow.hpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/widgets/VideoPlayerWindow.hpp)
- [VideoPlayerWindow.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/widgets/VideoPlayerWindow.cpp)
- [PlayerController.hpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/playercontroller/service/PlayerController.hpp)
- [PlayerController.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/playercontroller/service/PlayerController.cpp)
- [backend/CMakeLists.txt](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/CMakeLists.txt)

## 2. 这次实现前的问题

在这次修改之前，项目里其实已经有两个重要基础：

- `HomePage` 已经能发出 `playRequested(videoId, title, creator, duration)`
- `VideoPlayerWindow` 也已经存在，能显示被选中的视频信息

但链路中间还缺了一段关键接线：

- `AppBootstrap` 虽然创建了 `PlayerController`
- `MainWindow` 却没有把它注入到播放窗口
- `VideoPlayerWindow` 也没有真正调用 `OpenMedia`

也就是说，当时的流程还是：

1. 点击 `videoCard`
2. `MainWindow` 创建 `VideoPlayerWindow`
3. `VideoPlayerWindow` 只更新界面文本

而不是：

1. 点击 `videoCard`
2. `MainWindow` 创建 `VideoPlayerWindow`
3. `VideoPlayerWindow` 调用 `PlayerController::openMedia(...)`
4. `PlayerController` 进入打开媒体的状态并预留 `ijkPlayer` 接入点

这会带来一个明显问题：UI 看起来像已经进入播放器了，但真正的播放器控制器还没有参与这条链路。

## 3. 这次改动的目标边界

这次实现刻意把范围收得很清楚，只做下面这些事情：

- 打通 `videoCard -> MainWindow -> VideoPlayerWindow -> PlayerController`
- 让 `PlayerController` 成为这条链路里的真实控制器入口
- 在 `PlayerController` 内部留好 `ijkPlayer` 的创建和打开媒体接口位

这次明确不做的事情是：

- 不接真实的 `ijkPlayer` 头文件和库
- 不处理 `ffplay`
- 不处理底层消息循环
- 不开始做真实音视频解码和渲染

这样做的原因不是偷工，而是为了先把职责边界和调用方向固定住。等这条控制链稳定后，再把底层播放器挂进去，改动范围会小很多。

## 4. 现在这条链路里的职责划分

### 4.1 HomePage

`HomePage` 的职责没有变化，仍然只是：

- 展示视频卡片
- 监听卡片点击
- 发出 `playRequested(...)`

它表达的是“用户想播放这个视频”，而不是“请直接初始化底层播放器”。

### 4.2 MainWindow

`MainWindow` 这次仍然只负责壳层接线：

- 接收 `HomePage::playRequested(...)`
- 首次创建或复用 `VideoPlayerWindow`
- 把 `PlayerController` 注入给 `VideoPlayerWindow`
- 把选中的视频信息交给播放窗口

换句话说，`MainWindow` 负责“把窗口打开”，但不负责“怎么打开媒体”。

### 4.3 VideoPlayerWindow

`VideoPlayerWindow` 这次开始承担真正的播放器窗口职责：

- 持有 `PlayerController` 引用
- 把自己的 `playerSurface_` 交给 `PlayerController`
- 在 `showSelectedVideo(...)` 里调用 `playerController_.openMedia(...)`
- 订阅 `PlayerController` 的高层事件，更新窗口状态

这意味着播放窗口不再只是一个显示占位容器，而是已经成为控制器和 UI 之间的连接点。

### 4.4 PlayerController

这次 `PlayerController` 从一个空类变成了真正的控制器对象，负责：

- 接收 `openMedia/play/pause/stop` 这样的高层命令
- 维护最小播放状态
- 发出 UI 能理解的高层事件
- 预留 `ijkPlayer` 创建和 `open` 调用的落点

也就是说，虽然底层播放器还没接进来，但 `PlayerController` 已经先把“控制器层”站住了。

## 5. 这次是怎么把对象接起来的

### 5.1 启动装配层注入 PlayerController

这次先从启动装配层修正依赖注入：

- [AppBootstrap.cpp#L46](/d:/itffmpeg/av_media/online/duanshipin-pingtai/app/bootstrap/AppBootstrap.cpp#L46)

现在 `AppBootstrap` 会：

1. 创建 `PlayerController`
2. 把它传给 `MainWindow`

这一步很关键，因为它保证了控制器实例不是由页面自己 `new` 出来的，而是和认证控制器一样由装配层统一管理。

### 5.2 MainWindow 把控制器传给播放窗口

这次 `MainWindow` 的构造函数增加了一个 `PlayerController &` 参数：

- [MainWindow.hpp#L32](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/widgets/MainWindow.hpp#L32)

对应到播放链路里，`connectPlaybackFlow()` 现在做的是：

1. 接收首页发来的 `playRequested(...)`
2. 如果播放窗口还没创建，就先 `new VideoPlayerWindow(playerController_)`
3. 调用 `showSelectedVideo(...)`
4. 再执行 `show()`、`raise()`、`activateWindow()`

代码位置在：

- [MainWindow.cpp#L131](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/widgets/MainWindow.cpp#L131)

这让 `MainWindow` 继续维持“只接线，不做业务中心”的定位。

### 5.3 VideoPlayerWindow 真正调用 openMedia

`VideoPlayerWindow` 这次最大的变化，是它不再只更新文字，而是开始向 `PlayerController` 发命令：

- [VideoPlayerWindow.cpp#L25](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/widgets/VideoPlayerWindow.cpp#L25)

现在 `showSelectedVideo(...)` 做的是：

1. 先把窗口切到 Opening 的视觉状态
2. 调用 `playerController_.openMedia(videoId, title, creator, duration)`

这里就正式符合了你在“前后端交互”文档里提出的那条核心链路：

- `VideoPlayerWindow` 发命令
- `PlayerController` 接命令
- 控制器进入媒体打开过程

## 6. PlayerController 现在具备了什么能力

### 6.1 最小状态机

`PlayerController` 现在先维护了一套最小状态：

- `Idle`
- `Opening`
- `Prepared`
- `Playing`
- `Paused`
- `Stopped`
- `Error`

定义位置在：

- [PlayerController.hpp#L15](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/playercontroller/service/PlayerController.hpp#L15)

这套状态机现在还不复杂，但已经足够支撑：

- OpenMedia 进入 Opening
- OpenMedia 完成控制器准备后进入 Prepared
- 点击播放按钮进入 Playing
- 再次点击进入 Paused

### 6.2 高层命令入口

这次控制器先补了这些高层命令：

- `openMedia(...)`
- `requestPlay()`
- `requestPause()`
- `requestTogglePlayback()`
- `requestStop()`

声明位置在：

- [PlayerController.hpp#L32](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/playercontroller/service/PlayerController.hpp#L32)

这样后面 UI 的控制按钮就不需要直接碰底层播放器对象，而是统一走控制器。

### 6.3 高层事件

为了让 UI 不直接感知底层播放器实现，这次先提供了几个控制器级事件：

- `mediaChanged(...)`
- `playbackStateChanged(...)`
- `ijkPlayerCreated()`
- `ijkPlayerOpenRequested(...)`

这些事件目前主要用于：

- 更新 `VideoPlayerWindow` 的标题、提示文案和按钮状态
- 标出 `ijkPlayer` 后续真正接入的位置

## 7. 预留的 ijkPlayer 接口位在哪里

这次虽然没有接真实 `ijkPlayer`，但已经把控制器内部的两个关键落点留出来了：

- `ensureIjkPlayerCreated()`
- `openMediaWithIjkPlayer()`

实现位置在：

- [PlayerController.cpp#L96](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/playercontroller/service/PlayerController.cpp#L96)
- [PlayerController.cpp#L106](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/playercontroller/service/PlayerController.cpp#L106)

当前行为是：

1. `openMedia(...)` 先保存当前视频信息
2. 发出 `mediaChanged(...)`
3. 进入 `Opening`
4. `ensureIjkPlayerCreated()` 只发出 `ijkPlayerCreated()`
5. `openMediaWithIjkPlayer()` 只发出 `ijkPlayerOpenRequested(...)`
6. 最后进入 `Prepared`

也就是说，这里现在还是“占位接口”，但已经明确了后面真实接入时要替换的地方：

- `ensureIjkPlayerCreated()` 里创建或复用真实 `ijkPlayer`
- `openMediaWithIjkPlayer()` 里调用真实 `ijkPlayer` 的 `open` 或同类接口

## 8. VideoPlayerWindow 现在是怎么响应控制器事件的

这次 `VideoPlayerWindow` 新增了 `connectPlayerController()`：

- [VideoPlayerWindow.cpp#L39](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/widgets/VideoPlayerWindow.cpp#L39)

这里做了三件事：

1. 订阅 `mediaChanged(...)`，更新当前视频标题、作者、时长和侧边说明
2. 订阅 `playbackStateChanged(...)`，根据状态切换播放按钮文案和顶部提示
3. 订阅 `ijkPlayerCreated()` 和 `ijkPlayerOpenRequested(...)`，把当前预留接口位也显示在窗口提示里

此外，播放按钮也已经走控制器：

- 点击按钮 -> `requestTogglePlayback()`

这一步的意义在于，UI 已经不再自己判断播放器内部逻辑，而是只消费控制器发回来的高层状态。

## 9. 这次顺手修正的构建问题

这次还顺手修了一个工程级问题：

- `backend/CMakeLists.txt` 里原来写的是 `MediaPlayer.hpp/.cpp`
- 但实际文件已经是 `PlayerController.hpp/.cpp`

这会导致后端库的构建入口和实际代码不一致。

这次已经改成：

- [backend/CMakeLists.txt#L19](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/CMakeLists.txt#L19)

这样 `PlayerController` 的实现才真正参与了 `ServoBackend` 的编译。

## 10. 当前已经打通的真实事件流

现在这条链路可以按下面顺序理解：

1. 用户点击 `HomePage` 里的某个 `VideoCard`
2. `HomePage` 发出 `playRequested(videoId, title, creator, duration)`
3. `MainWindow` 接收这个信号
4. `MainWindow` 创建或复用 `VideoPlayerWindow`
5. `MainWindow` 调用 `VideoPlayerWindow::showSelectedVideo(...)`
6. `VideoPlayerWindow` 调用 `PlayerController::openMedia(...)`
7. `PlayerController` 进入 `Opening`
8. `PlayerController` 预留 `ijkPlayer` 创建和打开媒体调用位
9. `PlayerController` 进入 `Prepared`
10. `VideoPlayerWindow` 根据控制器事件更新界面

这里最重要的一点是：

- UI 到这里为止已经不再停留在“只是弹出一个窗口”
- 而是已经真正进入了“控制器驱动”的播放器事件流

## 11. 当前结果和还没做的部分

这次已经完成的部分是：

- `PlayerController` 被真正接入应用启动装配链
- `MainWindow` 把 `PlayerController` 注入给播放窗口
- `VideoPlayerWindow` 在选中视频后真正调用 `openMedia(...)`
- 播放按钮已经走控制器命令
- `ijkPlayer` 的创建和打开媒体接口位已经留出

这次还没有做的部分是：

- 真实 `ijkPlayer` 对象的创建
- 真实媒体打开和解码
- 控制器对底层播放器消息的翻译
- 首帧回调、播放结束、缓冲、错误等更完整事件
- `ffplay` 相关接线

所以更准确地说，这次实现完成的是：

- 控制器层接线
- 最小状态流
- 未来播放器内核接入的骨架

## 12. 本地验证

这次修改后已经重新构建：

- `QtFrontend` 构建通过

也就是说，至少从工程依赖和对象接线角度，这条链路已经是可编译、可运行的。

## 13. 后面最自然的继续方式

如果顺着这次的结构继续往下走，最自然的下一步是：

1. 在 `ensureIjkPlayerCreated()` 里接入真实 `ijkPlayer` 创建逻辑
2. 在 `openMediaWithIjkPlayer()` 里接入真实媒体打开逻辑
3. 把 `ijkPlayer` 或底层消息循环翻译成 `Prepared`、`FirstFrameReady`、`ErrorOccurred`、`PlaybackFinished` 这类高层事件
4. 再让 `VideoPlayerWindow` 继续只订阅这些高层事件

这样你前面在“前后端交互”文档里定下来的事件流模型，就能非常自然地从这次的骨架继续演进下去。
