# `IngestLocalFile()` 拆分草稿

目标：
- 不改原有实现文件
- 先把职责边界拆清楚
- 这里只放函数签名、调用顺序、职责说明

刻意留给你手写的 3 个点：
- `buildSourceObjectKey(videoID, safeFilename)` 的理解和命名规则
- `insertVideo()` 之后 `videos.status` 的状态流转
- `insertTranscodeJob()` 和 `markVideoQueued()` 的衔接细节

## 建议拆成 5 个小函数

```go
type localFileRef struct {
    AbsolutePath string
    Size         int64
}

type ingestDraft struct {
    SafeFilename        string
    ResolvedTitle       string
    ResolvedDescription string
    ContentType         string
}
```

```go
func resolveLocalFile(localPath string) (localFileRef, error)

func prepareIngestDraft(
    originalFilename string,
    title string,
    description string,
    file localFileRef,
) ingestDraft

func createUploadedVideoRecord(
    ctx context.Context,
    database *sql.DB,
    uploaderUserID *int64,
    file localFileRef,
    draft ingestDraft,
) (videoID int64, err error)

func uploadRawSourceAndBindObjectKey(
    ctx context.Context,
    database *sql.DB,
    minioStorage *storage.MinIOStorage,
    videoID int64,
    file localFileRef,
    draft ingestDraft,
) (objectKey string, err error)

func queueTranscodeAndMarkQueued(
    ctx context.Context,
    database *sql.DB,
    minioStorage *storage.MinIOStorage,
    videoID int64,
    objectKey string,
) (jobID int64, status string, err error)
```

可选的失败辅助函数：

```go
func failVideoIngest(
    ctx context.Context,
    database *sql.DB,
    videoID int64,
    cause error,
) error
```

职责：

1. `resolveLocalFile`
- 只负责本地文件层面的事情
- `filepath.Abs`
- `os.Stat`
- 拒绝目录
- 输出一个后续流程都能复用的 `localFileRef`

2. `prepareIngestDraft`
- 只负责把输入参数整理成“准备入库/上传”的中间数据
- 规范化文件名
- 决定最终标题
- 清理描述
- 推断 `ContentType`
- 不碰 MySQL，不碰 MinIO

3. `createUploadedVideoRecord`
- 只负责创建 `videos` 记录并返回 `videoID`
- 这里内部再调 `insertVideo(...)`
- 这一步故意不要把状态流转细节写死在草稿里，留给你自己展开

4. `uploadRawSourceAndBindObjectKey`
- 只负责“原片真的上传成功”这一段
- 先算 `objectKey`
- 再传原片到 raw bucket
- 再把真实 `objectKey` 回写到 `videos.source_object_key`
- `buildSourceObjectKey(...)` 的理解这里故意留白，方便你自己手写

5. `queueTranscodeAndMarkQueued`
- 只负责“把后续转码接上”
- 组装转码任务需要的 payload
- 插入 `transcode_jobs`
- 把视频推进到下一状态
- `insertTranscodeJob()` 和 `markVideoQueued()` 之间为什么这么衔接，这里故意不展开

6. `failVideoIngest`（可选）
- 不是主 happy path 的一步
- 只做 best-effort 的失败落库
- 避免在主流程里反复写 `_ = markVideoFailed(...)`

## 建议调用顺序

```go
func IngestLocalFile(
    ctx context.Context,
    database *sql.DB,
    minioStorage *storage.MinIOStorage,
    localPath string,
    originalFilename string,
    title string,
    description string,
    uploaderUserID *int64,
) (Result, error) {
    file, err := resolveLocalFile(localPath)
    if err != nil {
        return Result{}, err
    }

    draft := prepareIngestDraft(originalFilename, title, description, file)

    videoID, err := createUploadedVideoRecord(ctx, database, uploaderUserID, file, draft)
    if err != nil {
        return Result{}, err
    }

    objectKey, err := uploadRawSourceAndBindObjectKey(
        ctx,
        database,
        minioStorage,
        videoID,
        file,
        draft,
    )
    if err != nil {
        _ = failVideoIngest(ctx, database, videoID, err)
        return Result{}, err
    }

    jobID, status, err := queueTranscodeAndMarkQueued(
        ctx,
        database,
        minioStorage,
        videoID,
        objectKey,
    )
    if err != nil {
        _ = failVideoIngest(ctx, database, videoID, err)
        return Result{}, err
    }

    return Result{
        VideoID:   videoID,
        JobID:     jobID,
        Title:     draft.ResolvedTitle,
        ObjectKey: objectKey,
        Status:    status,
    }, nil
}
```

## 你手写时可以重点补的地方

```go
// TODO 1:
// objectKey 为什么要依赖 videoID，再拼 filename

// TODO 2:
// insertVideo() 后，videos.status 应该先是什么
// source_object_key 为什么先放占位值

// TODO 3:
// 为什么 job 先创建，再把 video 标成 queued
// 以及这两个动作失败时各自留下什么状态
```
