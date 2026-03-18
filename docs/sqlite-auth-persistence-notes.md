# SQLite 认证持久化实现笔记
## 1. 文档目的

这份文档记录这一次把认证数据从内存仓库切到 SQLite 的实现思路，重点解释三件事：

- 为什么这次改动没有脱离 `README` 里的分层预期
- 用户数据和会话数据分别落在了哪一层
- 登录、注册、登出、启动恢复会话这几条链路现在是怎么串起来的

这次实现主要分布在这些文件里：

- [AppBootstrap.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/app/bootstrap/AppBootstrap.cpp)
- [AuthService.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/service/auth/AuthService.cpp)
- [AuthController.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/controller/auth/AuthController.cpp)
- [SQLiteDatabase.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/infrastructure/database/SQLiteDatabase.cpp)
- [SQLiteUserRepository.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/infrastructure/database/SQLiteUserRepository.cpp)
- [SQLiteSessionRepository.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/infrastructure/database/SQLiteSessionRepository.cpp)
- [001_init_auth.sql](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/infrastructure/database/schema/001_init_auth.sql)

## 2. 先对齐 README 的边界

这次实现先对齐了 `README` 的几个核心要求：

- 页面层不能直接访问数据库
- `MainWindow` 只能做接线和切页
- 数据库存取必须留在 `backend/infrastructure/database`
- 业务规则仍然要通过 `AuthController -> AuthService -> Repository`

对应到代码里，这次没有把 SQLite 连接塞进页面，也没有让 `MainWindow` 去直接查库。

依赖装配仍然只在启动层完成：

- [AppBootstrap.cpp#L29](/d:/itffmpeg/av_media/online/duanshipin-pingtai/app/bootstrap/AppBootstrap.cpp#L29)

页面接线仍然只在主窗口完成：

- [MainWindow.cpp#L124](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/widgets/MainWindow.cpp#L124)

认证逻辑仍然收敛在服务层：

- [AuthService.cpp#L47](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/service/auth/AuthService.cpp#L47)

## 3. 这次加了哪些层

### 3.1 Repository 接口层

为了让会话持久化仍然遵守“先接口、后实现”的约束，这次补了一个新的会话仓库接口：

- [SessionRepository.hpp#L9](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/repository/session/SessionRepository.hpp#L9)

同时给用户仓库接口补了 `findById(...)`，因为恢复会话时拿到的是 `user_id`，不是用户名：

- [UserRepository.hpp#L16](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/repository/user/UserRepository.hpp#L16)

### 3.2 Infrastructure 实现层

SQLite 相关实现全部放在 `backend/infrastructure/database/` 下：

- 连接与建表入口：[SQLiteDatabase.cpp#L76](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/infrastructure/database/SQLiteDatabase.cpp#L76)
- 用户仓库实现：[SQLiteUserRepository.cpp#L17](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/infrastructure/database/SQLiteUserRepository.cpp#L17)
- 会话仓库实现：[SQLiteSessionRepository.cpp#L14](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/infrastructure/database/SQLiteSessionRepository.cpp#L14)

### 3.3 Service 业务层

这次 `AuthService` 不再只依赖用户仓库，而是同时依赖：

- `UserRepository`
- `SessionRepository`

构造函数位置：

- [AuthService.cpp#L39](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/service/auth/AuthService.cpp#L39)

这让下面四条业务链路都还能统一留在 service 层：

- 登录
- 注册
- 启动恢复会话
- 登出清理会话

## 4. 数据库和建表 SQL

这次把建表 SQL 单独保留成了一个文件，方便后面继续演进：

- [001_init_auth.sql](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/infrastructure/database/schema/001_init_auth.sql)

当前表结构只有两张核心表。

### 4.1 users

用户表定义在：

- [001_init_auth.sql#L3](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/infrastructure/database/schema/001_init_auth.sql#L3)

主要字段：

- `id`
- `username`
- `email`
- `avatar_path`
- `password_hash`
- `password_salt`
- `created_at`

这里有一个重要调整：不再把密码明文落库，而是由 `SQLiteUserRepository` 内部做盐值和哈希。

对应代码：

- 盐值生成：[SQLiteUserRepository.cpp#L120](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/infrastructure/database/SQLiteUserRepository.cpp#L120)
- 密码哈希：[SQLiteUserRepository.cpp#L125](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/infrastructure/database/SQLiteUserRepository.cpp#L125)
- 用户保存：[SQLiteUserRepository.cpp#L79](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/infrastructure/database/SQLiteUserRepository.cpp#L79)

### 4.2 sessions

会话表定义在：

- [001_init_auth.sql#L13](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/infrastructure/database/schema/001_init_auth.sql#L13)

主要字段：

- `id`
- `user_id`
- `token_hash`
- `created_at`
- `expires_at`
- `last_seen_at`
- `revoked_at`

当前实现里，登录或注册成功后会写入一个新会话；登出时会把所有未撤销会话统一标记为已撤销。

对应代码：

- 会话写入：[SQLiteSessionRepository.cpp#L14](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/infrastructure/database/SQLiteSessionRepository.cpp#L14)
- 活跃会话读取：[SQLiteSessionRepository.cpp#L37](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/infrastructure/database/SQLiteSessionRepository.cpp#L37)
- 会话撤销：[SQLiteSessionRepository.cpp#L61](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/infrastructure/database/SQLiteSessionRepository.cpp#L61)

## 5. 四条关键业务链路

### 5.1 登录

登录逻辑入口：

- [AuthService.cpp#L47](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/service/auth/AuthService.cpp#L47)

顺序是：

1. 校验输入
2. 校验用户名密码
3. 读取用户
4. 撤销旧会话
5. 创建新会话
6. 返回用户信息给控制器

### 5.2 注册

注册逻辑入口：

- [AuthService.cpp#L89](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/service/auth/AuthService.cpp#L89)

顺序是：

1. 校验输入
2. 检查用户名是否已存在
3. 生成用户 ID
4. 保存用户
5. 创建登录会话
6. 返回用户信息

### 5.3 启动恢复会话

恢复逻辑入口：

- [AuthService.cpp#L137](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/service/auth/AuthService.cpp#L137)

这条链路做的事情是：

1. 读取当前未撤销且未过期的最新会话
2. 根据 `user_id` 找到用户
3. 如果会话失效但用户不存在，就顺手撤销脏会话
4. 返回恢复出的用户给控制器

启动层在窗口创建完成后触发恢复：

- [AppBootstrap.cpp#L47](/d:/itffmpeg/av_media/online/duanshipin-pingtai/app/bootstrap/AppBootstrap.cpp#L47)

主窗口收到恢复信号后，只更新 `AccountPage` 的显示状态，不强制切到“我的”页面：

- [MainWindow.cpp#L141](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/widgets/MainWindow.cpp#L141)

### 5.4 登出

登出逻辑入口：

- [AuthService.cpp#L165](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/service/auth/AuthService.cpp#L165)

控制器增加了 `requestLogout()` 和 `restorePersistedSession()` 两个入口：

- [AuthController.hpp#L16](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/controller/auth/AuthController.hpp#L16)

主窗口不再自己“假装登出”，而是先把请求交给控制器，再根据结果更新页面：

- [MainWindow.cpp#L130](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/widgets/MainWindow.cpp#L130)
- [MainWindow.cpp#L159](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/widgets/MainWindow.cpp#L159)

## 6. 启动装配和数据库位置

数据库文件路径解析在：

- [AppBootstrap.cpp#L17](/d:/itffmpeg/av_media/online/duanshipin-pingtai/app/bootstrap/AppBootstrap.cpp#L17)

当前优先写入 `QStandardPaths::AppDataLocation`，如果拿不到，再退回当前目录下的 `data/`。

SQLite 打开失败时，启动层直接抛出明确错误，不把数据库异常偷偷吞掉：

- [AppBootstrap.cpp#L31](/d:/itffmpeg/av_media/online/duanshipin-pingtai/app/bootstrap/AppBootstrap.cpp#L31)

## 7. CMake 调整

为了让这套实现真正编进工程，这次补了 `Qt6::Sql` 依赖，并把新文件都加入了 `ServoBackend`：

- [backend/CMakeLists.txt#L1](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/CMakeLists.txt#L1)
- [backend/CMakeLists.txt#L27](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/CMakeLists.txt#L27)

## 8. 当前结果

这次落地后，工程已经具备这些能力：

- 用户数据不再只存在内存里
- 应用启动时会自动创建认证相关表
- 登录和注册会写入持久化会话
- 登出会撤销当前会话
- 应用重启后可以恢复上一次有效登录状态

本次本地验证做了两步：

- 重新构建 `QtFrontend`，构建通过
- 启动应用一次，确认 SQLite 数据库文件已经创建

## 9. 后续可以继续做什么

如果继续沿着现在这条线往下走，最自然的后续工作是：

1. 给 `sessions` 增加更细的会话粒度，比如只撤销当前会话而不是全部活跃会话
2. 把 `last_seen_at` 在恢复会话和关键操作后更新起来
3. 给用户表补更多资料字段，然后沿着 `Repository -> Service -> Controller -> AccountPage` 继续透传
4. 在后续接 HTTP 后端时，把 `SessionRepository` 的实现从 SQLite 替换成远端接口，而不是改页面层
