from pathlib import Path
import json,hashlib
b=Path.cwd();v=json.loads((b/'product-version.json').read_text('utf-8'));latest=json.loads((b/'verification/active-test.json').read_text('utf-8-sig'));run=Path(latest['root'])
if v.get('display') == '1.3.4':
    raise SystemExit('Legacy migration documentation writer is disabled for the formal 1.3.4 community release; use docs/1.3.4-community-verification.md')
(b/'docs').mkdir(exist_ok=True);(b/'开发文档').mkdir(exist_ok=True)
migration=f'''# {v['display']} 主线差异移植说明

## 来源与边界
WinUI 基线：{b.parent/'WinIsland-1.3.2alpha-winUI（实验性）'}。
主线参考产物：{b.parent/'1.3.3beta/build-1.3.3beta'}。
真实主线源码：{b.parent/'1.3.3beta/source'}；依据 README、构建脚本及 1.3.2alpha→1.3.3beta 的源码差异确认。本次采用 1.3.3beta 更新，没有把其他实验分支视为主线。
共同基础为 1.3.2alpha。三方合并记录见 verification/port-manifest.json 和 mainline-*.diff。

## 实际移植
- 新增 clipboard.h/cpp、clipboard_tests.cpp、clipboard_app.inc：文字剪贴板监听、去重、应用排除、DPAPI 加密历史、上限、搜索、编辑、复制、删除、导出和异步翻译契约。
- 合并 app.cpp、render.cpp/h、accessibility.cpp：剪贴板提示、列表及原生文本编辑、删除确认等主线关联行为。没有以主线覆盖整个 WinUI 工程。
- 合并 settings_bridge.cpp、settings_host.inc：clipboard.call 命令、异步分发、导出。保留既有 settings/media/mods/transfer 接口。
- WinUI 专项：settings-winui/clipboard_page.inc 实现导航、偏好、搜索、详情编辑、保存/取消/重读、批量删除、导出入口和翻译状态；clients.h 增加 ClipboardClient。仍使用官方 WinUI 控件与现有材质、标题栏及导航动画。
- 主程序负责数据与操作；WinUI 客户端没有加载插件 DLL；插件 API/ABI 和原有用户配置格式未因本次移植改变。
- 尚无实际翻译后端。翻译入口会报告未配置，不上传文本，也不把测试适配器当生产能力。

## 设置打不开的原因与修复
最终定位的异常为 Cannot locate resource from 'ms-appx:///Microsoft.UI.Xaml/Themes/themeresources.xaml'，Windows 曾记录 0xc000027b。
settings-winui/build.ps1 原先排除了 resources.pri，导致 XamlControlsResources 初始化失败。补齐同一 Windows App SDK 框架的 resources.pri 后，设置真实启动成功。
修复位置：
1. settings-winui/build.ps1 保留主题资源索引；
2. source/bundle-runtime.ps1 在打包前检查资源索引、WinUI DLL 和 VC 运行库；
3. settings-winui/app.h 为启动页面异常显示可诊断错误；
4. source/src/installation_ui.inc 更新过时的运行时注释。
主 EXE 重新构建内嵌包，哈希变化会使用新的独立组件集合；不覆盖正被旧进程加载的 DLL。
上次测试还有隐藏安装窗口的问题；本次采用可见隔离安装，并通过实际窗口观察确认，不沿用旧脚本的成功推断。

## 版本统一
唯一产品输入为 product-version.json；tools/generate_version.py 生成宿主头文件、EXE 资源、设置 EXE 资源和清单版本。
展示版本：{v['display']}；Windows 数字 FILEVERSION / PRODUCTVERSION 及应用清单：{v['numeric']}。
托盘和启动诊断使用 Version，关于页使用 WI_VERSION_W；设置普通页面不新增产品版本文案。插件协议与历史迁移标记没有机械替换。
EXE 文件属性、IPC capabilities、首次安装和关于页已经核验。托盘文本来源已检查，本轮未单独悬停截图。

## 构建、依赖与交付
执行 PowerShell 7：./build-release.ps1 -BuildTools E:/WinIslandBuildTools。
工具链：MSVC x64、Windows SDK、Python；WinUI 投影与 Windows App SDK 自包含文件位于本目录 dependency-cache/winui。
单独改宿主可执行 ./source/build.ps1 -OutputDirectory ./build-1.3.3exp-winUI -BuildTools E:/WinIslandBuildTools；之后同步 release 下的 EXE。
发布文件：release/{v['executable']}。本次最终文件为 29,883,392 字节（约 28.5 MiB）。
给用户发送这一个主 EXE 即可；首次运行释放 WinUI 客户端、官方运行时、VC DLL、歌词组件及许可证到所选统一数据根目录。
这表示单 EXE 分发，不表示运行时完全不需要外置 DLL。release/settings 是本地构建打包输入，无需随主 EXE 另发；不能只发送其中的 WinIslandSettings.exe。
包内检查未发现 Electron、Chromium 或 node.exe；详细清单见 verification/package-check.json。

## 数据与回退
首次定位沿用统一安装目录机制。不要删除旧配置、插件和数据来升级。
本次所有安装/配置测试都在 verification 下，未操作日常安装定位记录。
回退：退出实验版宿主及其设置窗口，运行原 WinUI 实验版或主线 1.3.3beta 的原 EXE。保留数据目录；新增 clipboard 历史由原来的兼容规则处理，不手工移除 components 或 install-location.json。
旧版本仍保留；verification/readonly-after-comparison.json 给出原目录全量哈希核对结果。
'''
(b/'docs/1.3.3exp-winUI-migration.md').write_text(migration,encoding='utf-8')
(b/'开发文档/本次移植与设置启动修复.txt').write_text(migration.replace('# ', '').replace('## ',''),encoding='utf-8-sig')
readme=f'''# WinIsland {v['display']} 社区版

本目录基于旧 WinUI 实验工程，按差异移植 1.3.3beta 的复制粘贴功能，保留 WinUI 设置框架。

运行 `release/{v['executable']}`。分发时只需这个 EXE；首次运行会把内嵌 WinUI 运行组件安装到统一数据目录。请先退出旧宿主，避免单实例逻辑把“打开设置”请求交给仍运行的旧版本。

设置打不开的问题已修复：恢复打包 WinUI 的 resources.pri 主题索引，并通过独立安装后的真实窗口验证。

- 构建：PowerShell 7 执行 `./build-release.ps1 -BuildTools E:/WinIslandBuildTools`。
- 移植与回退说明：docs/1.3.3exp-winUI-migration.md。
- 真实验证与未验证项：docs/1.3.3exp-winUI-verification.md。
- 中文 TXT：开发文档/本次移植与设置启动修复.txt、验证报告.txt。
- 产物哈希：verification/release-hashes.json。

这是正式社区版；真实在线歌词服务、多显示器、其他 DPI 和其他电脑仍需在目标环境继续验证。
'''
(b/'README-winui3-experimental.md').write_text(readme,encoding='utf-8')
(b/'交付说明.txt').write_text(readme.replace('# ',''),encoding='utf-8-sig')
print('Migration documentation written')
