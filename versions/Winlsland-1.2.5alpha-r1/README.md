# Winlsland-1.2.5alpha-r1

[返回项目总览](../../README.md)

## 归档身份

- 正式版本：`1.2.5alpha-r1`
- 源码状态：已收录所见源码；未重新构建证明与所有成品逐字节对应
- 筛选说明：按主 EXE 的 PE 链接构建时间筛选；仅保留本版最新构建及所见源码。
- 来源路径和内部构建标识详见根目录 catalog；内部修复后缀不属于正式版号。
- 文件数：76，大小约 40.21 MiB。

- 所选 EXE 链接构建时间：`2026-09-12T16:40:12+08:00`。

## 源码与构建

原源码位于 `project/`（如存在），按原字节复制，未为统一版本名称修改源码。 构建入口：`project/source/build.ps1`。

本轮只归档，不重新编译、不批量运行历史 EXE；不宣称所有历史版本都能在当前环境重建。具体依赖、历史验证限制请读 project 内原 README 和 docs；源代码版权按根 LICENSE，第三方按各自许可。

## 安装与运行

优先阅读原发布说明。`dist/` 保存该版本成品；多文件发行必须保留歌词服务、DLL、许可证等相对目录，不能只取主 EXE。较新内嵌发行版在首次启动确认数据路径。旧版本可能使用 `%LOCALAPPDATA%/WinIsland`；测试旧版前先备份日常配置，不同时运行多个历史版本。

## 成品清单

| 文件 | MiB | SHA-256 |
|---|---:|---|
| [dist/WinIsland-1.2.5alpha-r1.exe](dist/WinIsland-1.2.5alpha-r1.exe) | 1.10 | `92D91973E1751C06B47123F29467AE236B9881EAB4E85EE60155696CAFDA2E7E` |
| [dist/lyric-provider/WinIsland-LyricHelper.exe](dist/lyric-provider/WinIsland-LyricHelper.exe) | 0.85 | `7F40754409BD23EFE17EA30330A8A3713C6E4BC5E7D05AF417D6CB80281A4F03` |
| [dist/lyric-provider/WinIsland-LyricHelper.exe](dist/lyric-provider/WinIsland-LyricHelper.exe) | 0.85 | `7F40754409BD23EFE17EA30330A8A3713C6E4BC5E7D05AF417D6CB80281A4F03` |

## 身份证据

```text
source/src/core.h
inline constexpr wchar_t Version[] = L"1.2.5alpha-r1";
inline constexpr char BuildId[] = "1.2.5alpha-r1-adjustable-arc-20260912";
build-manifest.json
"version": "1.2.5alpha-r1",
```

## 目录、配置与联系

- `project/`：源码、资源、测试脚本、锁文件和原文档；可能包含本地构建所需的运行依赖副本。
- `dist/`：已存在的发行文件，不代表本轮重新验证过。
- `historical-*`、`archives/`：仅在发现时保留的历史素材。
- 配置字段随版本变化，以本版本原说明为准；不要跨版本直接覆盖配置。
- 许可证：根目录 BSD-3-Clause；第三方声明优先适用于对应第三方文件。
- 联系：725513212@qq.com。

## 原始说明入口

本版未发现原始 README；以本页已知信息与根安装说明为准。

主 EXE 链接时间：`2026-09-12T16:40:12+08:00`。本轮只核对归档字节，不代表重新运行或编译过此历史版本。
