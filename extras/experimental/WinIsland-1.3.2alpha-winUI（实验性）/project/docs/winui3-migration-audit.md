# WinUI 3 迁移审计（进行中）

实验版本：`1.3.2alpha-winUI（实验性）`。工作目录：`WinIsland-1.3.2alpha-winUI（实验性）`。
仅以只读的 `../1.3.2alpha` 为基线。需求中出现的 1.3.1alpha 和 WinIsland2 名称不作为本次版本依据；按用户最新命名执行。

## 基线与回退

原目录无 Git 仓库。`verification/mainline-sha256.json` 保存原始 4264 个文件的完整 SHA-256；总计 2702461829 字节。
全部复制后逐个校验通过，见 `verification/copy-integrity.json`（其中 target 是重命名前的历史路径）。原目录和实验目录均保留全部旧构建、SDK、资源及文档。
原宿主 BuildId：`1.3.2alpha-open-runtime-20260918`。原 EXE：170169344 字节，SHA-256：`F059F6A68A41F06AB67E9AAD0E5859F9E453A1BD5FEFF261229FA45769397176`。
继承的生成脚本可能含原工程绝对路径，不能直接执行；必须重新生成实验分支构建脚本。

## 已检查的实现与迁移边界

| 实现 | 结论 |
| --- | --- |
| source/src/app.cpp、core.h、settings_host.inc | C++20 宿主，设置为独立子进程。仅更换启动客户端和实验版本标识，灵动岛、媒体、通知业务保留。 |
| settings-electron/src/main.ts、preload.ts、renderer.ts、protocol.ts、transport.ts | Electron main/preload 受限桥接，令牌命名管道；界面及进程桥接须用原生 WinUI 重写，不能嵌套网页。 |
| settings-electron/src/plugins.ts、transfer.ts、transfer-main.ts、motion.ts、style.css | 搜索、详情、草稿、操作状态、页面转场须迁移。当前条目详情已取代内容预览；需求中安全内容展示为额外差异，未实现前不能声称已具备。 |
| source/src/settings_bridge.cpp/.h | 保留 protocol 1、当前用户 DACL、拒绝远程连接、随机令牌、命令白名单、请求长度与超时。 |
| source/src/embedded_components.cpp、installation_ui.inc、core.cpp | 固定定位记录、首次安装选盘、自定义路径、散列校验、暂存提交及旧路径迁移可复用。隔离测试不得碰日常定位记录。 |
| mod_system、mod_package、mod_dependency、community_runtime、open_runtime、SDK | 插件仅由宿主管理；启用状态、API/ABI、包格式、资源归属和退出协议不变。客户端不能加载 DLL。 |
| source/src/transfer.cpp、transfer_widget.cpp | 保留索引、复制线程、取消、失效、保存副本及屏幕组件；只迁移设置端 UI。主岛和屏幕组件不因 UI 迁移改变。 |
| media、music_sources、notifications、audio、render | 原核心复用。歌词辅助程序依赖 Qt 原生 DLL，并非 Node，不能随 Electron 一起删除。QuickJS 为插件脚本能力，必须保留。 |
| build-release.ps1、source/build.ps1、source/bundle-runtime.ps1 | 旧流程先打包 Electron，再将运行包嵌入宿主。新流程必须改为原生设置产物，并移除 app.asar 前置断言；先验证后排除旧运行时。 |

## 不可改变的协议

管道名 `\\.\pipe\WinIsland.Settings.<PID>`；4 字节小端长度和 UTF-8 JSON；请求上限 64 KiB、响应上限 4 MiB；每连接一请求，客户端读取后发送 1 字节确认。
请求含 protocol、token、command、params。设置写入使用 revision 和 patch，冲突必须重新读取，不能覆盖并发变更。
settings.read/write、capabilities.read、layout.reset、media.read、lyrics.import/clear、diagnostics.action、mods.list/affected/import/action/invoke/folder/log、transfer.call 保留语义。
插件 ID、API、ABI、入口、包、签名、依赖、运行状态和错误必须分别显示。刷新列表不能隐式启用插件。

## 原生实现方案

独立 C++/WinRT + WinUI 3 客户端，SettingsClient、PluginClient、TransferClient 共用后台命名管道传输，统一校验、超时、取消与错误。
UI 使用官方 NavigationView、ToggleSwitch、ComboBox、Expander、InfoBar 和原生文件选择/拖放。有效减少动画可结合系统设置，但不能自动改写宿主保存的手动选项。
只通过公开 IPC 读写业务数据；原生进程不是安全沙箱，不能把架构约束宣传为 OS 权限隔离。

## 工具链与当前验证状态

Windows 11 Pro 26100；MSVC 位于 E:\WinIslandBuildTools；Windows SDK 10.0.22621.0；机器没有 .NET SDK，只有 .NET 8 运行时。
已下载官方 Windows App SDK 1.6.250205002 做原生自包含启动验证。最终 SDK 版本、部署依赖和兼容性仍须以实际构建结果记录。
截至本次审计：完成复制与哈希校验；尚未完成 WinUI 客户端构建和迁移运行验证。继承报告不是本次迁移结果。
