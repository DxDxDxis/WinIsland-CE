# 1.3.3beta 复制粘贴开发说明

## 边界与版本

源码基线：`../1.3.2alpha`；产品版本：1.3.3beta。剪贴板功能是宿主内建模块，不另启进程、不注册全局键盘钩子。原插件 SDK/ABI 未为此改变；复制的 SDK 继续适用，旧插件无需因这个功能重新编译。未宣称全部第三方插件经过回归。

剪贴板持久化格式版本为 1。新增 `clipboard.call` 命令是设置宿主接口扩展，不是插件 ABI 改动。TranslationProvider 是宿主内的 C++ 源码适配接口，不是跨 DLL 的稳定 C++ ABI，后续服务商适配应与宿主一起编译。

## 调用路径

Windows `WM_CLIPBOARDUPDATE` → ClipboardStore 的独立消息线程 → Unicode 解析/排除应用/去重 → 原子加密落盘 → `ClipboardChangedMessage` → 主岛渲染。

设置页 `clipboard.ts` → 现有安全 preload → Electron main 白名单 → SettingsBridge → `settings_host.inc` → ClipboardStore。复制/保存/删除不在 renderer 中模拟成功。Electron 继续使用 contextIsolation、sandbox，关闭 nodeIntegration。

`clipboard.call` 操作包括 list/get/preferences/save/delete/clear/copy/copy-text/translate-start/translation-read/cancel-translation/reset-corrupt。导出由 Electron main 的保存对话框和文件写入实现，取消不写文件，以独占创建防止同名覆盖。

## 状态与存储

每条记录拥有 GUID、完整文本、时间、来源、版本、原文和可选译文。管理卡片按 ID 保留草稿及展开状态。保存必须携带版本，冲突拒绝覆盖；过滤不重新创建全部输入控件。IME composition 期间不更新搜索条件。

默认 20、范围 4–200；另有限制：单条最多 262144 UTF-16 单元，总原文/文本/译文约 8 Mi UTF-16 单元。过大剪贴板文本被明确忽略且不改系统剪贴板；达到总量保护时移除尾部旧项。非 Unicode 文本不解析为二进制内容。

索引 `<dataRoot>/clipboard/history.dat` 使用 DPAPI 当前用户保护和既有 writeAtomic。显示、监听、保存、退出清空为独立偏好。停止监听动态移除系统 listener；恢复时重新注册。退出时取消翻译、等待后台任务、停止消息线程并销毁编辑表面。日志不记录正文。

## 原生编辑绘制修复

原岛分为 DirectComposition 显示窗口与 `WS_EX_NOREDIRECTIONBITMAP` 输入窗口。普通 GDI EDIT 放在后者下会缺失文本和背景；给子 EDIT 加 WS_EX_LAYERED 会在尺寸过渡中留下原生白色滚动条残影。

修复使用一个由岛输入 HWND 拥有的 `WinIsland.Clipboard.EditorSurface`：同一进程、无标题/任务栏项、无独立生命周期，只覆盖岛内部文字区域。普通 EDIT 是该表面的子控件。隐藏编辑、收起、暂停或退出时同步隐藏/销毁；外部岛形和工具栏仍使用原 D2D 动画。不是单独应用或无关联的浮窗。

表面使用正常重定向绘制、WS_CLIPCHILDREN 和不拷贝旧像素的尺寸更新；矩形未变时跳过 SetWindowPos。字体只在实际 DPI/文字缩放变化时重建。去除原生非客户区滚动条，绘制窄暗色滚动标记，滚轮、PageUp/PageDown 及轨道拖动继续作用于真实 EDIT。

Win32 EDIT 显示要求 CRLF：仅在编辑器边界转换 LF，未编辑时返回完整原文，保存 LF 源文本继续使用 LF。选区偏移从控件实际字符串读取，不把 CRLF 的索引套到 LF 文本上。

当前选区操作通过原生选择与右键菜单实现；自动定位的选区快捷浮层尚未实现，不能算通过验收。

## 翻译边界

TranslationRequest/Result 提供请求 ID、文本、源/目标语言；provider 接受原子取消标记。生产版本没有 provider，明确报未配置且不外发文字。提供方必须支持有界超时和取消，以便退出及时释放。当前自动方向规则为检测汉字则英文，否则中文；完整自动语言检测仍依赖后端接入。

异步请求在独立 Jobs 线程执行，通过 translation-read 查询。历史版本变化、取消或请求 ID 不匹配时拒绝旧结果。联网译文、服务授权/配置、所有语言实际翻译效果未验证。

## 验证入口

- 原生数据单测：`WinIsland-1.3.3beta.exe --clipboard-test <新隔离目录>`。
- 设置与主进程联调：`node settings-electron/verify-clipboard.cjs`。
- 实际屏幕绘制回归：`node settings-electron/verify-clipboard-paint.cjs`。

绘制回归曾在旧实现明确失败（背景像素比例 0、文字不可见/滚动条重影），用于补足仅验证 HWND 存在与保存成功的盲点。截图及结果存于 verification 下时间戳目录，不以编译或不可见控件保存成功代替视觉通过。
