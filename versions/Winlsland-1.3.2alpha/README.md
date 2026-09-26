# 1.3.2alpha

[返回项目总览](../../README.md)

## 归档身份

- 正式版本：`1.3.2alpha`
- 产品构建标识：`1.3.2alpha-open-runtime-20260918`
- 来源目录：`1.3.2alpha`
- 本目录是从只读 release 源目录复制的独立开源归档；没有修改原工程。
- 版本目录名保留 `Winlsland-` 归档前缀，产品和可执行文件名称仍为 `WinIsland`。

## 内容

- `project/`：源码、SDK、设置客户端、示例、构建脚本和开发文档。
- `dist/`：对应 release 目录中的可运行构建产物；运行时文件保持相对目录。
- `catalog/`：根目录维护来源、文件计数和哈希记录。

## 安装与运行

从 `dist/` 解压并运行 `WinIsland-1.3.4.exe`。请保留同目录的 `settings/`、许可证和其他运行时文件；首次运行按该版本引导确认统一数据目录。不要将不同版本的 DLL、插件或用户配置混用。

## 构建与兼容

源码中的构建入口和原始说明按字节保留。1.3.2alpha/1.3.3beta/1.3.4 的插件 ABI、设置宿主和运行依赖以各自 manifest、SDK 与 README 为准；产品版号升级不自动改变 ABI。此次归档没有在新环境重新编译或宣称所有历史功能已通过完整回归。

## 主程序哈希

| 文件 | 字节 | SHA-256 | PE 链接时间 |
|---|---:|---|---|
| `WinIsland-1.3.4.exe` | 26047488 | `23FE7DBD9FBE18D4FE4BA143A247BA52DE6851882366DABFD9CD7092F3192477` | `2026-09-18T12:42:55+08:00` |
| `DeploymentAgent.exe` | 108080 | `E776F0DBCCBF645DD8F016276E90FB92C4F2BCAC23B98904D215A2E93967F418` | `2026-09-18T12:42:55+08:00` |
| `RestartAgent.exe` | 78368 | `D7A4A3FE4E470A727FAA6B32FDFB37CC6D39DA5424DF64531773E701D5F038D6` | `2026-09-18T12:42:55+08:00` |
| `WinIslandSettings.exe` | 850432 | `8FD6D53AD02351B2E5FCC7E431AB9DCC5E3B21FB2B31D1997DEA9AD635E1CC7F` | `2026-09-18T12:42:55+08:00` |
| `WinIsland-1.3.3beta.exe` | 170280960 | `0AD3EC5F6D360520717D169AC6B12B856447D0A2162CF4BB4B9468D828304E13` | `2026-09-18T12:42:55+08:00` |
| `WinIslandSettings.exe` | 246070784 | `7E43BA73A9CBA4FF845D2B8341F38280E7F33F57384F2F795478FBCCF956A589` | `2026-09-18T12:42:55+08:00` |
| `WinIsland-1.3.2alpha.exe` | 170169344 | `F059F6A68A41F06AB67E9AAD0E5859F9E453A1BD5FEFF261229FA45769397176` | `2026-09-18T12:42:55+08:00` |
| `WinIslandSettings.exe` | 246070784 | `A6764CBAB640AC62A1E61A88BCC5F4DBB4C90CC9E67A9603BF4997ADA5076C12` | `2026-09-18T12:42:55+08:00` |

## 许可与联系

项目自身采用 BSD-3-Clause；第三方组件继续适用其原许可，见根目录 `THIRD_PARTY_NOTICES.md` 和 `licenses/`。联系：725513212@qq.com。
