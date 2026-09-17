> **1.3.1alpha（2026-09-16）**：新增音乐/消息呈现开关与宿主管理的文件中转。路径、状态、IPC、线程、回退和限制以 [本版说明](1.3.1alpha文件中转与显示开关.md) 和 [验证报告](1.3.1alpha验证报告.md) 为准。插件 API/ABI 未升级。以下旧版内容作为沿革。

# AI交接 — WinIsland 1.2.7alpha-r1

本交付为动态媒体实现，不能误切回设置动画/安装器任务。用户不接受“仅静态首帧”替代GIF/视频。权威工程：E:\aaAAx项目\WinIsland-社区版-大更新\release\1.2.7alpha-r1；BuildId见build-manifest.json，根EXE与build-r1-final同SHA。最新源不是原alpha根旧EXE对应的发布；原alpha build-manifest指向release成品。

读取顺序：实现与验证报告 → 主程序开发者文档 → 媒体资源与皮肤扩展 → SDK接口参考 → 开放场景API → 插件开发者/扩展/官方适配。

已实现真实GIF合成/H.264/MF与可选PCM音轨、host时钟、generation、owner撤销、D2D帧缓存、输入拖动事件、大包上限、同owner多节点。源码模块路径见主程序手册。权威示例skin-example.cpp/build-skins.ps1；旧media-fixture.cpp/build-media-fixture.ps1是静态原型，不得覆盖最终包。

已验证33媒体+35双owner+140插件回归、自测、真实Renderer连续录制、软件后端、模拟四档DPI和原time-display包状态回归。未验证环境与确实未实现能力逐项在报告，禁止改成全部通过。尤其根scale/rotation/shadow、WebP/APNG、硬件/透明视频、完整IME不能宣称支持。

测试隔离在verification，command.txt须等CommandsProcessed确认才能复制完整截图。初次采样的中断与半PNG留在旧diagnostic目录，最终render-final为完整画面证据；software-proof-final状态和性能完成，software-render-final只保留完成的40帧，窗口退出导致其后续重录未完成。本地选择器打开/取消已观察，UIA编辑遇到CacheRequest/foreground错误，成功选择未验收；不伪造UI成功。

设置Electron源码和运行时保持原副本，不因旧文档“无动画”文字再擅自改UI。此任务未改用户time-display源码与设置。原alpha628项、c2清单39项哈希匹配；原alpha根旧EXE和release新EXE本来不同，报告记录来源区别。

继续工作优先补真实物理输入、本地选文件确认、设备恢复与长时间压力验收；若扩格式，先独立真实解码/暂停seek/释放再加capabilities。若扩根属性，必须同步窗口、Renderer和命中，不能只删UNSUPPORTED。任何后续编辑在新副本，保留这次最终快照。
