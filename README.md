# 短视频平台重写说明

## 1. 项目目标

本项目将重写为一个面向桌面端的短视频平台，采用“前后端分离”的工程组织方式：

- `frontend` 负责界面展示、页面导航、用户交互
- `backend` 负责业务逻辑、状态管理、数据访问、媒体能力
- `app` 负责程序启动和模块装配

当前仓库中的旧实现不再继续扩展。后续开发以本 README 作为重写基线，按新的分层架构逐步恢复功能。

## 2. 重写原则

### 2.1 只保留清晰的职责边界

- `MainWindow` 只做主窗口、导航容器、页面切换
- 页面类只负责输入收集和数据展示
- 业务逻辑不进入 UI 类
- 数据访问不直接暴露给 UI

### 2.2 优先做小而稳定的骨架

先搭建可运行、可扩展、可维护的最小工程，再一项项补功能，不从旧代码中直接搬运复杂逻辑。

### 2.3 为后续扩展预留空间

项目未来会逐步接入以下能力：

- 用户登录 / 注册 / 登出
- 账户中心
- 视频流展示
- 上传与发布
- 播放器能力
- 媒体处理
- 网络接口对接

## 3. 推荐目录结构

```text
app/
  main.cpp
  bootstrap/

frontend/
  CMakeLists.txt
  widgets/
    MainWindow.hpp
    MainWindow.cpp
  pages/
    HomePage/
    StreamPage/
    UploadPage/
    AccountPage/
  components/
    NavigationBar/
    VideoCard/
    Common/

backend/
  CMakeLists.txt
  domain/
    user/
    video/
  service/
    auth/
    profile/
    feed/
  repository/
    user/
    video/
  infrastructure/
    database/
    network/
    media/
    player/
  controller/
    auth/
    profile/
```

说明：

- `domain` 定义核心数据模型，不依赖 UI
- `service` 封装业务规则
- `repository` 负责数据读写接口
- `infrastructure` 放具体实现，例如 SQLite、HTTP、FFmpeg、播放器
- `controller` 负责衔接前端信号和后端服务

## 4. 分层架构

### 4.1 Presentation Layer

包括：

- `MainWindow`
- 各业务页面
- 可复用 UI 组件

职责：

- 展示界面
- 收集用户输入
- 发出信号
- 响应控制器返回结果

限制：

- 不直接访问数据库
- 不直接编写认证、校验、上传等业务逻辑

### 4.2 Controller Layer

包括：

- `AuthController`
- `ProfileController`
- 后续的 `FeedController`、`UploadController`

职责：

- 接收页面信号
- 调用对应 Service
- 将结果转成 UI 可消费的信号

### 4.3 Service Layer

包括：

- `AuthService`
- `ProfileService`
- `FeedService`
- `UploadService`

职责：

- 封装业务规则
- 维护业务流程
- 处理校验、权限、状态切换

限制：

- 不依赖具体页面
- 不弹窗、不操作 QWidget

### 4.4 Repository Layer

包括：

- `UserRepository`
- `VideoRepository`

职责：

- 提供统一的数据访问接口
- 隔离数据库和缓存等具体实现

### 4.5 Infrastructure Layer

包括：

- `SQLite` 数据实现
- HTTP / WebSocket 网络实现
- FFmpeg 媒体处理
- 播放器封装

职责：

- 提供具体技术实现
- 被 Repository 或 Service 调用

## 5. MainWindow 的最终职责

`MainWindow` 是后续重写的第一个稳定入口，但它只保留下列职责：

- 创建主布局
- 挂载左侧导航或顶部导航
- 管理页面栈
- 切换页面
- 接收控制器结果并更新当前显示页面

`MainWindow` 不负责：

- 登录鉴权
- 注册校验
- 用户信息查询
- 数据库存取
- 上传流程编排

一句话总结：

`MainWindow` 只做“壳”，不做“业务中心”。

## 6. 页面规划

第一阶段保留以下页面：

- `HomePage`：短视频流首页
- `StreamPage`：直播或流媒体入口页
- `UploadPage`：上传入口页
- `AccountPage`：统一的账户页

`AccountPage` 采用双状态设计：

- 未登录态：显示登录 / 注册表单，提示用户需要先登录
- 已登录态：显示基础个人资料、账户信息、登出按钮

每个页面遵守统一规则：

- 页面只暴露必要信号
- 页面不依赖数据库类
- 页面不保存核心业务状态
- 页面之间不直接互相操控内部逻辑

## 7. 第二阶段账户页方案

第二阶段不再拆成 `LoginPage + ProfilePage` 两个独立页面，而是统一为一个 `AccountPage`。

这样做的原因：

- 用户路径更简单：进入“我的”就是账户中心
- 交互更自然：未登录时提示登录，登录成功后原地切换到个人展示
- 页面更少，更适合当前重写阶段快速落地
- 不影响分层架构，因为业务逻辑仍然放在后端层

需要注意的边界：

- `AccountPage` 可以管理“页面状态”，但不能管理认证业务
- `AccountPage` 可以显示用户资料，但资料来源必须通过 Controller / Service 获取
- `MainWindow` 只负责切到 `AccountPage`，不负责判断账号密码

## 8. 典型交互流程

### 8.1 进入账户页

1. 用户点击导航中的“我的”
2. `MainWindow` 切换到 `AccountPage`
3. 如果当前未登录，则 `AccountPage` 显示未登录态
4. 如果当前已登录，则 `AccountPage` 显示个人资料态

### 8.2 登录流程

1. 用户在 `AccountPage` 的未登录态输入账号密码
2. `AccountPage` 发出 `loginRequested(...)`
3. `AuthController` 接收信号并调用 `AuthService`
4. `AuthService` 通过 `UserRepository` 查询用户并校验
5. `AuthController` 发出成功或失败信号
6. 登录成功后，`AccountPage` 自动切换到个人资料态

### 8.3 注册流程

1. 用户在 `AccountPage` 的未登录态提交注册信息
2. `AccountPage` 发出 `registerRequested(...)`
3. `AuthController` 调用 `AuthService`
4. 注册成功后，系统进入已登录态并显示基础个人资料

### 8.4 登出流程

1. 用户在 `AccountPage` 的已登录态点击登出
2. 页面发出 `logoutRequested()`
3. 控制器或会话管理模块清理当前登录状态
4. `AccountPage` 切回未登录态

### 8.5 上传流程

1. 用户在 `UploadPage` 选择视频
2. 页面发出上传请求信号
3. `UploadController` 调用 `UploadService`
4. `UploadService` 调用媒体处理和网络接口
5. 返回上传状态给前端页面

## 9. 当前阶段的开发策略

### 第一阶段：只搭骨架

目标：

- 清理当前 `MainWindow`
- 建立基础页面结构
- 建立 controller / service / repository 目录
- 保证工程可编译、可运行、可切页

此阶段不追求功能完整，只追求结构稳定。

### 第二阶段：先恢复用户系统

目标：

- 登录
- 注册
- 登出
- 基础个人页展示
- `AccountPage` 双状态切换

第二阶段的完成标准：

- 点击“我的”始终进入 `AccountPage`
- 未登录时，页面明确提示需要登录
- 登录成功后，页面自动切换到个人资料展示
- 登出后，页面恢复到未登录态

### 第三阶段：恢复短视频核心能力

目标：

- 首页视频流
- 视频卡片
- 基础播放器
- 上传入口

### 第四阶段：接入媒体与网络能力

目标：

- FFmpeg
- 网络请求
- 数据持久化
- 发布流程

## 10. 当前工程规范

### 10.1 CMake 组织

顶层：

- `app` 生成可执行程序
- `frontend` 生成前端库
- `backend` 生成后端库

要求：

- 前端库不直接依赖数据库实现细节
- 后端库尽量按模块拆分，避免所有源码继续堆在一个目标里
- 后续优先显式列出源码文件，减少对 `GLOB_RECURSE` 的依赖

### 10.2 编码规范

- 头文件和实现文件命名保持一致
- 公共接口优先放在稳定目录
- Qt 对象由父子关系管理时，不重复引入不必要的所有权复杂度
- 业务结果优先返回明确的结果对象，而不是在 UI 层硬编码字符串分支

## 11. 不再沿用的旧做法

以下做法后续不再继续：

- `MainWindow` 直接调用 `DatabaseManager`
- `MainWindow` 直接维护完整登录流程
- 页面直接访问底层数据
- UI 层保存过多业务状态
- 旧代码中“边写界面边塞业务”的方式

## 12. 里程碑验收标准

### M1：骨架完成

- 工程能启动
- 主窗口能显示
- 页面能切换
- 前后端目录职责清晰

### M2：用户系统完成

- 登录 / 注册 / 登出完整可用
- 账户页能在未登录态和已登录态之间正确切换
- 登录成功后自动进入个人资料展示
- 业务逻辑已从 UI 中抽离

### M3：短视频浏览完成

- 首页可展示视频列表
- 可进入播放

### M4：上传发布完成

- 支持基本上传
- 支持发布流程

## 13. 结论

本项目后续重写遵循一个核心原则：

先搭稳定骨架，再逐步恢复功能；先保证分层合理，再追求功能丰富。

第二阶段采用统一 `AccountPage` 的方案是合理的，但前提是不把认证逻辑重新塞回页面类里。页面可以统一，职责边界不能回退。

后续所有代码调整，优先判断是否符合本 README 的职责边界。如果与本 README 冲突，以本 README 的分层设计为准。
