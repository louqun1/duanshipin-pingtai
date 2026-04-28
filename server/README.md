# server

`server/` 是当前平台的 Go 后端工程。

当前最小连麦探针联调约定：

- MySQL、Redis、MinIO、SRS 跑在 `../docker-compose.yml` 对应的 Docker 容器里
- Go API 进程先直接跑在宿主机或虚拟机系统里
- 探针客户端跑在 `duanshipin-live-studio` 一侧，通过 `ws://<vm-ip>:8080/ws` 连到这里
- `/ws` 当前只服务最小 RTC probe 信令联调，不是完整连麦业务实现

## 1. 当前最小能力

当前这版 `/ws` 已支持：

- `probe.register`
- `probe.registered`
- `room.member-list`
- `rtc.offer`
- `rtc.answer`
- `rtc.ice-candidate`
- `signal.error`

它的目标只有一个：

- 让 `rtc_probe.exe` 和 `rtc_guest_probe.exe` 能通过现有 Go 服务完成最小 DataChannel 建连验证

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

## 10. 联调成功的最小标志

服务端应能看到类似日志：

- `probe signaling connected`
- `probe signaling registered: room_id=stage0 peer_id=controller-probe role=controller`
- `probe signaling registered: room_id=stage0 peer_id=guest-probe role=participant`

客户端应能看到类似日志：

- `registered as controller-probe`
- `registered as guest-probe`
- `creating offer`
- `data channel open`

如果你这一轮是带 `turn://probe:probe_turn_123@<vm-ip>:3478?transport=udp` 和 `--relay-only` 跑的，就说明：

- Go `/ws` 信令打通了
- `coturn` 已经真正提供了可用的 relay candidate
- 当前网络条件下，RTC 建连在 relay-only 模式下仍然可以完成

## 11. 当前限制

这版 `/ws` 只是最小 probe 信令，不包含：

- 完整登录鉴权
- 房间持久化
- Redis 在线状态同步
- 邀请超时
- 正式 `linkmic.apply/invite/accept/reject`
- 多实例广播

如果后面开始做正式连麦业务，再在现有 Go 后端上继续扩展这些能力。
