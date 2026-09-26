# 复制粘贴与现有插件 SDK

产品 1.3.3beta 的剪贴板功能由主程序内建实现。此版本没有修改 mod_api.h、community_api.h、open_api.h、scene_api.h、media_api.h 的 ABI。

TranslationProvider、TranslationRequest、TranslationResult 定义于 `../source/src/clipboard.h`，是供宿主源码集成的翻译适配接口，并非已对插件 DLL 导出的 ABI。适配器需支持取消、有界超时、明确错误、请求 ID，以及保持原文。未配置 provider 时不会外发文本。

Electron `clipboard.call` 的操作白名单是设置进程与宿主之间的内部协议扩展，不等于向所有插件公开剪贴板历史。插件不能因 SDK 文件存在便假定可以读取该隐私数据。

既有插件无需因剪贴板功能重新编译。真实第三方插件全量兼容测试尚未完成，详见 `../verification/final-report.md`。
