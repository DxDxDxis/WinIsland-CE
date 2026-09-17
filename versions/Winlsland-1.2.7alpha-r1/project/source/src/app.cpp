#include "media_runtime.h"
#include "render.h"
#include "mod_system.h"
#include "animation_runtime.h"
#include "outline.h"
#include "ui_theme.h"
#include "embedded_components.h"
#include <commctrl.h>
#include <gdiplus.h>
#include <shellscalingapi.h>
#include <winrt/base.h>
#include "settings_bridge.h"
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <thread>
#include <atomic>
#include <sstream>
namespace wi {
// Controls in the single clipped viewport remain addressable by their legacy IDs.
static HWND settingControl(HWND parent, int id) {
    if (auto child = GetDlgItem(parent, id))
        return child;
    return GetDlgItem(GetDlgItem(parent, 300), id);
}
static LRESULT sendSetting(HWND parent, int id, UINT msg, WPARAM wp, LPARAM lp) {
    return SendMessageW(settingControl(parent, id), msg, wp, lp);
}
static UINT getSettingText(HWND parent, int id, LPWSTR value, int length) {
    return GetWindowTextW(settingControl(parent, id), value, length);
}
static BOOL setSettingText(HWND parent, int id, LPCWSTR value) {
    return SetWindowTextW(settingControl(parent, id), value);
}
struct Display { HMONITOR handle; MONITORINFOEXW info; };
static std::vector<Display> displays() {
    std::vector<Display> result;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
        Display d{}; d.handle = monitor; d.info.cbSize = sizeof(d.info);
        if (GetMonitorInfoW(monitor, &d.info)) ((std::vector<Display>*)data)->push_back(d);
        return TRUE;
    }, (LPARAM)&result);
    return result;
}
struct Geo {
    double zoom = IslandScale, topRadius = 14.96;
    double w = 180.4, h = 29.92, r = 14.96, mh = 0, ma = 0, na = 0, offset = 0, ih = 0, ia = 0, modH=0;
};
struct MonitorResult {
    std::wstring message, path;
};
class App;
static App *app = nullptr;
class App {
  public:
    HWND hwnd = nullptr, inputWindow = nullptr, settingsWindow = nullptr, settingsContent = nullptr,
         tooltip = nullptr;
    HRGN inputRegion = nullptr;
    std::vector<int> inputShape;
    std::vector<std::pair<int, Box>> inputButtons;
    int pressedButton = -1;
    int pointerDowns = 0, pointerUps = 0, lastPointerHit = -1;
    double panelTouched = 0;
    double pressAt = 0, feedbackTick = 0, nextTelemetry = 0;
    int holdTriggers = 0;
    bool reducedMotion = false;
    std::unique_ptr<Telemetry> telemetry;
    std::wstring tooltipText;
    int modalFrames = 0;
    std::unique_ptr<Renderer> renderer;
    std::unique_ptr<ModLoader> mods;
    ModSnapshot modState;
    double nextMods=0;
    bool hostExpanded=false;
    uint32_t lastSceneFlags=~0u;
    std::string lastSceneMusic,lastSceneLyric,lastSceneNotice;
    float lastSceneWidth=0,lastSceneHeight=0;
    WiElement sceneHover=0,scenePressed=0,sceneFocus=0;
    Settings settings;
    Settings loggedSettings;
    Jobs jobs;
    std::unique_ptr<Media> media;
    std::unique_ptr<Notices> notices;
    std::unique_ptr<QqNotices> qqNotices;
    std::unique_ptr<Audio> audio;
    ToastHelper native;
    Scene scene;
    Geo geo, from, to;
    double animStart = 0, animSeconds = .4, holdUntil = 0, pausedAt = 0, feedbackUntil = 0, lastFrame = 0;
    bool closingNotice = false, animating = false, awaitHold = false, switching = false, hasMusic = false,
         healthy = false, suspended = false, diagnostic = false, overrideMusic = false, running = true,
         dirty = true;
    int delivered = 0, noticesShown = 0, commandsProcessed = 0, actionCount = 0, lastAction = -1;
    bool observation = false;
    std::string visibleNotice, lastLyricState;
    int visibleNotices = 0, completedNotices = 0, lyricIndex = -1;
    double nextLyricTrace = 0;
    double autoFps = 60;
    float dpi = 1;
    double availableWidth = 616, hostHeight = 432;
    POINT origin{};
    double anchorY = 0, anchorFrom = 0, anchorTo = 0, anchorStart = 0;
    HMONITOR activeMonitor = nullptr;
    RECT workArea{};
    bool positioning = false, dragging = false;
    POINT dragStart{};
    double scaleX = IslandScale * 1.5, scaleY = IslandScale * 1.5;
    double workWidth = 1920, workHeight = 1080;
    std::vector<Display> settingsDisplays;
    std::deque<std::shared_ptr<Notice>> queue;
    std::shared_ptr<Music> rawMusic;
    fs::path testDir, config, store;
    NOTIFYICONDATAW tray{sizeof(tray)};
    HFONT settingsFont = nullptr, smallFont = nullptr, titleFont = nullptr;
    float settingsDpi = 1;
    float fontDpi = 0;
    bool settingsLayoutBusy = false, topArcSavePending = false;
    double wheelRemainder = 0;
    int settingsScroll = 0;
    int settingsPage = 0;
    std::vector<Box> settingsCards;
    std::vector<std::pair<int, Box>> settingsFrames;
    bool settingsCreating = false, monitorPending = false;
    std::unique_ptr<SettingsBridge> settingsBridge;
    HANDLE electronProcess=nullptr; DWORD electronPid=0; std::atomic<uint64_t> settingsRevision{1}; std::mutex settingsSaveMutex;
    void dispatchSettings(); void openNativeSettings();

    int invalidInput = 0;
    std::wstring monitorMessage = L"尚未开始记录", exportPath;
    float testDpi = 0;
    std::mutex testMu;
    std::shared_ptr<Lyrics> testLyrics;
    bool testLyricsReady = false;
    HANDLE timer = nullptr;
    UINT taskbarMessage = RegisterWindowMessageW(L"TaskbarCreated");
    ~App() {
        settingsBridge.reset(); if(electronProcess)CloseHandle(electronProcess);

        jobs.finish();
        native.enable(false);
        notices.reset();
        qqNotices.reset();
        media.reset();
        audio.reset();
        telemetry.reset();
        closeModManager();
        mods.reset();
        if (inputWindow)
            DestroyWindow(inputWindow);
        if (inputRegion)
            DeleteObject(inputRegion);
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
        if (!diagnostic) wi::prepareEmbeddedComponents(false);
        config = (diagnostic ? dir : dataDir()) / L"settings.xml";
        store = (diagnostic ? dir : dataDir()) / L"lyrics";
        settings.load(config);
        mods = std::make_unique<ModLoader>((diagnostic ? dir : exePath().parent_path()) / L"mods",true,diagnostic?dir:dataDir());
        if (observation && !fs::exists(config)) settings.load(dataDir() / L"settings.xml");
        loggedSettings = settings;
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
        wc.lpszClassName = L"WinIsland.Native";
        RegisterClassExW(&wc);
        hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP |
                                   WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT,
                               wc.lpszClassName, L"WinIsland", WS_POPUP, 0, 0, 656, 432, nullptr, nullptr,
                               wc.hInstance, nullptr);
        if (!hwnd)
            throw std::runtime_error("window creation");
        // The composition canvas never receives pointer input. HTTRANSPARENT alone
        // only continues hit testing within the same thread, not into another app.
        wc.lpfnWndProc = inputProc;
        wc.lpszClassName = L"WinIsland.Native.Input";
        RegisterClassExW(&wc);
        inputWindow = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP | WS_EX_NOACTIVATE, wc.lpszClassName,
            L"WinIsland 音乐操作", WS_POPUP, 0, 0, 656, 432, hwnd, nullptr, wc.hInstance, nullptr);
        if (!inputWindow)
            throw std::runtime_error("input window creation");
        clearInput();
        INITCOMMONCONTROLSEX common{sizeof(common), ICC_WIN95_CLASSES};
        InitCommonControlsEx(&common);
        tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP, 0, 0, 0,
                                  0, inputWindow, nullptr, wc.hInstance, nullptr);
        TOOLINFOW tool{sizeof(tool)};
        tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        tool.hwnd = inputWindow;
        tool.uId = (UINT_PTR)inputWindow;
        tool.lpszText = LPSTR_TEXTCALLBACKW;
        SendMessageW(tooltip, TTM_ADDTOOLW, 0, (LPARAM)&tool);
        renderer = std::make_unique<Renderer>(hwnd, software);
        position();
        tray.hWnd = hwnd;
        tray.uID = 1;
        tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        tray.uCallbackMessage = WM_TRAY;
        tray.hIcon = wc.hIcon;
        wcscpy_s(tray.szTip, L"WinIsland 1.2.7alpha");
        Shell_NotifyIconW(NIM_ADD, &tray);
        WTSRegisterSessionNotification(hwnd, NOTIFY_FOR_THIS_SESSION);
        audio = std::make_unique<Audio>();
        media = std::make_unique<Media>(hwnd, store, diagnostic && !observation);
        telemetry = std::make_unique<Telemetry>();
        notices = std::make_unique<Notices>(hwnd, diagnostic && !observation && fs::exists(dir / L"notifications.db")
                                                      ? dir / L"notifications.db"
                                                      : fs::path{});
        qqNotices = std::make_unique<QqNotices>(hwnd, diagnostic && !observation);
        applySettings();
        settingsBridge=std::make_unique<SettingsBridge>(hwnd);
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        ShowWindow(inputWindow, SW_SHOWNOACTIVATE);
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
            using namespace winrt::Windows::Data::Json;
            JsonObject connection;connection.Insert(L"pipe",JsonValue::CreateStringValue(settingsBridge->pipeName));
            connection.Insert(L"token",JsonValue::CreateStringValue(settingsBridge->token));
            writeAtomic(dir/L"settings-connection.json",utf8(connection.Stringify().c_str()));
        }
        timer = CreateWaitableTimerExW(nullptr, nullptr, 2, TIMER_ALL_ACCESS);
        if (!timer)
            timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    }
    void position(HMONITOR requested = nullptr) {
        if (positioning) return;
        positioning = true;
        auto list = displays();
        if (list.empty()) { positioning = false; return; }
        HMONITOR monitor = requested ? requested : activeMonitor;
        if (!settings.monitorDevice.empty())
            for (const auto &d : list) if (settings.monitorDevice == d.info.szDevice) monitor = d.handle;
        auto found = std::find_if(list.begin(), list.end(), [&](const auto &d) { return d.handle == monitor; });
        if (found == list.end()) {
            monitor = MonitorFromWindow(GetForegroundWindow(), MONITOR_DEFAULTTOPRIMARY);
            found = std::find_if(list.begin(), list.end(), [&](const auto &d) { return d.handle == monitor; });
            if (found == list.end()) found = list.begin();
        }
        bool sameDisplay = activeMonitor == found->handle;
        activeMonitor = found->handle;
        workArea = found->info.rcWork;
        UINT dx = 96, dy = 96;
        GetDpiForMonitor(activeMonitor, MDT_EFFECTIVE_DPI, &dx, &dy);
        dpi = diagnostic && testDpi > 0 ? testDpi : dx / 96.f;
        workWidth = std::max(1., (workArea.right - workArea.left) / (double)dpi);
        workHeight = std::max(1., (workArea.bottom - workArea.top) / (double)dpi);
        scaleX = IslandScale * settings.islandZoom * settings.widthRatio * settings.dpiCorrection;
        scaleY = IslandScale * settings.islandZoom * settings.heightRatio * settings.dpiCorrection;
        // Small/portrait desktops may have less room than the requested layout.
        // Fit the content uniformly instead of stretching glyphs or cropping controls.
        double fit = std::min({1., (workWidth - 16) / (180.4 * scaleX),
                              workHeight * .9 / (170 * scaleY)});
        fit = std::max(.1, fit);
        scaleX *= fit; scaleY *= fit;
        availableWidth = std::max(32., std::min(616., (workWidth - 16) / scaleX));
        double zoom = std::min(scaleX, scaleY);
        hostHeight = std::min(432. * scaleY, workHeight * .9);
        int width = (int)std::ceil(std::min(656. * scaleX, workWidth) * dpi);
        int height = (int)std::ceil(hostHeight * dpi);
        RECT old{}; GetClientRect(hwnd, &old);
        // Keep a high-water canvas on this monitor during settings animations.
        // No swap-chain resize on every animation frame; input remains separately clipped.
        if (sameDisplay) {
            width = std::min((int)(workArea.right - workArea.left), std::max(width, (int)old.right));
            height = std::min((int)(workArea.bottom - workArea.top), std::max(height, (int)old.bottom));
        }
        hostHeight = height / dpi;
        origin = {workArea.left + (workArea.right - workArea.left - width) / 2,
                  workArea.top + (settings.topAttach ? 0 : (int)std::lround(8 * dpi))};
        RECT previousWindow{}; GetWindowRect(hwnd, &previousWindow);
        anchorFrom = sameDisplay ? previousWindow.top - geo.offset * dpi : origin.y;
        anchorTo = origin.y; anchorY = anchorFrom;
        anchorStart = anchorFrom == anchorTo ? 0 : now();
        if (!sameDisplay) {
            geo.zoom = std::min({geo.zoom, (workWidth-16) / std::max(1., geo.w),
                                workHeight*.9 / std::max(1., geo.h)});
        }
        clearInput();
        SetWindowPos(hwnd, HWND_TOPMOST, origin.x, (int)std::lround(anchorY + geo.offset * dpi), width, height, SWP_NOACTIVATE);
        SetWindowPos(inputWindow, HWND_TOPMOST, origin.x, (int)std::lround(anchorY + geo.offset * dpi), width, height, SWP_NOACTIVATE);
        if (renderer) renderer->resize(width, height, dpi);
        autoFps = detectRate();
        positioning = false;
        retarget(.4);
        dirty = true;
        monitorLog(*found);
    }
    void monitorLog(const Display &display) {
        monitor.event("显示布局；device=" + utf8(display.info.szDevice) + ";DPI=" + std::to_string(dpi * 96) +
                      ";work=" + std::to_string(workArea.left) + "," + std::to_string(workArea.top) + "," +
                      std::to_string(workArea.right) + "," + std::to_string(workArea.bottom) +
                      ";scaleX=" + std::to_string(scaleX) + ";scaleY=" + std::to_string(scaleY));
    }
    void clearInput() {
        pressAt = 0;
        scene.holdTriggered = false;
        pressedButton = -1;
        if (inputWindow && GetCapture() == inputWindow)
            ReleaseCapture();
        inputButtons.clear();
        inputShape.clear();
        if (inputRegion)
            DeleteObject(inputRegion);
        inputRegion = nullptr;
        if (inputWindow) {
            auto empty = CreateRectRgn(0, 0, 0, 0);
            if (!SetWindowRgn(inputWindow, empty, FALSE))
                DeleteObject(empty);
        }
    }
    HRGN inputShell(int clientWidth) {
        double left = (clientWidth-scene.width*dpi)/2;
        // Scan the same monotone cubics used by Direct2D. Keep each input
        // pixel inside the contour rather than rounding polygon vertices out
        // into transparent pixels (observable at fractional DPI).
        const auto outline = islandOutline(left, scene.width*dpi, scene.height*dpi,
                                            scene.radius*dpi, scene.topRadius*dpi);
        auto rightAt = [&](double y) {
            const OutlineCorner *c = y < outline[0].end.y ? &outline[0]
                                    : y > outline[1].start.y ? &outline[1] : nullptr;
            if (!c) return left+scene.width*dpi;
            double lo=0, hi=1;
            for (int i=0;i<22;++i) {
                double t=(lo+hi)/2;
                if (outlineAt(*c,t).y < y) lo=t; else hi=t;
            }
            return outlineAt(*c,(lo+hi)/2).x;
        };
        std::vector<RECT> spans;
        double height=scene.height*dpi, center=left+scene.width*dpi/2;
        for (int y=0;y<(int)std::ceil(height);++y) {
            double right=std::min(rightAt(y), rightAt(std::min(height,y+1.)));
            RECT row{(LONG)std::ceil(2*center-right), y, (LONG)std::floor(right), y+1};
            if (row.right<=row.left) continue;
            if (!spans.empty() && spans.back().left==row.left && spans.back().right==row.right)
                spans.back().bottom=row.bottom;
            else spans.push_back(row);
        }
        std::vector<BYTE> bytes(sizeof(RGNDATAHEADER)+spans.size()*sizeof(RECT));
        auto data=reinterpret_cast<RGNDATA*>(bytes.data());
        data->rdh={sizeof(RGNDATAHEADER),RDH_RECTANGLES,(DWORD)spans.size(),
                   (DWORD)(spans.size()*sizeof(RECT)),{0,0,clientWidth,(LONG)std::ceil(height)}};
        if (!spans.empty()) memcpy(data->Buffer,spans.data(),spans.size()*sizeof(RECT));
        auto region=spans.empty() ? CreateRectRgn(0,0,0,0) : ExtCreateRegion(nullptr,(DWORD)bytes.size(),data);
        return region;
    }
    void updateInput() {
        if(scenePressed&&std::none_of(scene.sceneHits.begin(),scene.sceneHits.end(),[&](auto& hit){return hit.id==scenePressed;})){
            WiInputEvent e{sizeof(e),1};e.kind=WI_DRAG_CANCEL;e.target=scenePressed;e.time=now();mods->sceneInput(e);scenePressed=0;if(GetCapture()==inputWindow)ReleaseCapture();
        }
        if(sceneFocus&&std::none_of(scene.sceneHits.begin(),scene.sceneHits.end(),[&](auto& hit){return hit.id==sceneFocus;})){
            WiInputEvent e{sizeof(e),1};e.kind=WI_FOCUS_LOST;e.target=sceneFocus;e.time=now();mods->sceneInput(e);sceneFocus=0;
        }
        auto root=scene.openPlan.resolve(sceneNode(1,"island",WI_CONTAINER,0,0,(float)scene.width,(float)scene.height));
        if(root.n(WI_VISIBLE,0,1)==0||root.n(WI_OPACITY,0,1)<=0){clearInput();return;}

        RECT client{}, screen{};
        GetClientRect(hwnd, &client);
        GetWindowRect(hwnd, &screen);
        RECT inputRect{};
        GetWindowRect(inputWindow, &inputRect);
        if (!EqualRect(&screen, &inputRect))
            SetWindowPos(inputWindow, nullptr, screen.left, screen.top, client.right, client.bottom,
                         SWP_NOACTIVATE | SWP_NOZORDER);
        // Integer raster boundaries are the cache key; steady music frames do not
        // create HRGNs or call SetWindowRgn just because progress/audio changed.
        std::vector<int> shape{
            (int)client.right,
            (int)std::ceil(scene.width * dpi * 2),
            (int)std::floor(scene.height * dpi),
            (int)std::floor(scene.musicH * dpi),
            (int)std::lround(std::min({scene.radius, scene.width / 2, scene.height / 2}) * dpi * 2),
            hasMusic && scene.music && scene.musicAlpha > .05 && !suspended};
        shape.push_back((int)std::lround(scene.topRadius * dpi * 2));
        shape.push_back(!hasMusic && !scene.music && !scene.notice && !suspended);
        for (auto &[id, b] : scene.buttons)
            shape.insert(shape.end(),
                         {id, (int)std::ceil(b.x * dpi), (int)std::ceil(b.y * dpi),
                          (int)std::floor((b.x + b.w) * dpi), (int)std::floor((b.y + b.h) * dpi)});
        if (inputRegion && inputShape == shape)
            return;
        inputShape = std::move(shape);
        auto region = CreateRectRgn(0, 0, 0, 0);
        inputButtons.clear();
        if (hasMusic && scene.music && scene.musicAlpha > .05 && !suspended) {
            double left = (client.right-scene.width*dpi)/2;
            auto shell = inputShell(client.right);
            for (const auto &[id, b] : scene.buttons) {
                double bottom = std::min<double>(b.y + b.h, scene.musicH);
                if (bottom <= b.y)
                    continue;
                auto button =
                    CreateRectRgn((int)std::ceil(b.x * dpi), (int)std::ceil(b.y * dpi),
                                  (int)std::floor((b.x + b.w) * dpi), (int)std::floor(bottom * dpi));
                CombineRgn(region, region, button, RGN_OR);
                DeleteObject(button);
                inputButtons.emplace_back(id, b);
            }
            CombineRgn(region, region, shell, RGN_AND);
            DeleteObject(shell);
        } else if (!hasMusic && !scene.music && !scene.notice && !suspended) {
            double left = (client.right-scene.width*dpi)/2;
            auto shell = inputShell(client.right);
            CombineRgn(region, shell, nullptr, RGN_COPY);
            DeleteObject(shell);
            inputButtons.emplace_back(5,
                                      Box{(float)(left / dpi + std::min(0., scene.topRadius)), 0,
                                          (float)(scene.width - 2*std::min(0., scene.topRadius)), (float)scene.height});
        }
        if(!suspended){
            auto shell=inputShell(client.right);
            for(auto [id,b]:scene.buttons)if(id>=50000 || (id>=101&&id<=104)){
                auto button=CreateRectRgn((int)std::ceil(b.x*dpi),(int)std::ceil(b.y*dpi),
                                         (int)std::floor((b.x+b.w)*dpi),(int)std::floor((b.y+b.h)*dpi));
                CombineRgn(button,button,shell,RGN_AND);CombineRgn(region,region,button,RGN_OR);DeleteObject(button);
                inputButtons.emplace_back(id,b);
            }
            DeleteObject(shell);
        }
        // Region changes affect only input: the antialiased DirectComposition canvas
        // is never clipped/resized. Unchanged frames do not allocate a new OS region.
        if (!inputRegion || !EqualRgn(region, inputRegion)) {
            auto copy = CreateRectRgn(0, 0, 0, 0);
            CombineRgn(copy, region, nullptr, RGN_COPY);
            if (!SetWindowRgn(inputWindow, copy, FALSE))
                DeleteObject(copy);
            if (inputRegion)
                DeleteObject(inputRegion);
            inputRegion = region;
        } else
            DeleteObject(region);
        if (std::none_of(inputButtons.begin(), inputButtons.end(),
                         [&](auto &b) { return b.first == scene.focus; }))
            scene.focus = 0;
    }
    void publishOpenScene(){
        if(!mods)return;
        std::vector<std::pair<std::string,std::string>> events;
        auto emit=[&](std::string topic,std::string payload=""){events.emplace_back(std::move(topic),std::move(payload));};
        uint32_t flags=(!scene.music&&!scene.notice?WI_IDLE:0)|(scene.music?WI_MUSIC:0)|(scene.music&&scene.music->playing?WI_PLAYING:0)|(!scene.lyric.empty()&&scene.music?WI_LYRIC:0)|(scene.notice?WI_NOTICE:0)|(scene.expanded?WI_IS_EXPANDED:0)|((settingsWindow||(electronProcess&&WaitForSingleObject(electronProcess,0)==WAIT_TIMEOUT))?WI_SETTINGS_OPEN:0)|(reducedMotion?WI_REDUCED_MOTION:0)|(renderer->fallback()?WI_SOFTWARE_RENDERER:0);
        auto changed=[&](uint32_t bit,const char* on,const char* off){if((flags&bit)!=(lastSceneFlags&bit))emit(flags&bit?on:off);};
        if(lastSceneFlags!=flags){emit("scene.changed");changed(WI_MUSIC,"scene.music.started","scene.music.stopped");changed(WI_LYRIC,"scene.lyric.started","scene.lyric.stopped");changed(WI_NOTICE,"scene.notice.shown","scene.notice.hidden");changed(WI_IS_EXPANDED,"scene.expanded","scene.collapsed");if(flags&WI_IDLE)emit("scene.idle");}
        std::string music=scene.music?utf8(scene.music->key()):"",lyric=utf8(scene.lyric),notice=scene.notice?scene.notice->correlation:"";
        if(music!=lastSceneMusic)emit("scene.music.changed",music);
        if(lyric!=lastSceneLyric)emit("scene.lyric.changed",lyric);
        if(scene.width!=lastSceneWidth||scene.height!=lastSceneHeight)emit("scene.layout.changed");
        lastSceneFlags=flags;lastSceneMusic=music;lastSceneLyric=lyric;lastSceneNotice=notice;lastSceneWidth=(float)scene.width;lastSceneHeight=(float)scene.height;
        WiSceneSnapshot state{sizeof(state),1};state.flags=flags;state.dpi=(uint32_t)std::lround(dpi*96);state.time=now();state.generation=++scene.publicState.generation;state.width=(float)scene.width;state.height=(float)scene.height;state.focus=sceneFocus;
        RECT rect;GetWindowRect(hwnd,&rect);state.x=(float)(rect.left+(rect.right-rect.left-scene.width*dpi)/2);state.y=(float)rect.top;
        auto root=scene.openPlan.resolve(sceneNode(1,"island",WI_CONTAINER,0,0,state.width,state.height));state.sizeMode=(uint32_t)root.n(WI_SIZE_MODE);
        mods->updateScene(state,scene.hostNodes,scene.visibleNodes);
        for(auto& [topic,payload]:events)mods->emit(topic,payload);
    }
    int inputAt(float x, float y) const {
        if (!inputRegion || !PtInRegion(inputRegion, (int)std::floor(x * dpi), (int)std::floor(y * dpi)))
            return -1;
        for(auto i=scene.sceneHits.rbegin();i!=scene.sceneHits.rend();++i){
            if(!i->block)continue;
            auto p=D2D1::Matrix3x2F(i->inverse._11,i->inverse._12,i->inverse._21,i->inverse._22,i->inverse._31,i->inverse._32).TransformPoint({x,y});
            if(i->bounds.hit(x,y)&&p.x>=0&&p.y>=0&&p.x<i->width&&p.y<i->height)return (int)i->id;
        }
        for (auto i = inputButtons.rbegin(); i != inputButtons.rend(); ++i)
            if (i->first<1000000&&!(i->first>=101&&i->first<=104)&&i->second.hit(x, y))
                return i->first;
        return -1;
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
        MONITORINFOEXW mi{}; mi.cbSize = sizeof(mi);
        GetMonitorInfoW(activeMonitor, &mi);
        DEVMODEW mode{}; mode.dmSize = sizeof(mode);
        if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency > 1)
            return mode.dmDisplayFrequency;
        return 60;
    }
    void applySettings() {
        BOOL animations = TRUE;
        SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0);
        reducedMotion = !animations;
        if (media)
            media->configure(settings.songSource, settings.lyricSource, settings.lyricApi, settings.playerFilter);
        if (telemetry)
            telemetry->configure(settings.showFps && !suspended, settings.showPing && !suspended,
                                 settings.pingTarget);
        scene.showFps = settings.showFps;
        scene.showPing = settings.showPing;
        autoFps = detectRate();
        if (hwnd)
            retarget(.4);
        native.enable(settings.hideNative && healthy && !suspended && !diagnostic);
        if (!awaitHold && scene.notice && holdUntil > 0)
            holdUntil = std::min(holdUntil, now() + settings.seconds);
        dirty = true;
    }
    void saveSettings(bool reconfigure = true) {
        ++settingsRevision;
        std::string changes;
        auto changed = [&](const char *key, auto before, auto after) {
            if (before != after) changes += std::string(key) + "=" + std::to_string(before) + "->" + std::to_string(after) + ";";
        };
        changed("FrameRate", loggedSettings.fps, settings.fps);
        changed("Resident", loggedSettings.resident, settings.resident);
        changed("HideNative", loggedSettings.hideNative, settings.hideNative);
        changed("DwellSeconds", loggedSettings.seconds, settings.seconds);
        changed("IslandZoom", loggedSettings.islandZoom, settings.islandZoom);
        changed("WidthRatio", loggedSettings.widthRatio, settings.widthRatio);
        changed("HeightRatio", loggedSettings.heightRatio, settings.heightRatio);
        changed("TopArcScale", loggedSettings.topArcScale, settings.topArcScale);
        changed("TopAttach", loggedSettings.topAttach, settings.topAttach);
        changed("RadiusMode", loggedSettings.radiusMode, settings.radiusMode);
        changed("DpiCorrection", loggedSettings.dpiCorrection, settings.dpiCorrection);
        changed("PlayerFilter", loggedSettings.playerFilter, settings.playerFilter);
        if (loggedSettings.monitorDevice != settings.monitorDevice) changes += "MonitorDevice=changed;";
        changed("SongSource", loggedSettings.songSource, settings.songSource);
        changed("LyricSource", loggedSettings.lyricSource, settings.lyricSource);
        changed("ShowFps", loggedSettings.showFps, settings.showFps);
        changed("ShowPing", loggedSettings.showPing, settings.showPing);
        if (loggedSettings.lyricApi != settings.lyricApi) changes += "LyricApi=changed;";
        if (loggedSettings.pingTarget != settings.pingTarget) changes += "PingTarget=changed;";
        if (!changes.empty()) monitor.event("设置更改；" + changes);
        loggedSettings = settings;
        auto s = settings;
        auto p = config;
        auto revision=settingsRevision.load();
        jobs.post([this,s,p,revision] { std::lock_guard lock(settingsSaveMutex);if(revision==settingsRevision.load())s.save(p); });
        if (reconfigure) applySettings();
    }
    void updateTopArc(bool reset = false) {
        double value = reset ? 1. : std::clamp((int)sendSetting(settingsWindow, 138, TBM_GETPOS, 0, 0), 50, 150) / 100.;
        settings.topArcScale = value;
        sendSetting(settingsWindow, 138, TBM_SETPOS, TRUE, (LPARAM)std::lround(value*100));
        wchar_t text[32]; swprintf_s(text, L"%.2f 倍", value);
        setSettingText(settingsWindow, 271, text);
        retarget(.18);
        topArcSavePending = true;
        SetTimer(settingsWindow, 8, 300, nullptr);
    }
    void flushTopArc() {
        if (!topArcSavePending) return;
        KillTimer(settingsWindow, 8); topArcSavePending = false;
        saveSettings(false);
    }
    double musicWidth() {
        if (!hasMusic || !scene.music)
            return 180.4;
        auto l = scene.music->lyrics;
        return std::min(availableWidth, (l ? (scene.expanded ? l->expanded : l->compact) : 460.46) * MusicScale);
    }
    double musicHeight() {
        return hasMusic ? (scene.expanded ? (scene.music && scene.music->timeline ? 121.8 : 105) : 48.384) *
                              MusicScale
                        : 0;
    }
    void retarget(double seconds) {
        Measure probe(LayoutWork);
        if (!renderer)
            return;
        Geo target;
        target.zoom = std::min(scaleX, scaleY);
        const double wx = scaleX / target.zoom, hy = scaleY / target.zoom;
        const double maxWidth = availableWidth * wx;
        target.w = musicWidth() * wx;
        target.mh = musicHeight() * hy;
        target.h = hasMusic ? target.mh : 29.92 * hy * IdleHeightScale;
        target.ma = hasMusic ? 1 : 0;
        bool information = settings.showFps || settings.showPing;
        target.ia = information ? 1 : 0;
        target.ih = information && !hasMusic ? 29.92 * hy * IdleHeightScale : 0;
        // A compact bar is a true capsule. Expanded panels retain the existing
        // softer corner size, interpolated with the same geometry animation.
        target.r = hasMusic && scene.expanded ? 23.1 * MusicScale : target.h / 2;
        target.offset = (!scene.notice || closingNotice) && !hasMusic && !settings.resident ? -std::max(45.92 * scaleY, scene.height + 8) : 0;
        if (scene.notice && !closingNotice) {
            auto &n = *scene.notice;
            double tw =
                n.title.size() > 100 ? maxWidth : renderer->textWidth(n.title, 14, true) * 16 / 14;
            double bw = n.body.size() > 100 ? maxWidth : renderer->textWidth(n.body, 12) * 14 / 12;
            const double noticeWidth = std::max(target.w, std::clamp(std::ceil(std::max(tw, bw)) + 48,
                                                   std::min(180.4 * wx, maxWidth), maxWidth)) * NoticeScale;
            // Shrink the notification, not the music header or its hit targets.
            target.w = hasMusic || information ? std::max(target.w, noticeWidth) : noticeWidth;
            scene.noticeTextWidth = target.w / NoticeScale - 48;
            double titleH = std::min(76.8, renderer->textHeight(n.title, 14, scene.noticeTextWidth, true));
            double bodyH = n.body.empty() ? 0 : renderer->textHeight(n.body, 12, scene.noticeTextWidth) + 6.4;
            target.h = target.mh + target.ih +
                       std::min(hostHeight / target.zoom - target.mh - target.ih,
                                std::max(52.8 * hy, titleH + bodyH + 33.6) * NoticeScale);
            scene.noticeBodyHeight = std::max(0., (target.h - target.mh - target.ih) / NoticeScale - titleH - 40);
            target.na = switching ? 0 : 1;
            target.r = 28 * NoticeScale;
        }
        // Registered plugin content occupies a separate footer; it never covers music or notices.
        size_t modRows=0;for(auto& r:modState.resources)if(r.kind==WI_LAYER||r.kind==WI_BUTTON)++modRows;
        target.modH=std::min(std::min<size_t>(3,modRows)*28.,std::max(0.,hostHeight/target.zoom-target.h-8));
        target.h+=target.modH;
        if (target.modH>0)target.offset=0;
        if (settings.radiusMode != 0 && !scene.expanded && (!scene.notice || closingNotice))
            target.r = std::min(target.w / 2, target.h * (settings.radiusMode == 1 ? .5 : 2. / 3));
        target.r = std::min({target.r, target.w / 2, target.h / 2});
        target.topRadius = settings.topAttach ? -std::min(TopArcBaseline * settings.topArcScale, target.r) : target.r;
        if (target.topRadius == to.topRadius && target.zoom == to.zoom && std::abs(target.w - to.w) < .01 && std::abs(target.h - to.h) < .01 && target.mh == to.mh &&
            target.ma == to.ma && target.na == to.na && target.offset == to.offset && target.r == to.r &&
            target.ih == to.ih && target.ia == to.ia && target.modH==to.modH)
            return;
        monitor.event("动画目标更新；宽=" + std::to_string(target.w) + " 高=" + std::to_string(target.h));
        from = geo;
        to = target;
        animStart = now();
        animSeconds = reducedMotion ? std::min(seconds, .16) : seconds*modState.replacement("animation-duration",1.);
        animating = true;
        dirty = true;
    }
    void onMusic(std::shared_ptr<Music> m) {
        if(m&&mods){auto s=mods->snapshot();m=std::make_shared<Music>(*m);m->title=wide(s.text("music-title",utf8(m->title)));m->artist=wide(s.text("music-artist",utf8(m->artist)));m->album=wide(s.text("music-album",utf8(m->album)));m->duration=s.replacement("music-duration",m->duration);mods->emit("music.changed",utf8(m->key()));}
        if (m && scene.music && m->key() != scene.music->key())
            scene.lyric.clear();
        if (pressAt && m) {
            pressAt = 0;
            scene.holdTriggered = false;
            pressedButton = -1;
            if (GetCapture() == inputWindow)
                ReleaseCapture();
        }
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
            auto style = GetWindowLongPtrW(inputWindow, GWL_EXSTYLE);
            SetWindowLongPtrW(inputWindow, GWL_EXSTYLE,
                              show ? style & ~WS_EX_NOACTIVATE : style | WS_EX_NOACTIVATE);
        }
        retarget(presence ? .56 : .4);
        updateSettingsMusic();
        dirty = true;
    }
    void enqueue(Notice n) {
        if(mods){auto s=mods->snapshot();n.title=wide(s.text("notice-title",utf8(n.title)));n.body=wide(s.text("notice-body",utf8(n.body)));mods->emit("notice.received",utf8(n.title));}
        if (n.correlation.empty()) n.correlation = sha256(n.key() + n.fingerprint).substr(0, 16);
        if (suspended) {
            traceNotice(n, "filtered", "reason=app_suspended");
            return;
        }
        traceNotice(n, "enqueued", "pending=" + std::to_string(queue.size()));
        auto p = std::make_shared<Notice>(std::move(n));
        if (!scene.notice) {
            setNotice(p);
            return;
        }
        if (scene.notice->key() == p->key()) {
            if (scene.notice->title != p->title || scene.notice->body != p->body) {
                traceNotice(*scene.notice, "ended", "reason=replaced_by_update");
                scene.notice = p;
                // The same toast ID can carry a new QQ message while shrinking.
                // Reverse from the current geometry and grant a new full dwell.
                closingNotice = switching = false;
                visibleNotice.clear();
                holdUntil = 0;
                awaitHold = true;
                retarget(.3);
                if (!animating) { awaitHold = false; holdUntil = now() + settings.seconds; }
                dirty = true;
            } else {
                traceNotice(*p, "filtered", "reason=duplicate_active_content");
            }
            return;
        }
        for (auto &q : queue)
            if (q->key() == p->key()) {
                q = p;
                traceNotice(*p, "merged", "reason=queued_item_updated");
                return;
            }
        queue.push_back(p);
    }
    void setNotice(std::shared_ptr<Notice> n) {
        if (pressAt) {
            pressAt = 0;
            scene.holdTriggered = false;
            pressedButton = -1;
            if (GetCapture() == inputWindow)
                ReleaseCapture();
        }
        scene.notice = n;
        visibleNotice.clear();
        ++noticesShown;
        traceNotice(*n, "show_requested", "pending=" + std::to_string(queue.size()));
        closingNotice = false;
        switching = false;
        holdUntil = 0;
        awaitHold = true;
        if (n->handler == 12)
            delivered++;
        retarget(.52);
    }
    void finishNotice() {
        if (scene.notice) traceNotice(*scene.notice, "hide_requested", "reason=dwell_elapsed");
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
        if(mods)for(int id:mods->takeSceneActions())action(id);
        if(mods&&now()>=nextMods){nextMods=now()+.016;ModSnapshot current;if(mods->snapshotSince(modState.revision,current)){
            modState=std::move(current);scene.modResources=modState.resources;scene.openPlan=modState.scene;
            scene.modTint=(unsigned)modState.replacement("island-tint",0);retarget(.18);dirty=true;
        }}
        double time = now();
        if (anchorStart) {
            double p = std::min(1., (time-anchorStart) / (reducedMotion ? .1 : .35));
            anchorY = mix(anchorFrom, anchorTo, ease(p)); dirty = true;
            if (p >= 1) anchorStart = 0;
        }
        double dtFeedback = feedbackTick ? std::clamp(time - feedbackTick, 0., .1) : 1. / 60;
        feedbackTick = time;
        for (auto [alpha, enabled] :
             {std::pair{&scene.fpsAlpha, settings.showFps}, std::pair{&scene.pingAlpha, settings.showPing}}) {
            double target = enabled ? 1. : 0.;
            if (std::abs(*alpha - target) > .001) {
                *alpha = reducedMotion ? target : mix(*alpha, target, 1 - std::exp(-dtFeedback * 22));
                dirty = true;
            } else
                *alpha = target;
        }
        if (pressAt && (hasMusic || scene.notice || GetCapture() != inputWindow)) {
            pressAt = 0;
            scene.holdTriggered = false;
        }
        if (pressAt && !scene.holdTriggered && time - pressAt >= .6) {
            scene.holdTriggered = true;
            ++holdTriggers;
            monitor.event("待机长按反馈触发");
        }
        double pressTarget = pressAt ? (scene.holdTriggered ? 1. : .45) : 0;
        if (std::abs(scene.press - pressTarget) > .001) {
            scene.press =
                reducedMotion ? pressTarget : mix(scene.press, pressTarget, 1 - std::exp(-dtFeedback * 22));
            dirty = true;
        }
        if (telemetry && time >= nextTelemetry && (settings.showFps || settings.showPing)) {
            nextTelemetry = time + .5;
            auto sample = telemetry->snapshot();
            if (scene.fps != sample.fps || scene.ping != sample.ping)
                dirty = true;
            scene.fps = sample.fps;
            scene.ping = sample.ping;
        }
        if (scene.expanded) {
            POINT pointer{};
            GetCursorPos(&pointer);
            bool hovered = WindowFromPoint(pointer) == inputWindow;
            if (hovered || pressedButton >= 0 || GetCapture() == inputWindow || scene.busy)
                panelTouched = time;
            else if (panelTouched && time - panelTouched >= 5) {
                hostExpanded = scene.expanded = false;
                panelTouched = 0;
                monitor.event("音乐控制面板无操作 5 秒，自动收回");
                retarget(.42);
            }
        }
        if (hasMusic && rawMusic && !rawMusic->playing && pausedAt && time - pausedAt >= 10) {
            hasMusic = false;
            retarget(.56);
        }
        if (holdUntil && time >= holdUntil)
            finishNotice();
        if (animating) {
            double t = std::min(1., (time - animStart) / animSeconds), p = ease(t);
            geo.topRadius = mix(from.topRadius, to.topRadius, p);
            geo.zoom = mix(from.zoom, to.zoom, p);
            geo.w = mix(from.w, to.w, p);
            geo.h = mix(from.h, to.h, p);
            geo.r = mix(from.r, to.r, p);
            geo.mh = mix(from.mh, to.mh, p);
            geo.ih = mix(from.ih, to.ih, p);
            geo.ia = mix(from.ia, to.ia, p);
            geo.modH = mix(from.modH, to.modH, p);
            geo.offset = mix(from.offset, to.offset, p);
            double fa = to.na > from.na ? ease((t - .16) / .84) : ease(t * 2);
            geo.na = mix(from.na, to.na, fa);
            geo.ma = mix(from.ma, to.ma, to.ma > from.ma ? ease((t - .16) / .84) : ease(t * 1.6));
            dirty = true;
            if (t >= 1) {
                geo = to;
                animating = false;
                scene.showFps = settings.showFps;
                scene.showPing = settings.showPing;
                if (!hasMusic) {
                    scene.music.reset();
                    scene.lyric.clear();
                    hostExpanded = scene.expanded = false;
                }
                if (closingNotice) {
                    if (scene.notice) { traceNotice(*scene.notice, "ended", "reason=contraction_complete"); ++completedNotices; }
                    scene.notice.reset();
                    closingNotice = false;
                    if (!queue.empty()) {
                        auto n = queue.front();
                        queue.pop_front();
                        setNotice(n);
                    }
                } else if (switching) {
                    if (scene.notice) { traceNotice(*scene.notice, "ended", "reason=next_in_queue"); ++completedNotices; }
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
            auto lyric = scene.music->lyrics;
            scene.lyric = lyricDisplay(*scene.music, lyricIndex);
            if (scene.lyric != previousLyric) dirty = true;
            if (feedbackUntil && time >= feedbackUntil) {
                scene.feedback.clear();
                feedbackUntil = 0;
            }
            auto f = audio->snapshot();
            bool valid = f.source == scene.music->key() && f.available;
            for (size_t i = 0; i < MusicBarCount; i++) {
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
        if(((lastSceneFlags & WI_SETTINGS_OPEN)!=0) != (settingsWindow!=nullptr))dirty=true;
        auto expansion=scene.openPlan.resolve(sceneNode(1,"island",WI_CONTAINER,0,0,0,0));
        bool expanded=expansion.n(WI_EXPANDED,0,hostExpanded)!=0;
        if(scene.expanded!=expanded){scene.expanded=expanded;retarget(.18);}
        if (dirty && !suspended) {
            // The same animated uniform content scale and shell geometry feed
            // rendering, OS input and accessibility. Independent ratios never distort glyphs.
            scene.topRadius = std::min({geo.topRadius, geo.w/2, geo.h/2}) * geo.zoom;
            scene.contentScale = geo.zoom;
            scene.width = geo.w * geo.zoom;
            scene.height = geo.h * geo.zoom;
            scene.radius = std::min({geo.r, geo.w / 2, geo.h / 2}) * geo.zoom;
            scene.musicH = geo.mh * geo.zoom;
            scene.musicAlpha = geo.ma;
            scene.noticeAlpha = geo.na;
            scene.infoH = geo.ih * geo.zoom;
            scene.infoAlpha = geo.ia;
            scene.modH=geo.modH*geo.zoom;
            auto island=scene.openPlan.resolve(sceneNode(1,"island",WI_CONTAINER,0,0,(float)scene.width,(float)scene.height));
            int sizeMode=(int)island.n(WI_SIZE_MODE);
            if(sizeMode==WI_PLUGIN_MANAGED){scene.width=std::max(1.,island.n(WI_RECT,2));scene.height=std::max(1.,island.n(WI_RECT,3));}
            else if(sizeMode==WI_INTRINSIC){for(auto& raw:scene.openPlan.created){auto node=scene.openPlan.resolve(raw);if(node.n(WI_VISIBLE,0,1)&&node.n(WI_LAYOUT)){scene.width=std::max(scene.width,node.n(WI_RECT)+node.n(WI_RECT,2));scene.height=std::max(scene.height,node.n(WI_RECT,1)+node.n(WI_RECT,3));}}}
            if(island.values.contains(WI_RADIUS)){scene.radius=island.n(WI_RADIUS);scene.topRadius=scene.radius;}
            if(island.values.contains(WI_BACKGROUND)){unsigned rr=(unsigned)(island.n(WI_BACKGROUND)*255),gg=(unsigned)(island.n(WI_BACKGROUND,1)*255),bb=(unsigned)(island.n(WI_BACKGROUND,2)*255);scene.modTint=(rr<<16)|(gg<<8)|bb;}
            RECT canvas{};GetClientRect(hwnd,&canvas);int cw=std::max(canvas.right,(LONG)std::ceil(scene.width*dpi+32)),ch=std::max(canvas.bottom,(LONG)std::ceil(scene.height*dpi+32));
            if(cw!=canvas.right||ch!=canvas.bottom){SetWindowPos(hwnd,nullptr,0,0,cw,ch,SWP_NOMOVE|SWP_NOACTIVATE|SWP_NOZORDER);renderer->resize(cw,ch,dpi);}
            int y = (int)std::lround(anchorY + geo.offset * dpi);
            int islandX=workArea.left+(workArea.right-workArea.left-cw)/2;
            if(island.values.contains(WI_POSITION)){islandX=(int)std::lround(island.n(WI_POSITION)-(cw-scene.width*dpi)/2);y=(int)island.n(WI_POSITION,1);}
            RECT r;
            GetWindowRect(hwnd, &r);
            if (y != r.top || islandX != r.left)
                SetWindowPos(hwnd, nullptr, islandX, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
            if (lastFrame && time - lastFrame < 5)
                perf.add(FrameGap, (time - lastFrame) * 1000);
            bool submitted = renderer->draw(scene);
            if (submitted) {
                updateInput();
                publishOpenScene();
                if (scene.notice && geo.na >= .95 && !closingNotice && !switching &&
                    geo.h - geo.mh - geo.ih >= 40 * NoticeScale && visibleNotice != scene.notice->correlation) {
                    visibleNotice = scene.notice->correlation;
                    ++visibleNotices;
                    traceNotice(*scene.notice, "visible", "render_submitted=1;alpha=" + std::to_string(geo.na));
                }
                if (scene.music && monitor.active()) {
                    const auto &m = *scene.music;
                    auto state = m.songTag + ":" + m.lyricState;
                    if (time >= nextLyricTrace || state != lastLyricState) {
                        nextLyricTrace = time + 2;
                        lastLyricState = state;
                        monitor.event("歌词呈现；数据=" + std::string(m.testSource ? "test" : "real") +
                            ";session=" + m.sessionTag + ";song=" + m.songTag + ";request=" + std::to_string(m.lyricRequest) +
                            ";state=" + m.lyricState + ";sync_source=" + m.lyricSyncSource + ";position_s=" + std::to_string(m.progress()) +
                            ";raw_s=" + std::to_string(m.timelineRawPosition) + ";updated_utc_100ns=" + std::to_string(m.timelineUpdated) +
                            ";valid_remaining_s=" + std::to_string(m.timelineExpires>0?std::max(0.,m.timelineExpires-time):0) +
                            ";timeline_event=" + std::to_string(m.timelineRevision) + ";reason=" + m.timelineReason +
                            ";lines=" + std::to_string(m.lyrics ? m.lyrics->lines.size() : 0) + ";index=" + std::to_string(lyricIndex) +
                            ";render_submitted=1;alpha=" + std::to_string(geo.ma));
                    }
                }
            }
            lastFrame = time;
            dirty = !submitted;
        } else if (!animating)
            lastFrame = 0;
    }
    void action(int id) {
        if((id>=1000000||(id>=101&&id<=104))&&mods){WiInputEvent e{sizeof(e),1};e.kind=WI_POINTER_CLICK;e.target=id;e.time=now();mods->sceneInput(e);return;}
        if(id>=50000&&mods){mods->invoke((uint64_t)id);return;}
        if (!hasMusic || !scene.music || id < 0 || id > 4)
            return;
        if (scene.expanded)
            panelTouched = now();
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
            hostExpanded = scene.expanded = !scene.expanded;
            panelTouched = scene.expanded ? now() : 0;
            retarget(.42);
        } else if (hasMusic && scene.music && !scene.busy) {
            scene.busy = true;
            std::wstring actionOverride;
            if(id==2)for(auto i=modState.resources.rbegin();i!=modState.resources.rend();++i)if(i->kind==WI_REPLACE&&i->key=="music-action"){actionOverride=wide(i->value);break;}
            media->command(!actionOverride.empty()?actionOverride.c_str():id == 1   ? L"previous"
                           : id == 2 ? L"toggle"
                           : id == 3 ? L"next"
                                     : L"mode",
                           scene.music->controlKey.empty() ? scene.music->key() : scene.music->controlKey);
        }
        dirty = true;
    }
    void pause(bool p) {
        clearInput();
        suspended = p;
        if (telemetry)
            telemetry->configure(settings.showFps && !p, settings.showPing && !p, settings.pingTarget);
        closingNotice = false;
        switching = false;
        awaitHold = false;
        queue.clear();
        scene.notice.reset();
        scene.busy = false;
        rawMusic.reset();
        hasMusic = false;
        scene.music.reset();
        scene.lyric.clear();
        hostExpanded = scene.expanded = false;
        holdUntil = 0;
        animating = false;
        geo = to = Geo{};
        geo.offset = to.offset = settings.resident ? 0 : -45.92;
        audio->select(nullptr);
        media->pause(p);
        notices->pause(p);
        qqNotices->pause(p);
        native.enable(!p && healthy && settings.hideNative && !diagnostic);
        ShowWindow(hwnd, p ? SW_HIDE : SW_SHOWNOACTIVATE);
        ShowWindow(inputWindow, p ? SW_HIDE : SW_SHOWNOACTIVATE);
        if (!p) {
            position();
            dirty = true;
        }
    }
    void updateMonitorControls() {
        if (!settingsWindow)
            return;
        setSettingText(settingsWindow, 108,
                       monitorPending     ? L"正在处理…"
                       : monitor.active() ? L"停止信息监测"
                                          : L"启动信息监测");
        EnableWindow(settingControl(settingsWindow, 108), !monitorPending);
        EnableWindow(settingControl(settingsWindow, 109), !monitorPending);
        setSettingText(settingsWindow, 110, monitorMessage.c_str());
        setSettingText(settingsWindow, 111, exportPath.c_str());
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
            << "\n通知读取状态: " << healthy << " (bool: Windows 通知数据库可读；非 QQ 权限/展示结果)\n"
            << "运行模式: " << (observation ? "真实来源观察（隔离配置；无模拟网络/媒体）" : diagnostic ? "测试（隔离配置；可注入媒体/网络）" : "正式") << '\n'
            << "配置路径: " << utf8(config.wstring()) << '\n'
            << "歌曲信息来源设置: " << settings.songSource << ";歌词来源设置: " << settings.lyricSource << '\n';
        if (rawMusic) env << "当前播放器: " << utf8(rawMusic->platform) << ";session=" << rawMusic->sessionTag
                          << ";song=" << rawMusic->songTag << ";state=" << rawMusic->lyricState << '\n';
        jobs.post([this, operation, overrideFolder, environment = env.str()] {
            auto result = std::make_unique<MonitorResult>();
            try {
                if (operation == 0) {
                    monitor.start(environment);
                    result->message = L"正在记录 · 每 5 秒采样，不记录聊天正文";
                } else if (operation == 1) {
                    monitor.sample();
                    monitor.stop();
                    writeAtomic((diagnostic ? testDir : dataDir()) / L"monitor" / L"last-session.txt", monitor.report());
                    result->message = L"监测已停止，本次报告可导出。";
                } else {
                    monitor.sample();
                    fs::path dir =
                        overrideFolder.empty() ? (diagnostic ? testDir : exePath().parent_path()) / L"daxian日志" : overrideFolder;
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
    void refreshDisplays() {
        settingsDisplays = displays();
        sendSetting(settingsWindow, 135, CB_RESETCONTENT, 0, 0);
        sendSetting(settingsWindow, 135, CB_ADDSTRING, 0, (LPARAM)L"自动（当前显示器，拖动切换）");
        int selected = 0;
        for (size_t i = 0; i < settingsDisplays.size(); ++i) {
            const auto &d = settingsDisplays[i];
            auto label = std::wstring(d.info.szDevice) + L" · " + std::to_wstring(d.info.rcMonitor.right-d.info.rcMonitor.left) + L" × " +
                         std::to_wstring(d.info.rcMonitor.bottom-d.info.rcMonitor.top);
            sendSetting(settingsWindow, 135, CB_ADDSTRING, 0, (LPARAM)label.c_str());
            if (settings.monitorDevice == d.info.szDevice) selected = (int)i+1;
        }
        sendSetting(settingsWindow, 135, CB_SETCURSEL, selected, 0);
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
            jobs.post([this] {
                monitor.event("应用退出");
                monitor.sample();
                monitor.stop();
                writeAtomic((diagnostic ? testDir : dataDir()) / L"monitor" / L"last-session.txt", monitor.report());
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
        if (observation && c != "music-live" && c != "monitor-start" && c != "monitor-stop" &&
            c != "monitor-export" && c != "music-expand" && c != "music-collapse" && c != "capture" &&
            c != "settings" && c != "close-settings") return;
        if(diagnostic&&c.rfind("media-click=",0)==0){
            auto key=c.substr(12);for(auto& n:scene.visibleNodes)if(n.key==key){WiInputEvent e{sizeof(e),1};e.kind=WI_POINTER_CLICK;e.target=n.id;e.time=now();mods->sceneInput(e);break;}return;
        }
        if(diagnostic&&c.rfind("mod-disable=",0)==0){mods->request("disable",c.substr(12));return;}
        if(diagnostic&&c.rfind("mod-enable=",0)==0){mods->request("enable",c.substr(11));return;}
        if (c == "music-stop") {
            overrideMusic = true;
            onMusic(nullptr);
        } else if (c == "music-live" || c == "media-fixture-only") {
            overrideMusic = false;
            media->fixtures = c == "media-fixture-only";
        } else if (c.rfind("phase=", 0) == 0)
            perf.setPhase(c.substr(6));
        else if (c == "perf-flush")
            jobs.post([this] {
                perf.flush();
                if (monitor.active()) {
                    monitor.sample();
                    writeAtomic((diagnostic ? testDir : dataDir()) / L"monitor" / L"active-session.txt", monitor.report());
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
            m->testSource = true;
            m->sessionTag = "test-ipc";
            m->platform = L"媒体集成测试";
            m->title = c == "music=next" ? L"合成歌曲 B" : L"合成歌曲 A";
            m->artist = L"测试歌手";
            m->songTag = sha256(utf8(m->key())).substr(0,16);
            m->playing = c != "music=pause";
            m->paused = !m->playing;
            m->timeline = true;
            m->position = 15;
            m->duration = 180;
            m->stamp = now();
            onMusic(m);
        } else if (c == "music-expand" || c == "music-collapse") {
            hostExpanded = scene.expanded = c == "music-expand";
            panelTouched = scene.expanded ? now() : 0;
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
        else if (c.rfind("song-source=", 0) == 0) {
            settings.songSource = std::clamp(std::stoi(c.substr(12)), 0, 2);
            saveSettings();
        } else if (c.rfind("lyric-source=", 0) == 0) {
            settings.lyricSource = std::clamp(std::stoi(c.substr(13)), 0, 5);
            saveSettings();
        } else if (c.rfind("show-fps=", 0) == 0) {
            settings.showFps = c == "show-fps=on";
            saveSettings();
        } else if (c.rfind("show-ping=", 0) == 0) {
            settings.showPing = c == "show-ping=on";
            saveSettings();
        } else if (c.rfind("ping-target=", 0) == 0) {
            auto value = wide(c.substr(12));
            if (validPingTarget(value)) {
                settings.pingTarget = value;
                saveSettings();
            }
        } else if (c.rfind("dpi=", 0) == 0) {
            testDpi = (float)std::clamp(std::stod(c.substr(4)), 1., 3.);
            position();
            if (settingsWindow) {
                settingsDpi = testDpi;
                layoutSettings();
            }
        } else if (c == "layout-reset") {
            settings.resetLayout(); position(); saveSettings();
        } else if (c.rfind("layout-", 0) == 0) {
            auto eq = c.find('=');
            if (eq != c.npos) {
                auto name = c.substr(0, eq);
                try {
                    double n = std::stod(c.substr(eq+1));
                    if (std::isfinite(n)) {
                        if (name == "layout-zoom" && n >= 1 && n <= 2) settings.islandZoom = n;
                        if (name == "layout-width" && n >= .75 && n <= 1.5) settings.widthRatio = n;
                        if (name == "layout-height" && n >= .75 && n <= 1.5) settings.heightRatio = n;
                        if (name == "layout-top") settings.topAttach = n != 0;
                        if (name == "layout-radius" && n >= 0 && n <= 2) settings.radiusMode = (int)n;
                        position(); saveSettings();
                    }
                } catch (...) {}
            }
        } else if (c.rfind("settings-page=", 0) == 0) {
            openNativeSettings(); settingsPage = std::clamp(std::stoi(c.substr(14)), 0, 4);
            settingsScroll = 0; layoutSettings();
        } else if (c.rfind("settings-input=", 0) == 0) {
            auto split = c.find(':');
            if (split != c.npos) {
                int id = std::stoi(c.substr(15, split-15));
                if (id == 130 || id == 131 || id == 132 || id == 136) {
                    setSettingText(settingsWindow, id, wide(c.substr(split+1)).c_str()); settingsChanged(id);
                }
            }
        } else if (c == "settings-reset") {
            settingsChanged(137);
        } else if (c.rfind("settings-scroll=", 0) == 0) {
            settingsScroll = std::max(0, std::stoi(c.substr(16))); layoutSettings();
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
        s << "PointerDowns=" << pointerDowns << "\nPointerUps=" << pointerUps
          << "\nPointerHit=" << lastPointerHit
          << "\nPanelIdleSeconds=" << (panelTouched ? now() - panelTouched : 0)
          << "\nModalFrames=" << modalFrames << "\nActionCount=" << actionCount
          << "\nLastAction=" << lastAction << "\nCommandsProcessed=" << commandsProcessed
          << "\nNoticesShown=" << noticesShown
          << "\nVersion=1.2.7alpha-r1\nIdle=" << truth(!scene.notice && !animating)
          << "\nAnimating=" << truth(animating)
          << "\nHolding=" << truth(scene.notice && !awaitHold && !switching) << "\nWidth=" << scene.width
          << "\nTopArcScale=" << settings.topArcScale
          << "\nHeight=" << scene.height << "\nTopRadius=" << scene.topRadius << "\nRadius=" << scene.radius
          << "\nMusicTop=0\nMusicHeight=" << scene.musicH << "\nMusicWidth=" << scene.width
          << "\nNoticeTop=" << scene.musicH + scene.infoH << "\nMusicExpanded=" << truth(scene.expanded)
          << "\nHasMusic=" << truth(hasMusic)
          << "\nMusicPlaying=" << truth(hasMusic && scene.music && scene.music->playing)
          << "\nMusicVisible=" << truth(scene.music != nullptr) << "\nMusicWindow=" << (uintptr_t)inputWindow
          << "\nRenderWindow=" << (uintptr_t)hwnd << "\nPending=" << queue.size()
          << "\nSyntheticDelivered=" << delivered << "\nConfiguredFrameRate=" << settings.fps
          << "\nEffectiveFrameRate=" << rate()
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
          << "\nReaderHealthy=" << truth(healthy) << "\nBars=" << scene.bars[0]
          << "\nBarCount=" << MusicBarCount << "\nMusicScale=" << MusicScale
          << "\nIslandScale=" << IslandScale << "\nContentScale=" << scene.contentScale
          << "\nIslandZoom=" << settings.islandZoom << "\nWidthRatio=" << settings.widthRatio
          << "\nHeightRatio=" << settings.heightRatio << "\nTopAttach=" << truth(settings.topAttach)
          << "\nWorkArea=" << workArea.left << "," << workArea.top << "," << workArea.right << "," << workArea.bottom
          << "\nOrigin=" << origin.x << "," << origin.y << "\n";
        auto f = audio->snapshot();
        s << "MonitorActive=" << truth(monitor.active()) << "\nMonitorPending=" << truth(monitorPending)
          << "\nSongSource=" << settings.songSource << "\nLyricSource=" << settings.lyricSource
          << "\nInfoSource=" << (scene.music ? utf8(scene.music->infoSource) : "")
          << "\nLyricStatus=" << (scene.music ? utf8(scene.music->lyricStatus) : "")
          << "\nLyricsLoaded=" << truth(scene.music && scene.music->lyrics && scene.music->lyrics->hasText())
          << "\nTimelineValid=" << truth(scene.music && scene.music->timeline) << "\nLyricProvider="
          << (scene.music && scene.music->lyrics ? utf8(scene.music->lyrics->provider) : "")
          << "\nLyricFormat=" << (scene.music && scene.music->lyrics ? utf8(scene.music->lyrics->format) : "")
          << "\nLyricSyncAvailable="
          << truth(scene.music && scene.music->timeline && scene.music->lyrics &&
                   !scene.music->lyrics->lines.empty())
          << "\nLyricState=" << (scene.music ? scene.music->lyricState : "no_music")
          << "\nLyricIndex=" << lyricIndex << "\nNoticesVisible=" << visibleNotices << "\nNoticesCompleted=" << completedNotices
          << "\nLyricSyncSource=" << (scene.music ? scene.music->lyricSyncSource : "none")
          << "\nPositionSeconds=" << (scene.music ? scene.music->progress() : 0)
          << "\nTimelineReason=" << (scene.music ? scene.music->timelineReason : "no_music")
          << "\nLyricRequest=" << (scene.music ? scene.music->lyricRequest : 0)
          << "\nBuild=" << BuildId
          << "\nShowFps=" << truth(settings.showFps) << "\nShowPing=" << truth(settings.showPing)
          << "\nFps=" << scene.fps << "\nPing=" << scene.ping
          << "\nFpsStatus=" << utf8(telemetry->snapshot().fpsStatus)
          << "\nPingStatus=" << utf8(telemetry->snapshot().pingStatus)
          << "\nPingTarget=" << utf8(telemetry->snapshot().pingTarget)
          << "\nForeground=" << telemetry->snapshot().foreground << "\nHoldTriggers=" << holdTriggers
          << "\nHoldTriggered=" << truth(scene.holdTriggered) << "\nPressFeedback=" << scene.press
          << "\nReducedMotion=" << truth(reducedMotion) << "\nMonitorHasData=" << truth(monitor.hasData())
          << "\nMonitorMessage=" << utf8(monitorMessage) << "\nExportPath=" << utf8(exportPath) << "\n";
        s << "NoticeTitle=" << (scene.notice && !observation ? utf8(scene.notice->title) : "")
          << "\nNoticeApplication=" << (scene.notice ? scene.notice->application : "")
          << "\nNoticeCorrelation=" << (scene.notice ? scene.notice->correlation : "")
          << "\nNoticeAlpha=" << geo.na
          << "\nNoticeClosing=" << truth(closingNotice) << "\nBusy=" << truth(scene.busy)
          << "\nHasCover=" << truth(scene.music && scene.music->cover)
          << "\nSettingsWindow=" << (uintptr_t)settingsWindow
          << "\nSettingsContent=" << (uintptr_t)settingsContent << "\nSettingsPage=" << settingsPage
          << "\nBarArea=" << scene.barArea.x << "," << scene.barArea.y << "," << scene.barArea.w << ","
          << scene.barArea.h << "\nSongTextRight=" << scene.songTextRight << "\nBarGap=" << scene.barGap
          << "\nDpi=" << dpi << "\n";
        for (auto &b : inputButtons)
            s << "Button" << b.first << "=" << b.second.x + b.second.w / 2 << ","
              << b.second.y + b.second.h / 2 << "\n";
        for(auto& n:scene.visibleNodes)s<<"SceneElement="<<n.id<<","<<n.owner<<","<<n.key<<","<<n.n(WI_RECT)<<","<<n.n(WI_RECT,1)<<","<<n.n(WI_RECT,2)<<","<<n.n(WI_RECT,3)<<"\n";
        for(auto& m:modState.media)s<<"Media="<<m.handle<<","<<m.owner<<","<<m.info.kind<<","<<m.info.state<<","<<m.info.generation<<","<<m.info.position<<","<<m.info.frameSequence<<","<<m.info.audioSamplesSubmitted<<"\n";
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
                if (modManagerMessage(message)) continue;
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
                auto monitorRoot=diagnostic?testDir:dataDir();
                jobs.post([monitorRoot] {
                    perf.flush();
                    if (monitor.active()) {
                        monitor.sample();
                        writeAtomic(monitorRoot / L"monitor" / L"active-session.txt", monitor.report());
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
        case SettingsRequestMessage: a.dispatchSettings(); return 0;
        case SettingsOpenMessage: a.openSettings(); return 0;
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
            if (a.qqNotices)
                for (auto &n : a.qqNotices->take()) a.enqueue(std::move(n));
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
                a.tooltipText = controlName(a.scene, a.scene.hover);
                ((NMTTDISPINFOW *)lp)->lpszText = a.tooltipText.data();
            }
            return 0;
        case WM_NCHITTEST: {
            POINT p{(short)LOWORD(lp), (short)HIWORD(lp)};
            ScreenToClient(h, &p);
            float x = p.x / a.dpi, y = p.y / a.dpi;
            if (h == a.inputWindow && a.inputAt(x, y) >= 0)
                return HTCLIENT;
            return HTTRANSPARENT;
        }
        case WM_MOUSEACTIVATE:
            return a.hasMusic || !a.scene.sceneHits.empty() || !a.scene.modResources.empty() ? MA_ACTIVATE : MA_NOACTIVATE;
        case WM_MOUSEMOVE: {
            if (GetCapture() == h && (a.pressedButton == 0 || a.pressedButton == 5)) {
                POINT point{}; GetCursorPos(&point);
                if (a.dragging || std::abs(point.x-a.dragStart.x) > GetSystemMetrics(SM_CXDRAG) ||
                    std::abs(point.y-a.dragStart.y) > GetSystemMetrics(SM_CYDRAG)) {
                    a.dragging = true; a.pressAt = 0; a.scene.holdTriggered = false;
                    a.panelTouched = now(); SetCursor(LoadCursorW(nullptr, IDC_SIZEALL)); return 0;
                }
            }
            float x = (short)LOWORD(lp) / a.dpi, y = (short)HIWORD(lp) / a.dpi;
            a.scene.hover = a.inputAt(x, y);
            if (a.pressAt && a.scene.hover != 5) {
                a.pressAt = 0;
                a.scene.holdTriggered = false;
                a.pressedButton = -1;
                ReleaseCapture();
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
        case WM_LBUTTONDOWN:
            ++a.pointerDowns;
            a.dragging = false; GetCursorPos(&a.dragStart);
            a.pressedButton = a.inputAt((short)LOWORD(lp) / a.dpi, (short)HIWORD(lp) / a.dpi);
            a.lastPointerHit = a.pressedButton;
            a.panelTouched = now();
            if (a.pressedButton == 5) {
                a.pressAt = now();
                a.scene.holdTriggered = false;
                a.dirty = true;
            }
            if (a.pressedButton >= 0)
                SetCapture(h);
            return 0;
        case WM_CAPTURECHANGED:
            a.dragging = false;
            a.pressedButton = -1;
            a.pressAt = 0;
            a.scene.holdTriggered = false;
            a.dirty = true;
            return 0;
        case WM_LBUTTONUP: {
            ++a.pointerUps;
            if (a.dragging) {
                POINT point{}; GetCursorPos(&point);
                auto destination = MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST);
                a.dragging = false; a.pressedButton = -1; a.pressAt = 0; ReleaseCapture();
                // A deliberate drag selects a monitor, preserving the top anchor.
                a.settings.monitorDevice.clear(); a.position(destination); a.saveSettings(); a.tick();
                if (a.settingsWindow) a.refreshDisplays();
                return 0;
            }
            a.panelTouched = now();
            a.scene.keyboard = false;
            float x = (short)LOWORD(lp) / a.dpi, y = (short)HIWORD(lp) / a.dpi;
            int pressed = a.pressedButton;
            a.pressAt = 0;
            a.scene.holdTriggered = false;
            a.pressedButton = -1;
            ReleaseCapture();
            if (pressed >= 0 && (pressed < 5 || pressed>=50000) && pressed == a.inputAt(x, y)) {
                SetFocus(h);
                a.action(pressed);
            }
            return 0;
        }
        case WM_KEYDOWN:
            a.panelTouched = now();
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
                a.hostExpanded = a.scene.expanded = false;
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
        case WM_WINDOWPOSCHANGED:
            if (h == a.hwnd && !a.positioning && a.renderer && a.settings.monitorDevice.empty()) {
                auto current = MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST);
                if (a.activeMonitor && current != a.activeMonitor) a.position(current);
            }
            break;
        case WM_DPICHANGED:
            if (a.renderer && !a.positioning) {
                auto suggested = (RECT*)lp;
                a.position(MonitorFromRect(suggested, MONITOR_DEFAULTTONEAREST)); a.tick();
            }
            return 0;
        case WM_DISPLAYCHANGE:
        case WM_SETTINGCHANGE:
            if (msg == WM_SETTINGCHANGE)
                a.applySettings();
            if (a.renderer) { a.position(); a.tick(); }
            if (msg == WM_DISPLAYCHANGE && a.settingsWindow) a.refreshDisplays();
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
    bool openSceneInput(HWND h,UINT msg,WPARAM wp,LPARAM lp){
        if(!mods)return false;
        bool pointer=msg==WM_MOUSEMOVE||msg==WM_LBUTTONDOWN||msg==WM_LBUTTONUP||msg==WM_MOUSEWHEEL;
        bool key=msg==WM_KEYDOWN||msg==WM_KEYUP||msg==WM_CHAR;
        if(!pointer&&!key&&msg!=WM_MOUSELEAVE&&msg!=WM_CAPTURECHANGED&&msg!=WM_KILLFOCUS)return false;
        POINT point{(short)LOWORD(lp),(short)HIWORD(lp)};if(msg==WM_MOUSEWHEEL)ScreenToClient(h,&point);
        float x=point.x/dpi,y=point.y/dpi;WiElement hit=0;bool block=false;
        if(pointer){for(auto it=scene.sceneHits.rbegin();it!=scene.sceneHits.rend();++it){auto m=D2D1::Matrix3x2F(it->inverse._11,it->inverse._12,it->inverse._21,it->inverse._22,it->inverse._31,it->inverse._32);auto p=m.TransformPoint({x,y});if(it->bounds.hit(x,y)&&p.x>=0&&p.y>=0&&p.x<it->width&&p.y<it->height){hit=it->id;block=it->block;break;}}}
        auto send=[&](WiElement id,uint32_t kind){if(!id)return;if(kind==WI_FOCUS_GAINED||kind==WI_FOCUS_LOST){dirty=true;mods->emit("scene.input.changed");}WiInputEvent e{sizeof(e),1};e.generation=scene.publicState.generation;e.target=id;e.kind=kind;e.key=(uint32_t)wp;e.x=x;e.y=y;for(auto& hit:scene.sceneHits)if(hit.id==id){auto m=D2D1::Matrix3x2F(hit.inverse._11,hit.inverse._12,hit.inverse._21,hit.inverse._22,hit.inverse._31,hit.inverse._32);auto p=m.TransformPoint({x,y});e.x=p.x;e.y=p.y;break;}e.modifiers=(GetKeyState(VK_SHIFT)<0?1:0)|(GetKeyState(VK_CONTROL)<0?2:0)|(GetKeyState(VK_MENU)<0?4:0);e.time=now();e.wheel=msg==WM_MOUSEWHEEL?GET_WHEEL_DELTA_WPARAM(wp)/120.f:0;mods->sceneInput(e);};
        if(msg==WM_MOUSEMOVE){if(hit!=sceneHover){send(sceneHover,WI_POINTER_LEAVE);send(hit,WI_POINTER_ENTER);sceneHover=hit;scene.hover=hit?(int)hit:-1;dirty=true;}send(scenePressed?scenePressed:hit,WI_POINTER_MOVE);if(scenePressed)send(scenePressed,WI_DRAG_UPDATE);}
        if(msg==WM_MOUSELEAVE){send(sceneHover,WI_POINTER_LEAVE);sceneHover=0;}
        if(msg==WM_LBUTTONDOWN&&hit){scenePressed=hit;SetCapture(h);SetFocus(h);if(sceneFocus!=hit){send(sceneFocus,WI_FOCUS_LOST);sceneFocus=hit;scene.focus=(int)hit;send(hit,WI_FOCUS_GAINED);}send(hit,WI_POINTER_DOWN);send(hit,WI_DRAG_START);}
        if(msg==WM_LBUTTONUP){auto pressed=scenePressed;scenePressed=0;send(pressed,WI_POINTER_UP);send(pressed,WI_DRAG_END);if(pressed&&pressed==hit){send(hit,WI_POINTER_CLICK);}if(pressed)ReleaseCapture();}
        if(msg==WM_MOUSEWHEEL)send(hit,WI_POINTER_WHEEL);
        if(msg==WM_CAPTURECHANGED){send(scenePressed,WI_DRAG_CANCEL);scenePressed=0;}
        if(msg==WM_KILLFOCUS){send(scenePressed,WI_DRAG_CANCEL);scenePressed=0;if(GetCapture()==h)ReleaseCapture();send(sceneFocus,WI_FOCUS_LOST);sceneFocus=0;}
        if(msg==WM_KEYDOWN&&wp==VK_TAB&&!scene.buttons.empty()){
            send(sceneFocus,WI_FOCUS_LOST);auto it=std::find_if(scene.buttons.begin(),scene.buttons.end(),[&](auto& b){return b.first==scene.focus;});int count=(int)scene.buttons.size(),index=it==scene.buttons.end()?-1:(int)(it-scene.buttons.begin());index=(index+(GetKeyState(VK_SHIFT)<0?count-1:1)+count)%count;scene.focus=scene.buttons[index].first;sceneFocus=(scene.focus>=1000000||(scene.focus>=101&&scene.focus<=104))?scene.focus:0;scene.keyboard=true;send(sceneFocus,WI_FOCUS_GAINED);dirty=true;return true;
        }
        if(key&&sceneFocus){if(msg==WM_KEYDOWN&&(wp==VK_RETURN||wp==VK_SPACE)){bool textInput=false;for(auto& n:scene.visibleNodes)if(n.id==sceneFocus&&n.type==WI_INPUT)textInput=true;if(!textInput)send(sceneFocus,WI_POINTER_CLICK);}send(sceneFocus,msg==WM_CHAR?WI_TEXT_INPUT:msg==WM_KEYDOWN?WI_KEY_DOWN:WI_KEY_UP);if(wp==VK_TAB){send(sceneFocus,WI_FOCUS_LOST);sceneFocus=0;return false;}return true;}
        return pointer&&((hit&&block)||scenePressed!=0);
    }
    static LRESULT CALLBACK inputProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        if(app&&(msg==WM_RBUTTONUP||msg==WM_CONTEXTMENU)){app->openSettings();return 0;}
        if(app&&app->openSceneInput(h,msg,wp,lp))return 0;
        switch (msg) {
        case WM_NCHITTEST:
        case WM_MOUSEACTIVATE:
        case WM_MOUSEMOVE:
        case WM_MOUSELEAVE:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_CAPTURECHANGED:
        case WM_KEYDOWN:
        case WM_GETOBJECT:
        case WM_NOTIFY:
        case WM_APP + 6:
        case WM_APP + 7:
            return proc(h, msg, wp, lp);
        case WM_PAINT:
            ValidateRect(h, nullptr);
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
static void roundedOutline(HDC dc, RECT rect, float radius, COLORREF edge, float stroke = 1) {
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
    Gdiplus::Pen pen(color(edge), stroke);
    g.DrawPath(&pen, &path);
}
static LRESULT CALLBACK styledProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    wchar_t className[32]{};
    GetClassNameW(h, className, 32);
    bool trackbar = wcscmp(className, TRACKBAR_CLASSW) == 0;
    if (trackbar && msg == WM_ERASEBKGND)
        return 1;
    if (trackbar && msg == WM_PAINT) {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(h, &ps);
        RECT r{};
        GetClientRect(h, &r);
        FillRect(dc, &r, (HBRUSH)GetStockObject(WHITE_BRUSH));
        float scale = app ? app->settingsDpi : 1.f;
        int left = (int)std::lround(10 * scale), right = r.right - (int)std::lround(10 * scale);
        int cy = r.top + r.bottom / 2;
        auto lo = (int)SendMessageW(h, TBM_GETRANGEMIN, 0, 0), hi = (int)SendMessageW(h, TBM_GETRANGEMAX, 0, 0);
        auto pos = (int)SendMessageW(h, TBM_GETPOS, 0, 0);
        double t = hi > lo ? std::clamp((pos - lo) / double(hi - lo), 0., 1.) : 0.;
        int x = left + (int)std::lround((right - left) * t);
        HPEN trackPen = CreatePen(PS_SOLID, std::max(2, (int)std::lround(4 * scale)), RGB(220, 225, 235));
        HGDIOBJ oldPen = SelectObject(dc, trackPen);
        MoveToEx(dc, left, cy, nullptr); LineTo(dc, right, cy);
        SelectObject(dc, oldPen); DeleteObject(trackPen);
        HPEN activePen = CreatePen(PS_SOLID, std::max(2, (int)std::lround(4 * scale)), RGB(61, 104, 242));
        oldPen = SelectObject(dc, activePen);
        MoveToEx(dc, left, cy, nullptr); LineTo(dc, x, cy);
        SelectObject(dc, oldPen); DeleteObject(activePen);
        HBRUSH thumb = CreateSolidBrush(GetPropW(h, L"wi.hover") ? RGB(79, 119, 247) : RGB(61, 104, 242));
        HGDIOBJ oldBrush = SelectObject(dc, thumb);
        int radius = (int)std::lround(8 * scale);
        Ellipse(dc, x - radius, cy - radius, x + radius, cy + radius);
        SelectObject(dc, oldBrush); DeleteObject(thumb);
        if (GetFocus() == h && !(SendMessageW(h, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS)) {
            RECT focus = r; InflateRect(&focus, -2, -2); DrawFocusRect(dc, &focus);
        }
        EndPaint(h, &ps);
        return 0;
    }
    if (wcscmp(className, L"Static") == 0) {
        if (msg == WM_ERASEBKGND)
            return 1;
        if (msg == WM_PAINT) {
            // STATIC's incremental text drawing may leave the old suffix visible.
            // Paint the complete opaque text surface inside the composed viewport.
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(h, &ps);
            RECT r;
            GetClientRect(h, &r);
            int id = GetDlgCtrlID(h);
            // Keep the legacy light settings header; the dark block caused a
            // residual rectangle behind the title and version labels.
            bool header = false;
            bool outside = id == 106 || (id >= 240 && id <= 242) || id == 222 || id == 260;
            SetDCBrushColor(dc, header ? RGB(30, 35, 48) : outside ? RGB(246, 247, 250) : RGB(255, 255, 255));
            FillRect(dc, &r, (HBRUSH)GetStockObject(DC_BRUSH));
            bool heading = id == 200 || id == 222 || (id >= 260 && id <= 267) || (id >= 240 && id <= 243) || id == 230 || id == 231 ||
                           id == 233 || id == 235 || (id >= 210 && id <= 216 && id % 2 == 0);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, header ? RGB(245, 247, 252) : id == 106 || id == 238 || id == 244 ? RGB(177, 49, 60)
                             : heading                           ? RGB(25, 28, 35)
                                                                 : RGB(97, 105, 121));
            auto old = SelectObject(dc, (HFONT)SendMessageW(h, WM_GETFONT, 0, 0));
            std::wstring value(GetWindowTextLengthW(h) + 1, L'\0');
            GetWindowTextW(h, value.data(), (int)value.size());
            DrawTextW(dc, value.c_str(), -1, &r,
                      DT_NOPREFIX | DT_WORDBREAK | DT_EDITCONTROL | DT_END_ELLIPSIS |
                          (id == 201 ? DT_RIGHT : 0));
            SelectObject(dc, old);
            EndPaint(h, &ps);
            return 0;
        }
        if (msg == WM_SETTEXT) {
            auto result = DefSubclassProc(h, msg, wp, lp);
            // Invalidate the owner's composed surface too: invalidating only a
            // child can leave its previously committed pixels on screen.
            RECT changed;
            GetWindowRect(h, &changed);
            MapWindowPoints(nullptr, GetParent(h), (POINT *)&changed, 2);
            RedrawWindow(GetParent(h), &changed, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
            return result;
        }
    }
    bool combo = wcscmp(className, L"ComboBox") == 0;
    if (combo && msg == WM_ERASEBKGND)
        return 1;
    if (combo && msg == WM_PAINT) {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(h, &ps);
        RECT r;
        GetClientRect(h, &r);
        FillRect(dc, &r, (HBRUSH)GetStockObject(WHITE_BRUSH));
        float scale = app ? app->settingsDpi : 1.f;
        bool focus = GetFocus() == h, enabled = IsWindowEnabled(h);
        roundedControl(dc, r, 7 * scale, enabled ? RGB(255, 255, 255) : RGB(239, 241, 246),
                       focus                      ? RGB(61, 104, 242)
                       : GetPropW(h, L"wi.hover") ? RGB(165, 186, 242)
                                                  : RGB(215, 221, 233));
        auto old = SelectObject(dc, (HFONT)SendMessageW(h, WM_GETFONT, 0, 0));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, enabled ? RGB(25, 28, 35) : RGB(151, 158, 172));
        wchar_t value[256]{};
        GetWindowTextW(h, value, 256);
        RECT textRect = r;
        textRect.left += (LONG)(10 * scale);
        textRect.right -= (LONG)(30 * scale);
        DrawTextW(dc, value, -1, &textRect, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(dc, old);
        auto pen = CreatePen(PS_SOLID, std::max(1, (int)scale), RGB(97, 105, 121));
        old = SelectObject(dc, pen);
        int x = r.right - (int)(16 * scale), y = r.bottom / 2;
        MoveToEx(dc, x - (int)(4 * scale), y - (int)(2 * scale), nullptr);
        LineTo(dc, x, y + (int)(2 * scale));
        LineTo(dc, x + (int)(4 * scale), y - (int)(2 * scale));
        SelectObject(dc, old);
        DeleteObject(pen);
        if (focus && !(SendMessageW(h, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS)) {
            InflateRect(&r, -4, -4);
            DrawFocusRect(dc, &r);
        }
        EndPaint(h, &ps);
        return 0;
    }
    if (msg == WM_MOUSEWHEEL) {
        if (combo && SendMessageW(h, CB_GETDROPPEDSTATE, 0, 0))
            return DefSubclassProc(h, msg, wp, lp);
        return SendMessageW(GetParent(h), WM_MOUSEWHEEL, wp, lp);
    }
    if (msg == WM_KEYDOWN &&
        (wp == VK_PRIOR || wp == VK_NEXT || wp == VK_UP || wp == VK_DOWN || wp == VK_HOME || wp == VK_END)) {
        bool edit = wcscmp(className, L"Edit") == 0 || wcscmp(className, TRACKBAR_CLASSW) == 0;
        if (!combo && !edit)
            return SendMessageW(GetParent(h), WM_KEYDOWN, wp, lp);
    }
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
        RECT r;
        GetWindowRect(h, &r);
        MapWindowPoints(nullptr, GetParent(h), (POINT *)&r, 2);
        InflateRect(&r, 10, 10);
        InvalidateRect(GetParent(h), &r, FALSE);
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
    HWND c = CreateWindowExW(0, cls, text.c_str(), WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | style, 0, 0, 1,
                             1, parent, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    SetWindowSubclass(c, styledProc, 2, 0);
    return c;
}
void App::openNativeSettings() {
    if (settingsWindow) {
        ShowWindow(settingsWindow, SW_RESTORE);
        SetForegroundWindow(settingsWindow);
        return;
    }
    settingsDpi = dpi;
    fontDpi = 0;
    settingsScroll = 0;
    settingsPage = 0;
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
    int w = std::min<int>((int)(608 * settingsDpi), work.right - work.left),
        h = std::min<int>((int)(700 * settingsDpi), work.bottom - work.top);
    settingsWindow = CreateWindowExW(
        WS_EX_CONTROLPARENT, wc.lpszClassName, L"WinIsland 设置",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
        work.left + (work.right - work.left - w) / 2, work.top + (work.bottom - work.top - h) / 2, w, h, hwnd,
        nullptr, wc.hInstance, nullptr);
    control(settingsWindow, L"STATIC", L"WinIsland", 0, 200);
    control(settingsWindow, L"STATIC", Version, SS_RIGHT, 201);
    control(settingsWindow, L"STATIC", L"设置会自动保存并立即生效", 0, 202);
    wc.lpszClassName = L"WinIsland.Native.Settings.Content";
    RegisterClassW(&wc);
    settingsContent = CreateWindowExW(WS_EX_CONTROLPARENT | WS_EX_COMPOSITED, wc.lpszClassName, L"设置内容",
                                      WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                      0, 0, 1, 1, settingsWindow, (HMENU)300, wc.hInstance, nullptr);
    for (int i = 0; i < 5; ++i) {
        const wchar_t *tabs[] = {L"界面通知", L"音乐歌词", L"实时信息", L"运行诊断", L"其他设置"};
        control(settingsWindow, L"BUTTON", tabs[i], BS_OWNERDRAW | WS_TABSTOP, 250 + i);
    }
    const wchar_t *labels[] = {L"灵动岛常驻", L"隐藏Windows自带通知", L"通知停驻时间", L"动画帧率"};
    const wchar_t *hints[] = {L"关闭后，仅在收到通知时滑入屏幕", L"运行期间隐藏系统横幅，保留通知中心记录",
                              L"展开后保持显示的时间，支持小数",
                              L"0 跟随系统；可填 30–240，低性能默认目标 60 FPS"};
    for (int i = 0; i < 4; i++) {
        control(settingsContent, L"STATIC", labels[i], 0, 210 + i * 2);
        control(settingsContent, L"STATIC", hints[i], 0, 211 + i * 2);
    }
    for (int id = 100; id <= 101; id++) {
        auto c = control(settingsContent, L"BUTTON", labels[id - 100], BS_AUTOCHECKBOX | WS_TABSTOP, id);
        SetWindowSubclass(c, toggleProc, 1, 0);
        SendMessageW(c, BM_SETCHECK,
                     (id == 100 ? settings.resident : settings.hideNative) ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    std::wostringstream sec;
    sec << settings.seconds;
    control(settingsContent, L"EDIT", sec.str(), ES_CENTER | WS_TABSTOP, 102);
    control(settingsContent, L"EDIT", std::to_wstring(settings.fps), ES_CENTER | WS_TABSTOP, 103);
    sendSetting(settingsWindow, 102, EM_SETLIMITTEXT, 12, 0);
    sendSetting(settingsWindow, 103, EM_SETLIMITTEXT, 3, 0);
    control(settingsContent, L"STATIC", L"秒", 0, 220);
    control(settingsContent, L"STATIC", L"FPS", 0, 221);
    control(settingsContent, L"STATIC", L"", 0, 106);
    control(settingsContent, L"STATIC", L"", 0, 107);
    control(settingsContent, L"BUTTON", L"载入当前歌曲歌词…", BS_OWNERDRAW | WS_TABSTOP, 104);
    control(settingsContent, L"BUTTON", L"清除当前歌词", BS_OWNERDRAW | WS_TABSTOP, 105);
    control(settingsContent, L"BUTTON", L"启动信息监测", BS_OWNERDRAW | WS_TABSTOP, 108);
    control(settingsContent, L"BUTTON", L"导出日志报告", BS_OWNERDRAW | WS_TABSTOP, 109);
    control(settingsContent, L"STATIC", L"", 0, 110);
    control(settingsContent, L"EDIT", L"", ES_READONLY | ES_AUTOHSCROLL | WS_TABSTOP, 111);
    control(settingsContent, L"STATIC", L"运行诊断", 0, 222);
    control(settingsContent, L"STATIC", L"歌曲信息来源", 0, 230);
    control(settingsContent, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 120);
    for (auto label : {L"自动（SMTC → 窗口标题）", L"仅 SMTC", L"仅窗口标题"})
        sendSetting(settingsWindow, 120, CB_ADDSTRING, 0, (LPARAM)label);
    sendSetting(settingsWindow, 120, CB_SETCURSEL, settings.songSource, 0);
    control(settingsContent, L"STATIC", L"播放器", 0, 247);
    control(settingsContent, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 126);
    for (auto label : {L"自动识别", L"网易云音乐", L"QQ 音乐", L"汽水音乐"})
        sendSetting(settingsWindow, 126, CB_ADDSTRING, 0, (LPARAM)label);
    sendSetting(settingsWindow, 126, CB_SETCURSEL, settings.playerFilter, 0);
    control(settingsContent, L"STATIC", L"歌词来源", 0, 231);
    control(settingsContent, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 121);
    for (auto label : {L"自动（缓存优先，多源顺序查询）", L"仅 LRCLIB（在线）", L"关闭歌词", L"仅网易云歌词",
                       L"仅 LRCLIB 搜索", L"QCloudMusicApi（网易云歌词）"})
        sendSetting(settingsWindow, 121, CB_ADDSTRING, 0, (LPARAM)label);
    sendSetting(settingsWindow, 121, CB_SETCURSEL, settings.lyricSource, 0);
    control(settingsContent, L"STATIC",
            L"在线查询会发送歌名、歌手、专辑和时长。\n手动载入的 LRC "
            L"优先；缺少进度时仅显示未同步歌词。",
            0, 232);
    for (int id : {122, 123}) {
        auto toggle =
            control(settingsContent, L"BUTTON", id == 122 ? L"显示帧率（FPS）" : L"显示网络延迟（Ping）",
                    BS_AUTOCHECKBOX | WS_TABSTOP, id);
        SetWindowSubclass(toggle, toggleProc, 1, 0);
        SendMessageW(toggle, BM_SETCHECK,
                     (id == 122 ? settings.showFps : settings.showPing) ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    control(settingsContent, L"STATIC", L"显示帧率（FPS）", 0, 233);
    control(settingsContent, L"STATIC", L"前台应用 DXGI 提交帧率；不支持则 --，非屏幕刷新率", 0, 234);
    control(settingsContent, L"STATIC", L"显示网络延迟（Ping）", 0, 235);
    control(settingsContent, L"STATIC", L"每 3 秒检测 ICMP 往返延迟，超时显示 --", 0, 236);
    control(settingsContent, L"STATIC", L"Ping 目标 IPv4 / IPv6（留空使用本地网关）", 0, 237);
    control(settingsContent, L"EDIT", settings.pingTarget, ES_AUTOHSCROLL | WS_TABSTOP, 124);
    sendSetting(settingsWindow, 124, EM_SETLIMITTEXT, 63, 0);
    control(settingsContent, L"STATIC", L"歌词 API 地址（兼容 LRCLIB 的 HTTPS 服务）", 0, 243);
    control(settingsContent, L"EDIT", settings.lyricApi, ES_AUTOHSCROLL | WS_TABSTOP, 125);
    sendSetting(settingsWindow, 125, EM_SETLIMITTEXT, 512, 0);
    control(settingsContent, L"STATIC", L"", 0, 244);
    control(settingsContent, L"STATIC", L"", 0, 238);
    control(settingsContent, L"STATIC", L"", 0, 239);
    control(settingsContent, L"STATIC", L"界面与通知", 0, 240);
    control(settingsContent, L"STATIC", L"音乐与歌词", 0, 241);
    control(settingsContent, L"STATIC", L"实时信息", 0, 242);
    control(settingsContent, L"STATIC", L"其他设置", 0, 260);
    const wchar_t *layoutLabels[] = {L"灵动岛缩放比例（1.0–2.0）", L"宽度倍率（0.75–1.5）",
        L"高度倍率（0.75–1.5）", L"顶部贴合工作区", L"圆角模式", L"显示器", L"DPI 自动识别 · 尺寸修正（0.8–1.25）"};
    for (int i = 0; i < 7; ++i) control(settingsContent, L"STATIC", layoutLabels[i], 0, 261+i);
    for (int id : {130, 131, 132, 136}) {
        double value = id == 130 ? settings.islandZoom : id == 131 ? settings.widthRatio :
                       id == 132 ? settings.heightRatio : settings.dpiCorrection;
        std::wostringstream out; out << value;
        control(settingsContent, L"EDIT", out.str(), ES_AUTOHSCROLL | WS_TABSTOP, id);
        sendSetting(settingsWindow, id, EM_SETLIMITTEXT, 12, 0);
    }
    auto attach = control(settingsContent, L"BUTTON", L"顶部贴合", BS_AUTOCHECKBOX | WS_TABSTOP, 133);
    SetWindowSubclass(attach, toggleProc, 1, 0);
    SendMessageW(attach, BM_SETCHECK, settings.topAttach ? BST_CHECKED : BST_UNCHECKED, 0);
    control(settingsContent, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 134);
    for (auto label : {L"自动（贴顶平直，下角圆润）", L"半圆下角（展开保留柔和圆角）", L"2/3 圆度（横条限制为半圆）"})
        sendSetting(settingsWindow, 134, CB_ADDSTRING, 0, (LPARAM)label);
    sendSetting(settingsWindow, 134, CB_SETCURSEL, settings.radiusMode, 0);
    control(settingsContent, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 135);
    refreshDisplays();
    control(settingsContent, L"BUTTON", L"恢复默认布局", BS_OWNERDRAW | WS_TABSTOP, 137);
    control(settingsContent, L"STATIC", L"顶部外扩圆弧", 0, 270);
    auto arcSlider = control(settingsContent, TRACKBAR_CLASSW, L"顶部外扩圆弧", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 138);
    SendMessageW(arcSlider, TBM_SETRANGE, TRUE, MAKELPARAM(50,150));
    SendMessageW(arcSlider, TBM_SETPAGESIZE, 0, 10);
    SendMessageW(arcSlider, TBM_SETPOS, TRUE, (LPARAM)std::lround(settings.topArcScale*100));
    wchar_t arcValue[32]; swprintf_s(arcValue, L"%.2f 倍", settings.topArcScale);
    control(settingsContent, L"STATIC", arcValue, 0, 271);
    control(settingsContent, L"BUTTON", L"恢复默认", BS_OWNERDRAW | WS_TABSTOP, 139);
    control(settingsContent, L"STATIC", L"0.5–1.5 倍；1.0 倍为新版基准。仅影响贴顶外弧，不改变主体尺寸。", 0, 272);
    control(settingsContent, L"STATIC", L"", 0, 268);
    control(settingsContent, L"STATIC", L"插件管理", 0, 280);
    control(settingsContent, L"STATIC", L"已发现插件：由主程序扫描并按生命周期管理", 0, 281);
    for (auto [id,label] : {std::pair{140,L"插件管理"}})
        control(settingsContent, L"BUTTON", label, BS_OWNERDRAW | WS_TABSTOP, id);
    control(settingsContent, L"STATIC", L"修改后立即生效并保存。拖动音乐横条或待机岛到其他显示器后松开，可切换目标屏幕。\n"
            L"宽高分别改变容器，文字和封面保持等比；空间不足时自动限制尺寸。DPI 修正只调整内容大小，不改变工作区坐标。", 0, 269);
    HWND previousTab = HWND_TOP;
    for (int id : {100, 101, 102, 103, 120, 121, 125, 126, 104, 105, 122, 123, 124, 108, 109, 111, 130, 131, 132, 133, 134, 135, 136, 138, 139, 137, 140, 141, 142, 143, 144}) {
        HWND child = settingControl(settingsWindow, id);
        SetWindowPos(child, previousTab, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        previousTab = child;
    }
    control(settingsContent, L"STATIC", L"导出 UTF-8 文本报告，可直接复制保存路径。", 0, 245);
    control(settingsContent, L"STATIC",
            L"监测记录什么？\n\n每 5 秒采样 "
            L"CPU、内存与资源占用，记录动画、音乐、歌词与消息环节的耗时和异常。\n\n不记录聊天正文、歌曲名称、"
            L"账号或凭据。可以随时停止，再导出本次完整报告。",
            0, 246);
    layoutSettings();
    settingsCreating = false;
    updateMonitorControls();
    updateSettingsMusic();
    setSettingText(settingsWindow, 281, L"查看模组信息、启用状态、依赖、设置与日志。");
    ShowWindow(settingsWindow, SW_SHOW);
    SetForegroundWindow(settingsWindow);
}
void App::layoutSettings() {
    if (!settingsWindow || !settingsContent || settingsLayoutBusy)
        return;
    settingsLayoutBusy = true;
    HFONT oldFont = nullptr, oldSmall = nullptr, oldTitle = nullptr;
    if (!settingsFont || fontDpi != settingsDpi) {
        oldFont = settingsFont;
        oldSmall = smallFont;
        oldTitle = titleFont;
        auto font = [&](int size, int weight) {
            return CreateFontW(-(int)std::lround(size * settingsDpi), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        };
        settingsFont = font(14, FW_NORMAL);
        smallFont = font(12, FW_NORMAL);
        titleFont = font(26, FW_SEMIBOLD);
        fontDpi = settingsDpi;
        // Hidden categories also release their old font before it is deleted.
        EnumChildWindows(
            settingsWindow,
            [](HWND child, LPARAM value) -> BOOL {
                auto &a = *(App *)value;
                int id = GetDlgCtrlID(child);
                bool smallText = id == 202 || id == 106 || id == 107 || id == 110 || id == 111 || id == 220 ||
                                 id == 221 || (id >= 211 && id <= 217 && id % 2) || id == 232 || id == 234 ||
                                 id == 236 || id == 237 || id == 238 || id == 239 || id >= 244 && id <= 247;
                SendMessageW(child, WM_SETFONT,
                             (WPARAM)(id == 200   ? a.titleFont
                                      : smallText ? a.smallFont
                                                  : a.settingsFont),
                             FALSE);
                return TRUE;
            },
            (LPARAM)this);
        for (int id : {120, 121, 126, 134, 135}) {
            sendSetting(settingsWindow, id, CB_SETITEMHEIGHT, -1, (LPARAM)std::lround(28 * settingsDpi));
            sendSetting(settingsWindow, id, CB_SETITEMHEIGHT, 0, (LPARAM)std::lround(28 * settingsDpi));
        }
    }
    RECT root;
    GetClientRect(settingsWindow, &root);
    double rootW = root.right / settingsDpi;
    auto pixel = [&](double x) { return (int)std::lround(x * settingsDpi); };
    HDWP batch = BeginDeferWindowPos(64);
    auto move = [&](HWND c, double x, double y, double w, double h, UINT extra = 0) {
        RECT current;
        GetWindowRect(c, &current);
        MapWindowPoints(nullptr, GetParent(c), (POINT *)&current, 2);
        bool visible = (GetWindowLongW(c, GWL_STYLE) & WS_VISIBLE) != 0;
        if ((extra & SWP_SHOWWINDOW) && visible)
            extra &= ~SWP_SHOWWINDOW;
        int width = pixel(std::max(1., w)), height = pixel(std::max(1., h));
        wchar_t cls[20];
        GetClassNameW(c, cls, 20);
        bool sameSize = current.right - current.left == width &&
                        (current.bottom - current.top == height ||
                         (wcscmp(cls, L"ComboBox") == 0 && (INT_PTR)GetPropW(c, L"wi.layoutH") == height));
        bool samePosition = current.left == pixel(x) && current.top == pixel(y);
        if (sameSize && samePosition && extra == 0)
            return;
        // A closed combo reports only its selection height. Re-applying the
        // drop-list height on every scroll triggers its native resize/repaint.
        SetPropW(c, L"wi.layoutH", (HANDLE)(INT_PTR)height);
        UINT flags = SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOREDRAW | SWP_NOCOPYBITS | extra;
        if (sameSize)
            flags |= SWP_NOSIZE;
        if (samePosition)
            flags |= SWP_NOMOVE;
        if (batch)
            batch = DeferWindowPos(batch, c, nullptr, pixel(x), pixel(y), pixel(std::max(1., w)),
                                   pixel(std::max(1., h)), flags);
        else
            SetWindowPos(c, nullptr, pixel(x), pixel(y), pixel(std::max(1., w)), pixel(std::max(1., h)),
                         flags);
    };
    move(settingControl(settingsWindow, 200), 26, 20, rootW - 176, 36);
    move(settingControl(settingsWindow, 201), rootW - 151, 30, 125, 24);
    move(settingControl(settingsWindow, 202), 26, 62, rootW - 52, 24);
    double tabW = (rootW - 52 - 24) / 5;
    for (int i = 0; i < 5; ++i)
        move(settingControl(settingsWindow, 250 + i), 26 + i * (tabW + 6), 98, tabW, 36);
    // Viewport alone owns scrolling and the buffered native child tree.
    move(settingsContent, 0, 150, rootW, root.bottom / settingsDpi - 150);
    if (batch)
        EndDeferWindowPos(batch);
    RECT client;
    GetClientRect(settingsContent, &client);
    double height = client.bottom / settingsDpi;
    const int pageHeight[] = {452, 756, 522, 466, 1410};
    SCROLLINFO si{sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS};
    si.nMin = 0;
    si.nMax = pageHeight[settingsPage] - 1;
    si.nPage = (UINT)height;
    settingsScroll = std::clamp(settingsScroll, 0, std::max(0, pageHeight[settingsPage] - (int)height));
    si.nPos = settingsScroll;
    SetScrollInfo(settingsContent, SB_VERT, &si, TRUE);
    GetClientRect(settingsContent, &client);
    double w = client.right / settingsDpi, inner = w - 84;
    settingsCards.clear();
    settingsFrames.clear();
    std::vector<int> shown;
    // Native child HWNDs paint their own opaque surfaces.  Moving them by a
    // full viewport width during a transition lets those surfaces protrude
    // outside their cards and produces the white blocks seen in rapid page
    // switches.  The category strip stays a stable self-drawn control group;
    // the content tree stays in the target geometry and uses the staggered,
    // short vertical entrance below.
    batch = BeginDeferWindowPos(64);
    auto place = [&](int id, double x, double y, double width, double h) {
        shown.push_back(id);
        move(settingControl(settingsContent, id), x, y - settingsScroll, width, h, SWP_SHOWWINDOW);
    };
    auto card = [&](double y, double h) {
        settingsCards.push_back({26.f, (float)y, (float)(w - 52), (float)h});
    };
    auto field = [&](int id, double x, double y, double width) {
        settingsFrames.push_back({id, {(float)x, (float)y, (float)width, 36}});
        place(id, x + 8, y + 8, width - 16, 22);
    };
    if (settingsPage == 0) {
        place(240, 28, 8, w - 56, 26);
        card(48, 164);
        card(228, 164);
        for (int i = 0; i < 4; ++i) {
            double y = i < 2 ? 62 + i * 80 : 242 + (i - 2) * 80;
            place(210 + i * 2, 42, y, w - 190, 24);
            place(211 + i * 2, 42, y + 29, w - 190, 36);
            if (i < 2)
                place(100 + i, w - 88, y + 8, 46, 26);
            else {
                field(100 + i, w - 132, y + 6, 62);
                place(220 + i - 2, w - 62, y + 15, 28, 24);
            }
        }
        place(106, 28, 405, w - 56, 36);
    } else if (settingsPage == 1) {
        place(241, 28, 8, w - 56, 26);
        card(48, 536);
        card(594, 144);
        place(230, 42, 66, inner, 24);
        place(120, 42, 98, inner, 184);
        place(231, 42, 150, inner, 24);
        place(121, 42, 182, inner, 184);
        place(247, 42, 264, inner, 24);
        place(126, 42, 296, inner, 184);
        place(243, 42, 390, inner, 40);
        field(125, 42, 436, inner);
        place(244, 42, 482, inner, 42);
        place(232, 42, 528, inner, 54);
        place(107, 42, 610, inner, 60);
        double left = std::min(212., (inner - 10) * .56);
        place(104, 42, 676, left, 38);
        place(105, 52 + left, 676, inner - left - 10, 38);
    } else if (settingsPage == 2) {
        place(242, 28, 8, w - 56, 26);
        card(48, 192);
        card(256, 240);
        place(233, 42, 66, inner - 64, 24);
        place(122, w - 88, 66, 46, 26);
        place(234, 42, 104, inner, 40);
        place(235, 42, 158, inner - 64, 24);
        place(123, w - 88, 158, 46, 26);
        place(236, 42, 196, inner, 32);
        place(237, 42, 274, inner, 40);
        field(124, 42, 324, inner);
        place(238, 42, 372, inner, 42);
        place(239, 42, 422, inner, 60);
    } else if (settingsPage == 3) {
        place(222, 28, 8, w - 56, 26);
        card(48, 194);
        card(258, 184);
        place(108, 42, 66, (inner - 10) / 2, 38);
        place(109, 52 + (inner - 10) / 2, 66, (inner - 10) / 2, 38);
        place(110, 42, 120, inner, 44);
        field(111, 42, 173, inner);
        place(245, 42, 214, inner, 20);
        place(246, 42, 276, inner, 148);
    } else {
        place(260, 28, 8, w-56, 26);
        // Keep each group in its own card with a clear bottom safety margin;
        // the old coordinates overlapped the plugin controls and clipped the
        // reset button at the viewport edge.
        card(48, 244); card(308, 364); card(688, 240); card(948, 190); card(1158, 230);
        for (int i=0; i<3; ++i) {
            place(261+i, 42, 66+i*76, inner-110, 44);
            field(130+i, w-142, 66+i*76, 100);
        }
        place(264, 42, 326, inner-70, 28); place(133, w-88, 326, 46, 26);
        place(265, 42, 372, inner, 24); place(134, 42, 404, inner, 180);
        place(266, 42, 460, inner, 24); place(135, 42, 492, inner, 200);
        place(267, 42, 548, inner, 44); field(136, 42, 600, inner);
        place(270, 42, 706, inner-90, 28); place(271, w-120, 710, 78, 24);
        place(138, 42, 746, inner-116, 32); place(139, w-146, 743, 104, 36);
        place(272, 42, 795, inner, 48);
        place(280, 42, 970, inner, 28); place(281, 42, 1005, inner, 28);
        place(140, 42, 1045, inner, 38);
        place(269, 42, 1190, inner, 100); place(137, 42, 1304, 180, 38);
        place(268, 42, 1351, inner, 32);
    }
    for (HWND c = GetWindow(settingsContent, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        if (std::find(shown.begin(), shown.end(), GetDlgCtrlID(c)) == shown.end() &&
            (GetWindowLongW(c, GWL_STYLE) & WS_VISIBLE))
            move(c, 0, 0, 1, 1, SWP_HIDEWINDOW);
    }
    if (batch)
        EndDeferWindowPos(batch);
    if (oldFont)
        DeleteObject(oldFont);
    if (oldSmall)
        DeleteObject(oldSmall);
    if (oldTitle)
        DeleteObject(oldTitle);
    // No bitmap scrolling/copying old pixels. One viewport invalidation after
    // geometry settles lets WS_EX_COMPOSITED commit background and children together.
    RedrawWindow(settingsContent, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
    settingsLayoutBusy = false;
}
void App::updateSettingsMusic() {
    if (!settingsWindow)
        return;
    bool ready = hasMusic && scene.music && !scene.music->loading;
    EnableWindow(settingControl(settingsWindow, 104), ready && scene.music->timeline);
    EnableWindow(settingControl(settingsWindow, 105), ready);
    std::wstring hint = !ready                   ? L"等待所选来源提供有效歌曲；窗口标题来源还需真实音频活动。"
                        : !scene.music->timeline ? L"当前播放器未提供进度，封面和控制可用，同步歌词暂不可用。"
                                                 : L"可为当前歌曲载入 LRC，按真实播放进度同步。";
    if (ready)
        hint = scene.music->infoSource + L"；" + scene.music->lyricStatus +
               (scene.music->timeline ? L"" : L"；无实际播放进度，不能同步歌词");
    wchar_t previous[512];
    getSettingText(settingsWindow, 107, previous, 512);
    if (hint != previous)
        setSettingText(settingsWindow, 107, hint.c_str());
    if (telemetry) {
        auto t = telemetry->snapshot();
        auto status = L"FPS：" + t.fpsStatus + L"\nPing：" + t.pingStatus +
                      (settings.showPing ? L"；目标 " + t.pingTarget : L"");
        getSettingText(settingsWindow, 239, previous, 512);
        if (status != previous)
            setSettingText(settingsWindow, 239, status.c_str());
    }
}
void App::settingsChanged(int id) {
    if (id >= 130 && id <= 137) {
        if (id == 137) {
            settings.resetLayout();
            sendSetting(settingsWindow, 138, TBM_SETPOS, TRUE, 100);
            setSettingText(settingsWindow, 271, L"1.00 倍");
            bool creating = settingsCreating; settingsCreating = true;
            for (int field : {130, 131, 132, 136})
                setSettingText(settingsWindow, field, field == 130 ? L"1.5" : L"1");
            sendSetting(settingsWindow, 133, BM_SETCHECK, BST_CHECKED, 0);
            sendSetting(settingsWindow, 134, CB_SETCURSEL, 0, 0);
            sendSetting(settingsWindow, 135, CB_SETCURSEL, 0, 0);
            settingsCreating = creating;
        } else if (id == 133) settings.topAttach = sendSetting(settingsWindow, id, BM_GETCHECK, 0, 0) == BST_CHECKED;
        else if (id == 134) settings.radiusMode = std::clamp((int)sendSetting(settingsWindow, id, CB_GETCURSEL, 0, 0), 0, 2);
        else if (id == 135) {
            int n = (int)sendSetting(settingsWindow, id, CB_GETCURSEL, 0, 0);
            settings.monitorDevice = n > 0 && n <= (int)settingsDisplays.size() ? settingsDisplays[n-1].info.szDevice : L"";
        } else {
            wchar_t text[64]{}; getSettingText(settingsWindow, id, text, 64); wchar_t *end = nullptr;
            double n = wcstod(text, &end), lo = id == 130 ? 1. : id == 136 ? .8 : .75,
                   hi = id == 130 ? 2. : id == 136 ? 1.25 : 1.5;
            if (!*text || !end || *end || !std::isfinite(n) || n < lo || n > hi) {
                invalidInput = id; setSettingText(settingsWindow, 268, L"请输入所标范围内的数值；原设置仍生效。");
                InvalidateRect(settingsContent, nullptr, FALSE); return;
            }
            (id == 130 ? settings.islandZoom : id == 131 ? settings.widthRatio : id == 132 ? settings.heightRatio : settings.dpiCorrection) = n;
        }
        invalidInput = 0; setSettingText(settingsWindow, 268, L"布局已更新");
        position(); saveSettings(); tick(); return;
    }
    if (id == 125) {
        wchar_t text[513]{};
        getSettingText(settingsWindow, id, text, 513);
        if (!validLyricApi(text)) {
            setSettingText(settingsWindow, 244,
                           L"请输入 HTTPS 服务根地址，不含账号、查询或片段。原配置仍生效。");
            return;
        }
        settings.lyricApi = text;
        setSettingText(settingsWindow, 244, L"");
    }
    if (id == 120)
        settings.songSource = (int)sendSetting(settingsWindow, id, CB_GETCURSEL, 0, 0);
    if (id == 121)
        settings.lyricSource = (int)sendSetting(settingsWindow, id, CB_GETCURSEL, 0, 0);
    if (id == 126)
        settings.playerFilter = std::clamp((int)sendSetting(settingsWindow, id, CB_GETCURSEL, 0, 0), 0, 3);
    if (id == 122)
        settings.showFps = sendSetting(settingsWindow, id, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (id == 123)
        settings.showPing = sendSetting(settingsWindow, id, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (id == 124) {
        wchar_t text[64]{};
        getSettingText(settingsWindow, id, text, 64);
        if (!validPingTarget(text)) {
            setSettingText(settingsWindow, 238,
                           L"请输入有效 IPv4 / IPv6 地址，或留空使用网关。原配置仍生效。");
            return;
        }
        settings.pingTarget = text;
        setSettingText(settingsWindow, 238, L"");
    }
    if (id == 100)
        settings.resident = sendSetting(settingsWindow, 100, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (id == 101)
        settings.hideNative = sendSetting(settingsWindow, 101, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (id == 102 || id == 103) {
        wchar_t value[64];
        getSettingText(settingsWindow, id, value, 64);
        wchar_t *end;
        double number = wcstod(value, &end);
        bool valid =
            *value && !*end && std::isfinite(number) &&
            (id == 102 ? (number >= .1 && number <= 3600)
                       : (number == 0 || (number >= 30 && number <= 240 && std::floor(number) == number)));
        setSettingText(settingsWindow, 106,
                       valid       ? L""
                       : id == 102 ? L"请输入 0.1–3600 秒；当前设置保持生效。"
                                   : L"请输入 0 或 30–240 的整数；当前帧率保持生效。");
        invalidInput = valid ? 0 : id;
        InvalidateRect(settingsContent, nullptr, FALSE);
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
            setSettingText(settingsWindow, 106, L"歌曲已切换，请重新选择。");
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
            media->reload(clear);
        } catch (...) {
            PostMessageW(hwnd, WM_FEEDBACK, 0, 0);
        }
    });
}

LRESULT CALLBACK App::settingsProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (!app)
        return DefWindowProcW(h, msg, wp, lp);
    auto &a = *app;
    bool content = h == a.settingsContent;
    if (msg == WM_SIZE && !content && !a.settingsCreating && !a.settingsLayoutBusy) {
        a.layoutSettings();
        RedrawWindow(h, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
        return 0;
    }
    if (msg == WM_GETMINMAXINFO && !content) {
        auto info = (MINMAXINFO *)lp;
        info->ptMinTrackSize = {(LONG)(430 * a.settingsDpi), (LONG)(360 * a.settingsDpi)};
        return 0;
    }
    if (msg == WM_KEYDOWN) {
        UINT operation = wp == VK_UP      ? SB_LINEUP
                         : wp == VK_DOWN  ? SB_LINEDOWN
                         : wp == VK_PRIOR ? SB_PAGEUP
                         : wp == VK_NEXT  ? SB_PAGEDOWN
                         : wp == VK_HOME  ? SB_TOP
                         : wp == VK_END   ? SB_BOTTOM
                                          : UINT_MAX;
        if (operation != UINT_MAX) {
            SendMessageW(h, WM_VSCROLL, operation, 0);
            return 0;
        }
    }
    if (msg == WM_HSCROLL && (HWND)lp == settingControl(a.settingsWindow, 138)) {
        a.updateTopArc();
        // The trackbar is fully owner-drawn.  Force the old thumb and track
        // pixels to be discarded on every drag update; relying on the native
        // control's partial invalidation leaves a trail in the composited
        // settings viewport.
        InvalidateRect((HWND)lp, nullptr, FALSE);
        UpdateWindow((HWND)lp);
        if (LOWORD(wp) == TB_ENDTRACK) a.flushTopArc();
        return 0;
    }
    if (msg == WM_TIMER && wp == 8 && !content) { a.flushTopArc(); return 0; }
    if (msg == WM_COMMAND && !a.settingsCreating) {
        int id = LOWORD(wp), code = HIWORD(wp);
        if (id >= 250 && id <= 254 && code == BN_CLICKED) {
            for (int combo : {120, 121, 126, 134, 135})
                sendSetting(a.settingsWindow, combo, CB_SHOWDROPDOWN, FALSE, 0);
            int nextPage = id - 250;
            a.settingsPage = nextPage;
            a.settingsScroll = 0;
            a.wheelRemainder = 0;
            a.layoutSettings();
            for (int tab = 250; tab <= 254; ++tab)
                InvalidateRect(settingControl(a.settingsWindow, tab), nullptr, FALSE);
            return 0;
        }
        if (id == 139 && code == BN_CLICKED) { a.updateTopArc(true); a.flushTopArc(); return 0; }
        if ((id == 100 || id == 101) && code == BN_CLICKED)
            a.settingsChanged(id);
        if ((id == 102 || id == 103) && code == EN_CHANGE)
            a.settingsChanged(id);
        if (((id == 120 || id == 121 || id == 126 || id == 134 || id == 135) && code == CBN_SELCHANGE) ||
            ((id == 122 || id == 123 || id == 133 || id == 137) && code == BN_CLICKED) ||
            ((id == 124 || id == 125 || id == 130 || id == 131 || id == 132 || id == 136) && code == EN_KILLFOCUS))
            a.settingsChanged(id);
        if ((id == 102 || id == 103) && code == EN_KILLFOCUS) {
            std::wostringstream s;
            s << (id == 102 ? a.settings.seconds : a.settings.fps);
            setSettingText(h, id, s.str().c_str());
        }
        if (id == 104 && code == BN_CLICKED)
            a.importLyrics(false);
        if (id == 105 && code == BN_CLICKED)
            a.importLyrics(true);
        if (id == 108 && code == BN_CLICKED)
            a.monitoring(monitor.active() ? 1 : 0);
        if (id == 109 && code == BN_CLICKED)
            a.monitoring(2);
        if (code == BN_CLICKED && id == 140 && a.mods) openModManager(a.settingsWindow, *a.mods);
        return 0;
    }
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(h, &ps);
        // The embedded manager owns the complete settings surface while open.
        HWND embeddedManager = !content ? FindWindowExW(h, nullptr, L"WinIsland.ModManager", nullptr) : nullptr;
        bool managerTransition = embeddedManager && IsWindowVisible(embeddedManager);
        SetDCBrushColor(dc, ui_theme::Background);
        FillRect(dc, &ps.rcPaint, (HBRUSH)GetStockObject(DC_BRUSH));
        // Legacy settings layout uses a single light canvas; controls provide
        // their own hierarchy and selected-state colour.
        if (content) {
            RECT root{}; GetClientRect(h,&root);
            double rootW=root.right/a.settingsDpi;
            auto rect = [&](Box b) {
                return RECT{(LONG)std::lround(b.x * a.settingsDpi),
                            (LONG)std::lround((b.y - a.settingsScroll) * a.settingsDpi),
                            (LONG)std::lround((b.x + b.w) * a.settingsDpi),
                            (LONG)std::lround((b.y + b.h - a.settingsScroll) * a.settingsDpi)};
            };
            for (auto box : a.settingsCards) {
                RECT r = rect(box), visible;
                if (IntersectRect(&visible, &r, &ps.rcPaint))
                    roundedControl(dc, RECT{r.left, r.top + (LONG)std::lround(3 * a.settingsDpi), r.right, r.bottom + (LONG)std::lround(3 * a.settingsDpi)}, 12 * a.settingsDpi, RGB(226, 230, 238), RGB(226, 230, 238));
                if (IntersectRect(&visible, &r, &ps.rcPaint))
                    roundedControl(dc, r, 12 * a.settingsDpi, RGB(255, 255, 255), RGB(228, 232, 239));
            }
            for (auto [id, box] : a.settingsFrames) {
                RECT r = rect(box), visible;
                if (!IntersectRect(&visible, &r, &ps.rcPaint))
                    continue;
                bool focus = GetFocus() == settingControl(h, id);
                COLORREF edge = a.invalidInput == id ? RGB(177, 49, 60)
                                : focus              ? RGB(61, 104, 242)
                                                     : RGB(215, 221, 233);
                    roundedControl(dc, r, 7 * a.settingsDpi, RGB(255, 255, 255), edge, focus ? 1.5f : 1.f);
            }
        } else if (!managerTransition) {
            // The category strip belongs to the settings window, not the
            // scrolling content child. Painting it in the content HWND used
            // to add its y=98 offset below the real tabs at y=98.
            RECT root{}; GetClientRect(h,&root);
            double rootW=root.right/a.settingsDpi;
            roundedControl(dc,RECT{(LONG)std::lround(26*a.settingsDpi),(LONG)std::lround(98*a.settingsDpi),(LONG)std::lround((rootW-26)*a.settingsDpi),(LONG)std::lround(134*a.settingsDpi)},12*a.settingsDpi,RGB(238,241,247),RGB(226,230,238));
        }
        EndPaint(h, &ps);
        return 0;
    }
    if (msg == WM_CTLCOLORSTATIC || msg == WM_CTLCOLORDLG || msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX) {
        HDC dc = (HDC)wp;
        if (msg == WM_CTLCOLORLISTBOX) {
            // Native combo popups are separate list boxes. Returning the
            // system white brush here was the source of the bright rectangle
            // around themed settings dropdowns.
            static HBRUSH listBrush = CreateSolidBrush(ui_theme::Surface);
            SetBkColor(dc, ui_theme::Surface);
            SetTextColor(dc, ui_theme::Text);
            return (LRESULT)listBrush;
        }
        SetBkColor(dc, RGB(255, 255, 255));
        SetTextColor(dc, RGB(25, 28, 35));
        return (LRESULT)GetStockObject(WHITE_BRUSH);
    }
    if (msg == WM_DRAWITEM) {
        auto *d = (DRAWITEMSTRUCT *)lp;
        if (d->CtlType == ODT_BUTTON) {
            HDC dc = d->hDC;
            RECT r = d->rcItem;
            bool nav = d->CtlID >= 250 && d->CtlID <= 254;
            bool selected = nav && d->CtlID == 250 + a.settingsPage;
            if (!nav) {
                HBRUSH bg = CreateSolidBrush(RGB(255, 255, 255));
                FillRect(dc, &r, bg);
                DeleteObject(bg);
            }
            bool disabled = d->itemState & ODS_DISABLED, pressed = d->itemState & ODS_SELECTED,
                 hover = GetPropW(d->hwndItem, L"wi.hover") != nullptr;
            COLORREF fill = nav ? (selected ? RGB(61, 104, 242) : RGB(246, 247, 250))
                            : selected   ? RGB(61, 104, 242)
                            : disabled ? RGB(239, 241, 246)
                            : pressed  ? RGB(221, 230, 252)
                            : hover    ? RGB(234, 240, 255)
                                       : RGB(255, 255, 255);
            COLORREF edge = nav ? (selected ? RGB(61, 104, 242) : RGB(246, 247, 250))
                            : selected   ? RGB(61, 104, 242)
                            : disabled ? RGB(228, 232, 239)
                            : hover    ? RGB(165, 186, 242)
                                       : RGB(215, 223, 237);
            if (nav)
                roundedControl(dc, r, 10 * a.settingsDpi,
                               selected ? RGB(61, 104, 242) : RGB(238, 241, 247),
                               selected ? RGB(61, 104, 242) : RGB(238, 241, 247));
            else
                roundedControl(dc, r, 8 * a.settingsDpi, fill, edge);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, selected   ? RGB(255, 255, 255)
                             : disabled ? RGB(151, 158, 172)
                                        : RGB(44, 68, 128));
            HGDIOBJ old = SelectObject(dc, a.settingsFont);
            wchar_t value[128];
            GetWindowTextW(d->hwndItem, value, 128);
            DrawTextW(dc, value, -1, &r, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_END_ELLIPSIS);
            SelectObject(dc, old);
            // The selected tab already has its rounded blue state; the native
            // rectangular focus frame was the stray inner box seen in settings.
            // Keep a lightweight keyboard cue without reintroducing that box.
            if (nav && (d->itemState & ODS_FOCUS) &&
                !(SendMessageW(d->hwndItem, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS)) {
                // Keep a compact keyboard cue without drawing a long line
                // under the tab, which looked like a stray divider during
                // animated navigation.  Mouse-focused tabs keep the normal
                // system "hide focus" behaviour.
                RECT focusRect = r;
                InflateRect(&focusRect, -4, -4);
                roundedOutline(dc, focusRect, 6 * a.settingsDpi,
                               selected ? RGB(255, 255, 255) : RGB(61, 104, 242),
                               std::max(1.f, 1.25f * a.settingsDpi));
            } else if ((d->itemState & ODS_FOCUS) && !(d->itemState & ODS_NOFOCUSRECT)) {
                InflateRect(&r, -4, -4);
                DrawFocusRect(dc, &r);
            }
            return TRUE;
        }
    }
    if (msg == WM_APP + 9) {
        if (!content || !IsWindowVisible((HWND)wp))
            return 0;
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
    if (msg == WM_LBUTTONDOWN && content) {
        float x = (short)LOWORD(lp) / a.settingsDpi, y = (short)HIWORD(lp) / a.settingsDpi + a.settingsScroll;
        for (auto [id, box] : a.settingsFrames)
            if (box.hit(x, y)) {
                SetFocus(settingControl(h, id));
                return 0;
            }
    }
    if (msg == WM_ERASEBKGND)
        return 1;
    if (msg == WM_DPICHANGED && !content) {
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
        if (!content)
            return a.settingsContent ? SendMessageW(a.settingsContent, msg, wp, lp) : 0;
        for (int id : {120, 121, 126, 134, 135})
            if (sendSetting(h, id, CB_GETDROPPEDSTATE, 0, 0))
                sendSetting(h, id, CB_SHOWDROPDOWN, FALSE, 0);
        SCROLLINFO si{sizeof(si), SIF_ALL};
        GetScrollInfo(h, SB_VERT, &si);
        int next = a.settingsScroll;
        if (msg == WM_MOUSEWHEEL) {
            a.wheelRemainder += GET_WHEEL_DELTA_WPARAM(wp) * 36. / WHEEL_DELTA;
            int pixels = (int)a.wheelRemainder;
            a.wheelRemainder -= pixels;
            next -= pixels;
        } else
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
            case SB_THUMBPOSITION:
                next = HIWORD(wp);
                break;
            }
        next = std::clamp(next, 0, std::max(0, si.nMax - (int)si.nPage + 1));
        if (next != a.settingsScroll) {
            a.settingsScroll = next;
            a.layoutSettings();
        }
        return 0;
    }
    if (msg == WM_CLOSE) {
        DestroyWindow(h);
        return 0;
    }
    if (msg == WM_DESTROY) {
        if (!content) a.flushTopArc();
        if (content)
            a.settingsContent = nullptr;
        else
            a.settingsWindow = nullptr;
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}
#include "settings_host.inc"
int runApp(const fs::path &dir, bool software, bool observation) {
    App instance;
    app = &instance;
    instance.observation = observation;
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
        else if (argc == 4 && std::wstring(argv[1]) == L"--media-test")
            result = wi::MediaEngine::selfTest(argv[2],argv[3]) ? 0 : 1;
        else if (argc == 4 && std::wstring(argv[1]) == L"--media-host-test")
            result = wi::mediaHostTest(argv[2],argv[3]);
        else if (argc == 3 && std::wstring(argv[1]) == L"--mods-test")
            result = wi::modSystemTest(argv[2]);
        else if (argc == 3 && std::wstring(argv[1]) == L"--scene-test")
            result = wi::sceneStoreTest(argv[2]);
        else if (argc == 3 && std::wstring(argv[1]) == L"--animation-test")
            result = wi::AnimationRuntime::selfTest(argv[2]) ? 0 : 1;
        else if (argc == 3 && std::wstring(argv[1]) == L"--self-test")
            result = wi::selfTest(argv[2]);
        else if (argc == 3 && std::wstring(argv[1]) == L"--lyric-providers-test")
            result = wi::lyricProvidersNetworkTest(argv[2]);
        else if (argc == 3 && std::wstring(argv[1]) == L"--qcloud-network-test")
            result = wi::lyricQcloudNetworkTest(argv[2]);
        else if (argc == 3 && std::wstring(argv[1]) == L"--lyric-network-test")
            result = wi::lyricNetworkTest(argv[2]);
        else if (argc == 3 && std::wstring(argv[1]) == L"--components-test")
            result = wi::embeddedComponentsTest(argv[2]);
        else {
            wi::fs::path dir;
            bool soft = GetEnvironmentVariableW(L"WINISLAND_SOFTWARE", nullptr, 0) > 0;
            bool observation = argc == 3 && std::wstring(argv[1]) == L"--observe";
            if (argc == 3 && (std::wstring(argv[1]) == L"--verify" || observation))
                dir = argv[2];
            wchar_t perfdir[32768];
            DWORD n = GetEnvironmentVariableW(L"WINISLAND_PERF_DIR", perfdir, 32768);
            if (n > 0 && n < 32768)
                wi::perf.start(perfdir);
            HANDLE mutex =
                CreateMutexW(nullptr, TRUE, dir.empty() ? L"Local\\WinIsland.Desktop.Singleton" : nullptr);
            if (!dir.empty() || GetLastError() != ERROR_ALREADY_EXISTS)
                result = wi::runApp(dir, soft, observation);
            else if(auto existing=FindWindowW(L"WinIsland.Native",nullptr)){AllowSetForegroundWindow(ASFW_ANY);PostMessageW(existing,wi::SettingsOpenMessage,0,0);}
            if (mutex)
                CloseHandle(mutex);
        }
    } catch (const std::exception &e) {
        MessageBoxW(nullptr, wi::wide(e.what()).c_str(), L"WinIsland 1.2.7alpha", MB_ICONERROR);
        result = 1;
    }
    LocalFree(argv);
    if (graphicsToken)
        Gdiplus::GdiplusShutdown(graphicsToken);
    CoUninitialize();
    return result;
}







