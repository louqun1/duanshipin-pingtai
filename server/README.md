# server

`server/` 是当前平台的 Go 后端工程。

当前最小连麦探针联调约定：

- MySQL、Redis、MinIO、SRS 跑在 `../docker-compose.yml` 对应的 Docker 容器里
- Go API 进程先直接跑在宿主机或虚拟机系统里
- 探针客户端跑在 `duanshipin-live-studio` 一侧，通过 `ws://<vm-ip>:8080/ws` 连到这里
- `/ws` 当前同时服务：
- 阶段 0 最小 RTC probe 信令联调
- 阶段 1 最小业务信令 probe 联调
- `/api/live-rooms/*` 已提供 DB-backed 的阶段 1 正式业务接口
- `/ws` 的业务信令状态仍然是单进程内存态，用于 probe 联调和最小 RTC relay

## 1. 当前最小能力

当前这版 `/ws` 已支持：

- `probe.register`
- `probe.registered`
- `room.member-list`
- `room.member-join`
- `room.member-leave`
- `linkmic.apply`
- `linkmic.invite`
- `linkmic.accept`
- `linkmic.reject`
- `linkmic.cancel`
- `linkmic.hangup`
- `linkmic.kick`
- `linkmic.state-sync`
- `rtc.offer`
- `rtc.answer`
- `rtc.ice-candidate`
- `rtc.join-params`
- `linkmic.connected`
- `signal.error`

它当前有两个直接目标：

- 让 `rtc_probe.exe` 和 `rtc_guest_probe.exe` 能通过现有 Go 服务完成最小 DataChannel 建连验证
- 让新的 `--mode signal` 探针先把房间成员列表、申请/邀请、接受/拒绝、挂断和状态流转跑通

补充说明：

- 连麦房间状态当前只保存在 Go 进程内存里
- 当前不依赖 Redis 就能完成阶段 0 / 阶段 1 探针联调
- 进程重启后房间在线状态和连麦状态都会清空

## 2. Docker 依赖启动

在 `duanshipin-pingtai` 根目录执行：

```bash
cd /path/to/duanshipin-pingtai
docker compose up -d mysql redis minio srs coturn
docker compose ps
```

当前 `docker-compose.yml` 默认端口映射是：

- MySQL: `3307 -> 3306`
- Redis: `6380 -> 6379`
- MinIO API: `9000`
- MinIO Console: `9001`
- SRS RTMP: `1935`
- SRS HTTP API: `1985`
- SRS HTTP-FLV: `18080`
- coturn STUN/TURN: `3478/tcp + 3478/udp`
- coturn relay UDP ports: `49160-49200/udp`

如果 Go API 进程跑在宿主机或虚拟机系统里，连接 Docker 容器时直接走这些宿主机端口即可。
`coturn` 当前使用 `host network` 模式运行，所以要确保虚拟机防火墙放行：

- `3478/tcp`
- `3478/udp`
- `49160-49200/udp`

当前仓库已经补了最小 `coturn` 配置文件：

- `../ops/coturn/turnserver.conf`

当前阶段 0 默认测试凭据是：

- username: `probe`
- password: `probe_turn_123`
- cli password: `probe_cli_123`

说明：

- 这组凭据只适合内网联调和阶段 0 验证
- 如果后面要对公网开放，必须先改掉这组静态密码
- 如果你的虚拟机在公网 NAT 后面，再额外给 `turnserver.conf` 增加 `external-ip=<public-ip>/<private-ip>`
- 如果虚拟机 IP 不是 `192.168.3.28`，先同步修改 `../ops/coturn/turnserver.conf` 里的 `relay-ip` 和 `external-ip`

可以用下面的命令确认 `coturn` 已经启动：

```bash
cd /path/to/duanshipin-pingtai
docker compose ps coturn
docker compose logs --tail=50 coturn
```

## 3. Go 依赖准备

进入 `server/` 目录：

```bash
cd /path/to/duanshipin-pingtai/server
go mod tidy
```

这一步会：

- 拉取 `mysql` 驱动
- 拉取 `minio` SDK
- 拉取 `gorilla/websocket`
- 生成或更新 `go.sum`

## 4. 编译命令

推荐先编译，再启动：

```bash
cd /path/to/duanshipin-pingtai/server
mkdir -p bin
go build -o ./bin/vod-platform-api ./cmd/api
go build -o ./bin/vod-platform-worker ./cmd/worker
go build -o ./bin/vod-platform-importer ./cmd/importer
```

当前最小 probe 联调只强依赖：

- `./bin/vod-platform-api`

`worker` 和 `importer` 不是 `/ws` 最小信令联调的前置条件。

## 5. 启动前环境变量

如果 Go API 进程跑在宿主机或虚拟机系统里，并连接 Docker 映射端口，可以先设置：

```bash
export HTTP_ADDR=:8080
export PUBLIC_BASE_URL=http://<vm-ip>:8080
export PUBLIC_SIGNALING_URL=ws://<vm-ip>:8080/ws
export MYSQL_DSN='vod_user:vod_pass_123@tcp(127.0.0.1:3307)/vod_platform?charset=utf8mb4&parseTime=True&loc=Local'
export MINIO_ENDPOINT=127.0.0.1:9000
export MINIO_ACCESS_KEY=minioadmin
export MINIO_SECRET_KEY=minioadmin123
export MINIO_USE_SSL=false
export MINIO_RAW_BUCKET=raw-media
export MINIO_VOD_BUCKET=vod-media
export MINIO_IMAGE_BUCKET=image-assets
```

说明：

- `PUBLIC_BASE_URL` 要换成虚拟机真实可访问地址
- `PUBLIC_SIGNALING_URL` 用于 `rtcJoinParams.signalingUrl`，建议明确写成 `ws://<vm-ip>:8080/ws`
- 当前最小 `/ws` 联调不依赖 Redis，所以这里先不用额外设置 Redis 环境变量
- 如果后面把 Go API 也容器化，再把 `127.0.0.1:3307` 这类地址改成 Docker network 里的服务名，例如 `mysql:3306`、`minio:9000`

## 6. 启动命令

编译后启动：

```bash
cd /path/to/duanshipin-pingtai/server
./bin/vod-platform-api
```

也可以直接运行源码：

```bash
cd /path/to/duanshipin-pingtai/server
go run ./cmd/api
```

正常情况下应看到类似日志：

```text
api listening on :8080
```

## 7. 启动后自检

先检查健康接口：

```bash
curl http://127.0.0.1:8080/healthz
```

期望返回：

```json
{
  "status": "ok"
}
```

`/ws` 是 WebSocket 接口，不适合直接用普通 `curl` 做功能验证。最简单的验证方式就是直接跑 `rtc_probe` 和 `rtc_guest_probe`。

## 8. Windows Probe 编译命令

在 `duanshipin-live-studio` 仓库根目录执行：

```powershell
cd D:\itffmpeg\av_media\online\duanshipin-live-studio
powershell -ExecutionPolicy Bypass -File .\scripts\configure_rtc_probes.ps1 -Build
```

这条命令已经在当前机器上实际执行通过。

说明：

- 当前机器里的 `third_party/libdatachannel-install` 已经存在
- `configure_rtc_probes.ps1 -Build` 会直接完成 probe 配置、编译，并自动复制 OpenSSL 运行时 DLL
- 只有在你换了一台新机器、或者本地还没有 `libdatachannel-install` 时，才需要额外先跑一次 `setup_libdatachannel.ps1`

编译完成后，探针输出目录通常是：

- `build-rtc\apps\rtc_probe\rtc_probe.exe`
- `build-rtc\apps\rtc_guest_probe\rtc_guest_probe.exe`

## 9. Windows Probe 联调命令

假设：

- VM IP 是 `192.168.3.28`
- Go API 监听 `:8080`
- `coturn` 已通过 `docker compose up -d coturn` 启动
- 当前阶段 0 的静态 TURN 凭据仍为 `probe / probe_turn_123`
- 本轮需要确认是否真的走了 TURN relay，因此两个 probe 都带 `--relay-only`

可以先开两个终端。

终端 1：

```powershell
cd D:\itffmpeg\av_media\online\duanshipin-live-studio
.\build-rtc\apps\rtc_guest_probe\rtc_guest_probe.exe `
  --signaling-url ws://192.168.3.28:8080/ws `
  --room-id stage0 `
  --ice-server turn://probe:probe_turn_123@192.168.3.28:3478?transport=udp `
  --relay-only `
  --timeout-ms 30000
```

终端 2：

```powershell
cd D:\itffmpeg\av_media\online\duanshipin-live-studio
.\build-rtc\apps\rtc_probe\rtc_probe.exe `
  --signaling-url ws://192.168.3.28:8080/ws `
  --room-id stage0 `
  --ice-server turn://probe:probe_turn_123@192.168.3.28:3478?transport=udp `
  --relay-only `
  --timeout-ms 30000
```

如果你只想验证 STUN，也可以临时换成：

```powershell
--ice-server stun:192.168.3.28:3478
```

但如果你要确认真正的 TURN 中继可用，还是应该优先使用带用户名密码的 `turn://` 形式，并同时带上 `--relay-only`。

启动 `coturn` 后可以先在服务器上确认容器状态：

```bash
cd /path/to/duanshipin-pingtai
docker compose ps coturn
```

## 10. 阶段 1 Signal Probe 联调命令

阶段 1 业务信令模式对应 `duanshipin-live-studio/docs/guest_link_mic_stage1_signal_probe.md`。

### 邀请流

先启动嘉宾端：

```powershell
cd D:\itffmpeg\av_media\online\duanshipin-live-studio
.\build-rtc\apps\rtc_guest_probe\rtc_guest_probe.exe `
  --mode signal `
  --action accept `
  --signaling-url ws://192.168.3.28:8080/ws `
  --room-id stage1 `
  --timeout-ms 30000 `
  --request-timeout-ms 10000
```

再启动主播端：

```powershell
cd D:\itffmpeg\av_media\online\duanshipin-live-studio
.\build-rtc\apps\rtc_probe\rtc_probe.exe `
  --mode signal `
  --action invite `
  --signaling-url ws://192.168.3.28:8080/ws `
  --room-id stage1 `
  --timeout-ms 30000 `
  --request-timeout-ms 10000 `
  --auto-hangup-ms 2000
```

### 申请流

先启动主播端：

```powershell
cd D:\itffmpeg\av_media\online\duanshipin-live-studio
.\build-rtc\apps\rtc_probe\rtc_probe.exe `
  --mode signal `
  --action accept `
  --signaling-url ws://192.168.3.28:8080/ws `
  --room-id stage1 `
  --timeout-ms 30000 `
  --request-timeout-ms 10000 `
  --auto-hangup-ms 2000
```

再启动嘉宾端：

```powershell
cd D:\itffmpeg\av_media\online\duanshipin-live-studio
.\build-rtc\apps\rtc_guest_probe\rtc_guest_probe.exe `
  --mode signal `
  --action apply `
  --signaling-url ws://192.168.3.28:8080/ws `
  --room-id stage1 `
  --timeout-ms 30000 `
  --request-timeout-ms 10000
```

当前服务端会在 `accept` 后自动向双方下发：

- `rtc.join-params`
- `linkmic.connected`
- `linkmic.state-sync`

它们目前是为了阶段 1 探针联调提供的最小占位实现，后续再接正式业务参数。

## 11. 联调成功的最小标志

服务端应能看到类似日志：

- `probe signaling connected`
- `probe signaling registered: room_id=stage0 peer_id=controller-probe role=controller`
- `probe signaling registered: room_id=stage0 peer_id=guest-probe role=participant`
- `probe signaling handled: room_id=stage1 type=linkmic.invite ...`
- `probe signaling handled: room_id=stage1 type=linkmic.accept ...`

客户端应能看到类似日志：

- `registered as controller-probe`
- `registered as guest-probe`
- `creating offer`
- `data channel open`
- `event=send.linkmic.invite`
- `event=recv.rtc.join-params`
- `event=send.linkmic.hangup`

如果你这一轮是带 `turn://probe:probe_turn_123@<vm-ip>:3478?transport=udp` 和 `--relay-only` 跑的，就说明：

- Go `/ws` 信令打通了
- `coturn` 已经真正提供了可用的 relay candidate
- 当前网络条件下，RTC 建连在 relay-only 模式下仍然可以完成

如果你这一轮是 `--mode signal` 跑的，就说明：

- 在线成员列表同步打通了
- `linkmic.apply / invite / accept / reject / hangup` 的最小业务闭环打通了
- 服务端可以在接受后给双方下发同一个 `request_id` 下的后续状态消息

## 12. 当前限制

这版 `/ws` 仍然是最小 probe 信令，不包含：

- `/ws` 完整登录鉴权
- `/ws` 业务信令状态持久化
- Redis 在线状态同步
- 服务端侧邀请超时回收
- 多实例广播
- 真正按业务生成 RTC 房间/票据
- App/前端 侧还未切到新的 `/api/live-rooms/*` 正式接口

如果后面开始做正式连麦业务，再在现有 Go 后端上继续扩展这些能力。

## 13. Stage 1 Formal Live-Room APIs

The stage 1 server now exposes DB-backed business APIs under `/api/live-rooms`.
They are separate from the probe-only `/ws` flow.

All live-room APIs require:

- `Authorization: Bearer <accessToken>`

Current endpoints:

- `POST /api/live-rooms`
- `GET /api/live-rooms`
- `GET /api/live-rooms/{roomKey}`
- `GET /api/live-rooms/{roomKey}/members`
- `POST /api/live-rooms/{roomKey}/presence`
- `POST /api/live-rooms/{roomKey}/linkmic/apply`
- `POST /api/live-rooms/{roomKey}/linkmic/invite`
- `POST /api/live-rooms/{roomKey}/linkmic/respond`
- `POST /api/live-rooms/{roomKey}/linkmic/cancel`
- `POST /api/live-rooms/{roomKey}/linkmic/hangup`
- `GET /api/live-rooms/{roomKey}/linkmic/state`
- `POST /api/live-rooms/{roomKey}/close`

Business rules enforced by the API:

- one active live room per owner
- one pending or accepted linkmic request per room
- invite target must already be online in the room
- only the request target can accept or reject
- only the request initiator can cancel
- only active participants can hang up
- closing a room marks members offline and closes the active request

Persistence:

- `live_rooms`
- `live_room_presences`
- `linkmic_requests`

Realtime updates:

- `GET /api/events` publishes `linkmic.room.updated`
- SSE events carry room/request snapshots
- `rtcJoinParams` are returned only by the direct HTTP response of `linkmic/respond` and `linkmic/state`
- `rtcJoinParams.signalingUrl` now comes from `PUBLIC_SIGNALING_URL`
- if `PUBLIC_SIGNALING_URL` is unset, the server derives it from `PUBLIC_BASE_URL` as `http -> ws` / `https -> wss`

Quick example flow:

```bash
curl -X POST http://127.0.0.1:8080/api/live-rooms \
  -H "Authorization: Bearer <token>" \
  -H "Content-Type: application/json" \
  -d '{"roomKey":"stage1-room","title":"Stage 1 Room"}'

curl -X POST http://127.0.0.1:8080/api/live-rooms/stage1-room/presence \
  -H "Authorization: Bearer <token>" \
  -H "Content-Type: application/json" \
  -d '{"action":"join"}'

curl -X POST http://127.0.0.1:8080/api/live-rooms/stage1-room/linkmic/apply \
  -H "Authorization: Bearer <token>" \
  -H "Content-Type: application/json" \
  -d '{"note":"request linkmic"}'
```
