# 构建与源码完整性

本次整理没有修改原工程，没有重新编译历史版本。`project/` 尽量保留相对结构和源文件字节；缺失的历史源码不以新源码补齐。原构建脚本中的开发机绝对路径仍可见，这是历史事实，不是安装要求。

## 最新 C++ + Electron 主线

工作目录：`versions/Winlsland-1.3.1alpha/project`。

所需工具：

- Windows x64、PowerShell。
- Visual Studio 2022 C++ Build Tools，Windows SDK 22621 或更高的兼容 SDK。
- Node.js / npm，用于设置程序；精确 npm 依赖在 `settings-electron/package-lock.json`。
- QuickJS-NG、miniz、GSAP 等对应版本源码已按可找到的内容保留在 `source/third_party`；许可见根第三方声明。
- 歌词辅助程序需要 Qt Core/Network 与 Crypto++。本地完整归档保留已有运行目录；源码仓库不保存这些大型运行二进制。

```powershell
# 在当前版本 project 目录，传入你自己的 MSVC 工具目录。
.\build-release.ps1 -BuildTools 'C:\你的BuildTools目录'
```

实际流程：npm 锁文件安装 → 两份 TypeScript 编译配置 → `package.cjs` 打包 Electron → 释放包 manifest/资源打包 → `source/build.ps1` 编译 C++20 → 输出 EXE。不要在 source 目录直接把编译参数套用到其他版本。

原脚本对外部依赖的要求不会因复制自动消失。当前 `package.cjs` 在未找到锁定版本 Electron ZIP 时调用 `prepare-runtime.ps1` 准备运行时；`ELECTRON_SKIP_BINARY_DOWNLOAD` 关闭的是 npm 安装阶段的重复下载。首次在新机器上构建需要网络与工具依赖；归档不声称离线零依赖即可编译。

## 克隆纯源码后的运行依赖

根 `.gitignore` 排除了 `dist`、`project/release`、`lyric-provider` 等运行二进制。直接使用本地完整归档时这些文件仍存在；从 GitHub 克隆源码时需要：

1. 安装本版 MSVC、SDK 和 npm 工具依赖。
2. 根据锁文件下载设置页依赖，并检查 Electron 打包脚本的路径参数。
3. 构建歌词服务，或从同版本可信 Releases 取得运行依赖并验证哈希。保留对应许可证。
4. 将本版本构建所需 `lyric-provider` 放到 project 的预期位置；settings 由设置打包步骤生成。
5. 再执行主发布脚本。只在自己的工作副本中调整工具路径，不修改只读历史原件。

共享歌词工程位于 `shared/lyric-provider-source`：`lyric-helper` 为程序源码，`third_party` 为已找到的 QCloudMusicApi / Crypto++，`build-qcloud.ps1` 为原构建脚本。按脚本参数设置 `-BuildTools`、`-QtRoot`、`-CMake`。Qt SDK 与构建工具未作为自有源码重新分发。

## 早期原生版本

1.2.3beta 之后多数版本使用 C++20、Win32、Direct2D/DirectComposition、C++/WinRT。构建入口可能是 `project/build.ps1` 或 `project/source/build.ps1`，以该版本 README 为准。没有 Qt 服务的早期版本无需照搬较新版依赖。

1.2.3beta 源码从原历史 ZIP 恢复；同名原工作区实际已经变成 1.2.3beta_2，所以分开存放。

## 早期 C# / WPF 版本

1.1.1 源码对应内部 1.1，由原发布说明和成品相同 SHA 记录关联；原 `build.ps1` 使用 Windows/.NET Framework 工具链。1.2.2beta 源码在名称仍含 1.1.2 的旧工作区中找到，以 `AppVersion.cs` 为准。

**1.1.2、1.2.0、1.2.1beta** 的独立完整对应源码目前未找到。成品被归档，但不能承诺从现有源码重建出这些 EXE。未把早期局部源码片段冒充完整发布源码。

## 测试

保留源码测试、测试脚本和 SDK 示例。原生自检示例（仅在支持该参数的版本使用）：

```powershell
.\WinIsland-1.3.1alpha.exe --self-test 'C:\隔离测试\core.txt'
.\WinIsland-1.3.1alpha.exe --verify 'C:\隔离测试\data'
```

诊断会话可能生成 IPC 连接信息，不提交 `settings-connection.json`。不要使用日常数据目录做异常退出、迁移、插件卸载测试。旧报告中的通过记录是当时的验证，不代表本轮又执行了一遍。

最新修复构建身份可从 `project/build-manifest.json`、`source/src/core.h` 和总复制清单核对。构建日期不单独用于判断源码是否对应。
