#include "render.h"
#include <commctrl.h>
#include <gdiplus.h>
#include <shellscalingapi.h>
#include <winrt/base.h>
namespace wi {
struct Geo {
    double w = 180.4, h = 29.92, r = 14.96, mh = 0, ma = 0, na = 0, offset = 0;
};
struct MonitorResult {
    std::wstring message, path;
};
class App;
static App *app = nullptr;
class App {
  public:
    HWND hwnd = nullptr, settingsWindow = nullptr, tooltip = nullptr;
    std::wstring tooltipText;
    int modalFrames = 0;
    std::unique_ptr<Renderer> renderer;
    Settings settings;
    Jobs jobs;
    std::unique_ptr<Media> media;
    std::unique_ptr<Notices> notices;
    std::unique_ptr<Audio> audio;
    ToastHelper native;
    Scene scene;
    Geo geo, from, to;
    double animStart = 0, animSeconds = .4, holdUntil = 0, pausedAt = 0, feedbackUntil = 0, lastFrame = 0;
    bool closingNotice = false, animating = false, awaitHold = false, switching = false, hasMusic = false,
         healthy = false, suspended = false, diagnostic = false, overrideMusic = false, running = true,
         dirty = true;
    int delivered = 0, noticesShown = 0, commandsProcessed = 0, actionCount = 0, lastAction = -1;
    double autoFps = 60;
    float dpi = 1;
    double availableWidth = 616, hostHeight = 432;
    POINT origin{};
    std::deque<std::shared_ptr<Notice>> queue;
    std::shared_ptr<Music> rawMusic;
    fs::path testDir, config, store;
    NOTIFYICONDATAW tray{sizeof(tray)};
    HFONT settingsFont = nullptr, smallFont = nullptr, titleFont = nullptr;
    float settingsDpi = 1;
    int settingsScroll = 0;
    bool settingsCreating = false, monitorPending = false;
    int invalidInput = 0;
    std::wstring monitorMessage = L"尚未开始记录", exportPath;
    float testDpi = 0;
    std::mutex testMu;
    std::shared_ptr<Lyrics> testLyrics;
    bool testLyricsReady = false;
    HANDLE timer = nullptr;
    UINT taskbarMessage = RegisterWindowMessageW(L"TaskbarCreated");
    ~App() {
        jobs.finish();
        native.enable(false);
        notices.reset();
        media.reset();
        audio.reset();
        if (timer)
            CloseHandle(timer);
        if (settingsFont)
            DeleteObject(settingsFont);
        if (smallFont)
            DeleteObject(smallFont);
        if (titleFont)
            DeleteObject(titleFont);
    }
    void startup(const fs::path &dir, bool software) {
        testDir = dir;
        diagnostic = !dir.empty();
        config = (diagnostic ? dir : dataDir()) / L"settings.xml";
        store = (diagnostic ? dir : dataDir()) / L"lyrics";
        settings.load(config);
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
        wc.lpszClassName = L"WinIsland.Native";
        RegisterClassExW(&wc);
        hwnd = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP | WS_EX_NOACTIVATE, wc.lpszClassName,
            L"WinIsland", WS_POPUP, 0, 0, 656, 432, nullptr, nullptr, wc.hInstance, nullptr);
        if (!hwnd)
            throw std::runtime_error("window creation");
        INITCOMMONCONTROLSEX common{sizeof(common), ICC_WIN95_CLASSES};
        InitCommonControlsEx(&common);
        tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP, 0, 0, 0,
                                  0, hwnd, nullptr, wc.hInstance, nullptr);
        TOOLINFOW tool{sizeof(tool)};
        tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        tool.hwnd = hwnd;
        tool.uId = (UINT_PTR)hwnd;
        tool.lpszText = LPSTR_TEXTCALLBACKW;
        SendMessageW(tooltip, TTM_ADDTOOLW, 0, (LPARAM)&tool);
        renderer = std::make_unique<Renderer>(hwnd, software);
        position();
        tray.hWnd = hwnd;
        tray.uID = 1;
        tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        tray.uCallbackMessage = WM_TRAY;
        tray.hIcon = wc.hIcon;
        wcscpy_s(tray.szTip, L"WinIsland 1.2.3beta");
        Shell_NotifyIconW(NIM_ADD, &tray);
        WTSRegisterSessionNotification(hwnd, NOTIFY_FOR_THIS_SESSION);
        audio = std::make_unique<Audio>();
        media = std::make_unique<Media>(hwnd, store, diagnostic);
        notices = std::make_unique<Notices>(hwnd, diagnostic && fs::exists(dir / L"notifications.db")
                                                      ? dir / L"notifications.db"
                                                      : fs::path{});
        applySettings();
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        if (!diagnostic) {
            HKEY key;
            if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0,
                                nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
                auto path = L"\"" + exePath().wstring() + L"\"";
                RegSetValueExW(key, L"WinIsland", 0, REG_SZ, (BYTE *)path.c_str(),
                               (DWORD)(path.size() + 1) * 2);
                RegCloseKey(key);
            }
        } else {
            fs::create_directories(dir);
            writeAtomic(dir / L"ready.txt", std::to_string(GetCurrentProcessId()));
        }
        timer = CreateWaitableTimerExW(nullptr, nullptr, 2, TIMER_ALL_ACCESS);
        if (!timer)
            timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    }
    void position() {
        autoFps = detectRate();
        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);
        GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &info);
        UINT dx = 96, dy = 96;
        GetDpiForMonitor(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), MDT_EFFECTIVE_DPI, &dx, &dy);
        dpi = diagnostic && testDpi > 0 ? testDpi : dx / 96.f;
        availableWidth =
            std::max(180.4, std::min<double>(616., (info.rcMonitor.right - info.rcMonitor.left) / dpi - 32));
        hostHeight = std::max(96., std::min(432., (info.rcMonitor.bottom - info.rcMonitor.top) / dpi * .65));
        int width = (int)std::min(656. * dpi, (double)info.rcMonitor.right - info.rcMonitor.left);
        origin = {info.rcMonitor.left + (info.rcMonitor.right - info.rcMonitor.left - width) / 2,
                  info.rcMonitor.top + (int)(8 * dpi)};
        if (diagnostic)
            origin.y += (int)(160 * dpi); // Keep automated pointer tests clear of the user's running island.
        SetWindowPos(hwnd, HWND_TOPMOST, origin.x, origin.y + (int)(geo.offset * dpi), width,
                     (int)std::ceil(hostHeight * dpi), SWP_NOACTIVATE);
        if (renderer)
            renderer->resize(width, (UINT)std::ceil(hostHeight * dpi), dpi);
        retarget(.3);
        dirty = true;
    }
    double rate() {
        return settings.fps ? settings.fps : renderer->fallback() ? 60 : autoFps;
    }
    double detectRate() {
        SYSTEM_INFO sys;
        GetSystemInfo(&sys);
        MEMORYSTATUSEX memory{sizeof(memory)};
        GlobalMemoryStatusEx(&memory);
        if (renderer->fallback() || sys.dwNumberOfProcessors <= 2 ||
            memory.ullTotalPhys < 4ull * 1024 * 1024 * 1024)
            return 60;
        DWM_TIMING_INFO timing{sizeof(timing)};
        if (SUCCEEDED(DwmGetCompositionTimingInfo(nullptr, &timing)) && timing.rateRefresh.uiDenominator &&
            timing.rateRefresh.uiNumerator)
            return (double)timing.rateRefresh.uiNumerator / timing.rateRefresh.uiDenominator;
        DEVMODEW mode{};
        mode.dmSize = sizeof(mode);
        if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency > 1)
            return (int)mode.dmDisplayFrequency;
        return 60;
    }
    void applySettings() {
        autoFps = detectRate();
        if (hwnd)
            retarget(.4);
        native.enable(settings.hideNative && healthy && !suspended && !diagnostic);
        if (!awaitHold && scene.notice && holdUntil > 0)
            holdUntil = std::min(holdUntil, now() + settings.seconds);
        dirty = true;
    }
    void saveSettings() {
        monitor.event("设置更改；FPS=" + std::to_string(settings.fps));
        auto s = settings;
        auto p = config;
        jobs.post([s, p] { s.save(p); });
        applySettings();
    }
    double musicWidth() {
        if (!hasMusic || !scene.music)
            return 180.4;
        auto l = scene.music->lyrics;
        return std::min(availableWidth, l ? (scene.expanded ? l->expanded : l->compact) : 460.46);
    }
    double musicHeight() {
        return hasMusic ? (scene.expanded ? (scene.music && scene.music->timeline ? 121.8 : 105) : 48.384)
                        : 0;
    }
    void retarget(double seconds) {
        Measure probe(LayoutWork);
        if (!renderer)
            return;
        Geo target;
        target.w = musicWidth();
        target.mh = musicHeight();
        target.h = hasMusic ? target.mh : 29.92;
        target.ma = hasMusic ? 1 : 0;
        target.r = hasMusic ? (scene.expanded ? 23.1 : 21) : 14.96;
        target.offset = (!scene.notice || closingNotice) && !hasMusic && !settings.resident ? -45.92 : 0;
        if (scene.notice && !closingNotice) {
            auto &n = *scene.notice;
            double tw =
                n.title.size() > 100 ? availableWidth : renderer->textWidth(n.title, 14, true) * 16 / 14;
            double bw = n.body.size() > 100 ? availableWidth : renderer->textWidth(n.body, 12) * 14 / 12;
            target.w =
                std::max(target.w, std::clamp(std::ceil(std::max(tw, bw)) + 48, 180.4, availableWidth));
            scene.noticeTextWidth = target.w - 48;
            double titleH = std::min(76.8, renderer->textHeight(n.title, 14, scene.noticeTextWidth, true));
            double bodyH = n.body.empty() ? 0 : renderer->textHeight(n.body, 12, scene.noticeTextWidth) + 6.4;
            target.h = target.mh + std::min(hostHeight - target.mh, std::max(52.8, titleH + bodyH + 33.6));
            scene.noticeBodyHeight = std::max(0., target.h - target.mh - titleH - 40);
            target.na = switching ? 0 : 1;
            target.r = 28;
        }
        if (std::abs(target.w - to.w) < .01 && std::abs(target.h - to.h) < .01 && target.mh == to.mh &&
            target.ma == to.ma && target.na == to.na && target.offset == to.offset && target.r == to.r)
            return;
        monitor.event("动画目标更新；宽=" + std::to_string(target.w) + " 高=" + std::to_string(target.h));
        from = geo;
        to = target;
        animStart = now();
        animSeconds = seconds;
        animating = true;
        dirty = true;
    }
    void onMusic(std::shared_ptr<Music> m) {
        Measure probe(UiWork);
        if ((!rawMusic) != (!m) ||
            (m && rawMusic && (m->key() != rawMusic->key() || m->playing != rawMusic->playing)))
            monitor.event(std::string("音乐来源/状态更新；存在=") + (m ? "1" : "0") +
                          " 播放=" + (m && m->playing ? "1" : "0"));
        rawMusic = m;
        if (audio)
            audio->select(m.get());
        if (m && m->playing)
            pausedAt = 0;
        else if (m && !pausedAt)
            pausedAt = now();
        bool show = m && (m->playing || !pausedAt || now() - pausedAt < 10);
        bool presence = show != hasMusic;
        hasMusic = show;
        if (show)
            scene.music = m;
        if (!m)
            pausedAt = 0;
        if (presence) {
            auto style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
            SetWindowLongPtrW(hwnd, GWL_EXSTYLE, show ? style & ~WS_EX_NOACTIVATE : style | WS_EX_NOACTIVATE);
        }
        retarget(presence ? .56 : .4);
        updateSettingsMusic();
        dirty = true;
    }
    void enqueue(Notice n) {
        if (suspended)
            return;
        monitor.event("收到新通知；当前待展示=" + std::to_string(queue.size()));
        auto p = std::make_shared<Notice>(std::move(n));
        if (!scene.notice) {
            setNotice(p);
            return;
        }
        if (scene.notice->key() == p->key()) {
            if (scene.notice->title != p->title || scene.notice->body != p->body) {
                scene.notice = p;
                retarget(.3);
            }
            return;
        }
        for (auto &q : queue)
            if (q->key() == p->key()) {
                q = p;
                return;
            }
        queue.push_back(p);
    }
    void setNotice(std::shared_ptr<Notice> n) {
        scene.notice = n;
        ++noticesShown;
        monitor.event("通知进入展示；待展示=" + std::to_string(queue.size()));
        closingNotice = false;
        switching = false;
        holdUntil = 0;
        awaitHold = true;
        if (n->handler == 12)
            delivered++;
        retarget(.52);
    }
    void finishNotice() {
        holdUntil = 0;
        awaitHold = false;
        if (queue.empty()) {
            closingNotice = true;
            retarget(.46);
        } else {
            switching = true;
            retarget(.14);
        }
    }
    void tick() {
        double time = now();
        if (hasMusic && rawMusic && !rawMusic->playing && pausedAt && time - pausedAt >= 10) {
            hasMusic = false;
            retarget(.56);
        }
        if (holdUntil && time >= holdUntil)
            finishNotice();
        if (animating) {
            double t = std::min(1., (time - animStart) / animSeconds), p = ease(t);
            geo.w = mix(from.w, to.w, p);
            geo.h = mix(from.h, to.h, p);
            geo.r = mix(from.r, to.r, p);
            geo.mh = mix(from.mh, to.mh, p);
            geo.offset = mix(from.offset, to.offset, p);
            double fa = to.na > from.na ? ease((t - .16) / .84) : ease(t * 2);
            geo.na = mix(from.na, to.na, fa);
            geo.ma = mix(from.ma, to.ma, to.ma > from.ma ? ease((t - .16) / .84) : ease(t * 1.6));
            dirty = true;
            if (t >= 1) {
                geo = to;
                animating = false;
                if (!hasMusic) {
                    scene.music.reset();
                    scene.expanded = false;
                }
                if (closingNotice) {
                    scene.notice.reset();
                    closingNotice = false;
                    if (!queue.empty()) {
                        auto n = queue.front();
                        queue.pop_front();
                        setNotice(n);
                    }
                } else if (switching) {
                    auto n = queue.front();
                    queue.pop_front();
                    setNotice(n);
                } else if (awaitHold && scene.notice) {
                    awaitHold = false;
                    holdUntil = time + settings.seconds;
                }
            }
        }
        if (scene.music) {
            auto previousLyric = scene.lyric;
            scene.lyric = scene.music->timeline && scene.music->lyrics
                              ? scene.music->lyrics->at(scene.music->progress())
                              : L"";
            if (scene.lyric != previousLyric)
                monitor.event("歌词时间轴行更新");
            if (feedbackUntil && time >= feedbackUntil) {
                scene.feedback.clear();
                feedbackUntil = 0;
            }
            auto f = audio->snapshot();
            bool valid = f.source == scene.music->source && f.available;
            for (int i = 0; i < 6; i++) {
                double peak = scene.music->playing && valid && !f.muted ? f.peaks[i] : 0;
                double target =
                    .08 + .92 * std::clamp((20 * std::log10(std::max(.001, peak)) + 60) / 60, 0., 1.);
                double dt = lastFrame ? std::clamp(time - lastFrame, .001, .1) : 1. / 60;
                if (std::abs(target - scene.bars[i]) > .0005)
                    dirty = true;
                scene.bars[i] += (float)((target - scene.bars[i]) *
                                         (1 - std::pow(target > scene.bars[i] ? .5 : .8, dt * 60)));
            }
            if (hasMusic && scene.music->playing)
                dirty = true;
        }
        if (dirty && !suspended) {
            scene.width = geo.w;
            scene.height = geo.h;
            scene.radius = geo.r;
            scene.musicH = geo.mh;
            scene.musicAlpha = geo.ma;
            scene.noticeAlpha = geo.na;
            int y = origin.y + (int)std::lround(geo.offset * dpi);
            RECT r;
            GetWindowRect(hwnd, &r);
            if (y != r.top)
                SetWindowPos(hwnd, nullptr, origin.x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
            if (lastFrame && time - lastFrame < 5)
                perf.add(FrameGap, (time - lastFrame) * 1000);
            bool submitted = renderer->draw(scene);
            lastFrame = time;
            dirty = !submitted;
        } else if (!animating)
            lastFrame = 0;
    }
    void action(int id) {
        if (!hasMusic || !scene.music || id < 0 || id > 4)
            return;
        if (id && (!scene.expanded || scene.busy ||
                   !(id == 1   ? scene.music->prev
                     : id == 2 ? scene.music->toggle
                     : id == 3 ? scene.music->next
                               : scene.music->mode)))
            return;
        ++actionCount;
        lastAction = id;
        monitor.event("音乐操作 id=" + std::to_string(id));
        scene.focus = id;
        if (id == 0) {
            scene.expanded = !scene.expanded;
            retarget(.42);
        } else if (hasMusic && scene.music && !scene.busy) {
            scene.busy = true;
            media->command(id == 1   ? L"previous"
                           : id == 2 ? L"toggle"
                           : id == 3 ? L"next"
                                     : L"mode",
                           scene.music->key());
        }
        dirty = true;
    }
    void pause(bool p) {
        suspended = p;
        closingNotice = false;
        switching = false;
        awaitHold = false;
        queue.clear();
        scene.notice.reset();
        scene.busy = false;
        rawMusic.reset();
        hasMusic = false;
        scene.music.reset();
        scene.expanded = false;
        holdUntil = 0;
        animating = false;
        geo = to = Geo{};
        geo.offset = to.offset = settings.resident ? 0 : -45.92;
        audio->select(nullptr);
        media->pause(p);
        notices->pause(p);
        native.enable(!p && healthy && settings.hideNative && !diagnostic);
        ShowWindow(hwnd, p ? SW_HIDE : SW_SHOWNOACTIVATE);
        if (!p) {
            position();
            dirty = true;
        }
    }
    void updateMonitorControls() {
        if (!settingsWindow)
            return;
        SetDlgItemTextW(settingsWindow, 108,
                        monitorPending     ? L"正在处理…"
                        : monitor.active() ? L"停止信息监测"
                                           : L"启动信息监测");
        EnableWindow(GetDlgItem(settingsWindow, 108), !monitorPending);
        EnableWindow(GetDlgItem(settingsWindow, 109), !monitorPending);
        SetDlgItemTextW(settingsWindow, 110, monitorMessage.c_str());
        SetDlgItemTextW(settingsWindow, 111, exportPath.c_str());
    }
    void monitoring(int operation, fs::path overrideFolder = {}) {
        if (monitorPending)
            return;
        if (operation == 2 && !monitor.hasData()) {
            monitorMessage = L"没有可导出的记录，请先启动信息监测。";
            updateMonitorControls();
            return;
        }
        monitorPending = true;
        updateMonitorControls();
        std::ostringstream env;
        env << "渲染后端: "
            << (renderer->fallback() ? "Direct2D 软件" : "Direct3D11 / DirectComposition 硬件")
            << "\n界面缩放: " << dpi << "\n配置帧率: " << settings.fps << "，有效目标: " << rate()
            << "\n通知读取状态: " << healthy << "\n";
        jobs.post([this, operation, overrideFolder, environment = env.str()] {
            auto result = std::make_unique<MonitorResult>();
            try {
                if (operation == 0) {
                    monitor.start(environment);
                    result->message = L"正在记录 · 每 5 秒采样，不记录聊天正文";
                } else if (operation == 1) {
                    monitor.sample();
                    monitor.stop();
                    writeAtomic(dataDir() / L"monitor" / L"last-session.txt", monitor.report());
                    result->message = L"监测已停止，本次报告可导出。";
                } else {
                    monitor.sample();
                    fs::path dir =
                        overrideFolder.empty() ? exePath().parent_path() / L"daxian日志" : overrideFolder;
                    try {
                        result->path = monitor.exportTo(dir).wstring();
                        result->message = L"报告已导出，下方路径可选中复制。";
                    } catch (...) {
                        wchar_t documents[MAX_PATH];
                        if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, 0, documents)))
                            throw;
                        auto fallback = fs::path(documents) / L"daxian日志";
                        result->path = monitor.exportTo(fallback).wstring();
                        result->message = L"程序目录不可写，已改存到文档文件夹。";
                    }
                }
            } catch (...) {
                result->message = L"操作失败：请检查磁盘空间和目录权限，本次监测记录仍可重试导出。";
            }
            if (PostMessageW(hwnd, WM_APP + 8, 0, (LPARAM)result.get()))
                result.release();
        });
    }
    void openSettings();
    void layoutSettings();
    void updateSettingsMusic();
    void settingsChanged(int id);
    void importLyrics(bool clear);
    static LRESULT CALLBACK settingsProc(HWND, UINT, WPARAM, LPARAM);
    void quit() {
        if (!running)
            return;
        if (monitor.active())
            jobs.post([] {
                monitor.event("应用退出");
                monitor.sample();
                monitor.stop();
                writeAtomic(dataDir() / L"monitor" / L"last-session.txt", monitor.report());
            });
        running = false;
        native.enable(false);
        Shell_NotifyIconW(NIM_DELETE, &tray);
        WTSUnRegisterSessionNotification(hwnd);
        if (!diagnostic) {
            HKEY key;
            if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0,
                              KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
                RegDeleteValueW(key, L"WinIsland");
                RegCloseKey(key);
            }
        }
        if (settingsWindow)
            DestroyWindow(settingsWindow);
        DestroyWindow(hwnd);
    }
    void command(const std::string &c) {
        if (c == "music-stop") {
            overrideMusic = true;
            onMusic(nullptr);
        } else if (c == "music-live" || c == "media-fixture-only") {
            overrideMusic = false;
            media->fixtures = c == "media-fixture-only";
        } else if (c.rfind("phase=", 0) == 0)
            perf.setPhase(c.substr(6));
        else if (c == "perf-flush")
            jobs.post([] {
                perf.flush();
                if (monitor.active()) {
                    monitor.sample();
                    writeAtomic(dataDir() / L"monitor" / L"active-session.txt", monitor.report());
                }
            });
        else if (c == "perf-poll") {
            if (scene.music)
                retarget(.4);
        } else if (c == "perf-lyrics" && scene.music) {
            jobs.post([this] {
                std::string s;
                for (int i = 0; i < 4000; i++)
                    s += "[00:00.00]" +
                         utf8(L"性能测试歌词用于测量排版耗时，不是真实歌曲歌词，第" + std::to_wstring(i) +
                              L"行") +
                         "\n";
                auto l = Lyrics::parse(s);
                l->measure();
                std::lock_guard lock(testMu);
                testLyrics = l;
                testLyricsReady = true;
            });
        } else if (c.rfind("music=", 0) == 0) {
            overrideMusic = true;
            auto m = std::make_shared<Music>();
            m->source = L"WinIsland.MediaFixture";
            m->platform = L"媒体集成测试";
            m->title = c == "music=next" ? L"合成歌曲 B" : L"合成歌曲 A";
            m->artist = L"测试歌手";
            m->playing = c != "music=pause";
            m->paused = !m->playing;
            m->timeline = true;
            m->position = 15;
            m->duration = 180;
            m->stamp = now();
            onMusic(m);
        } else if (c == "music-expand" || c == "music-collapse") {
            scene.expanded = c == "music-expand";
            retarget(.42);
        } else if (c == "notice-short")
            enqueue({-20, 0, 0, L"完成", L"已收到"});
        else if (c == "notice-long")
            enqueue({-21, 0, 0, L"WinIsland 集成测试", L"这是一条用于验证灵动岛的 Windows 系统通知。"});
        else if (c == "notice-wide")
            enqueue({-22, 0, 0, L"较长消息布局验证",
                     L"这是一条较长的合成消息，用于检查同一个容器向下延伸、横向自适应和自动换行。音乐控制始终"
                     L"保留在上方，消息结束后恢复原来尺寸。"});
        else if (c == "burst")
            for (int i = 0; i < 12; i++)
                enqueue({-100 - i, 12, i, L"合成会话 " + std::to_wstring(i + 1), L"连续消息与音乐共存测试"});
        else if (c == "monitor-start")
            monitoring(0);
        else if (c == "monitor-stop")
            monitoring(1);
        else if (c == "monitor-export")
            monitoring(2);
        else if (c == "monitor-export-blocked")
            monitoring(2, testDir / L"blocked-export");
        else if (c.rfind("dpi=", 0) == 0) {
            testDpi = (float)std::clamp(std::stod(c.substr(4)), 1., 3.);
            position();
            if (settingsWindow) {
                settingsDpi = testDpi;
                layoutSettings();
            }
        } else if (c == "settings")
            openSettings();
        else if (c == "close-settings" && settingsWindow)
            DestroyWindow(settingsWindow);
        else if (c == "capture") {
            dirty = true;
            tick();
            renderer->capture(testDir / L"capture.png");
        } else if (c == "lyrics-fixture" && scene.music) {
            auto path = testDir / L"fixture.lrc";
            auto key = scene.music->key();
            jobs.post([this, path, key] {
                auto s = readFile(path);
                auto l = Lyrics::parse(s);
                if (l)
                    l->measure();
                writeAtomic(store / (sha256(utf8(key)) + ".lrc"), s);
                {
                    std::lock_guard lock(testMu);
                    testLyrics = l;
                    testLyricsReady = true;
                }
                media->reload();
            });
        } else if (c.rfind("frame=", 0) == 0) {
            int fps = std::stoi(c.substr(6));
            if (fps == 0 || (fps >= 30 && fps <= 240)) {
                settings.fps = fps;
                saveSettings();
            }
        } else if (c.rfind("seconds=", 0) == 0) {
            double n = std::stod(c.substr(8));
            if (n >= .1 && n <= 3600) {
                settings.seconds = n;
                saveSettings();
            }
        } else if (c.rfind("resident=", 0) == 0) {
            settings.resident = c == "resident=on";
            saveSettings();
        } else if (c.rfind("native=", 0) == 0) {
            settings.hideNative = c == "native=on";
            saveSettings();
        }
    }
    void diagnostics() {
        if (!diagnostic)
            return;
        Measure probe(DiagnosticIO);
        if (fs::exists(testDir / L"exit.request")) {
            quit();
            return;
        }
        auto path = testDir / L"command.txt";
        if (fs::exists(path)) {
            auto c = readFile(path, 4096);
            fs::remove(path);
            while (!c.empty() && isspace((unsigned char)c.back()))
                c.pop_back();
            try {
                command(c);
                ++commandsProcessed;
            } catch (...) {
            }
        }
        {
            std::lock_guard lock(testMu);
            if (testLyricsReady) {
                testLyricsReady = false;
                if (scene.music) {
                    scene.music = std::make_shared<Music>(*scene.music);
                    scene.music->lyrics = testLyrics;
                    retarget(.4);
                    dirty = true;
                }
            }
        }
        auto truth = [](bool b) { return b ? "True" : "False"; };
        std::ostringstream s;
        s << "ModalFrames=" << modalFrames << "\nActionCount=" << actionCount << "\nLastAction=" << lastAction
          << "\nCommandsProcessed=" << commandsProcessed << "\nNoticesShown=" << noticesShown
          << "\nVersion=1.2.3beta\nIdle=" << truth(!scene.notice && !animating)
          << "\nAnimating=" << truth(animating)
          << "\nHolding=" << truth(scene.notice && !awaitHold && !switching) << "\nWidth=" << geo.w
          << "\nHeight=" << geo.h << "\nMusicTop=0\nMusicHeight=" << geo.mh << "\nMusicWidth=" << geo.w
          << "\nNoticeTop=" << geo.mh << "\nMusicExpanded=" << truth(scene.expanded)
          << "\nHasMusic=" << truth(hasMusic)
          << "\nMusicPlaying=" << truth(hasMusic && scene.music && scene.music->playing)
          << "\nMusicVisible=" << truth(scene.music != nullptr) << "\nMusicWindow=" << (uintptr_t)hwnd
          << "\nPending=" << queue.size() << "\nSyntheticDelivered=" << delivered
          << "\nConfiguredFrameRate=" << settings.fps << "\nEffectiveFrameRate=" << rate()
          << "\nRenderer=" << (renderer->fallback() ? "software" : "DirectComposition")
          << "\nFixtureTitle=" << (scene.music ? utf8(scene.music->title) : "")
          << "\nFixtureLyric=" << utf8(scene.lyric) << "\nHasLyric=" << truth(!scene.lyric.empty())
          << "\nMusicControls="
          << (scene.music ? (int)scene.music->prev + (int)scene.music->toggle + (int)scene.music->next : 0)
          << "\nModeVisible=" << truth(scene.music && scene.music->mode) << "\nMode="
          << (scene.music ? (scene.music->shuffle       ? "随机播放"
                             : scene.music->repeat == 1 ? "单曲循环"
                             : scene.music->repeat == 2 ? "列表循环"
                                                        : "顺序播放")
                          : "")
          << "\nReaderHealthy=" << truth(healthy) << "\nBars=" << scene.bars[0] << "\n";
        auto f = audio->snapshot();
        s << "MonitorActive=" << truth(monitor.active()) << "\nMonitorPending=" << truth(monitorPending)
          << "\nMonitorHasData=" << truth(monitor.hasData()) << "\nMonitorMessage=" << utf8(monitorMessage)
          << "\nExportPath=" << utf8(exportPath) << "\n";
        s << "NoticeTitle=" << (scene.notice ? utf8(scene.notice->title) : "") << "\nNoticeAlpha=" << geo.na
          << "\nNoticeClosing=" << truth(closingNotice) << "\nBusy=" << truth(scene.busy)
          << "\nHasCover=" << truth(scene.music && scene.music->cover)
          << "\nSettingsWindow=" << (uintptr_t)settingsWindow << "\nDpi=" << dpi << "\n";
        for (auto &b : scene.buttons)
            s << "Button" << b.first << "=" << b.second.x + b.second.w / 2 << ","
              << b.second.y + b.second.h / 2 << "\n";
        s << "AudioAvailable=" << truth(f.available)
          << "\nAudioPeak=" << *std::max_element(f.peaks.begin(), f.peaks.end())
          << "\nAudioMuted=" << truth(f.muted) << "\n";
        // A diagnostics consumer may hold the old file without FILE_SHARE_DELETE.
        // A missed snapshot must never terminate the application or block an animation.
        try {
            writeAtomic(testDir / L"state.txt", s.str());
        } catch (...) {
        }
    }
    int run() {
        double nextFrame = now(), nextDiagnostic = 0, nextPerf = now() + 5;
        while (running) {
            MSG message;
            for (int batch = 0; batch < 64 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE); ++batch) {
                if (message.message == WM_QUIT) {
                    running = false;
                    break;
                }
                if (settingsWindow && IsDialogMessageW(settingsWindow, &message))
                    continue;
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            if (!running)
                break;
            double t = now();
            if (t >= nextDiagnostic) {
                nextDiagnostic = t + .1;
                diagnostics();
            }
            if (!running)
                break;
            if (t >= nextPerf) {
                nextPerf = t + 5;
                jobs.post([] {
                    perf.flush();
                    if (monitor.active()) {
                        monitor.sample();
                        writeAtomic(dataDir() / L"monitor" / L"active-session.txt", monitor.report());
                    }
                });
            }
            if (t >= nextFrame) {
                tick();
                double interval = 1. / rate();
                nextFrame += interval * (std::floor(std::max(0., t - nextFrame) / interval) + 1);
            }
            double wait = std::min(nextFrame, nextDiagnostic) - now();
            LARGE_INTEGER due;
            due.QuadPart = -(LONGLONG)std::max(1000., wait * 1e7);
            SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);
            MsgWaitForMultipleObjectsEx(1, &timer, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }
        perf.flush();
        return 0;
    }
    static LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        if (!app)
            return DefWindowProcW(h, msg, wp, lp);
        auto &a = *app;
        if (msg == a.taskbarMessage) {
            Shell_NotifyIconW(NIM_ADD, &a.tray);
            return 0;
        }
        switch (msg) {
        case WM_APP + 8: {
            std::unique_ptr<MonitorResult> r((MonitorResult *)lp);
            a.monitorPending = false;
            a.monitorMessage = r->message;
            if (!r->path.empty())
                a.exportPath = r->path;
            a.updateMonitorControls();
            return 0;
        }
        case WM_GETOBJECT:
            if ((LONG)lp == OBJID_CLIENT)
                return accessibleObject(h, wp, a.scene, a.dpi);
            break;
        case WM_APP + 6:
            a.action((int)wp);
            return 0;
        case WM_APP + 7:
            SetFocus(h);
            a.scene.focus = (int)wp;
            a.scene.keyboard = true;
            a.dirty = true;
            return 0;
        case WM_DATA:
            if (a.media) {
                auto m = a.media->take();
                if (!a.overrideMusic)
                    a.onMusic(m);
            }
            return 0;
        case WM_NOTICE:
            if (a.notices)
                for (auto &n : a.notices->take())
                    a.enqueue(std::move(n));
            return 0;
        case WM_HEALTH:
            a.healthy = wp != 0;
            a.native.enable(a.healthy && a.settings.hideNative && !a.suspended && !a.diagnostic);
            return 0;
        case WM_FEEDBACK:
            if (!wp)
                monitor.event("播放器控制未获确认");
            a.scene.busy = false;
            if (!wp) {
                a.scene.feedback = L"播放器未响应";
                a.feedbackUntil = now() + 2;
            }
            a.dirty = true;
            return 0;
        case WM_TIMER:
            if (wp == 1 && a.running) {
                ++a.modalFrames;
                a.tick();
            }
            return 0;
        case WM_NOTIFY:
            if (((NMHDR *)lp)->hwndFrom == a.tooltip && ((NMHDR *)lp)->code == TTN_GETDISPINFOW) {
                a.tooltipText = a.hasMusic ? controlName(a.scene, a.scene.hover) : L"";
                ((NMTTDISPINFOW *)lp)->lpszText = a.tooltipText.data();
            }
            return 0;
        case WM_NCHITTEST: {
            POINT p{(short)LOWORD(lp), (short)HIWORD(lp)};
            ScreenToClient(h, &p);
            float x = p.x / a.dpi, y = p.y / a.dpi;
            for (auto &[id, b] : a.scene.buttons)
                if (a.hasMusic && b.hit(x, y) && y < a.geo.mh)
                    return HTCLIENT;
            return HTTRANSPARENT;
        }
        case WM_MOUSEACTIVATE:
            return a.hasMusic ? MA_ACTIVATE : MA_NOACTIVATE;
        case WM_MOUSEMOVE: {
            float x = (short)LOWORD(lp) / a.dpi, y = (short)HIWORD(lp) / a.dpi;
            a.scene.hover = -1;
            for (auto i = a.scene.buttons.rbegin(); i != a.scene.buttons.rend(); i++)
                if (i->second.hit(x, y)) {
                    a.scene.hover = i->first;
                    break;
                }
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            a.dirty = true;
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, h, 0};
            TrackMouseEvent(&track);
            return 0;
        }
        case WM_MOUSELEAVE:
            a.scene.hover = -1;
            a.dirty = true;
            return 0;
        case WM_LBUTTONUP: {
            a.scene.keyboard = false;
            float x = (short)LOWORD(lp) / a.dpi, y = (short)HIWORD(lp) / a.dpi;
            for (auto i = a.scene.buttons.rbegin(); i != a.scene.buttons.rend(); i++)
                if (i->second.hit(x, y)) {
                    SetFocus(h);
                    a.action(i->first);
                    break;
                }
            return 0;
        }
        case WM_KEYDOWN:
            if (wp == VK_TAB && !a.scene.buttons.empty()) {
                a.scene.keyboard = true;
                auto i = std::find_if(a.scene.buttons.begin(), a.scene.buttons.end(),
                                      [&](auto &b) { return b.first == a.scene.focus; });
                int n = (int)a.scene.buttons.size(),
                    index = i == a.scene.buttons.end() ? 0 : (int)(i - a.scene.buttons.begin());
                index = (index + ((GetKeyState(VK_SHIFT) < 0) ? n - 1 : 1)) % n;
                a.scene.focus = a.scene.buttons[index].first;
                a.dirty = true;
            } else if (wp == VK_RETURN || wp == VK_SPACE)
                a.action(a.scene.focus);
            else if (wp == VK_ESCAPE && a.scene.expanded) {
                a.scene.expanded = false;
                a.retarget(.42);
            }
            return 0;
        case WM_TRAY:
            if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU) {
                HMENU menu = CreatePopupMenu();
                AppendMenuW(menu, MF_STRING, 1, L"设置");
                AppendMenuW(menu, MF_STRING, 2, L"退出");
                POINT p;
                GetCursorPos(&p);
                SetForegroundWindow(h);
                // Windows modal loops do not service our waitable frame timer.
                SetTimer(h, 1, (UINT)std::max(16., 1000. / a.rate()), nullptr);
                int result = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, 0, h, nullptr);
                KillTimer(h, 1);
                DestroyMenu(menu);
                if (result == 1)
                    a.openSettings();
                if (result == 2)
                    a.quit();
            }
            return 0;
        case WM_DPICHANGED:
        case WM_DISPLAYCHANGE:
        case WM_SETTINGCHANGE:
            if (a.renderer)
                a.position();
            return 0;
        case WM_WTSSESSION_CHANGE:
            if (wp == WTS_SESSION_LOCK || wp == WTS_CONSOLE_DISCONNECT || wp == WTS_REMOTE_DISCONNECT)
                a.pause(true);
            else if (wp == WTS_SESSION_UNLOCK || wp == WTS_SESSION_LOGON || wp == WTS_CONSOLE_CONNECT ||
                     wp == WTS_REMOTE_CONNECT)
                a.pause(false);
            return 0;
        case WM_POWERBROADCAST:
            if (wp == PBT_APMSUSPEND)
                a.pause(true);
            if (wp == PBT_APMRESUMEAUTOMATIC || wp == PBT_APMRESUMESUSPEND)
                a.pause(false);
            return TRUE;
        case WM_CLOSE:
            a.quit();
            return 0;
        case WM_PAINT:
            ValidateRect(h, nullptr);
            a.dirty = true;
            return 0;
        case WM_ERASEBKGND:
            return 1;
        }
        return DefWindowProcW(h, msg, wp, lp);
    }
};

static void roundedControl(HDC dc, RECT rect, float radius, COLORREF fill, COLORREF edge, float stroke = 1) {
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    float x = rect.left + .7f, y = rect.top + .7f, w = rect.right - rect.left - 1.4f,
          h = rect.bottom - rect.top - 1.4f;
    float r = std::min(radius, std::min(w, h) / 2), d = r * 2;
    Gdiplus::GraphicsPath path;
    path.AddArc(x, y, d, d, 180, 90);
    path.AddArc(x + w - d, y, d, d, 270, 90);
    path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    path.AddArc(x, y + h - d, d, d, 90, 90);
    path.CloseFigure();
    auto color = [](COLORREF c) { return Gdiplus::Color(255, GetRValue(c), GetGValue(c), GetBValue(c)); };
    Gdiplus::SolidBrush brush(color(fill));
    g.FillPath(&brush, &path);
    Gdiplus::Pen pen(color(edge), stroke);
    g.DrawPath(&pen, &path);
}
static LRESULT CALLBACK styledProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (msg == WM_MOUSEMOVE) {
        if (!GetPropW(h, L"wi.hover")) {
            SetPropW(h, L"wi.hover", (HANDLE)1);
            InvalidateRect(h, nullptr, FALSE);
        }
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, h, 0};
        TrackMouseEvent(&track);
    }
    if (msg == WM_MOUSELEAVE) {
        RemovePropW(h, L"wi.hover");
        InvalidateRect(h, nullptr, FALSE);
    }
    if (msg == WM_SETFOCUS || msg == WM_KILLFOCUS) {
        InvalidateRect(GetParent(h), nullptr, FALSE);
        if (msg == WM_SETFOCUS)
            PostMessageW(GetParent(h), WM_APP + 9, (WPARAM)h, 0);
    }
    if (msg == WM_NCDESTROY)
        RemovePropW(h, L"wi.hover");
    return DefSubclassProc(h, msg, wp, lp);
}
static LRESULT CALLBACK toggleProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT r;
        GetClientRect(h, &r);
        FillRect(dc, &r, (HBRUSH)GetStockObject(WHITE_BRUSH));
        bool on = SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED;
        COLORREF color = on ? RGB(61, 104, 242) : RGB(205, 210, 221);
        RECT track = r;
        InflateRect(&track, -1, -1);
        roundedControl(dc, track, (float)r.bottom / 2, color, color);
        Gdiplus::Graphics g(dc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::SolidBrush white(Gdiplus::Color::White);
        float inset = r.bottom * .15f, d = r.bottom - 2 * inset, x = on ? r.right - inset - d : inset;
        g.FillEllipse(&white, x, inset, d, d);
        if (GetFocus() == h && (SendMessageW(h, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS) == 0) {
            InflateRect(&r, -1, -1);
            DrawFocusRect(dc, &r);
        }
        EndPaint(h, &ps);
        return 0;
    }
    LRESULT result = DefSubclassProc(h, msg, wp, lp);
    if (msg == BM_SETCHECK || msg == WM_SETFOCUS || msg == WM_KILLFOCUS || msg == WM_UPDATEUISTATE)
        InvalidateRect(h, nullptr, FALSE);
    return result;
}
static HWND control(HWND parent, const wchar_t *cls, const std::wstring &text, DWORD style, int id) {
    HWND c = CreateWindowExW(0, cls, text.c_str(), WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1, parent,
                             (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    if (wcscmp(cls, L"BUTTON") == 0 || wcscmp(cls, L"EDIT") == 0)
        SetWindowSubclass(c, styledProc, 2, 0);
    return c;
}
void App::openSettings() {
    if (settingsWindow) {
        ShowWindow(settingsWindow, SW_RESTORE);
        SetForegroundWindow(settingsWindow);
        return;
    }
    settingsDpi = dpi;
    settingsScroll = 0;
    settingsCreating = true;
    WNDCLASSW wc{};
    wc.lpfnWndProc = settingsProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
    wc.lpszClassName = L"WinIsland.Native.Settings";
    RegisterClassW(&wc);
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int w = std::min<int>((int)(552 * settingsDpi), work.right - work.left),
        h = std::min<int>((int)(756 * settingsDpi), work.bottom - work.top);
    settingsWindow = CreateWindowExW(WS_EX_CONTROLPARENT, wc.lpszClassName, L"WinIsland 设置",
                                     WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VSCROLL | WS_CLIPCHILDREN,
                                     work.left + (work.right - work.left - w) / 2,
                                     work.top + (work.bottom - work.top - h) / 2, w, h, hwnd, nullptr,
                                     wc.hInstance, nullptr);
    control(settingsWindow, L"STATIC", L"WinIsland", 0, 200);
    control(settingsWindow, L"STATIC", Version, SS_RIGHT, 201);
    control(settingsWindow, L"STATIC", L"设置会自动保存并立即生效", 0, 202);
    const wchar_t *labels[] = {L"灵动岛常驻", L"隐藏Windows自带通知", L"通知停驻时间", L"动画帧率"};
    const wchar_t *hints[] = {L"关闭后，仅在收到通知时滑入屏幕", L"运行期间隐藏系统横幅，保留通知中心记录",
                              L"展开后保持显示的时间，支持小数",
                              L"0 跟随系统；可填 30–240，低性能默认目标 60 FPS"};
    for (int i = 0; i < 4; i++) {
        control(settingsWindow, L"STATIC", labels[i], 0, 210 + i * 2);
        control(settingsWindow, L"STATIC", hints[i], 0, 211 + i * 2);
    }
    for (int id = 100; id <= 101; id++) {
        auto c = control(settingsWindow, L"BUTTON", labels[id - 100], BS_AUTOCHECKBOX | WS_TABSTOP, id);
        SetWindowSubclass(c, toggleProc, 1, 0);
        SendMessageW(c, BM_SETCHECK,
                     (id == 100 ? settings.resident : settings.hideNative) ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    std::wostringstream sec;
    sec << settings.seconds;
    control(settingsWindow, L"EDIT", sec.str(), ES_CENTER | WS_TABSTOP, 102);
    control(settingsWindow, L"EDIT", std::to_wstring(settings.fps), ES_CENTER | WS_TABSTOP, 103);
    SendDlgItemMessageW(settingsWindow, 102, EM_SETLIMITTEXT, 12, 0);
    SendDlgItemMessageW(settingsWindow, 103, EM_SETLIMITTEXT, 3, 0);
    control(settingsWindow, L"STATIC", L"秒", 0, 220);
    control(settingsWindow, L"STATIC", L"FPS", 0, 221);
    control(settingsWindow, L"STATIC", L"", 0, 106);
    control(settingsWindow, L"STATIC", L"", 0, 107);
    control(settingsWindow, L"BUTTON", L"载入当前歌曲歌词…", BS_OWNERDRAW | WS_TABSTOP, 104);
    control(settingsWindow, L"BUTTON", L"清除当前歌词", BS_OWNERDRAW | WS_TABSTOP, 105);
    control(settingsWindow, L"BUTTON", L"启动信息监测", BS_OWNERDRAW | WS_TABSTOP, 108);
    control(settingsWindow, L"BUTTON", L"导出日志报告", BS_OWNERDRAW | WS_TABSTOP, 109);
    control(settingsWindow, L"STATIC", L"", 0, 110);
    control(settingsWindow, L"EDIT", L"", ES_READONLY | ES_AUTOHSCROLL | WS_TABSTOP, 111);
    control(settingsWindow, L"STATIC", L"运行诊断", 0, 222);
    layoutSettings();
    settingsCreating = false;
    updateMonitorControls();
    updateSettingsMusic();
    ShowWindow(settingsWindow, SW_SHOW);
    SetForegroundWindow(settingsWindow);
}
void App::layoutSettings() {
    if (!settingsWindow)
        return;
    if (settingsFont)
        DeleteObject(settingsFont);
    if (smallFont)
        DeleteObject(smallFont);
    if (titleFont)
        DeleteObject(titleFont);
    auto font = [&](int size, int weight) {
        return CreateFontW(-(int)std::lround(size * settingsDpi), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    };
    settingsFont = font(14, FW_NORMAL);
    smallFont = font(11, FW_NORMAL);
    titleFont = font(26, FW_SEMIBOLD);
    RECT client;
    GetClientRect(settingsWindow, &client);
    double w = client.right / settingsDpi;
    double height = client.bottom / settingsDpi;
    SCROLLINFO si{sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS};
    si.nMin = 0;
    si.nMax = 708;
    si.nPage = (UINT)height;
    settingsScroll = std::clamp(settingsScroll, 0, std::max(0, 709 - (int)height));
    si.nPos = settingsScroll;
    SetScrollInfo(settingsWindow, SB_VERT, &si, TRUE);
    GetClientRect(settingsWindow, &client);
    w = client.right / settingsDpi;
    auto place = [&](int id, double x, double y, double width, double h, HFONT f = nullptr) {
        HWND c = GetDlgItem(settingsWindow, id);
        SendMessageW(c, WM_SETFONT, (WPARAM)(f ? f : settingsFont), FALSE);
        MoveWindow(c, (int)(x * settingsDpi), (int)((y - settingsScroll) * settingsDpi),
                   (int)(std::max(1., width) * settingsDpi), (int)(h * settingsDpi), TRUE);
    };
    place(200, 26, 22, w - 170, 34, titleFont);
    place(201, w - 145, 32, 119, 22);
    place(202, 26, 62, w - 52, 20, smallFont);
    for (int i = 0; i < 4; i++) {
        int y = 100 + i * 80;
        place(210 + i * 2, 42, y + 12, w - 174, 22);
        place(211 + i * 2, 42, y + 39, w - 174, 30, smallFont);
    }
    place(100, w - 88, 123, 46, 26);
    place(101, w - 88, 203, 46, 26);
    place(102, w - 128, 290, 54, 20);
    place(103, w - 128, 370, 54, 20);
    place(220, w - 63, 290, 25, 22, smallFont);
    place(221, w - 63, 370, 28, 22, smallFont);
    place(106, 28, 424, w - 56, 25, smallFont);
    place(107, 28, 454, w - 56, 44, smallFont);
    double first = std::min(180., (w - 62) * .55);
    place(104, 26, 510, first, 34);
    place(105, 36 + first, 510, w - 62 - first, 34);
    place(222, 28, 569, w - 56, 24);
    place(108, 26, 602, (w - 62) / 2, 34);
    place(109, 36 + (w - 62) / 2, 602, (w - 62) / 2, 34);
    place(110, 28, 648, w - 56, 30, smallFont);
    place(111, 30, 682, w - 60, 20, smallFont);
    InvalidateRect(settingsWindow, nullptr, TRUE);
}
void App::updateSettingsMusic() {
    if (!settingsWindow)
        return;
    bool ready = hasMusic && scene.music && !scene.music->loading;
    EnableWindow(GetDlgItem(settingsWindow, 104), ready && scene.music->timeline);
    EnableWindow(GetDlgItem(settingsWindow, 105), ready);
    std::wstring hint = !ready                   ? L"播放音乐并开启系统媒体控件后，会显示歌曲。"
                        : !scene.music->timeline ? L"当前播放器未提供进度，封面和控制可用，同步歌词暂不可用。"
                                                 : L"可为当前歌曲载入 LRC，按真实播放进度同步。";
    wchar_t previous[512];
    GetDlgItemTextW(settingsWindow, 107, previous, 512);
    if (hint != previous)
        SetDlgItemTextW(settingsWindow, 107, hint.c_str());
}
void App::settingsChanged(int id) {
    if (id == 100)
        settings.resident = SendDlgItemMessageW(settingsWindow, 100, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (id == 101)
        settings.hideNative = SendDlgItemMessageW(settingsWindow, 101, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (id == 102 || id == 103) {
        wchar_t value[64];
        GetDlgItemTextW(settingsWindow, id, value, 64);
        wchar_t *end;
        double number = wcstod(value, &end);
        bool valid =
            *value && !*end && std::isfinite(number) &&
            (id == 102 ? (number >= .1 && number <= 3600)
                       : (number == 0 || (number >= 30 && number <= 240 && std::floor(number) == number)));
        SetDlgItemTextW(settingsWindow, 106,
                        valid       ? L""
                        : id == 102 ? L"请输入 0.1–3600 秒；当前设置保持生效。"
                                    : L"请输入 0 或 30–240 的整数；当前帧率保持生效。");
        invalidInput = valid ? 0 : id;
        InvalidateRect(settingsWindow, nullptr, FALSE);
        if (!valid)
            return;
        if (id == 102)
            settings.seconds = number;
        else
            settings.fps = (int)number;
    }
    saveSettings();
}
void App::importLyrics(bool clear) {
    if (!scene.music)
        return;
    auto key = scene.music->key();
    fs::path file;
    if (!clear) {
        ComPtr<IFileOpenDialog> dialog;
        if (FAILED(
                CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
            return;
        COMDLG_FILTERSPEC filter{L"LRC 歌词文件", L"*.lrc"};
        dialog->SetFileTypes(1, &filter);
        dialog->SetTitle(L"为当前歌曲载入歌词");
        SetTimer(hwnd, 1, (UINT)std::max(16., 1000. / rate()), nullptr);
        HRESULT shown = dialog->Show(settingsWindow);
        KillTimer(hwnd, 1);
        if (FAILED(shown))
            return;
        ComPtr<IShellItem> item;
        dialog->GetResult(&item);
        PWSTR path = nullptr;
        item->GetDisplayName(SIGDN_FILESYSPATH, &path);
        file = path;
        CoTaskMemFree(path);
        if (!scene.music || scene.music->key() != key) {
            SetDlgItemTextW(settingsWindow, 106, L"歌曲已切换，请重新选择。");
            return;
        }
    }
    jobs.post([this, key, file, clear] {
        try {
            auto dest = store / (sha256(utf8(key)) + ".lrc");
            if (clear) {
                std::error_code ec;
                fs::remove(dest, ec);
            } else {
                auto data = readFile(file);
                auto l = Lyrics::parse(data);
                if (!l || l->lines.empty()) {
                    PostMessageW(hwnd, WM_FEEDBACK, 0, 0);
                    return;
                }
                writeAtomic(dest, data);
            }
            media->reload();
        } catch (...) {
            PostMessageW(hwnd, WM_FEEDBACK, 0, 0);
        }
    });
}

LRESULT CALLBACK App::settingsProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (!app)
        return DefWindowProcW(h, msg, wp, lp);
    auto &a = *app;
    if (msg == WM_COMMAND && !a.settingsCreating) {
        int id = LOWORD(wp), code = HIWORD(wp);
        if ((id == 100 || id == 101) && code == BN_CLICKED)
            a.settingsChanged(id);
        if ((id == 102 || id == 103) && code == EN_CHANGE)
            a.settingsChanged(id);
        if ((id == 102 || id == 103) && code == EN_KILLFOCUS) {
            std::wostringstream s;
            s << (id == 102 ? a.settings.seconds : a.settings.fps);
            SetDlgItemTextW(h, id, s.str().c_str());
        }
        if (id == 104 && code == BN_CLICKED)
            a.importLyrics(false);
        if (id == 105 && code == BN_CLICKED)
            a.importLyrics(true);
        if (id == 108 && code == BN_CLICKED)
            a.monitoring(monitor.active() ? 1 : 0);
        if (id == 109 && code == BN_CLICKED)
            a.monitoring(2);
        return 0;
    }
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT r;
        GetClientRect(h, &r);
        HBRUSH bg = CreateSolidBrush(RGB(246, 247, 250));
        FillRect(dc, &r, bg);
        DeleteObject(bg);

        double width = r.right / a.settingsDpi;
        for (int i = 0; i < 4; i++) {
            RECT card{(LONG)(26 * a.settingsDpi), (LONG)((100 + i * 80 - a.settingsScroll) * a.settingsDpi),
                      r.right - (LONG)(26 * a.settingsDpi),
                      (LONG)((172 + i * 80 - a.settingsScroll) * a.settingsDpi)};
            roundedControl(dc, card, 10 * a.settingsDpi, RGB(255, 255, 255), RGB(230, 233, 239));
        }
        for (int i = 0; i < 2; i++) {
            int id = 102 + i;
            RECT frame{(LONG)((width - 132) * a.settingsDpi),
                       (LONG)((283 + i * 80 - a.settingsScroll) * a.settingsDpi),
                       (LONG)((width - 70) * a.settingsDpi),
                       (LONG)((315 + i * 80 - a.settingsScroll) * a.settingsDpi)};
            COLORREF border = a.invalidInput == id              ? RGB(193, 55, 63)
                              : GetFocus() == GetDlgItem(h, id) ? RGB(61, 104, 242)
                                                                : RGB(215, 221, 233);
            roundedControl(dc, frame, 8 * a.settingsDpi, RGB(255, 255, 255), border,
                           GetFocus() == GetDlgItem(h, id) ? 1.5f : 1);
        }
        EndPaint(h, &ps);
        return 0;
    }
    if (msg == WM_CTLCOLORSTATIC) {
        int id = GetDlgCtrlID((HWND)lp);
        HDC dc = (HDC)wp;
        bool card = (id >= 210 && id <= 217) || id == 220 || id == 221;
        SetBkColor(dc, card ? RGB(255, 255, 255) : RGB(246, 247, 250));
        SetTextColor(dc, id == 106                                              ? RGB(193, 55, 63)
                         : id == 200 || (id >= 210 && id <= 216 && id % 2 == 0) ? RGB(25, 28, 35)
                                                                                : RGB(105, 112, 126));
        static HBRUSH gray = CreateSolidBrush(RGB(246, 247, 250));
        return (LRESULT)(card ? GetStockObject(WHITE_BRUSH) : gray);
    }
    if (msg == WM_CTLCOLOREDIT) {
        SetBkColor((HDC)wp, RGB(255, 255, 255));
        SetTextColor((HDC)wp, RGB(25, 28, 35));
        return (LRESULT)GetStockObject(WHITE_BRUSH);
    }
    if (msg == WM_DRAWITEM) {
        auto *d = (DRAWITEMSTRUCT *)lp;
        if (d->CtlType == ODT_BUTTON) {
            HDC dc = d->hDC;
            RECT r = d->rcItem;
            HBRUSH bg = CreateSolidBrush(RGB(246, 247, 250));
            FillRect(dc, &r, bg);
            DeleteObject(bg);
            bool disabled = d->itemState & ODS_DISABLED, pressed = d->itemState & ODS_SELECTED,
                 hover = GetPropW(d->hwndItem, L"wi.hover") != nullptr;
            COLORREF fill = disabled  ? RGB(239, 241, 246)
                            : pressed ? RGB(221, 230, 252)
                            : hover   ? RGB(234, 240, 255)
                                      : RGB(255, 255, 255);
            COLORREF edge = disabled ? RGB(228, 232, 239) : hover ? RGB(165, 186, 242) : RGB(215, 223, 237);
            roundedControl(dc, r, 8 * a.settingsDpi, fill, edge);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, disabled ? RGB(151, 158, 172) : RGB(44, 68, 128));
            HGDIOBJ old = SelectObject(dc, a.settingsFont);
            wchar_t value[128];
            GetWindowTextW(d->hwndItem, value, 128);
            DrawTextW(dc, value, -1, &r, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_END_ELLIPSIS);
            SelectObject(dc, old);
            if ((d->itemState & ODS_FOCUS) && !(d->itemState & ODS_NOFOCUSRECT)) {
                InflateRect(&r, -4, -4);
                DrawFocusRect(dc, &r);
            }
            return TRUE;
        }
    }
    if (msg == WM_APP + 9) {
        RECT child, client;
        GetWindowRect((HWND)wp, &child);
        MapWindowPoints(nullptr, h, (POINT *)&child, 2);
        GetClientRect(h, &client);
        int delta = child.top < 0                  ? child.top
                    : child.bottom > client.bottom ? child.bottom - client.bottom
                                                   : 0;
        if (delta) {
            a.settingsScroll += (int)(delta / a.settingsDpi) + (delta > 0 ? 10 : -10);
            a.layoutSettings();
        }
        return 0;
    }
    if (msg == WM_LBUTTONDOWN) {
        RECT client;
        GetClientRect(h, &client);
        double x = (short)LOWORD(lp) / a.settingsDpi,
               y = (short)HIWORD(lp) / a.settingsDpi + a.settingsScroll, w = client.right / a.settingsDpi;
        for (int i = 0; i < 2; i++)
            if (x >= w - 132 && x <= w - 70 && y >= 283 + i * 80 && y <= 315 + i * 80) {
                SetFocus(GetDlgItem(h, 102 + i));
                return 0;
            }
    }
    if (msg == WM_ERASEBKGND)
        return 1;
    if (msg == WM_DPICHANGED) {
        a.settingsDpi = HIWORD(wp) / 96.f;
        RECT *r = (RECT *)lp;
        MONITORINFO info{sizeof(info)};
        GetMonitorInfoW(MonitorFromRect(r, MONITOR_DEFAULTTONEAREST), &info);
        int width = std::min(r->right - r->left, info.rcWork.right - info.rcWork.left),
            height = std::min(r->bottom - r->top, info.rcWork.bottom - info.rcWork.top);
        SetWindowPos(h, nullptr, std::clamp(r->left, info.rcWork.left, info.rcWork.right - width),
                     std::clamp(r->top, info.rcWork.top, info.rcWork.bottom - height), width, height,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        a.layoutSettings();
        return 0;
    }
    if (msg == WM_VSCROLL || msg == WM_MOUSEWHEEL) {
        SCROLLINFO si{sizeof(si), SIF_ALL};
        GetScrollInfo(h, SB_VERT, &si);
        int next = a.settingsScroll;
        if (msg == WM_MOUSEWHEEL)
            next -= GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * 36;
        else
            switch (LOWORD(wp)) {
            case SB_TOP:
                next = 0;
                break;
            case SB_BOTTOM:
                next = si.nMax;
                break;
            case SB_LINEUP:
                next -= 24;
                break;
            case SB_LINEDOWN:
                next += 24;
                break;
            case SB_PAGEUP:
                next -= (int)si.nPage;
                break;
            case SB_PAGEDOWN:
                next += (int)si.nPage;
                break;
            case SB_THUMBTRACK:
                next = si.nTrackPos;
                break;
            }
        a.settingsScroll = next;
        a.layoutSettings();
        return 0;
    }
    if (msg == WM_CLOSE) {
        DestroyWindow(h);
        return 0;
    }
    if (msg == WM_DESTROY) {
        a.settingsWindow = nullptr;
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}
int runApp(const fs::path &dir, bool software) {
    App instance;
    app = &instance;
    instance.startup(dir, software);
    int result = instance.run();
    app = nullptr;
    return result;
}
} // namespace wi
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc;
    auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Gdiplus::GdiplusStartupInput graphicsInput;
    ULONG_PTR graphicsToken = 0;
    Gdiplus::GdiplusStartup(&graphicsToken, &graphicsInput, nullptr);
    SetCurrentProcessExplicitAppUserModelID(L"WinIsland.Desktop");
    int result = 0;
    try {
        if (argc == 5 && std::wstring(argv[1]) == L"--native-helper")
            result = wi::runToastHelper(std::stoul(argv[2]), std::stoull(argv[3]), std::stoull(argv[4]));
        else if (argc == 3 && std::wstring(argv[1]) == L"--self-test")
            result = wi::selfTest(argv[2]);
        else {
            wi::fs::path dir;
            bool soft = GetEnvironmentVariableW(L"WINISLAND_SOFTWARE", nullptr, 0) > 0;
            if (argc == 3 && std::wstring(argv[1]) == L"--verify")
                dir = argv[2];
            wchar_t perfdir[32768];
            DWORD n = GetEnvironmentVariableW(L"WINISLAND_PERF_DIR", perfdir, 32768);
            if (n > 0 && n < 32768)
                wi::perf.start(perfdir);
            HANDLE mutex =
                CreateMutexW(nullptr, TRUE, dir.empty() ? L"Local\\WinIsland.Desktop.Singleton" : nullptr);
            if (!dir.empty() || GetLastError() != ERROR_ALREADY_EXISTS)
                result = wi::runApp(dir, soft);
            if (mutex)
                CloseHandle(mutex);
        }
    } catch (const std::exception &e) {
        MessageBoxW(nullptr, wi::wide(e.what()).c_str(), L"WinIsland 1.2.3beta", MB_ICONERROR);
        result = 1;
    }
    LocalFree(argv);
    if (graphicsToken)
        Gdiplus::GdiplusShutdown(graphicsToken);
    CoUninitialize();
    return result;
}
