from pathlib import Path
import json
b=Path.cwd();t=json.loads((b/'verification/active-test.json').read_text('utf-8-sig'));run=Path(t['root']);relative=str(run.relative_to(b))
p=b/'tools/verify-ipc.ps1';s=p.read_text('utf-8-sig');s+='\n# Restore user-visible choices after the restart assertions, including a failed run.\nif($Phase -eq \'restart\' -and (Test-Path -LiteralPath $originalFile)){ & $PSCommandPath -Phase restore }\n';p.write_text(s,encoding='utf-8-sig')
report=f'''# 1.3.3exp-winUI（实验性）验证报告

验证日期：2026-09-20。最终隔离运行目录：{relative}。

## 已真实通过
- C++ 宿主 Release 构建、WinUI C++/WinRT Release 构建。日志：verification/fixed-native-build.log、startup-ui-build.log。
- 最终主 EXE 在独立中文/空格路径显示首次安装界面，完成内嵌组件释放、SHA-256 校验。
- 主程序启动来自所选数据目录 components/runtime 下的 WinIslandSettings.exe；窗口真实可见、正常读取设置，不是仅创建进程。
- 设置 IPC 修改音乐/消息显示开关成功，完整退出宿主后重启恢复。
- 二次启动使用原定位记录，定位文件哈希保持一致，未再次要求选择数据目录；再次打开 WinUI 设置成功。
- 关于页、首次安装、宿主/设置 EXE 属性和 capabilities.read 版本正确。证据：ui-about.txt、version-check.json。
- WinUI 文件中转页面显示通过真实 IPC 导入的 TXT/MD 两条记录；仅中转、保存副本、原文件保留、复制副本哈希一致，重启恢复索引和启用状态。证据：ui-transfer.txt、{relative}/ipc-write.json、ipc-restart.json。
- 新的复制粘贴 WinUI 页面真实加载、显示偏好；偏好可保存和恢复。
- 实际 Windows 剪贴板文字更新被宿主捕获，打开显示开关后 ClipboardMode=1，渲染输出出现“粘贴到此处”及文字卡片。证据：clipboard-island-live.json、隔离数据目录 capture.png。
- 组件释放测试通过；统一目录测试 14 项通过；剪贴板核心测试 17 项通过（含 Unicode、去重、编辑版本、加密恢复、未配置翻译错误）；中转组件自测返回 0。见 native-tests.json 和对应 test-* 目录。
- 插件核心回归补齐现场编译的 scene C ABI fixture 后 148 项通过、0 失败。包括实际 DLL 生命周期、场景接口、依赖及启用状态恢复；报告 {relative}/test-mods-complete/mod-tests.txt。
- 内嵌包 254 文件，包含 settings/resources.pri；文件名审计没有 Electron、Chromium、node.exe。package-check.json。
- 原 1.3.3beta 全量 3980 文件、原 WinUI 实验工程 30375 文件、共同基础 1.3.2alpha 共 4265 文件 SHA-256 核对：全部零修改、零新增、零缺失。readonly-after-comparison.json。

## 本次发现并处理的问题
1. 设置不可启动：resources.pri 被脚本排除。原错误是 WinUI 找不到 themeresources.xaml，最终修复打包规则、增加必需文件门禁，并加入启动异常提示。
2. 旧自动验证误判：隐藏启动使首次安装窗被隐藏；旧流程还曾使用保留变量。旧 verify-live.ps1 的结果不作为本报告成功依据，实际验证使用独立路径、可见安装及真实窗口。
3. 用户试用时灵动岛不显示复制内容：当前打开的隔离测试实例 show=false（为验证持久化而关闭），listening=true，已经有历史记录。恢复 show=true 后用真实 Windows 剪贴板更新验证展示成功，不是通过伪造界面判定。已删除合成测试条目，恢复当前新建测试实例的音乐/消息显示选择；verify-ipc.ps1 增加原值快照和 restore 阶段，重启断言后恢复。
4. 第一次插件回归 129 通过、1 失败，原因为 scene-fixture DLL 未构建；在新目录实际编译 fixture 后重跑得到 148/0。保留初次失败报告便于审计。
5. 一次链接遇到测试宿主仍在占用 EXE；结束该测试实例后重建成功，最终产物哈希见 release-hashes.json。

## 已完成代码，但本轮尚未逐项运行验证
- WinUI 复制条目的编辑保存/取消/冲突、搜索、批量删除、TXT/MD 导出对话框、输入法组合等完整操作链；核心实现有自测，不能视为全部 UI 用例通过。
- 插件管理 UI 的安装、重装、日志和模组设置逐项交互；实际 DLL 生命周期通过核心回归，但不等同所有 UI 路径通过。
- 所有旧音乐播放器、真实消息来源、插件扩展点的完整跨版本回归。
- 多显示器、负坐标、各 DPI、窄窗口、快速反向动画、拖出到其他应用、复制取消与磁盘断开；组件自测不能替代设备和真实应用验证。
- 其他电脑/干净虚拟机、Windows 10、标准用户账户环境；本轮测试在当前开发机执行。
- 本轮未重建 Debug 配置，未做性能对比，不宣称更快或更省内存。

## 未实现 / 有意保留的边界
- 真实翻译后端仍未配置；与主线状态一致。界面明确报告服务未配置，不发送文本。
- 不承诺单进程或完全无外置 DLL；交付为单 EXE 分发，首次安装按需释放官方运行时。
- 不把旧插件 API/ABI 或数据格式升级为新版本。本轮只移植主线更新并修复 WinUI 发布链。

## 复验方式
1. 主 EXE 使用 --verify-installed "本实验目录内的新验证路径" 启动；安装到其隔离默认目录。日常运行不加该参数。
2. verification/active-test.json 记录该实例 PID、路径；tools/verify-ipc.ps1 -Phase write 测试设置/中转写入，保存原开关。
3. 关闭该验证实例并重启相同隔离路径；执行 -Phase restart 验证恢复，然后自动执行 restore。中途停止可单独执行 -Phase restore。
4. 自测参数：--components-test / --storage-test / --clipboard-test / --transfer-widget-test / --mods-test，后接本实验目录内绝对输出目录。
5. 最终验收需观察设置窗口和灵动岛真实画面；仅有返回码不算完成交互验收。
'''
(b/'docs/1.3.3exp-winUI-verification.md').write_text(report,encoding='utf-8')
(b/'开发文档/验证报告.txt').write_text(report.replace('## ','').replace('# ',''),encoding='utf-8-sig')
ipc='''1.3.3exp-winUI（实验性）开发者与 IPC 补充

基础架构：source/src/settings_bridge.cpp 管理受限命名管道、当前用户 ACL、令牌、长度限制和取消/超时；settings_host.inc 在宿主侧分发。客户端是 settings-winui/clients.h，页面经 SettingsClient / PluginClient / TransferClient / ClipboardClient 调用。插件仍由 C++ 宿主加载，设置进程不加载插件 DLL。

新增协议：原 protocol=1 不变，白名单增加 clipboard.call；params.action 包含 list、get、preferences、save、copy、copy-text、delete、clear、reset-corrupt、translate-start、translation-read、cancel-translation、export。具体字段和校验以 source/src/clipboard.cpp、settings_host.inc 为准。
list：query 可选，返回 entries、preferences、revision、error、recoveryRequired、translationAvailable；get：id，返回完整 entry；save：id、text、version，乐观版本冲突拒绝；delete：id 或 ids；clear/reset-corrupt：confirmed=true；preferences：preferences 对象，降低容量时可能需要 confirmed=true。所有调用先检查 ok，失败展示 code/error，不因超时假定操作没有执行。
export：id、part=original/translation/both，由宿主文件对话框选择 TXT/MD 路径；客户端不能任意写入路径。翻译尚无后端。

线程与归属：宿主 jobs 执行复制粘贴存储/导出任务；设置 UI 等待 Transport 的异步回复，回到 UI 后再更新控件；DLL ABI 不变。历史由 ClipboardStore 管理，当前 Windows 用户 DPAPI 加密，保存在 dataRoot/clipboard/history.dat，原子写入；不是通用跨用户数据文件。

构建发布：product-version.json 是产品版本源；generate_version.py 更新代码资源。settings-winui/build.ps1 复制自包含 Windows App SDK，必须包含 resources.pri 以及其他 PRI/DLL，不能只复制 EXE。source/bundle-runtime.ps1 打包前进行必需资源检查。最终资源归入主 EXE，使用统一安装路径释放，禁止写入旧参考工程。

功能开放边界未因本次迁移扩大或缩小。主线 TranslationProvider 是宿主原生替换接口，本次没有宣称它已自动成为通用插件注册服务。详细插件 ABI 见 sdk 中各头文件；不能把这里的设置 IPC 当作任意执行命令或加载原生 DLL 的接口。

验证与交接：docs/1.3.3exp-winUI-migration.md 记录三方合并来源；验证报告.txt 区分已运行和未验证项。设置无法启动优先检查资源索引，不要只看 CreateProcess 成功；未接入真实翻译时不要伪造译文。后续应补齐 WinUI 卡片编辑、导出、输入法、多显示器/高 DPI 的 UI 验收。所有修改留在当前实验目录，参考目录保持只读。
'''
(b/'开发文档/开发者与IPC接口补充.txt').write_text(ipc,encoding='utf-8-sig')
(b/'docs/1.3.3exp-winUI-ipc.md').write_text(ipc,encoding='utf-8')
print('Verification and developer documentation written')
