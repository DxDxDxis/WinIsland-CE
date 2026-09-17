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
    // Host restricts WI_SETTING keys to ASCII letters/digits, '-' and '_'.
    // time-display.enabled is NOT accepted by c2 for settings.
    static constexpr const char* SettingKey = "time-display-enabled";
    uint64_t handles[4]{}; // setting, music, notice, timer
    bool enabled = false, clockFailed = false, showTime = false;
    int reason = 0;
    char localTime[32]{};

    void log(const char* s) noexcept { host.log(host.context, s); }
    void clear() noexcept {
        enabled = false; showTime = false;
        // Host revokes owner resources before onDisable; already removed is OK.
        for (auto& h : handles) { if (h) host.remove(host.context, h); h = 0; }
        localTime[0] = 0;
    }
    static int tick(void* context, const char*) noexcept {
        auto& s = *static_cast<ClockMod*>(context);
        if (!s.enabled || !s.showTime) return 0;
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
    static bool parseSwitch(const char* value, bool& result) noexcept {
        if (!value) return false;
        const char* end = value + strlen(value);
        auto space=[](char c){return c==' '||c=='\t'||c=='\r'||c=='\n';};
        while(value<end && space(*value)) ++value;
        while(end>value && space(end[-1])) --end;
        size_t n=(size_t)(end-value);
        if ((n==4 && !_strnicmp(value,"true",4)) || (n==1 && *value=='1')) { result=true; return true; }
        if ((n==5 && !_strnicmp(value,"false",5)) || (n==1 && *value=='0')) { result=false; return true; }
        return false;
    }
    bool applySwitch(bool value) noexcept {
        if (value == showTime && (!value || handles[3])) return true;
        if (!value) {
            showTime=false;
            if(handles[3]) host.remove(host.context,handles[3]);
            handles[3]=0; localTime[0]=0;
            log("显示时间设置：关闭；已停止时间刷新，保持隐藏");
            return true;
        }
        WinIslandResource timer{sizeof(timer),1,WI_TIMER,"clock-check","本地时间检查（安全隐藏）","",tick,this,30000};
        handles[3]=host.add(host.context,&timer);
        if(!handles[3]) {
            showTime=false; localTime[0]=0;
            log("时间刷新任务注册失败；设置请求未能执行，保持隐藏");
            return false;
        }
        showTime=true; tick(this,nullptr);
        log("显示时间设置：开启；当前宿主缺少固定待机文本目标和完整状态，时间仍隐藏");
        return true;
    }
    static int settingChanged(void* ctx,const char* text) noexcept {
        auto& s=*static_cast<ClockMod*>(ctx);
        if(!s.enabled) return 0;
        bool value=false;
        if(!parseSwitch(text,value)) s.log("显示时间设置格式无效（请输入 true/false 或 1/0）；按关闭处理");
        // The real host persists the raw string before invoking this callback.
        // It provides no setting-write API, so an invalid saved value remains
        // editable. Do not return failure and disable the entire plugin.
        s.applySwitch(value);
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
            {sizeof(WinIslandResource),1,WI_SETTING,SettingKey,"显示时间","true",settingChanged,&s,0},
            {sizeof(WinIslandResource),1,WI_EVENT,"music.changed","音乐信息观察","",music,&s,0},
            {sizeof(WinIslandResource),1,WI_EVENT,"notice.received","通知观察","",notice,&s,0}
        };
        for (unsigned i=0; i<3; ++i) {
            s.handles[i] = s.host.add(s.host.context, &resources[i]);
            if (!s.handles[i]) {
                s.log("资源注册失败，撤销已注册资源并保持隐藏"); s.clear(); return 1;
            }
        }
        s.log("已注册设置项：time-display-enabled（显示时间）；在插件设置页编辑 true/false");
        char saved[32]{}; bool value=false;
        if(!s.host.getSetting(s.host.context,SettingKey,saved,sizeof(saved)) || !parseSwitch(saved,value)) {
            s.log("显示时间设置读取失败或格式无效，使用安全默认值：关闭");
            value=false;
        }
        s.enabled = true;
        if(!s.applySwitch(value)){s.clear();return 1;}
        if(!value)s.log("显示时间设置：关闭；未启动时间刷新任务");
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
        !host->log || !host->add || !host->remove || !host->getSetting) return nullptr;
    auto* s = new(std::nothrow) ClockMod;
    if (!s) return nullptr;
    s->host = *host;
    s->api = {sizeof(IWinIslandMod),WINISLAND_MOD_ABI,s,ClockMod::load,ClockMod::enable,
              ClockMod::disable,ClockMod::unload,ClockMod::destroy};
    return &s->api;
}
