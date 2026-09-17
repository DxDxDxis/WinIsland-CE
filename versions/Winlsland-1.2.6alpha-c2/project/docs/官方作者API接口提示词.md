# 官方作者 API 接口规范与实现提示词

请以当前 `source/src/mod_api.h`、`scene_api.h`、`scene_store.*`、`scene_host.inc`、`scene_render.inc` 和 `开放场景API.md` 为实现依据，为 WinIsland 官方版本维护可兼容、可验证的插件接口。

## 兼容前提

保留生命周期 ABI `0x00010002` 和原 POD 函数表前缀。新能力只能通过 `size` 检查后的 `queryInterface` 提供，例如 `winisland.scene/version=1`、`winisland.settings/version=1`。禁止用 C++ 虚类、STL、异常、裸 HWND、Direct2D/D3D11 对象或不明确的跨 CRT 所有权作为稳定接口。

## 场景接口要求

官方实现必须真正连接节点存储、属性覆盖、布局、Direct2D 绘制、统一命中、状态快照、输入事件和 owner 撤销。至少实现 Text、Image、Icon、Button、Container、Shape、Progress、Slider、Input、CustomDraw，以及矩形、父子、圆角、颜色、字体、透明度、缩放、旋转、裁剪、可见性、输入开关和 z 顺序。根节点支持 HostManaged、Overlay、PluginManaged、Intrinsic，并明确各模式对窗口尺寸和命中区域的影响。

## 状态与共存要求

快照必须公开待机、音乐、歌词、通知、展开、设置页面、DPI、岛位置/尺寸、可见节点和代次；事件必须覆盖内容开始与结束。每次修改必须记录 owner、target、property、priority、sequence。禁用或卸载只清理对应 owner 并重算剩余层，禁止整页旧快照恢复。

## 设置和保护边界

原生设置框架、安装器、加载器、安全校验、托盘、权限和开机启动继续由宿主控制。插件可通过独立设置表注册文本、数字、开关和选项。开放场景代表灵动岛内容可扩展，不代表任意 DLL 的崩溃、死循环或内存破坏可以被隔离。

## 错误、线程和资源

每个函数必须定义线程、所有权、句柄生命周期和错误码。缺失能力返回 `UNSUPPORTED`，无效句柄返回 `NOT_FOUND`，代次不一致返回 `CONFLICT`，超出节点、属性、位图或绘图命令预算返回 `LIMIT`。提交的数据由宿主复制，渲染线程不得持有插件内存。卸载前必须停止并排空回调、定时器、输入和绘图注册。

## 发布和验收

每项 API 扩展都要附可编译头文件、宿主实现、最小 DLL 示例、错误路径和回归测试。测试真实渲染、输入、DPI、窗口恢复、双 owner 覆盖、撤销、旧插件兼容、重复启停、资源上限和主程序退出。文档与能力查询结果必须一致；未实现、实验性和当前不开放的能力必须明确标注，不能用接口存在代替功能完成。
