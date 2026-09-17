# C++ 内嵌 JavaScript 动画引擎方案

## 1. 方案目标

在保留现有 C++/Win32/Direct2D/DirectComposition 架构的基础上，引入 JavaScript 动画脚本，用于扩展设置界面和灵动岛的状态动画。C++ 继续负责窗口、渲染、音乐、歌词、消息、输入和后台任务；JavaScript 只负责动画时间轴、缓动和状态过渡。

## 2. 工作原理

```text
C++ 状态机
    ↓ 发送受控状态
JavaScript 动画运行时
    ↓ 返回动画属性
C++ 校验并应用
    ↓
Direct2D / DirectComposition 绘制
```

C++ 将以下状态发送给 JavaScript：

- `idle`
- `music`
- `music-expanded`
- `notice`
- `music-notice`
- `settings-page-change`
- `long-press`

JavaScript 根据状态创建和控制时间轴，例如宽度、高度、圆角、透明度、位移、缩放、内容延迟和分阶段入场。JavaScript 不直接访问 Win32、播放器、文件系统或网络。

## 3. 推荐依赖

### JavaScript 引擎

推荐 **V8 Embedded**：

- 支持现代 JavaScript。
- 适合运行 GSAP Core、Timeline、Tween 和 Easing。
- 可通过 Isolate 限制脚本内存和执行时间。
- 需要随发布包提供 V8 运行库和数据文件。

如果更重视体积，可以改用 QuickJS-ng；但使用完整 GSAP 时需要先验证兼容性。

### 动画库

推荐加入：

- GSAP Core
- GSAP Tween
- GSAP Timeline
- GSAP Easing
- GSAP Flip（需要自行提供布局状态数据）
- GSAP MotionPath（仅在确实需要路径动画时加入）

GSAP 只负责计算动画值，不直接操作 DOM 或浏览器窗口。依赖 DOM 的插件不能直接照搬到纯 C++ 环境。

## 4. 需要新增的文件

建议新增以下文件：

```text
src/animation_runtime.h       // 动画运行时接口
src/animation_runtime.cpp     // V8 初始化、脚本加载和执行
src/animation_bridge.h        // C++ 与 JavaScript 的数据桥
src/animation_bridge.cpp      // 状态、属性和事件转换
src/animation_scheduler.h     // 动画时间轴调度和取消
src/animation_scheduler.cpp   // 帧时间、暂停、恢复和反向播放
animations/island.js          // 灵动岛动画脚本
animations/settings.js        // 设置界面动画脚本
animations/presets.js         // 缓动和常用动画预设
animations/manifest.json      // 脚本版本和校验信息
third_party/v8/               // V8 库、头文件和运行数据
third_party/gsap/             // GSAP 文件及许可证
```

如果使用 QuickJS-ng，将 `third_party/v8` 替换为 `third_party/quickjs-ng`，其余接口保持不变。

## 5. C++ 与 JavaScript 接口

C++ 至少提供以下接口：

```text
animation.start(name, options)
animation.cancel(name)
animation.reverse(name)
animation.pause(name)
animation.resume(name)
animation.setReducedMotion(enabled)
animation.setPerformanceMode(mode)
```

脚本只能返回白名单属性：

```text
x, y, width, height, radius,
opacity, scale, offsetX, offsetY,
contentOpacity, lyricOpacity, noticeOpacity
```

C++ 必须校验数值范围、时间长度和目标对象，禁止脚本直接修改 HWND、窗口样式、输入区域、文件路径或进程权限。

## 6. 可实现的效果

- 设置分类页面左右滑动切换。
- 标题、卡片、控件由上到下分阶段进入。
- 灵动岛从待机状态自然展开为音乐状态。
- 消息区域从音乐容器底部向下延展。
- 歌词淡入、滑入和切歌时的平滑替换。
- 音乐封面、歌曲文字和竖条的错峰入场。
- 长按时轻微缩放、亮度变化和边框反馈。
- 快速切换时动画反向播放或从当前进度继续。
- 圆角、顶部外扩弧度和透明度连续变化。

## 7. 性能和线程规则

- JavaScript 不得运行在窗口消息线程中。
- 动画脚本运行在独立线程或受控 Isolate 中。
- C++ 主线程只接收经过验证的动画属性。
- 位移、缩放和透明度优先交给 DirectComposition。
- 文字、歌词、封面和频谱继续由 Direct2D 绘制。
- 静态文字布局、按钮路径和圆角几何应缓存。
- 没有属性变化时不提交新帧。
- 脚本超时、异常或内存超限时，自动取消脚本并回退到 C++ 基础动画。
- 系统启用“减少动态效果”或检测到低性能时，降低帧率并关闭非必要效果。

## 8. 资源和发布注意事项

- V8、GSAP 及其插件必须随发布包提供，并记录版本和许可证。
- 脚本文件应校验 SHA-256，加载失败时使用内置 C++ 默认动画。
- 不依赖 Node.js、浏览器、CEF 或 WebView2。
- 不允许脚本发起网络请求、读取聊天内容或保存账号凭据。
- 动画脚本与应用版本绑定，脚本版本不匹配时禁止加载。
- 需要提供禁用脚本动画的兼容开关，确保核心功能可用。
- 发布前应在 DirectComposition 硬件模式和 Direct2D 软件模式分别验证。

## 9. 主要限制

- GSAP 的 DOM、CSS、ScrollTrigger 等浏览器功能不能直接使用。
- V8 会增加安装包体积和常驻内存。
- JavaScript 动画不能替代 C++ 状态机、窗口命中测试或播放器接口。
- 复杂脚本必须设置执行预算，否则可能造成动画延迟。
- GSAP 高级插件的授权和分发许可需要单独确认。

## 10. 推荐落地顺序

1. 先实现 V8 初始化、脚本加载、超时和异常回退。
2. 接入统一 `AnimationScheduler`，只支持宽高、圆角、透明度和位移。
3. 接入 GSAP Core、Timeline 和 Easing。
4. 迁移设置页面切换动画。
5. 迁移灵动岛展开、消息延展和歌词入场动画。
6. 通过性能测试后，再考虑 Flip 或 MotionPath 等插件。


