# WinIsland 插件与内嵌 JavaScript 动画实施提示词

请基于 `release/1.2.6alpha-c2-js-settings/source` 实现和扩展 WinIsland Mod 系统。开始前多次阅读：`docs/插件系统开发者文档.md`、`docs/插件系统扩展开发文档.md`、`docs/内嵌JavaScript动画引擎方案.md` 以及最新源码中的 `mod_api.h`、`mod_system.*`、`mod_ui.cpp`、`render.*`。本任务只修改与本项目一致的 C++20/MSVC/Win32/DirectComposition/Direct2D/DirectWrite/D3D11 代码。

## 目标

保留当前 DLL ModLoader、生命周期、owner 资源登记、依赖图、可逆修改、设置中的插件管理和单文件 `.wimod` 方向。在此基础上加入宿主驱动的内嵌 JavaScript 动画层，让第三方模组可以携带动画脚本并修改/添加动画效果。JavaScript 必须依赖 WinIsland，不能单独运行，也不能直接控制窗口或播放器。

## 必须完成的功能

1. 音乐播放状态整体相对当前基准缩小 10%。使用一个统一、非累积的缩放常量（目标为 `0.9` 相对基准），不要在不同绘制函数重复乘法。音乐封面、标题、作者、频谱、歌词、按钮、命中区域、展开状态和消息共存状态必须使用一致的几何结果。
2. 按 `内嵌JavaScript动画引擎方案.md` 增加 AnimationRuntime、AnimationBridge、AnimationScheduler。C++ 管理状态、帧提交、校验、回退和 Direct2D/DirectComposition；脚本只返回动画属性。
3. 允许 `.wimod` 内包含 `animations/*.js`、`animations/manifest.json`、预设和资源。加载器校验路径、脚本版本、大小、哈希和模组 API 兼容性。
4. 允许模组注册动画状态、时间轴、缓动、属性目标和完成回调；动画资源必须带 owner 并由 ChangeManager/ResourceRegistry 管理。
5. 支持 `start`、`cancel`、`pause`、`resume`、`reverse`、`setReducedMotion` 和性能模式切换。支持快速状态变化时从当前值继续、反向或取消旧时间轴。
6. 脚本异常、超时、内存超限、非法属性、版本不匹配时立即取消脚本并回退到内置 C++ 动画，记录插件日志，不能破坏主循环。
7. 设置界面继续提供插件管理和插件模组管理；动画属于插件资源。禁用或卸载模组时停止新帧，平滑淡出其 UI，取消时间轴，恢复前一有效动画或默认动画，移除资源后再卸载 DLL。

## JavaScript 边界

脚本可接收：`idle`、`music`、`music-expanded`、`notice`、`music-notice`、`settings-page-change`、`long-press` 以及经过筛选的只读状态。脚本可返回的属性至少包括：`x`、`y`、`width`、`height`、`radius`、`opacity`、`scale`、`offsetX`、`offsetY`、`contentOpacity`、`lyricOpacity`、`noticeOpacity`。

C++ 必须校验目标、范围、NaN/Infinity、持续时间、帧率和内存。脚本不得访问 HWND、窗口样式、输入区域、文件系统、网络、进程、账号凭据或未公开的 C++ 对象。GSAP 只允许使用不依赖 DOM 的 Core/Timeline/Easing；不要直接移植 DOM、CSS 或 ScrollTrigger 逻辑。

## 接口扩展

在现有 POD C ABI 中增加向后兼容的动画结构和函数，所有结构带 `size/version`。新增资源类型建议为 `WI_ANIMATION`。动画句柄必须可撤销，回调可注销，字符串采用 UTF-8，禁止跨 DLL 传递 STL、异常和不明确所有权的内存。为动画 API 增加能力查询，使旧插件在没有 JS 引擎时仍能运行 C++ 功能。

## 验收

必须提供接口头文件、宿主实现、最小 JS 示例、`.wimod` 示例、构建说明和测试。验证音乐缩小 10%、待机/音乐/展开/消息共存、DPI 100/125/150/200%、减少动态效果、脚本异常回退、脚本超时、重复启停、单独禁用模组、替换链恢复、动画淡出和主程序退出。输出修改文件清单、设计取舍、测试命令、测试结果和已知限制。

