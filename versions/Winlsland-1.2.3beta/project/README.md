# WinIsland 1.2.3beta

原生 C++20 Windows 桌面版本。源码、测试工具和发布程序分别放在 `src`、`tests`、`release`；原版本保留在上级目录。

## 运行

最终发布包为 `WinIsland-1.2.3beta-Windows-x64.zip`，解压后双击 `WinIsland-1.2.3beta.exe`，或直接运行 `发布包/WinIsland-1.2.3beta.exe`。普通运行与旧版共用单实例锁，试用前先从旧版托盘菜单退出旧程序。托盘右键打开设置或退出。

最低目标系统为 Windows 10 1809 **x64**，建议 Windows 11。进程音频回环需要系统 build 20348 或更新版本；不具备该接口时，歌曲信息与受支持的控制仍可使用，音量竖条保持基线，不模拟音频。Windows 10 实机验证情况以验证报告为准。

应用直接链接静态 VC/C++ 运行库，图标、清单与版本信息嵌入 EXE。无需安装 Python、.NET、Visual Studio、WebView2 或第三方 Qt 运行库。Direct2D、DirectWrite、DirectComposition、GDI+、WASAPI、WinRT、XmlLite 和 winsqlite3 由支持的 Windows 系统提供。硬件渲染初始化失败或设备丢失时会切换为 Direct2D 软件渲染；原有设置中的帧率仍可调整。

配置沿用 `%LOCALAPPDATA%/WinIsland/settings.xml`，歌词沿用该目录下 `lyrics` 的 SHA256 文件命名。不更改 QQ、音乐软件的账号数据，也不读取聊天数据库。

## 监测与日志

设置 → **启动信息监测**。记录期间按钮变为 **停止信息监测**。**导出日志报告** 在记录中和停止后都可使用；记录中导出只是快照，不会停止监测。

默认路径：`EXE 所在目录/daxian日志/daxian日志_年月日_时分秒.txt`，同秒重名会加序号，避免覆盖。默认目录不可写时改存到当前用户的“文档/daxian日志”，并在设置中显示实际路径。两处都不可写时提示失败，内存中的监测记录保留，可修复目录权限后重试。路径栏可以选择、复制。

报告为带 BOM 的 UTF-8 文本，包含版本、系统、CPU/GPU 型号、内存、渲染路径、缩放和帧率配置、资源采样、关键模块耗时、操作及异常事件。资源每 5 秒在后台采样；事件最多 1024 条，采样最多 720 条，超出后滚动保留并注明淘汰数；统计直方图覆盖本次完整监测。报告不记录聊天正文、歌曲名称、账号或凭据。后台快照位于 `%LOCALAPPDATA%/WinIsland/monitor`，方便退出或故障后寻找最近记录。

**注意测量口径：**“活动帧回调间隔”衡量应用调度，不等于显示器真实呈现帧率；“渲染提交”不包含 GPU 完整执行时间；当前报告没有直接采集 GPU 百分比。报告中的无异常记录不能证明所有场景都无故障。

## 音乐与通知能力

- 网易云音乐、QQ 音乐、汽水音乐通过 Windows SMTC 提供歌曲信息；需要在对应软件启用系统媒体控件。显示来源稳定保留在正在播放的软件上。
- 上一首、播放/暂停、下一首、播放模式均向当前显示的 SMTC 会话发命令，并回读确认。没有声明能力或命令未获确认时隐藏对应操作。SMTC 不提供真实账号收藏 API，因此没有虚假收藏入口。原版本也没有可用的音量滑块。
- 歌词来自用户为当前歌曲导入的 LRC，按播放器真实时间轴同步。不使用网络抓取或虚构时间轴；未提供进度的播放器不显示同步歌词。
- 系统通知从 Windows 通知数据库只读获取，按事件身份与内容摘要去重和排队。音乐与通知共用同一个容器，消息始终在音乐底部展开。QQ 如果没有向 Windows 发布系统通知（例如前台会话、免打扰或关闭横幅），应用无法凭空接收该消息；这仍是平台限制。

## 构建

安装 Visual Studio 2022 Build Tools 的 MSVC x64 工具和 Windows SDK 10.0.22621 或更新版本。PowerShell 执行：

```powershell
.\build.ps1
# 构建原生测试媒体发生器：
.\build.ps1 -IncludeTests
# 自定义 Build Tools 安装位置：
.\build.ps1 -BuildTools E:\WinIslandBuildTools -IncludeTests
```

发布配置使用 `/std:c++20 /MT /O2`。`tests/WinIsland-MediaFixture.exe`（实际输出在 `tests/bin`）仅用于验证，会创建真实 Windows 媒体会话和低音量 PCM 测试音，不是生产播放器或发布依赖。

## 验证工具

```powershell
.\release\WinIsland-1.2.3beta.exe --self-test C:\Temp\native-tests.txt
python .\tests\verify-native.py --exe .\release\WinIsland-1.2.3beta.exe --output C:\Temp\native-run --fixture .\tests\bin\WinIsland-MediaFixture.exe
```

`--output` 使用尚不存在的目录。加 `--software` 可测试软件渲染。`--verify` 使用隔离设置和显式合成输入，生产模式不会生成测试歌曲或消息。Python 仅为测试脚本依赖，终端用户不需要它。最终功能核对与性能数据见 `docs` 中的交付报告。
