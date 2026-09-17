#include "core.h"
#include <dxgi.h>
#include <psapi.h>
#include <tlhelp32.h>

namespace wi {
Monitor monitor;
static std::string timestamp(bool filename = false) {
    SYSTEMTIME t;
    GetLocalTime(&t);
    char s[64];
    if (filename)
        sprintf_s(s, "%04d%02d%02d_%02d%02d%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    else
        sprintf_s(s, "%04d-%02d-%02d %02d:%02d:%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
                  t.wSecond);
    return s;
}
static double cpuSeconds() {
    FILETIME c, e, k, u;
    GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u);
    return (((uint64_t)k.dwHighDateTime << 32) | k.dwLowDateTime) / 1e7 +
           (((uint64_t)u.dwHighDateTime << 32) | u.dwLowDateTime) / 1e7;
}
static std::string environment() {
    std::ostringstream s;
    OSVERSIONINFOEXW os{sizeof(os)};
    auto rtl =
        (LONG(WINAPI *)(OSVERSIONINFOEXW *))GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
    if (rtl)
        rtl(&os);
    SYSTEM_INFO sys;
    GetNativeSystemInfo(&sys);
    MEMORYSTATUSEX mem{sizeof(mem)};
    GlobalMemoryStatusEx(&mem);
    s << "应用版本: " << utf8(Version)
      << "\n构建标识: " << BuildId << "\nEXE: " << utf8(exePath().wstring())
      << "\nEXE SHA256: " << sha256(readFile(exePath(), 16 * 1024 * 1024))
      << "\nPID: " << GetCurrentProcessId()
      << "\n架构: 原生 C++20 / Win32 / x64 / 静态 VC 运行库\nWindows: " << os.dwMajorVersion << '.'
      << os.dwMinorVersion << " build " << os.dwBuildNumber << "\n逻辑处理器: " << sys.dwNumberOfProcessors
      << "\n物理内存 MB: " << mem.ullTotalPhys / 1048576 << "\n";
    HKEY key;
    wchar_t cpu[256]{};
    DWORD bytes = sizeof(cpu);
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0,
                      KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
        RegQueryValueExW(key, L"ProcessorNameString", nullptr, nullptr, (BYTE *)cpu, &bytes);
        RegCloseKey(key);
        s << "CPU: " << utf8(cpu) << '\n';
    }
    ComPtr<IDXGIFactory1> dxgi;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&dxgi)))) {
        for (UINT i = 0;; i++) {
            ComPtr<IDXGIAdapter1> a;
            if (FAILED(dxgi->EnumAdapters1(i, &a)))
                break;
            DXGI_ADAPTER_DESC1 d{};
            if (a && SUCCEEDED(a->GetDesc1(&d)))
                s << "GPU: " << utf8(d.Description) << "; 专用显存 MB=" << d.DedicatedVideoMemory / 1048576
                  << "; vendor=" << d.VendorId << " device=" << d.DeviceId << '\n';
        }
    }
    s << "显示器数量: " << GetSystemMetrics(SM_CMONITORS) << "\n主屏像素: " << GetSystemMetrics(SM_CXSCREEN)
      << 'x' << GetSystemMetrics(SM_CYSCREEN) << '\n';
    SYSTEM_POWER_STATUS power{};
    GetSystemPowerStatus(&power);
    s << "交流供电: " << (int)power.ACLineStatus << "; 节能状态: " << (int)power.SystemStatusFlag << '\n';
    HMODULE sqlite = LoadLibraryExW(L"winsqlite3.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    s << "系统 winsqlite3: " << (sqlite ? "可用" : "缺失，通知能力受限") << '\n';
    if (sqlite)
        FreeLibrary(sqlite);
    s << "系统条件: SMTC 需要 Windows 10 1809+；进程音频回环需要 build 20348+，不可用时尝试系统会话峰值。\n";
    return s.str();
}
struct Monitor::Session {
    std::array<Metric, MetricCount> metrics;
    std::mutex mutex;
    std::string begun = timestamp(), ended, environmentText;
    double begin = now(), end = 0, cpuStart = cpuSeconds(), previousTime = begin, previousCpu = cpuStart;
    std::deque<std::string> events, samples;
    uint64_t omittedEvents = 0, omittedSamples = 0;
    double lastSlow = 0;
};
void Monitor::start(const std::string &renderEnvironment) {
    auto s = std::make_shared<Session>();
    s->environmentText = environment() + renderEnvironment;
    {
        std::lock_guard lock(control);
        last = s;
        current.store(s);
    }
    event("监测开始");
    sample();
}
void Monitor::stop() {
    auto s = current.exchange(nullptr);
    if (!s)
        return;
    std::lock_guard l(s->mutex);
    s->end = now();
    s->ended = timestamp();
    s->events.push_back(s->ended + " 监测停止");
}
bool Monitor::hasData() {
    std::lock_guard l(control);
    return last != nullptr;
}
void Monitor::add(MetricId id, double ms) {
    auto s = current.load();
    if (!s)
        return;
    s->metrics[id].add(ms);
    if (id == FrameGap && ms > 50) {
        std::lock_guard l(s->mutex);
        if (now() - s->lastSlow >= 1) {
            s->lastSlow = now();
            if (s->events.size() >= 1024) {
                s->events.pop_front();
                ++s->omittedEvents;
            }
            s->events.push_back(timestamp() + " 活动帧回调间隔超过 50ms: " + std::to_string(ms));
        }
    }
}
void Monitor::event(const std::string &event) {
    auto s = current.load();
    if (!s)
        return;
    std::lock_guard l(s->mutex);
    if (s->events.size() >= 1024) {
        s->events.pop_front();
        ++s->omittedEvents;
    }
    s->events.push_back(timestamp() + " " + event.substr(0, 512));
}
void Monitor::sample() {
    auto s = current.load();
    if (!s)
        return;
    PROCESS_MEMORY_COUNTERS_EX mem{sizeof(mem)};
    GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&mem, sizeof(mem));
    DWORD handles = 0;
    GetProcessHandleCount(GetCurrentProcess(), &handles);
    SYSTEM_INFO sys;
    GetSystemInfo(&sys);
    double t = now(), cpu = cpuSeconds();
    std::lock_guard l(s->mutex);
    std::ostringstream row;
    double elapsed = t - s->previousTime;
    row << std::fixed << std::setprecision(3) << timestamp() << ", " << t - s->begin << ", "
        << (elapsed > 0
                ? (cpu - s->previousCpu) / elapsed / std::max<DWORD>(1, sys.dwNumberOfProcessors) * 100
                : 0)
        << ", " << mem.WorkingSetSize / 1048576. << ", " << mem.PrivateUsage / 1048576. << ", " << handles
        << ", " << GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) << ", "
        << GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
    s->previousTime = t;
    s->previousCpu = cpu;
    if (s->samples.size() >= 720) {
        s->samples.pop_front();
        ++s->omittedSamples;
    }
    s->samples.push_back(row.str());
}
std::string Monitor::report() {
    std::shared_ptr<Session> s;
    {
        std::lock_guard l(control);
        s = last;
    }
    if (!s)
        throw std::runtime_error("no monitoring session");
    std::ostringstream out;
    std::lock_guard l(s->mutex);
    out << "daxian 运行诊断报告\n\n[监测信息]\n开始（本地时间）: " << s->begun
        << "\n结束: " << (s->ended.empty() ? "正在监测，以下为当前快照" : s->ended)
        << "\n导出时间: " << timestamp() << "\n持续秒数: " << (s->end ? s->end : now()) - s->begin
        << "\n\n[系统与渲染环境]\n"
        << s->environmentText;
    out << "\n[性能汇总]\n阶段, 样本数, 平均毫秒, P95毫秒桶, P99毫秒桶, 最大毫秒\n";
    const char *names[] = {"渲染提交", "活动帧回调间隔", "后台歌词测量", "音乐监测/控制", "通知读取",
                           "音频采样", "音乐状态应用",   "容器布局",     "诊断IPC读写", "辅助功能读取"};
    static_assert(std::size(names) == MetricCount, "Every performance metric needs a report name");
    for (int j = 0; j < MetricCount; j++) {
        auto &m = s->metrics[j];
        uint64_t count = m.count, seen = 0;
        int p95 = 0, p99 = 0;
        for (int i = 0; i < 1002; i++) {
            seen += m.bins[i];
            if (seen < count * .95)
                p95 = i + 1;
            if (seen < count * .99)
                p99 = i + 1;
        }
        out << names[j] << ", " << count << ", " << (count ? m.total / 1000. / count : 0) << ", " << p95
            << ", " << p99 << ", " << m.max / 1000. << '\n';
    }
    out << "\n[资源采样，每5秒；CPU为全机逻辑处理器归一化占用率]\n时间, 经过秒数, CPU百分比, 工作集MB, "
           "私有MB, 句柄数, GDI对象, USER对象\n";
    for (auto &r : s->samples)
        out << r << '\n';
    out << "\n[操作与异常事件]\n";
    for (auto &e : s->events)
        out << e << '\n';
    out << "\n[数据范围与说明]"
           "\n不记录聊天正文、歌曲名称、账号或凭据。事件最多保留1024条，资源采样最多720条；旧记录滚动淘汰，统"
           "计直方图覆盖本次完整监测。\n已滚动淘汰事件: "
        << s->omittedEvents << "，采样: " << s->omittedSamples
        << "。\n帧间隔为应用活动回调，不等于屏幕呈现FPS；渲染提交耗时不包含GPU完整执行时间。未直接采样GPU占用"
           "率。大于1001ms的P95/"
           "P99统一进入末桶，最大值保留原值。\n没有异常记录不等于所有功能或硬件都无故障。\n";
    return "\xEF\xBB\xBF" + out.str();
}
fs::path Monitor::exportTo(const fs::path &directory) {
    auto text = report();
    fs::create_directories(directory);
    auto name = L"daxian日志_" + wide(timestamp(true));
    for (int suffix = 0; suffix < 10000; suffix++) {
        auto path = directory / (name + (suffix ? L"_" + std::to_wstring(suffix) : L"") + L".txt");
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_FILE_EXISTS)
                continue;
            throw std::runtime_error("report directory is not writable");
        }
        DWORD written = 0;
        BOOL ok = WriteFile(file, text.data(), (DWORD)text.size(), &written, nullptr);
        CloseHandle(file);
        if (!ok || written != text.size()) {
            DeleteFileW(path.c_str());
            throw std::runtime_error("report write failed");
        }
        return path;
    }
    throw std::runtime_error("report name collision limit");
}
} // namespace wi


