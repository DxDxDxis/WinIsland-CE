# WinIsland 1.2.4beta_2

当前最新构建：`1.2.4beta_2-capsule95-20260907-r2`。本轮将所有灵动岛状态的宽高统一调整为修改前的 95%，普通横条使用半圆两端，内容和鼠标命中区域同步更新。尺寸及实际验证见 [外形与尺寸95验证](docs/外形与尺寸95验证.md)。下文歌词能力和限制继续适用。

基于原 `1.2.4beta` 的独立版本，原版本目录未覆盖。当前成品在 `release/1.2.4beta_2/`，运行其中 `WinIsland-1.2.4beta_2.exe`。分发时复制整个目录，不能只复制主 EXE；`lyric-provider` 是新增歌词后端的运行依赖。

**本次不宣称网易云最小化后的连续同步已经完全解决。** 本机网易云 3.1.39.205426 的 SMTC 时间轴全零，最小化后辅助功能当前歌词行冻结。新版本保留已匹配歌词、取消按窗口可见性直接拒读的判断，只在读到真实变化时使用后台当前行；数据冻结时明确显示静态歌词。QCloudMusicApi 能取得歌词正文，不能提供客户端的实际播放秒数。

设置、音乐控制、QQ 通知、FPS/Ping、动画和歌词缓存继续沿用原实现。正式配置仍位于 `%LOCALAPPDATA%\WinIsland`；观察／回归测试使用独立配置目录。

## 歌词策略

默认歌曲识别：SMTC 优先，窗口标题备用。默认歌词顺序：本地导入／校验缓存 → LRCLIB 精确 → 网易云搜索并确认 ID、普通歌词 → 网易云新版歌词 → LRCLIB 候选搜索。成功取得同步歌词后停止查询；纯文本不伪造时间轴。

新增设置“QCloudMusicApi（网易云歌词）”：歌曲 ID 搜索继续使用原生 WinHTTP 与原有匹配校验，歌词正文通过 QCloudMusicApi 的 `lyric` / `lyric_new` 获取。这是网易云接口的 C++ 封装，并非新的曲库。自动模式中，它只在网易云歌词阶段发生传输错误时作为可用的备用传输；不会通过换域名绕过 HTTP/API 429、403。

没有引入未经验证的第三方代理、HTML 抓取、Node/Python 运行时或播放器注入。重复录音、缺少可核验元数据、服务无歌词仍可能导致“暂无歌词”；不会直接选搜索结果第一首。

## 构建

需要 MSVC 2022 x64 Build Tools、Windows SDK 22621+。主程序使用 C++20、Win32/DirectComposition/C++WinRT，静态链接 MSVC 运行库。歌词子程序使用 C++17、Qt 6.8.3 Core/Network、Crypto++ 8.9.0。

```powershell
.\build.ps1 -BuildTools E:\WinIslandBuildTools -OutputDirectory .\release\1.2.4beta_2 -IncludeTests
.\build-qcloud.ps1 -BuildTools E:\WinIslandBuildTools
```

`build-qcloud.ps1` 可通过 `-QtRoot`、`-CMake` 指定开发工具路径；默认使用本目录 `tools/qt`、`tools/python-build/cmake/data/bin/cmake.exe`。编译在物理 ASCII 暂存目录完成，以避开 NMake 对中文路径生成 PDB 的问题；不依赖目录联接。构建完成后自动复制所需运行 DLL。

完整发布目录应含主 EXE、`lyric-provider`、许可证和说明。测试工具只用于开发，不随运行程序启动。主 EXE 命令行 `--self-test 文件`、`--qcloud-network-test 文件` 可分别执行原生回归和真实 HTTPS／歌词解析检查。

## 运行条件与依赖

最低 Windows 10 1809 x64；推荐受支持的 Windows 10/11。系统提供 UCRT、Schannel、DirectX、SMTC；不需要开发环境、Qt 安装器、Python、Node 或 OpenSSL 安装。Qt TLS 使用 Windows Schannel，并校验证书。

随包提供 Qt6Core、Qt6Network、Schannel TLS 插件和其需要的 MSVC 运行 DLL。Crypto++、QCloudMusicApi 静态链接到有界子程序；Qt 采用可替换的动态 DLL。主界面不加载 Qt，歌词请求仅在后台 worker 调用子程序。

子程序每次只处理一个数字歌曲 ID，进程最长 7 秒、响应最多 1 MB；切歌／退出会取消过期请求并终止本程序创建的子进程。网络超时 5 秒、每阶段最多 2 次尝试，延续原服务退避。新旧请求按歌曲关联标识隔离。

详细证据、范围和限制见 `docs/交付与验证.md`。Qt 与其他第三方版本、来源及许可见发布目录 `licenses/NOTICE.txt`。
