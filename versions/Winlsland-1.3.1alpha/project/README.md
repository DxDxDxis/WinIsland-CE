# WinIsland 1.3.1alpha 社区版

独立工程基于 `../1.3.0-path-state-fix`，所有本次源码、构建和隔离测试位于本目录，旧版保留。

主程序：`release/WinIsland-1.3.1alpha.exe`。单 EXE 内嵌 Electron 设置运行时和歌词依赖，首次安装沿用路径引导，数据仍进入确认的统一根目录。不要直接运行 settings 子程序。

新增音乐/消息显示开关，以及文件中转、保存副本、搜索、条目内详情、原生中转组件和 OLE 拖放。中转开关首次默认关闭，只有明确选择保存模式才复制文件。

- [使用、实现、数据和回退说明](docs/1.3.1alpha文件中转与显示开关.md)
- [实际验证及限制](docs/1.3.1alpha验证报告.md)
- [构建身份](build-manifest.json)

构建：`./build-release.ps1 -BuildTools E:\WinIslandBuildTools`。插件 ABI 沿用 `0x00010002`。

本次中转专项修复见 [修复说明与验收](docs/1.3.1alpha文件中转专项修复报告.md)。

早期报告保留为沿革，不能当作本版验证结果。verification 含测试文件、本地路径和短期 IPC 凭据，不作为发行包分发。

文件中转本轮修复：[吸附、动画提速、圆角和自动收回报告](docs/1.3.1alpha文件中转吸附与动画优化报告.md)。

当前设置布局、手动降低动画和黑色接收底板修订见 [修复报告](docs/1.3.1alpha设置布局与手动动画修复报告.md)。构建标识 `1.3.1alpha-settings-adjust-20260917`。
