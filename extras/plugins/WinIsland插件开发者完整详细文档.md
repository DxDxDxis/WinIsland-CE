# WinIsland 插件开发者完整详细文档

> 当前参考版本：`1.2.6alpha-c2-open-scene`。本文件是开发者手册；交接专用内容见同目录的《时间显示插件_AI交接完整详细版.md》。

## 1. 插件是什么

WinIsland 插件是由主程序加载的 x64 DLL。它不能脱离主程序运行，不能用独立 EXE、独立悬浮窗或私有内存读取来替代正式接口。主程序负责加载、缓存、线程、渲染、设置持久化、日志和卸载；插件负责业务逻辑、自己的场景内容、设置、资源和清理。

开发适配必须在新目录完成，保留旧源码、包、缓存和用户配置。每次记录目标 EXE、SDK、DLL 和 wimod 的绝对路径与 SHA256。

## 2. 插件工程

```text
my-plugin/
├─ src/                 DLL 源码
├─ sdk/                 目标宿主的 mod_api.h、scene_api.h
├─ package/mod.json
├─ package/bin/my-plugin.dll
├─ package/README.md
├─ package/LICENSE
├─ tests/
├─ verification/
├─ build.ps1
└─ my-plugin.wimod
```

源码、构建对象、发布包和验证产物分开。不能从旧缓存复制 DLL 充当新构建结果。

## 3. mod.json

```json
{
  "id": "my-plugin",
  "name": "我的插件",
  "version": "1.0.0",
  "author": "作者",
  "description": "插件功能简介。",
  "entry": "bin/my-plugin.dll",
  "apiVersion": 1,
  "dependencies": []
}
```

id 是稳定身份，只允许 ASCII 字母、数字、短横线、下划线，最长 80 个字符；它决定配置、日志、缓存和依赖。中文放 name。version 使用三段版本号。entry 是包内相对路径。apiVersion 当前必须为数字 1。gameVersion 只有在确认宿主版本比较器支持的语法后才能填写。dependencies 可以是 ID 字符串或含 id/version 的对象。

## 4. 生命周期和导出

当前 ABI 为 `0x00010002u`。DLL 必须导出：

```cpp
extern "C" __declspec(dllexport) uint32_t WinIsland_ModAbi();
extern "C" __declspec(dllexport)
IWinIslandMod* WinIsland_CreateMod(const WinIslandHostApi*);
```

工厂返回含 size、version、context、onLoad、onEnable、onDisable、onUnload、destroy 的表。插件自己分配和释放 context；宿主调用 destroy。回调返回 0 表示成功。回调必须可重复清理，不跨 DLL 抛异常。

onLoad 用来查询能力和初始化；onEnable 注册设置、事件、定时器并创建场景节点；onDisable 清理自己的状态；onUnload 释放剩余本地资源。宿主可能在 onDisable 前先撤销资源，所以清理函数要接受 WI_STOPPED、NOT_FOUND 等合法结果，不能无限重试。

## 5. 接口调用规则

结构体先清零，填写 size/version。所有字符串为 UTF-8，调用期间借用。扩展接口真实形状：

```cpp
int (*queryInterface)(void*, const char*, uint32_t, void*, uint32_t);
```

调用前检查宿主表 size 和函数指针，再检查输出表 size/version。不要伪造 owner；context 已绑定当前插件。当前回调在加载器工作线程串行执行，onDraw 也不是 GPU 线程；不要从自建线程调用宿主。

## 6. 开放场景 API

基本流程是：

```text
snapshot → find/create → read/set/clear → commit
```

create/set 只写待提交计划，commit 后渲染器才接收复制数据。普通节点的 WI_RECT 是父节点相对 DIP，顺序为 x、y、width、height；不要再次乘 DPI。visible 是最近成功渲染区域，不等同于刚创建的节点。

generation 冲突时重新 snapshot 并有限重试。句柄不能永久缓存，WI_NOT_FOUND 时清除并在合理时机重建。根节点的缩放、旋转和阴影当前不支持；WI_SIZE_MODE 只写根节点。属性层按 owner、priority、sequence 计算。

删除自己的节点是真删除；对其他 owner 的节点删除是可逆隐藏层。禁用时只撤销本插件的层、节点、输入和绘图，不能恢复一份启动时旧场景。

## 7. 状态、待机和设置

当前 flags 包括 idle、music、playing、lyric、notice、expanded、settingsOpen、reducedMotion、softwareRenderer。当前 WI_IDLE 主要由没有音乐和通知推导，不是所有内容为空的证明。显示类插件至少排除音乐、播放、歌词、通知、展开和设置；有正式占用接口时优先使用。必须排除自己的节点，避免显隐循环。

设置接口支持 define/read/write 和 text、number、switch、choice。switch 当前规范值是 0/1。设置 key 使用 ASCII 字母、数字、短横线、下划线；宿主保存 settings.xml，插件不要直接写 XML。当前定义结构没有 callback 字段；没有通知时，通过宿主定时器重新 read。首次没有保存值才使用默认值，旧 true/false 与 1/0 迁移不得改变用户意图。

## 8. 时间显示插件规范

```text
id=time-display
name=时间显示
author=daxian
version=1.0.0
key=time-display-enabled
格式=2026.9.21 12:05
```

使用 Windows 本地时间 API，格式为年.月.日 空格 24小时:分钟，不显示秒。处理午夜、月底、闰日、系统时间调整、时区变化和休眠恢复。可用约 1 秒宿主定时器检查，但只有文字、显隐或布局变化时才 set/commit。

节点使用普通 WI_TEXT，不接收和阻塞输入，不写根尺寸，不申请 Intrinsic 扩容。验证短日期、长日期、窄尺寸、100/125/150/200% DPI、硬件和软件渲染。音乐、歌词、通知和展开内容存在时隐藏，回到待机恢复。

## 9. 资源、错误和共存

资源种类包括设置、事件、定时器、替换、任务和动画。add 返回句柄，场景/设置成功码是 WI_OK=0，生命周期回调成功码也是 0，但旧 getSetting 成功值为 1，不能统一判断。

重点错误：INVALID 参数无效；NOT_FOUND 目标暂时不存在或句柄失效；VERSION size/version 不匹配；THREAD 调用线程错误；STOPPED 正在关闭；LIMIT 超限；UNSUPPORTED 能力缺失；CONFLICT generation 或层冲突。关闭一个无依赖插件不能影响其他插件。不要替其他插件释放资源，不要整体恢复旧场景。

## 10. 打包和完成标准

wimod 是 ZIP，根目录直接放 mod.json：

```text
time-display.wimod
├─ mod.json
├─ bin/time-display.dll
├─ README.md
└─ LICENSE
```

不能有外层目录、绝对路径、..、重复路径、符号链接或加密条目。当前宿主会解压到版本/哈希缓存，并在数据目录保存设置、状态和日志；单文件分发不代表运行时不落地。检查 x64 PE、导出、依赖、入口路径、包内 DLL 与本次构建 DLL 的哈希。

完成必须同时满足：真实宿主加载成功；时间在正确状态真实可见并在忙碌状态隐藏；设置重启/重载保留；多轮启停无重复资源和残留 UI；不影响其他插件；源码和包可复现；验证报告区分静态、加载器和真实屏幕结果，未测项明确写出。

