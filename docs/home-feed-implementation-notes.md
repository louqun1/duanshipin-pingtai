# 首页视频流实现笔记
## 1. 文档目的

这份文档记录第三阶段里“首页短视频卡片 + 独立视频播放窗口”是怎么一步步落地的，目标是帮助你理解：

- 为什么 `HomePage` 和 `StreamPage` 不能混用
- 为什么短视频点击后应该弹出单独的视频播放窗口
- `HomePage`、`VideoCard`、`VideoPlayerWindow`、`MainWindow` 各自承担什么职责
- 后面如果要接真实播放器、视频数据源、分页加载，应该从哪里继续扩展

对应的实现主要分布在这些文件里：

- [MainWindow.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/widgets/MainWindow.cpp)
- [VideoPlayerWindow.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/widgets/VideoPlayerWindow.cpp)
- [HomePage.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/pages/HomePage/HomePage.cpp)
- [StreamPage.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/pages/StreamPage/StreamPage.cpp)
- [VideoCard.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/components/VideoCard/VideoCard.cpp)

## 2. 先澄清这次实现里的页面职责

这次有一个非常关键的边界需要先明确：

- `HomePage` 负责短视频首页
- `StreamPage` 负责直播入口或直播工作区
- 短视频点击后不应该切到 `StreamPage`
- 短视频点击后应该弹出一个独立的视频播放窗口

这个边界很重要，因为它直接决定了后续代码是否会越写越乱。

如果把短视频点击直接导向 `StreamPage`，后面很容易出现这些问题：

- 直播页和短视频播放页职责混在一起
- 页面命名和真实业务不一致
- 后面接直播功能时会和短视频播放器互相挤占空间

所以这次修正后的目标不是“点卡片进入一个播放页”，而是：

1. 首页展示短视频卡片
2. 点击任意卡片
3. 弹出独立播放窗口
4. 播放窗口展示当前选中的视频信息
5. `StreamPage` 保持直播语义不变

## 3. 这次实现前的状态

在这一轮改动之前：

- `HomePage` 已经被改成了响应式短视频卡片流
- `VideoCard` 组件已经抽出来了
- 但点击卡片后的流转被错误地接到了 `StreamPage`
- `StreamPage` 也被误改成了短视频播放工作区

这意味着界面虽然“能跳”，但业务语义已经偏了：

- 首页是短视频
- `StreamPage` 是直播
- 这两者不应该直接连成同一条播放路径

所以这次不是在原基础上继续堆功能，而是先做一次职责纠偏。

## 4. 总体设计思路

修正之后的结构是这样的：

- `HomePage` 负责展示短视频流和发出播放意图
- `VideoCard` 负责单个短视频卡片的视觉和点击区域
- `MainWindow` 负责接收首页信号并弹出独立播放窗口
- `VideoPlayerWindow` 负责承载短视频播放窗口骨架
- `StreamPage` 继续保留为直播入口页

也就是说，`MainWindow` 这次仍然只做“壳层接线”，但接线的目标不再是切换到某个页面，而是打开一个新的顶层窗口。

## 5. 第一步：保留首页卡片流，不改短视频首页定位

首页这次没有被推翻，而是保留了已经完成的短视频卡片流实现：

- [HomePage.hpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/pages/HomePage/HomePage.hpp)
- [HomePage.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/pages/HomePage/HomePage.cpp)

它依然负责：

- 展示视频卡片
- 根据宽度动态计算列数
- 支持整张卡片可点击
- 发出 `playRequested(...)`

这里只需要纠正一点：

- `playRequested(...)` 表达的是“用户想播放这个短视频”
- 它不表达“请切到 StreamPage”

这个区别虽然小，但在架构上非常重要。

## 6. 第二步：让 VideoCard 继续只负责“整卡可点击”

`VideoCard` 的定位也没有变化：

- [VideoCard.hpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/components/VideoCard/VideoCard.hpp)
- [VideoCard.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/components/VideoCard/VideoCard.cpp)

它现在依然是：

- 一个可复用的卡片组件
- 整张卡片都是点击区域
- 用于承载标题、作者、时长和封面占位

之所以这一层不用改，是因为卡片本身只表达“我被点了”，并不关心点击之后是切页、弹窗还是播放。

这也是组件职责划分合理带来的好处：

- 上层交互逻辑改了
- 卡片组件本身不用推翻重写

## 7. 第三步：新增独立的 VideoPlayerWindow

这次最核心的修正，就是增加了一个专门承载短视频播放的顶层窗口：

- [VideoPlayerWindow.hpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/widgets/VideoPlayerWindow.hpp)
- [VideoPlayerWindow.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/widgets/VideoPlayerWindow.cpp)

为什么这里我选择“独立窗口”，而不是把它放回页面栈里？

因为你的业务边界已经很明确：

- `HomePage` 是短视频浏览
- `StreamPage` 是直播
- 短视频播放不属于直播页

既然不应该占用现有页面栈里的 `StreamPage`，最自然的做法就是让短视频有自己的顶层窗口。

当前这个窗口还只是播放占位骨架，但它已经把后续真实播放器最需要的承载位留好了：

- 左侧大尺寸视频预览区
- 右侧当前视频信息区
- 顶部窗口标题和说明区
- 后续控制栏和互动区预留位置

## 8. 第四步：让 MainWindow 只负责弹出这个新窗口

这次 `MainWindow` 的修正重点在于：

- 它不再把首页点击导向 `StreamPage`
- 而是创建或复用 `VideoPlayerWindow`

相关代码在：

- [MainWindow.hpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/widgets/MainWindow.hpp)
- [MainWindow.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/widgets/MainWindow.cpp#L111)

现在 `connectPlaybackFlow()` 做的是：

1. 接收 `HomePage::playRequested(...)`
2. 如果播放窗口还没创建，就先创建 `VideoPlayerWindow`
3. 把选中的视频信息传给播放窗口
4. 调用 `show()`、`raise()`、`activateWindow()`

这意味着：

- 首页不需要知道播放窗口怎么构建
- 播放窗口不需要知道首页有多少卡片
- 主窗口只负责把两边接起来

这仍然符合 `README` 里对 `MainWindow` 的定位：它是壳层和导航容器，不是业务中心。

## 9. 第五步：让播放窗口只负责“显示当前选中的视频”

`VideoPlayerWindow` 现在提供了一个显示层入口：

- `showSelectedVideo(...)`

实现位置在：

- [VideoPlayerWindow.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/widgets/VideoPlayerWindow.cpp#L15)

这个方法只负责：

- 更新窗口标题
- 更新播放器占位区标题
- 更新视频标题、作者、时长
- 更新右侧说明内容

它不负责：

- 真正开始播放
- 打开媒体文件
- 请求网络地址
- 初始化解码器或播放器引擎

这点和前面账户页的实现思路是一致的：

- 先把界面结构和交互链路打通
- 再把真实业务能力接上去

## 10. 第六步：把 StreamPage 恢复成直播页语义

这次修正不只是“新增一个窗口”，还包括把 `StreamPage` 从错误的短视频播放器职责里撤出来：

- [StreamPage.hpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/pages/StreamPage/StreamPage.hpp)
- [StreamPage.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/pages/StreamPage/StreamPage.cpp)

现在 `StreamPage` 的含义重新回到：

- 直播预览占位区
- 直播模块说明
- 后续直播功能的入口面板

也就是说：

- 短视频播放窗口是 `VideoPlayerWindow`
- 直播工作区还是 `StreamPage`

这一步的价值不是界面变多，而是职责重新清晰了。

## 11. 当前这套正确的信号流转链路

修正之后，短视频播放的链路应该理解为：

1. 用户进入 `HomePage`
2. 用户点击任意一个 `VideoCard`
3. `HomePage` 发出 `playRequested(videoId, title, creator, duration)`
4. `MainWindow` 接收这个信号
5. `MainWindow` 创建或复用 `VideoPlayerWindow`
6. `MainWindow` 调用 `VideoPlayerWindow::showSelectedVideo(...)`
7. `MainWindow` 弹出独立播放窗口

而不是：

- 首页点击
- 切到 `StreamPage`

这条边界是之后继续开发时必须一直守住的。

## 12. 为什么这套修正更符合当前项目结构

这套修正后的设计更合理，原因主要有三点：

第一，页面职责和业务语义重新对齐了。

- `HomePage` 对应短视频首页
- `StreamPage` 对应直播
- `VideoPlayerWindow` 对应短视频播放窗口

第二，后续扩展路径更清晰了。

- 短视频播放器往 `VideoPlayerWindow` 里加
- 直播相关功能往 `StreamPage` 里加
- 两条线互不打架

第三，不需要推翻已经完成的首页卡片流。

- 响应式布局保留
- 卡片组件保留
- 点击信号保留
- 只修改接线和承载容器

## 13. 当前实现已经完成的部分

现在已经完成的：

- 首页短视频卡片流
- 整卡可点击
- 根据窗口宽度动态调整卡片列数
- 点击卡片后弹出独立视频播放窗口
- 播放窗口显示当前选中视频的基础信息
- `StreamPage` 恢复为直播入口页语义

## 14. 当前还没有做的部分

现在还没做的：

- 真实视频播放能力
- 封面图加载
- 播放进度、暂停、音量、全屏等控制
- 评论、点赞、分享等互动区
- 真实视频列表数据源
- 播放窗口和后端播放器模块的正式对接

所以目前更准确地说，这是：

- 短视频首页 UI 骨架
- 独立播放窗口骨架
- 正确的页面与窗口职责边界

## 15. 后面最自然的扩展顺序

如果你准备继续往下做，我建议顺序是：

1. 先把 `VideoPlayerWindow` 左侧占位区换成真实播放器控件
2. 再把 `HomePage` 里的演示数据替换成真实视频列表
3. 再考虑短视频详情、推荐、评论、操作区怎么挂进播放窗口
4. 直播相关能力继续单独在 `StreamPage` 里推进

这样短视频和直播两条线会一直保持清晰分离。

## 16. 建议的阅读顺序

如果你想顺着这次修正回看代码，建议按这个顺序：

1. 先看 [VideoCard.hpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/components/VideoCard/VideoCard.hpp)
2. 再看 [VideoCard.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/components/VideoCard/VideoCard.cpp)
3. 再看 [HomePage.hpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/pages/HomePage/HomePage.hpp)
4. 再看 [HomePage.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/pages/HomePage/HomePage.cpp)
5. 再看 [VideoPlayerWindow.hpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/widgets/VideoPlayerWindow.hpp)
6. 再看 [VideoPlayerWindow.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/widgets/VideoPlayerWindow.cpp)
7. 最后看 [MainWindow.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/widgets/MainWindow.cpp)

这个顺序更符合现在真实的交互链路：

- 先看卡片
- 再看首页
- 再看播放器窗口
- 最后看主窗口如何把它们接起来
