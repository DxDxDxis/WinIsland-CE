# WinIsland 1.3.3exp-winUI（实验性） 社区版

本目录基于旧 WinUI 实验工程，按差异移植 1.3.3beta 的复制粘贴功能，保留 WinUI 设置框架。

运行 `release/WinIsland-1.3.3exp-winUI（实验性）.exe`。分发时只需这个 EXE；首次运行会把内嵌 WinUI 运行组件安装到统一数据目录。请先退出旧宿主，避免单实例逻辑把“打开设置”请求交给仍运行的旧版本。

设置打不开的问题已修复：恢复打包 WinUI 的 resources.pri 主题索引，并通过独立安装后的真实窗口验证。

- 构建：PowerShell 7 执行 `./build-release.ps1 -BuildTools E:/WinIslandBuildTools`。
- 移植与回退说明：docs/1.3.3exp-winUI-migration.md。
- 真实验证与未验证项：docs/1.3.3exp-winUI-verification.md。
- 中文 TXT：开发文档/本次移植与设置启动修复.txt、验证报告.txt。
- 产物哈希：verification/release-hashes.json。

仍为实验版本；未接入真实翻译服务，多显示器、其他 DPI 和其他电脑仍需验证。
