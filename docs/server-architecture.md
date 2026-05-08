# Server 架构分析与亮点梳理

## 项目概览

**项目名称**: vod-platform-server  
**语言版本**: Go 1.26  
**核心定位**: 统一音视频平台的Go后端，支持VOD(点播)管道和直播+多人连麦

## 整体架构设计

项目采用**分离式微服务架构**，包含3个独立的进程入口点：

- **API服务** (`cmd/api/`): HTTP API服务器，提供RESTful + WebSocket接口
- **后台转码Worker** (`cmd/worker/`): 独立进程，轮询处理视频转码队列
- **导入工具** (`cmd/importer/`): CLI工具，用于本地文件导入到VOD平台

**核心技术栈**:
- **数据库**: MySQL (用户、会话、视频、直播状态)
- **对象存储**: MinIO S3兼容存储 (3个bucket: raw/vod/images)
- **通讯**: gorilla/websocket (WebSocket信令)
- **驱动**: go-sql-driver/mysql, minio-go/v7

## 三个主要服务入口

### 1. API服务
核心HTTP服务器，监听`:8080`，提供RESTful + WebSocket接口。

**主要路由分类**:
- **认证**: `/api/auth/register`, `/api/auth/login`, `/api/auth/logout`, `/api/me`
- **视频**: `/api/videos` (列表查询), `/api/videos/upload` (上传), `/api/videos/{id}` (详情)
- **直播**: `/api/live-rooms` (创建/查询), 房间控制接口
- **内部**: `/internal/events/video-updated`, `/vod/{key}`, `/image/{key}`
- **通知**: `/api/events` (SSE流)
- **信令**: `/ws` (WebSocket信令)

### 2. 后台转码Worker
独立进程，轮询处理视频转码队列。

**处理流程**:
1. 发现待处理任务 → 锁定任务
2. 从MinIO下载原始文件到临时目录
3. ffprobe分析元数据(分辨率、时长、编码)
4. ffmpeg转码为HLS格式 + 生成JPG封面
5. 上传HLS分片到vod bucket + 封面到image bucket
6. 更新MySQL videos表
7. 通知API发送SSE事件 → 客户端刷新

**关键特性**:
- 轮询间隔：空闲3秒，出错重试5秒
- 使用`FOR UPDATE`行级锁防止并发重复处理
- 原子更新videos和transcode_jobs表
- 本地临时目录处理(自动清理)

### 3. 导入工具
CLI工具，用于本地文件导入到VOD平台。

```bash
./importer -file path/to/video.mp4 -title "视频标题" -desc "描述"
```

处理流程：
1. 本地文件检验 → 生成object_key
2. 创建videos记录(状态=pending)
3. 上传到raw bucket
4. 插入transcode_jobs记录(供worker拉取)

## 数据库Schema分析

### 核心表结构

1. **users** - 用户账户
2. **user_sessions** - 会话管理
3. **videos** - 视频元数据
4. **video_likes** - 点赞追踪
5. **live_rooms** - 直播间
6. **live_room_presences** - 在线成员状态
7. **linkmic_requests** - 连麦申请状态机 (核心表)
8. **transcode_jobs** - VOD处理队列

## WebSocket信令系统

**架构**:
- 多个客户端 → WebSocket连接 → probeSignalClient → probeSignalHub → probeSignalRoom → probeLinkMicSession

**信令消息类型**: 共19个，包括注册、房间成员、连麦申请/邀请/控制、WebRTC SDP交换、ICE候选等。

**连麦状态转移**: 完整状态机，从idle到guest-applying/host-inviting，再到linkmic-active/rtc-active。

**WebSocket生命周期**: 24小时Ping/Pong超时，1MB消息限制，10秒写超时，32消息缓冲。

## VOD端到端处理流程

```
前端上传 → API接收 → 创建videos记录 → 上传到raw bucket → 插入transcode_jobs
    ↓
Worker轮询 → 下载原始视频 → ffprobe元数据 → ffmpeg转码HLS → 生成封面 → 上传到MinIO
    ↓
更新videos表 → 通知API → SSE广播 → 客户端刷新 → 播放HLS流
```

## 认证与授权

**会话设计**: 生成16字节随机token，SHA256哈希存储，30天有效期，支持撤销。

**密码存储**: 盐值哈希，SHA256(password + salt)。

## 实时事件系统

**EventBroker**: 观察者模式，内存事件广播。

**SSE流**: 客户端订阅 `/api/events`，Worker完成处理后通过API广播事件。

## MinIO三桶结构

- **raw-media**: 原始上传文件
- **vod-media**: 转码产物 (HLS分片)
- **image-assets**: 媒体资源 (封面图)

## 配置系统

通过环境变量配置MySQL、HTTP地址、MinIO等，支持自动URL派生。

## 关键设计特点

| 特性 | 实现 | 优势 |
|------|------|------|
| **异步转码** | Worker轮询+数据库队列 | 不阻塞API, 可水平扩展 |
| **状态管理** | SQL驱动状态机 | 强一致性, 无单点故障 |
| **实时通知** | SSE + 内存eventBroker | 轻量级, 无消息队列依赖 |
| **安全认证** | 盐值哈希+过期token | 抗彩虹表, 可撤销会话 |
| **并发控制** | MySQL `FOR UPDATE`行锁 | 防止重复处理 |
| **多租户隔离** | room_key + WebSocket分房间 | 低延迟信令 |
| **媒体加速** | MinIO S3接口 + 多bucket分离 | 可灵活部署CDN |

## 亮点总结

1. **职责分离清晰**: API/Worker/Importer各司其职，支持独立部署和扩展。
2. **可靠的状态管理**: 数据库驱动状态机，确保一致性和故障恢复。
3. **实时交互能力**: WebSocket信令 + SSE推送，实现低延迟通信。
4. **生产级质量**: 事务锁、错误重试、资源清理、并发控制。
5. **可扩展架构**: 支持多Worker并发、MinIO扩容、CDN集成。
6. **完整业务闭环**: 从上传到转码到播放的全流程自动化。
7. **安全设计**: 盐值密码哈希、会话管理、权限控制。
8. **轻量高效**: 无重型消息队列依赖，内存事件广播，快速响应。

这是一个设计清晰、分层合理的VOD+直播后端系统，具备高可用性和扩展性。