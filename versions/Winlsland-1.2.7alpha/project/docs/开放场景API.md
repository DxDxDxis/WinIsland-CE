# 开放场景 API：实现、边界与验证

构建：1.2.6alpha-c2-open-scene-20260913。产品版本仍为 1.2.6alpha-c2；生命周期 ABI 仍为 0x00010002。基线为 no-motion/source，旧目录没有修改。其历史 build-manifest.json 指向 theme-installer/build-final3，不能作为最新构建依据；此次使用实际 source/build.ps1、core.h 和重新构建结果。

## 原因与本次实现

旧 ModLoader 的生命周期可正常工作。限制在于 WI_LAYER/WI_BUTTON 只拥有 key/label/value，App 把它们计入最多三行、每行 28 DIP 的尾部高度，Renderer 再按固定位置绘制。没有可查询元素、可更新文字或场景状态接口。因此加载成功不等于能自由显示文本。

现在新增独立版本的通用场景表。创建元素默认 Overlay，不增加尾部高度；只有显式选择 PluginManaged/Intrinsic 才影响岛尺寸。旧 WI_LAYER/WI_BUTTON 保留原尺寸语义，以兼容旧模组。

调用链：DLL 工作线程回调 → queryInterface → 创建/修改暂存节点 → commit 当前 owner → ModSnapshot 复制绘制数据 → App 合并宿主状态和尺寸 → Renderer Direct2D 绘制 → 同一几何建立输入区域 → 输入排队回到 ModLoader 工作线程。

## 公开结构与表

完整可编译接口在 source/src/mod_api.h 与 scene_api.h。

- WinIslandHostApi 尾部追加 queryInterface；旧字段布局与生命周期 ABI 不变。
- 查询 winisland.scene/version=1 得到 WiSceneApi。
- 查询 winisland.settings/version=1 得到 WiSettingsApi。
- WiPropertyValue：size/version、4 个 double、句柄、1024 字节 UTF-8 缓冲。
- WiElementInfo：ID、owner、key、parent、type、可见性、矩形、透明度、z。
- WiSceneSnapshot：代次、时间、状态标志、DPI、岛位置/尺寸、焦点、可见节点数量、尺寸模式。
- WiInputEvent：目标、类型、代次、时间、局部坐标、滚轮、按键、修饰键。
- WiDrawContext / WiDrawCommand：绘图上下文与复制的绘图指令。

所有输出由调用者分配。字符串 UTF-8，宿主复制；禁止传递 STL、引擎对象和跨 CRT 所有权。x64 Windows 使用平台 C 调用约定。结构 size 至少为当前结构大小，version 必须匹配。无效版本/目标/参数返回明确 WiResult。

## 通用操作

| 能力 | 当前入口 |
|---|---|
| 查询当前状态 | snapshot |
| 查询节点、父子列表、按类型筛选 | enumerate；parent=0/type=0 表示不过滤 |
| 获取实际可见占用区域 | visible；矩形为岛内最终 DIP 包围盒 |
| 按 key 查询 | find；优先当前 owner，再宿主，外部重复 key 返回 CONFLICT |
| 创建 | create：Container/Text/Image/Icon/Button/Shape/Progress/Slider/Input/CustomDraw |
| 复制 | clone：复制当前有效属性与宿主拥有的图像/绘图数据，不复制回调 |
| 更新/重绘/替换 | set(WI_ELEMENT_TYPE) 与其他属性，然后 commit |
| 移动和重排 | set(WI_PARENT)、set(WI_RECT)、set(WI_Z_ORDER) |
| 删除 | remove：自己创建的节点销毁；外部节点采用当前 owner 的可逆隐藏 |
| 撤销属性覆盖 | clear(target, property) |
| 监听输入 | listen |
| 提交绘制 | draw、bitmap；宿主复制后绘制线程消费 |
| 自定义绘图回调 | onDraw；状态/尺寸/输入改变或 requestDraw 时在工作线程调用 |
| 发布或撤销全部本模组修改 | commit / reset |

宿主当前公开 key：island、host.music、host.idle、host.notice、host.music.title、host.music.subtitle、host.lyric、host.notice.title、host.notice.body、host.music.cover、host.music.spectrum.0..11、host.music.progress、host.music.position、host.music.button.1..4、host.telemetry.fps/ping；旧资源映射为 legacy.<owner>.<key>。只有当前存在的宿主内容可查到，不存在返回 NOT_FOUND，不制造假音乐节点。

## 几何、样式与尺寸

普通节点支持矩形、最小/最大尺寸、父节点、锚点、边距、内边距、圆角、RGBA 背景/边框/文字色、边框宽度、透明度、二维缩放、旋转、字体/字号/字重、对齐、可见性、矩形裁剪、输入开关、z、value 和自定义 UTF-8 数据。

WI_RECT=[x,y,width,height]；普通节点相对父节点，单位为最终 DIP，不再乘 MusicScale。WI_SCALE=[sx,sy]，旋转单位度，RGBA 为直通 alpha 的 0..1。字体可选任意已安装的 DirectWrite 字体。Image/位图使用复制的预乘 BGRA32。Icon 使用字体字符，也可组合位图/路径。Progress/Slider 是通用可视元素；Slider 数值与 Input 文本由插件通过事件和属性控制。

根节点 island：WI_RECT 必须 x=y=0；WI_POSITION 的 x/y 是岛左上角的屏幕物理像素，snapshot.x/y 使用相同坐标。根节点支持尺寸、位置、圆角、背景、边框、显隐和透明度；WI_EXPANDED 对主程序展开状态施加可撤销覆盖。

尺寸模式：HostManaged 保持宿主布局；Overlay 不增加尺寸；PluginManaged 使用根节点指定宽高；Intrinsic 扩大到 WI_LAYOUT=1 的插件节点包围范围。当前 Intrinsic 测量只处理节点原始矩形，复杂嵌套/旋转布局应由插件计算后使用 PluginManaged。

同一属性优先级较大者获胜；相同优先级按最新 set 序号。Z 顺序在同一父容器内排序，相同 z 按元素 ID；父容器构成绘制上下文。不同插件均能覆盖宿主或其他插件属性，owner 始终由宿主绑定，不能在参数中冒充。

## 场景状态

flags 含 idle、music、playing、lyric、notice、expanded、settingsOpen、reducedMotion、softwareRenderer。事件包括 scene.changed、scene.idle、scene.music.started/changed/stopped、scene.lyric.started/changed/stopped、scene.notice.shown/hidden、scene.expanded/collapsed、scene.layout.changed、scene.input.changed。

回调收到事件后重新读取快照；UI 先发布状态再发事件。visible 是最近成功提交的实际显示结果；enumerate/read 包含暂存属性，不能将其当作已经显示。generation 非零时不匹配返回 CONFLICT；可以重新取快照重试。

## 绘制与输入

onDraw 在加载器工作线程执行，不是 Direct2D 绘制线程回调。提供尺寸、DPI、时间、局部绘制范围、最近输入、焦点与场景状态；调用 draw/bitmap/set 后 commit。矩形、椭圆、线、文本、三角形以及 BEGIN/LINE/CUBIC/END 组合路径由宿主绘制。路径 END 决定颜色，stroke>0 描边，否则填充；CUBIC 的 x/y、width/height、x2/y2 分别是两控制点和终点。

不向 DLL 暴露 ID2D1DeviceContext、D3D11 或 HWND 指针。绘图命令和位图可缓存，卸载后只有宿主复制的数据，下一快照清除。位图每幅最多 2048×2048、所有插件合计 64MiB；最多 1024 创建节点、32768 属性层、每节点最多 4096 绘图指令。超限返回 LIMIT。

命中使用绘制变换的逆矩阵，考虑祖先矩形裁剪；输入事件在工作线程串行调用。支持 enter/leave/move/down/up/click/wheel、key.down/up、focus.gained/lost、text.input。鼠标按下自动捕获，失去捕获/释放时结束；Tab 切换，Enter/Space 触发操作。宿主播放按钮回调返回 handled=1 时拦截默认操作，0 才进入默认音乐操作。回调优先级为同目标最近注册者优先。

边界：当前采用最上方命中目标，BLOCK_INPUT=0 可让原生输入继续；尚未实现向所有下层插件逐层异步冒泡、任意显式捕获请求或完整系统级快捷键 API。Input 提供文本事件与绘制，不是完整 IME、剪贴板、选区编辑器。不得将这些写成已经完成。

## 修改链与卸载

SceneEdit 记录 owner、target、property、previous、value、priority、sequence。previous 仅供诊断，恢复永远重算剩余层与当前宿主基线，不写回历史整页快照。同一 owner/target/property 的 set 替换该 owner 的旧层，防止每帧无限累积。commit 只发布当前 owner 的修改。

禁用：关闭 accepting → 串行工作线程结束早先回调 → 按 owner 清理属性、节点、输入与绘图注册 → 发布新快照 → 原有 onDisable/onUnload/destroy/FreeLibrary。删除别人的父节点不会销毁别人的子节点；失去有效父节点的子树不绘制，所属插件可重新挂接。句柄不复用，销毁后返回 NOT_FOUND，旧输入被丢弃。

同进程 DLL 的任意内存破坏、TerminateProcess 或死循环不能被安全隔离。SEH/C++ 异常保护仅覆盖可捕获异常；原生回调超过 50ms 在返回后记录，不能强制安全抢占死循环。任何未通过正式 API 的内存修改都不保证恢复。

## 设置保护与兼容

原生设置窗口框架、创建销毁、安装目录/组件管理、加载器、校验、托盘、开机启动与系统权限未开放。插件自己的配置页可通过 WiSettingsApi.define 注册文本/数字/开关/选项，read/write 读写独立 settings.xml。数字检查范围与有限性，开关接受 0/1，选项必须来自 choices。插件管理配置页复用普通编辑框和统一主题选择框。

旧 0x00010002 模组继续使用旧函数表前缀、旧事件/定时器/动画接口。新模组先检查 host->size，再调用尾部 queryInterface。未获得扩展时可回退旧能力；不要机械改变生命周期 ABI。QuickJS/GSAP 与现有动画接口保留，插件可把动画输出写入通用属性；设置页面依然无过渡动画。

## 尚未完全开放/验证

- 整个 HWND 岛壳的旋转、缩放和外部模糊阴影未接入统一命中；根节点对 SCALE/ROTATION/SHADOW 返回 UNSUPPORTED。普通节点支持缩放旋转和实色偏移阴影；模糊参数非零返回 UNSUPPORTED。
- 宿主外壳与少数装饰/旧原生行为仍由专用路径承担。不能声称渲染器任意内部实现和所有业务流程都已元素化。
- 未提供裸 GPU 上下文或跨编译器 C++ 渲染对象，也没有后台安全终止任意 DLL 的能力。
- 跨显示器 DPI、所有节点组合、大位图高负载、真实 IME 和系统屏幕阅读器的完整实测尚未完成。

## 构建与测试

在本版本目录执行：

```powershell
./source/tests/build-scene-fixture.ps1
./source/examples/build.ps1
./source/build.ps1 -OutputDirectory ./build-open-scene
$p = Start-Process ./build-open-scene/WinIsland-1.2.6alpha-c2.exe -ArgumentList '--scene-test verification/scene-tests.txt' -WindowStyle Hidden -PassThru -Wait
$p.ExitCode
$p = Start-Process ./build-open-scene/WinIsland-1.2.6alpha-c2.exe -ArgumentList '--mods-test verification/mods' -WindowStyle Hidden -PassThru -Wait
$p.ExitCode
```

同样使用 --self-test、--animation-test 检查现有逻辑与保留引擎。测试使用副本内隔离目录，不改日常配置。时间显示插件未修改；source/tests/scene-fixture.cpp 仅用于宿主 API 验证，非用户插件升级。

最终测试和产物哈希见 verification/ 与 build-manifest.json。
