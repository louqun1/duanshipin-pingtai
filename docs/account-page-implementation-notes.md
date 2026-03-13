# 统一账户页实现笔记

## 1. 文档目的

这份文档记录第二阶段“用户系统”是怎么一步一步落地的，目标是帮助你理解：

- 为什么把登录、注册、登出、基础个人展示放到一个页面
- 前端和后端分别承担什么职责
- 我是按什么顺序把功能接起来的
- 你后面可以沿着什么方向继续扩展

对应的实现主要分布在这些文件：

- [MainWindow.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/widgets/MainWindow.cpp)
- [AccountPage.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/pages/AccountPage/AccountPage.cpp)
- [AuthController.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/controller/auth/AuthController.cpp)
- [AuthService.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/service/auth/AuthService.cpp)
- [AppBootstrap.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/app/bootstrap/AppBootstrap.cpp)

## 2. 先解决的核心问题

在第二阶段开始前，项目里虽然已经有骨架，但账户相关功能还是分散的：

- 一个 `LoginPage`
- 一个 `ProfilePage`
- 一个空的认证控制器骨架

这会带来几个问题：

- 用户路径不自然：进入“我的”以后，还要在登录页和个人页之间切
- 页面职责不够稳定：后面很容易又把登录状态判断塞回 `MainWindow`
- 第二阶段目标太分散：同时维护两个页面，成本更高

所以这次先做了一个设计收敛：

- 保留一个统一的 `AccountPage`
- 未登录时显示登录 / 注册表单
- 已登录时显示个人资料和登出按钮
- 登录成功后自动切到已登录态

这一步是产品交互上的简化，不是架构上的退步。

## 3. 总体设计思路

整个功能是按“页面统一、业务分层”这个原则实现的。

也就是说：

- 页面上看起来是一个账户页
- 但认证逻辑仍然不写在页面里
- 页面只负责发信号、切换显示状态、展示结果
- 认证仍然走 `AuthController -> AuthService -> UserRepository`

这点很重要，因为它决定了以后你要换成 SQLite、HTTP 接口、JWT、记住登录状态时，不需要重写整个页面。

## 4. 第一步：先把账户功能统一到一个页面

先新增了 `AccountPage`：

- [AccountPage.hpp](/d:/itffmpeg/av_media/online/duanshipinpingtai/frontend/pages/AccountPage/AccountPage.hpp)
- [AccountPage.cpp](/d:/itffmpeg/av_media/online/duanshipinpingtai/frontend/pages/AccountPage/AccountPage.cpp)

这个页面内部没有直接做登录逻辑，而是先解决“一个页面如何同时展示两种状态”。

我选的做法是 `QStackedWidget`。

原因很简单：

- 它适合做“未登录态 / 已登录态”切换
- 不需要频繁创建销毁控件
- 页面结构清晰，后续加“编辑资料态”也方便

对应代码在：

- `stateStack_` 的创建：[AccountPage.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/pages/AccountPage/AccountPage.cpp#L31)
- 未登录面板构建： [AccountPage.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/pages/AccountPage/AccountPage.cpp#L39)
- 已登录面板构建： [AccountPage.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/pages/AccountPage/AccountPage.cpp#L112)

## 5. 第二步：只让页面负责“发出信号”

在未登录态里，页面负责收集三类输入：

- 用户名
- 密码
- 邮箱

然后页面只发出三种意图：

- `loginRequested(username, password)`
- `registerRequested(username, password, email)`
- `logoutRequested()`

这些都定义在：

- [AccountPage.hpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/pages/AccountPage/AccountPage.hpp#L19)

点击按钮时，页面只是 `emit` 这些信号：

- 登录信号发出位置：[AccountPage.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/pages/AccountPage/AccountPage.cpp#L98)
- 注册信号发出位置：[AccountPage.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/pages/AccountPage/AccountPage.cpp#L102)
- 登出信号发出位置：[AccountPage.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/pages/AccountPage/AccountPage.cpp#L154)

为什么只发信号，不直接调服务？

因为页面不应该知道：

- 数据从哪来
- 用户怎么校验
- 登录失败为什么失败
- 注册时是否允许重复用户名

这些属于业务层，不属于页面层。

## 6. 第三步：页面只管理“显示状态”

统一账户页要成立，关键不是把所有逻辑塞进去，而是只让它管理“显示状态”。

所以我专门给 `AccountPage` 做了两个公开方法：

- `showLoggedOutState(...)`
- `showAuthenticatedState(...)`

定义在：

- [AccountPage.hpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/pages/AccountPage/AccountPage.hpp#L16)

这两个函数只做三件事：

1. 切换 `QStackedWidget` 当前页
2. 更新页面上的提示文字
3. 更新个人信息显示内容

具体代码在：

- 未登录态切换：[AccountPage.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/pages/AccountPage/AccountPage.cpp#L159)
- 已登录态切换：[AccountPage.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/pages/AccountPage/AccountPage.cpp#L179)

这里的设计重点是：

- 页面可以决定“显示哪个子界面”
- 页面不能决定“用户是否真的登录成功”

## 7. 第四步：让控制器返回足够的展示数据

原来的 `AuthController` 成功信号只返回用户名，不够做基础个人展示。

所以我先调整了控制器输出，让它成功时把：

- `username`
- `email`

一起返回。

定义在：

- [AuthController.hpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/controller/auth/AuthController.hpp#L20)

实现中，控制器从 `AuthService` 拿到 `AuthResult`，如果成功，就把结果中的用户信息通过信号发给前端：

- 登录成功发信号：[AuthController.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/controller/auth/AuthController.cpp#L11)
- 注册成功发信号：[AuthController.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/controller/auth/AuthController.cpp#L22)

这一步为什么重要：

- 页面不需要反查用户资料
- `MainWindow` 不需要自己拼用户信息
- 后面把字段从 `username/email` 扩展到头像、简介时，只需要沿着这条链路继续补

## 8. 第五步：业务逻辑仍然留在 Service

虽然这次页面统一了，但认证规则没有回到 UI。

业务逻辑仍然在：

- [AuthService.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/service/auth/AuthService.cpp)

目前 `AuthService` 负责：

- 校验输入是否为空
- 校验用户名是否已存在
- 调用 `UserRepository`
- 组装 `AuthResult`

比如：

- 登录逻辑：[AuthService.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/service/auth/AuthService.cpp#L10)
- 注册逻辑：[AuthService.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/service/auth/AuthService.cpp#L35)

换句话说：

- 页面负责“我要登录”
- 控制器负责“把请求转给业务层”
- 服务负责“判断这个登录是否成立”

## 9. 第六步：MainWindow 只负责接线和切页

`MainWindow` 现在承担的是“壳”的职责。

这次对它做了两类改动：

### 9.1 页面结构改动

原来是：

- `Home`
- `Stream`
- `Upload`
- `Profile`
- `Login`

现在改成：

- `Home`
- `Stream`
- `Upload`
- `Account`

这个变化在：

- [MainWindow.hpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/widgets/MainWindow.hpp#L28)
- [MainWindow.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/widgets/MainWindow.cpp#L44)

### 9.2 信号接线改动

`MainWindow` 做的不是认证，而是“把页面和控制器连起来”。

集中在这个函数里：

- [MainWindow.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/widgets/MainWindow.cpp#L124)

这个函数里干了四件事：

1. `AccountPage -> AuthController`
2. `AuthController 成功 -> MainWindow`
3. `AuthController 失败 -> AccountPage`
4. `logoutRequested -> 切回未登录态`

这一步是整个闭环的关键，因为它把前后端层真正串了起来。

## 10. 第七步：登录成功后自动进入个人资料展示

这是你特别关心的交互。

我没有把这段逻辑直接写在页面按钮回调里，而是写成一个统一入口：

- [MainWindow.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/widgets/MainWindow.cpp#L170)

`showAuthenticatedAccount(...)` 做两件事：

1. 让 `AccountPage` 切到已登录态
2. 让主窗口切到 `Account` 页面

这样带来的好处是：

- 登录成功时可以复用
- 注册成功时也可以复用
- 以后“记住登录状态自动恢复”也可以复用

## 11. 第八步：通过 Bootstrap 注入依赖

为了不让 `MainWindow` 自己 new 一堆业务对象，项目里保留了启动装配层：

- [AppBootstrap.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/app/bootstrap/AppBootstrap.cpp)

这里的顺序是：

1. 创建 `UserRepository`
2. 创建 `AuthService`
3. 创建 `AuthController`
4. 把 `AuthController` 注入 `MainWindow`

这样写的目的不是“多一层绕”，而是让依赖关系稳定：

- `MainWindow` 只依赖控制器
- 控制器只依赖服务
- 服务只依赖仓储接口

这就是分层的价值。

## 12. 最终信号流转图

你可以把当前这套实现理解成下面这条链：

1. 用户在 `AccountPage` 输入账号密码
2. `AccountPage` 发出 `loginRequested`
3. `MainWindow` 把这个信号转给 `AuthController`
4. `AuthController` 调用 `AuthService`
5. `AuthService` 调用 `UserRepository`
6. `AuthController` 发回成功或失败信号
7. `MainWindow` 根据结果更新 `AccountPage`
8. 登录成功时，`AccountPage` 显示个人资料态

这个过程里没有任何一步让页面直接访问数据库。

## 13. 为什么这样实现适合学习

这套实现比较适合学习，因为它同时包含了三层内容：

- UI 组织方式：一个页面里切换两种展示状态
- Qt 事件方式：信号槽如何把页面和控制器串起来
- 架构方式：UI、控制器、服务、仓储如何分工

你可以按下面顺序阅读源码：

1. 先看 [AccountPage.hpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/pages/AccountPage/AccountPage.hpp)
2. 再看 [AccountPage.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/pages/AccountPage/AccountPage.cpp)
3. 再看 [MainWindow.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/frontend/widgets/MainWindow.cpp)
4. 再看 [AuthController.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/controller/auth/AuthController.cpp)
5. 最后看 [AuthService.cpp](/d:/itffmpeg/av_media/online/duanshipin-pingtai/backend/service/auth/AuthService.cpp)

这个顺序比较符合“从界面现象到业务本质”的理解方式。

## 14. 当前实现的边界

现在这版是第二阶段的基础闭环，不是最终版。

已经完成的：

- 登录
- 注册
- 登出
- 账户页双状态切换
- 登录成功自动进入个人资料展示

还没做的：

- 会话持久化
- 真正的数据库用户表
- 头像、简介、编辑资料
- 登录态全局管理
- 权限控制

所以你现在看到的是一个“结构对、流程通”的版本，适合作为后续扩展的起点。

## 15. 后面怎么继续学

如果你想继续沿着这套代码学习，建议按下面顺序练习：

1. 先自己在 `AccountPage` 里加一个“昵称”展示字段
2. 再把 `AuthController` 的成功信号扩成更多资料字段
3. 再把 `InMemoryUserRepository` 换成真正的 SQLite 版本
4. 最后再做“程序启动时恢复登录状态”

这样你会把“页面改动、控制器改动、服务改动、仓储改动”整条链都走一遍。
