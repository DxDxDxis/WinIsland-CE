# Winlsland-1.2.3beta_2

[返回项目总览](../../README.md)

## 归档身份

- 正式版本：`1.2.3beta_2`
- 源码状态：已收录所见源码；未重新构建证明与所有成品逐字节对应
- 筛选说明：当前 core.h 为 1.2.3beta_2；只选择本版最新成品。
- 来源路径和内部构建标识详见根目录 catalog；内部修复后缀不属于正式版号。
- 文件数：46，大小约 1.06 MiB。

- 所选 EXE 链接构建时间：`2026-09-06T19:41:22+08:00`。

## 源码与构建

原源码位于 `project/`（如存在），按原字节复制，未为统一版本名称修改源码。 构建入口：`project/build.ps1`。

本轮只归档，不重新编译、不批量运行历史 EXE；不宣称所有历史版本都能在当前环境重建。具体依赖、历史验证限制请读 project 内原 README 和 docs；源代码版权按根 LICENSE，第三方按各自许可。

## 安装与运行

GitHub 用户请从[本版本发行页](https://github.com/DxDxDxis/WinIsland-CE/releases/tag/v1.2.3beta_2)下载运行包。`dist/` 仅保存在本地完整归档，未放入 Git 源码仓库；下方成品表列出 ZIP 内 EXE 的原始哈希。

优先阅读原发布说明。`dist/` 保存该版本成品；多文件发行必须保留歌词服务、DLL、许可证等相对目录，不能只取主 EXE。较新内嵌发行版在首次启动确认数据路径。旧版本可能使用 `%LOCALAPPDATA%/WinIsland`；测试旧版前先备份日常配置，不同时运行多个历史版本。

## 成品清单

| 文件 | MiB | SHA-256 |
|---|---:|---|
| [dist/WinIsland-1.2.3beta_2.exe](https://github.com/DxDxDxis/WinIsland-CE/releases/tag/v1.2.3beta_2) | 0.71 | `0D9F084666198DA4EF09C4D1884F12B61F94E18D763F9DA66A3D9218C23E396E` |

## 身份证据

```text
src/core.h
inline constexpr wchar_t Version[] = L"1.2.3beta_2";
```

## 目录、配置与联系

- `project/`：源码、资源、测试脚本、锁文件和原文档；可能包含本地构建所需的运行依赖副本。
- `dist/`：已存在的发行文件，不代表本轮重新验证过。
- `historical-*`、`archives/`：仅在发现时保留的历史素材。
- 配置字段随版本变化，以本版本原说明为准；不要跨版本直接覆盖配置。
- 许可证：根目录 BSD-3-Clause；第三方声明优先适用于对应第三方文件。
- 联系：725513212@qq.com。

## 原始说明入口

- [README.md](project/README.md)（原文保留，内部修复后缀不是正式版号）。

主 EXE 链接时间：`2026-09-06T19:41:22+08:00`。本轮只核对归档字节，不代表重新运行或编译过此历史版本。
