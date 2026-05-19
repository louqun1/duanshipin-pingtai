# 短视频音视频平台原型

本项目是一个统一音视频平台原型，重点不是做通用 CRUD 产品，而是围绕音视频工程链路做学习和验证。

当前仓库同时覆盖三条主线：

- 自研播放器内核：Qt + FFmpeg + SDL2 + OpenGL，关注 demux、decode、A/V sync、seek、渲染和音频输出。
- 点播链路：上传、对象存储、转码、HLS 产物、封面生成、播放入口。
- 直播与连麦：RTMP 推流、HTTP-FLV 拉流、WebSocket 信令、WebRTC 连麦房间流转。

## 项目定位

这是一个偏工程学习型的项目。优先级最高的是把音视频数据流看清楚、拆小、手写关键路径，而不是一次性堆出完整产品功能。

学习核心包括：

- 播放器内核：解复用、解码、音视频同步、seek、渲染队列。
- 点播流水线：上传原片、转码任务、HLS 切片、播放 URL 组织。
- 直播观看：RTMP ingest、HTTP-FLV 拉流、FLV tag 解析、H.264/AAC 解码。
- WebRTC 连麦：信令协议、房间成员、申请/邀请/接受/拒绝/挂断流程。

## 仓库结构

```text
app/
  Qt 桌面端启动入口和模块装配

frontend/
  Qt Widgets 页面、播放器窗口、直播页、上传页、账户页

backend/
  C++ 侧业务分层、播放器内核、HTTP-FLV 学习链路、SQLite/Repository/Service/Controller

server/
  Go 后端服务，包含 HTTP API、WebSocket 信令、VOD 导入、转码 worker、直播连麦状态接口

ops/
  coturn 等运行配置

docs/
  阶段设计、排查记录、音视频链路说明和面试梳理资料

docker-compose.yml
  MySQL、Redis、MinIO、SRS、coturn 等本地依赖
```

## 技术栈

桌面端：

- C++17
- Qt 6 Widgets / Network / Sql / OpenGLWidgets
- FFmpeg
- SDL2
- CMake

服务端：

- Go
- MySQL
- MinIO
- gorilla/websocket
- ffmpeg / ffprobe

基础设施：

- SRS：RTMP ingest 和 HTTP-FLV 分发
- coturn：WebRTC STUN/TURN
- Redis：后续在线状态和分布式状态扩展预留

## 核心数据流

### 1. 本地播放器链路

```text
媒体 URL / 本地文件
  -> FFmpeg avformat 打开输入
  -> demux 音视频包
  -> 音频/视频解码队列
  -> 音频重采样与 SDL 回调输出
  -> 视频帧转换与 OpenGL 渲染
  -> 播放状态、进度、seek、暂停/恢复
```

这一部分是学习重点，后续改动应该优先解释数据流，再拆成小里程碑实现。

### 2. HTTP-FLV 直播观看链路

```text
SRS HTTP-FLV URL
  -> Qt QNetworkReply 接收 chunk
  -> 手写/半手写 FLV 解析
  -> 分离 audio tag / video tag
  -> FFmpeg 解码 H.264 / AAC
  -> 视频帧进入渲染队列
  -> 音频帧进入音频输出队列
```

当前分支重点在“自己处理 HTTP-FLV”，因此这条链路尽量保留可观察日志和阶段性 TODO，方便逐段验证。

### 3. 点播上传与转码链路

```text
客户端选择视频
  -> Go API 接收上传
  -> 原始文件进入 MinIO raw-media
  -> MySQL 写入 video / transcode job
  -> worker 调用 ffmpeg 转 HLS
  -> HLS 切片和封面进入 MinIO vod-media / image-assets
  -> API 返回播放信息
```

更详细的服务端说明见 [server/README.md](server/README.md)。

### 4. 直播连麦链路

```text
主播创建直播间
  -> 观众进入房间并上报 presence
  -> apply / invite / accept / reject / hangup
  -> WebSocket/SSE 同步房间和连麦状态
  -> 下发 WebRTC join params
  -> 客户端完成 offer / answer / ICE candidate 交换
```

这部分关注房间流转和信令正确性，媒体混流、多人扩展、持久化和多实例广播应分阶段推进。

## 开发原则

音视频核心任务采用小步学习方式：

- 先画清楚数据流，再写代码。
- 先做最小可验证 milestone，再扩展完整功能。
- 对 demux、decode、A/V sync、seek、HTTP-FLV、RTMP、WebRTC 等核心点，优先保留 skeleton + TODO。
- 关键代码尽量自己手写，提交后再 review。

交付型任务可以直接实现：

- DTO
- 路由注册
- 配置加载
- 日志和中间件
- 简单后台页面
- 普通 CRUD

## 桌面端构建

根目录 CMake 工程会生成 `QtFrontend`。

依赖路径可以通过 CMake cache 配置：

- `FLASHPOINT_QT_ROOT`：Qt 6 安装路径
- `FLASHPOINT_FFMPEG_ROOT`：FFmpeg 开发包路径
- `FLASHPOINT_SDL2_ROOT`：SDL2 开发包路径

Windows 示例：

```powershell
cmake -S . -B build -G Ninja `
  -DFLASHPOINT_QT_ROOT=D:/itffmpeg/Qt/6.10.3/llvm-mingw_64

cmake --build build
```

运行产物通常位于：

```text
build/bin/QtFrontend.exe
```

## 服务端启动

先启动基础依赖：

```powershell
docker compose up -d mysql redis minio srs coturn
```

再启动 Go API：

```powershell
cd server
go mod tidy
go run ./cmd/api
```

健康检查：

```powershell
curl http://127.0.0.1:8080/healthz
```

期望返回：

```json
{"status":"ok"}
```

更多环境变量、端口和 probe 联调命令见 [server/README.md](server/README.md)。

## 常用验证

桌面端最小验证：

- CMake configure 成功。
- `QtFrontend` 能启动。
- 首页、直播页、上传页、账户页能切换。
- HTTP-FLV 页面输入可访问 URL 后能看到网络接收、FLV 解析、视频解码或明确错误日志。

服务端最小验证：

- `docker compose ps` 中 MySQL、MinIO、SRS、coturn 状态正常。
- `/healthz` 返回 `ok`。
- 上传接口能写入 MinIO 和 MySQL。
- worker 能把原始视频转成 HLS。
- `/ws` 能完成最小 probe 信令注册和消息转发。

## 当前限制

- 播放器内核仍处于学习和重构阶段，部分逻辑保留 TODO。
- HTTP-FLV 链路优先验证视频解析和渲染，音频输出、同步和异常恢复会继续拆阶段推进。
- WebRTC 连麦当前偏 probe 和业务信令验证，完整鉴权、多实例广播、正式 RTC 票据仍需后续补齐。
- VOD worker 依赖本机 ffmpeg / ffprobe 可用。

## 目标

最终希望这个仓库能沉淀成一个可讲清楚、可运行、可扩展的音视频工程样板：

- 桌面端能播放本地/点播/直播媒体。
- 服务端能完成上传、转码、存储、播放信息分发。
- 直播链路能跑通 RTMP ingest、HTTP-FLV 观看和 WebRTC 连麦信令。
- 每条核心链路都有清晰的阶段说明、验证方法和可复盘的学习记录。
