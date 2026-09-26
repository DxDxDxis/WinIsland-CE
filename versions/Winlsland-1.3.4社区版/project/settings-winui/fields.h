#pragma once
struct Field {int page; wchar_t const *key,*name,*hint; int type; double min,max; std::vector<winrt::hstring> options;};
static std::vector<Field> const fields={
{0,L"resident",L"灵动岛常驻",L"关闭后，仅在收到通知时滑入屏幕",0,0,0,{}},
{0,L"hideNative",L"隐藏 Windows 自带通知",L"保留通知中心记录；只在核心健康运行时接管横幅",0,0,0,{}},
{0,L"showMessages",L"在灵动岛显示消息",L"仅控制灵动岛消息；与隐藏 Windows 自带通知独立。关闭后丢弃待显示消息。",0,0,0,{}},
{0,L"seconds",L"通知停驻时间",L"0.1–3600 秒，支持小数",1,.1,3600,{}},
{0,L"fps",L"动画帧率",L"0 跟随系统；或 30–240 的整数，仅控制灵动岛动画",1,0,240,{}},
{1,L"songSource",L"歌曲信息来源",L"",3,0,0,{L"自动（SMTC → 窗口标题）",L"仅 SMTC",L"仅窗口标题"}},
{1,L"playerFilter",L"播放器",L"支持 SMTC 与窗口标题自动识别；可手动限定来源。",3,0,0,{L"自动识别",L"网易云音乐",L"QQ 音乐",L"汽水音乐",L"酷狗音乐",L"Apple Music"}},
{1,L"lyricSource",L"歌词来源",L"",3,0,0,{L"自动识别（缓存优先）",L"仅 LRCLIB（在线）",L"关闭歌词",L"仅网易云歌词",L"仅 LRCLIB 搜索",L"网易云（兼容旧配置）",L"QQ 音乐",L"酷狗音乐",L"汽水音乐",L"Apple Music"}},
{1,L"lyricApi",L"歌词 API 地址",L"兼容 LRCLIB 的 HTTPS 服务；在线查询发送歌曲信息",2,0,0,{}},
{1,L"showMusic",L"在灵动岛显示音乐",L"关闭音乐卡片与歌词显示，不暂停或改变播放器。",0,0,0,{}},
{2,L"showFps",L"显示帧率（FPS）",L"前台应用 DXGI 提交帧率；不支持时显示 --",0,0,0,{}},
{2,L"showPing",L"显示网络延迟（Ping）",L"每 3 秒检测 ICMP 往返延迟",0,0,0,{}},
{2,L"pingTarget",L"Ping 目标",L"IPv4 / IPv6；留空使用本地网关",2,0,0,{}},
{9,L"settingsReduceMotion",L"降低动画",L"减少设置窗口内的页面、控件和弹窗动效，不影响灵动岛及插件。",0,0,0,{}},
{9,L"settingsDisableBlur",L"关闭模糊效果",L"关闭设置窗口的玻璃效果，使用当前主题对应的不透明纯色背景。",0,0,0,{}},
{4,L"islandZoom",L"灵动岛缩放比例",L"",1,1,2,{}},
{4,L"widthRatio",L"宽度倍率",L"",1,.75,1.5,{}},
{4,L"heightRatio",L"高度倍率",L"",1,.75,1.5,{}},
{4,L"dpiCorrection",L"DPI 尺寸修正",L"不改变工作区坐标",1,.8,1.25,{}},
{4,L"topAttach",L"顶部贴合工作区",L"",0,0,0,{}},
{4,L"radiusMode",L"圆角模式",L"",3,0,0,{L"自动（贴顶平直，下角圆润）",L"半圆下角",L"2/3 圆度"}},
{4,L"monitorDevice",L"显示器",L"",4,0,0,{}},
{4,L"topArcScale",L"顶部外扩圆弧",L"0.5–1.5 倍，默认 1.0；仅影响贴顶外弧",1,.5,1.5,{}}
};
