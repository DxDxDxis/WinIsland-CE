#include "../sdk/mod_api.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <new>

// No host-private state, window probing, threads, or layout replacements.
// c2 exposes arrival events only, not an authoritative idle snapshot. Its
// WI_LAYER always adds a footer and height. Thus this adapter MUST stay hidden.
static bool formatTime(const SYSTEMTIME& t, char (&out)[32]) noexcept {
    out[0] = 0;
    FILETIME valid{};
    if (!SystemTimeToFileTime(&t, &valid)) return false;
    return sprintf_s(out, "%u.%u.%u %02u:%02u", t.wYear, t.wMonth,
                     t.wDay, t.wHour, t.wMinute) > 0;
}

struct ClockMod {
    WinIslandHostApi host{};
    IWinIslandMod api{};
    uint64_t handles[3]{};
    bool enabled = false, clockFailed = false;
    int reason = 0;
    char localTime[32]{};

    void log(const char* s) noexcept { host.log(host.context, s); }
    void clear() noexcept {
        enabled = false;
        // Host revokes owner resources before onDisable; already removed is OK.
        for (auto& h : handles) { if (h) host.remove(host.context, h); h = 0; }
        localTime[0] = 0;
    }
    static int tick(void* context, const char*) noexcept {
        auto& s = *static_cast<ClockMod*>(context);
        if (!s.enabled) return 0;
        SYSTEMTIME t{};
        GetLocalTime(&t); // Windows local timezone, including DST/time changes.
        char text[32]{};
        bool valid = formatTime(t, text);
        if (!valid && !s.clockFailed) s.log("本地时间无效，时间内容保持隐藏");
        if (valid && s.clockFailed) s.log("本地时间读取恢复；仍无法确认待机，保持隐藏");
        s.clockFailed = !valid;
        // Minute precision: no resource replacement or logging on unchanged text.
        if (strcmp(s.localTime, text)) strcpy_s(s.localTime, text);
        return 0;
    }
    static int music(void* ctx, const char*) noexcept {
        auto& s = *static_cast<ClockMod*>(ctx);
        if (s.enabled && s.reason != 1) {
            s.reason = 1;
            s.log("收到音乐信息事件，时间保持隐藏；该事件不能证明正在播放或已回到待机");
        }
        return 0;
    }
    static int notice(void* ctx, const char*) noexcept {
        auto& s = *static_cast<ClockMod*>(ctx);
        if (s.enabled && s.reason != 2) {
            s.reason = 2;
            s.log("收到通知事件，时间保持隐藏；该事件不提供通知显示结束状态");
        }
        return 0;
    }
    static int load(void* ctx, const char*) noexcept {
        static_cast<ClockMod*>(ctx)->log("时间显示插件已加载；当前 API 无权威待机状态及固定尺寸待机文本区域，采用安全隐藏模式");
        return 0;
    }
    static int enable(void* ctx, const char*) noexcept {
        auto& s = *static_cast<ClockMod*>(ctx);
        if (s.enabled) return 0;
        s.clear(); s.reason = 0;
        WinIslandResource resources[] = {
            {sizeof(WinIslandResource),1,WI_EVENT,"music.changed","音乐信息观察","",music,&s,0},
            {sizeof(WinIslandResource),1,WI_EVENT,"notice.received","通知观察","",notice,&s,0},
            {sizeof(WinIslandResource),1,WI_TIMER,"clock-check","本地时间检查（安全隐藏）","",tick,&s,30000}
        };
        for (unsigned i=0; i<3; ++i) {
            s.handles[i] = s.host.add(s.host.context, &resources[i]);
            if (!s.handles[i]) {
                s.log("资源注册失败，撤销已注册资源并保持隐藏"); s.clear(); return 1;
            }
        }
        s.enabled = true; tick(ctx, nullptr);
        s.log("时间显示插件已启用；无法确认待机，隐藏时间；未注册 WI_LAYER，不改变灵动岛尺寸");
        return 0;
    }
    static int disable(void* ctx, const char*) noexcept {
        auto& s = *static_cast<ClockMod*>(ctx); s.clear();
        s.log("时间显示插件已禁用，已清理事件和定时任务"); return 0;
    }
    static int unload(void* ctx, const char*) noexcept {
        auto& s = *static_cast<ClockMod*>(ctx); s.clear();
        s.log("时间显示插件已卸载"); return 0;
    }
    static void destroy(void* ctx) noexcept { delete static_cast<ClockMod*>(ctx); }
};

extern "C" WINISLAND_MOD_API uint32_t WinIsland_ModAbi() { return WINISLAND_MOD_ABI; }
extern "C" WINISLAND_MOD_API IWinIslandMod* WinIsland_CreateMod(const WinIslandHostApi* host) {
    if (!host || host->size < sizeof(WinIslandHostApi) || host->version != WINISLAND_MOD_ABI ||
        !host->log || !host->add || !host->remove) return nullptr;
    auto* s = new(std::nothrow) ClockMod;
    if (!s) return nullptr;
    s->host = *host;
    s->api = {sizeof(IWinIslandMod),WINISLAND_MOD_ABI,s,ClockMod::load,ClockMod::enable,
              ClockMod::disable,ClockMod::unload,ClockMod::destroy};
    return &s->api;
}
