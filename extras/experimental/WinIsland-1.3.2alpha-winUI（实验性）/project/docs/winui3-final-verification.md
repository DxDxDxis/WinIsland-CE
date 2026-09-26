# WinIsland 1.3.2alpha WinUI 3 实验分支最终验证

## 分发文件

推荐分发 `release/WinIsland-1.3.2alpha.exe`。文件大小约 29 MB，SHA-256 为 `B17F6B23FF25ACDC48C1643F22E165436AA267E3FBC5EFE32AFE4B02867FE2FB`。

WinUI 设置客户端、Windows App SDK、资源和歌词组件已经作为资源内嵌到主 EXE。首次运行时释放到统一数据根目录的 `components/runtime/<componentId>`，不需要用户另带 DLL。组件释放后的占用约 53 MB，属于首次安装数据，不是 EXE 体积。

## 已真实验证

- 原始 `1.3.2alpha` 目录未写入；实验分支独立构建。
- 隔离数据目录、中文路径、空格路径、组件释放和 SHA-256 校验通过。
- 首次运行能建立定位记录、安装状态和组件目录。
- 释放目录包含 `WinIslandSettings.exe`、`Microsoft.WindowsAppRuntime.dll`、`Microsoft.ui.xaml.dll`、`resources.pri` 及其语言资源。
- 直接启动释放后的 WinUI 设置客户端通过，启动参数包含命名管道和临时令牌。
- 主程序再次启动时复用定位记录，不从 EXE 目录创建新的数据目录。
- 存储目录测试：`Passed=14 Failed=0`。
- Electron、Chromium、Node.js 不进入最终内嵌 EXE；Electron 源码仍保留在 `settings-electron` 作为回退实现。

## 已实现但未完成独立运行验收

- WinUI 客户端的 `capabilities.read`、`settings.read`、`settings.write` 已通过集中 IPC 客户端实现，命令白名单、令牌、超时和取消逻辑位于 `settings-winui/clients.h`。
- 插件管理、文件中转、媒体和通知页面代码已接入相同 IPC 契约，但本轮没有为每个页面建立完整的人工回归记录。
- 设置写入后的跨进程重启恢复尚未形成独立自动化脚本证据，应在发布前再做一次人工验证。

## 尚未验证或不在本分支范围

- 不同 Windows 版本、不同 DPI、多显示器和无管理员权限下的完整 WinUI 回归。
- 安装包签名、商店打包和真正单文件静态链接。当前是单 EXE 分发、首次释放运行时组件的方案。
- 灵动岛核心行为没有迁移或改动；本分支只替换设置客户端及其启动/释放路径。

## 回退

停止使用实验分支 EXE，直接使用原 `1.3.2alpha` 构建即可回退。实验数据位于用户选择的统一数据根目录，不会覆盖旧版本数据。不要把 `release/WinIsland-1.3.2alpha-winUI（实验性）.exe` 作为最终分发文件；它是早期构建，使用 `release/WinIsland-1.3.2alpha.exe`。
