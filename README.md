# Flashpoint — 统一音视频平台原型

基于 Qt/C++ 与 Go 构建的统一音视频平台原型，实现从视频上传、转码、点播播放，到直播推流、HTTP-FLV 观看，再到 WebRTC 连麦互动的完整闭环。

## 架构概览

```
┌──────────────────────────────────────────────────────────────┐
│                      Qt 桌面客户端                            │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐       │
│  │  首页推荐  │ │  直播观看  │ │  视频上传  │ │  个人中心  │       │
│  └──────────┘ └──────────┘ └──────────┘ └──────────┘       │
│  ┌──────────────────────────────────────────────────┐       │
│  │              播放器内核 (FFmpeg + OpenGL)          │       │
│  │       demux / decode / A/V sync / seek            │       │
│  └──────────────────────────────────────────────────┘       │
│  ┌──────────────────────────────────────────────────┐       │
│  │            直播播放器 (HTTP-FLV 解包 + 渲染)        │       │
│  └──────────────────────────────────────────────────┘       │
├──────────────────────────────────────────────────────────────┤
│                      Go 服务端                               │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐       │
│  │  REST API │ │  Worker   │ │ /ws 信令  │ │ 混流服务  │       │
│  │  (CRUD)   │ │ (转码任务)  │ │ (连麦)    │ │ (linkmic) │       │
│  └──────────┘ └──────────┘ └──────────┘ └──────────┘       │
├──────────────────────────────────────────────────────────────┤
│                      Docker 基础设施                         │
│  MySQL · Redis · MinIO · SRS · Coturn                       │
└──────────────────────────────────────────────────────────────┘
```

## 核心能力

### 点播 (VOD)
- 视频上传至对象存储（MinIO）
- Worker 异步转码为 HLS（FFmpeg 切片 + 播放清单）
- SSE 实时推送转码完成状态
- 自研播放器加载 HLS 播放，支持播放/暂停/Seek/音量

### 直播 (Live)
- **推流端**（独立仓库）：摄像头/麦克风采集 → H.264/AAC 编码 → RTMP 推送至 SRS
- **观看端**：HTTP-FLV 拉流 → 自研解包/解码 → 音视频同步 → OpenGL 渲染
- 背压控制、丢帧策略、音频主时钟同步

### 连麦 (LinkMic)
- 房间管理（创建/加入/离开）
- WebSocket 信令（申请/邀请/接受/拒绝/挂断/踢出）
- WebRTC DataChannel + 音视频轨道
- 服务端 XStack + AMix 混流分发

## 技术栈

| 层级 | 技术 |
|------|------|
| 桌面客户端 | Qt 6.10 + C++17 + CMake |
| 播放器内核 | FFmpeg 6.0 + SDL2 + OpenGL |
| 服务端 | Go 1.26 + gorilla/websocket |
| 存储 | MySQL 8.0 + Redis 7 + MinIO |
| 流媒体 | SRS 5 (RTMP/HTTP-FLV) |
| WebRTC | Coturn (STUN/TURN) + libdatachannel |
| 部署 | Docker Compose |

## 项目结构

```
duanshipin-pingtai/
├── app/                    # Qt 应用入口
│   └── bootstrap/          # AppBootstrap 组装依赖
├── frontend/               # Qt 前端
│   ├── pages/              # 页面：首页/直播/上传/个人
│   ├── widgets/            # 主窗口/播放器窗口/OpenGL 控件
│   └── components/         # 可复用组件 (VideoCard)
├── backend/                # C++ 后端核心
│   ├── playerEngine/       # 播放器内核 (FFPlayer)
│   ├── playercontroller/   # 播放器控制层 (状态机)
│   ├── liveplayer/         # 直播拉流 + 解码 + 渲染
│   ├── controller/auth/    # 认证控制器
│   └── infrastructure/     # 数据库等基础设施
├── server/                 # Go 服务端
│   ├── cmd/api/            # REST API 入口
│   ├── cmd/worker/         # 转码 Worker
│   ├── cmd/importer/       # 数据导入工具
│   └── internal/           # 内部包（配置/DB/存储/信令/混流）
├── ops/                    # 运维配置 (coturn 等)
├── docs/                   # 设计文档
└── docker-compose.yml      # 基础设施编排
```

## 快速开始

### 1. 启动基础设施

```bash
docker compose up -d mysql redis minio srs coturn
```

### 2. 初始化数据库

```bash
# 连接 MySQL（端口 3307）
mysql -h 127.0.0.1 -P 3307 -u root -proot123456 vod_platform < server/migrations/*.sql
```

### 3. 启动 Go 服务端

```bash
cd server
go mod tidy
go build -o ./bin/vod-platform-api ./cmd/api
go build -o ./bin/vod-platform-worker ./cmd/worker

# 启动 API
HTTP_ADDR=:8080 \
MYSQL_DSN='vod_user:vod_pass_123@tcp(127.0.0.1:3307)/vod_platform?charset=utf8mb4&parseTime=True' \
MINIO_ENDPOINT=127.0.0.1:9000 \
./bin/vod-platform-api

# 启动转码 Worker（另一个终端）
./bin/vod-platform-worker
```

### 4. 编译桌面客户端

```bash
cmake -B build -G "MinGW Makefiles" \
  -DFLASHPOINT_QT_ROOT=D:/itffmpeg/Qt/6.10.3/llvm-mingw_64 \
  -DFLASHPOINT_FFMPEG_ROOT=D:/itffmpeg/av_media/online/ffmpeg-6.0

cmake --build build --config Release
```

> **注意**：桌面客户端仅支持 Windows，依赖 Qt 6.10、FFmpeg 6.0 和 SDL2。

## 环境端口

| 服务 | 端口 | 用途 |
|------|------|------|
| MySQL | 3307→3306 | 数据库 |
| Redis | 6380→6379 | 缓存 |
| MinIO | 9000 / 9001 | 对象存储 / 控制台 |
| SRS | 1935 / 1985 / 18080 | RTMP 推流 / API / HTTP-FLV |
| Coturn | 3478 | STUN/TURN |
| Go API | 8080 | REST + WebSocket |

## 关键设计

- **播放器三层架构**：UI → PlayerController（状态机）→ PlayerEngine（FFmpeg 内核），职责清晰、可单独测试
- **HLS 点播管道**：上传与转码解耦，Worker 异步处理，SSE 推送状态变更
- **HTTP-FLV 观看链**：自研拉流 → FLV 解包 → 解码 → 背压控制 → OpenGL 渲染，不依赖黑盒播放器
- **连麦信令状态机**：前后端对称状态机，request_id 绑定，支持完整的申请/邀请/接受/拒绝/挂断闭环
- **跨房间混流**：服务端 XStack 画面拼合 + AMix 音频混合，单路 HTTP-FLV 分发给观众
