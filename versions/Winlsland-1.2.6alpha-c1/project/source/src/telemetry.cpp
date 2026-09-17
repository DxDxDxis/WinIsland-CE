#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include "core.h"
#include <iphlpapi.h>
#include <icmpapi.h>
#include <evntrace.h>
#include <evntcons.h>

namespace wi {
static bool parseTarget(std::wstring target, SOCKADDR_INET &address) {
    address = {};
    if (InetPtonW(AF_INET, target.c_str(), &address.Ipv4.sin_addr) == 1) {
        address.si_family = AF_INET;
        auto n = address.Ipv4.sin_addr.S_un.S_addr;
        return n && n != INADDR_BROADCAST && (ntohl(n) >> 28) != 0xe;
    }
    auto percent = target.find(L'%');
    ULONG scope = 0;
    if (percent != target.npos) {
        auto suffix = target.substr(percent + 1);
        if (suffix.empty() || suffix.size() > 9 || suffix.find_first_not_of(L"0123456789") != suffix.npos)
            return false;
        scope = wcstoul(suffix.c_str(), nullptr, 10);
        target.resize(percent);
    }
    if (InetPtonW(AF_INET6, target.c_str(), &address.Ipv6.sin6_addr) != 1 ||
        IN6_IS_ADDR_UNSPECIFIED(&address.Ipv6.sin6_addr) || IN6_IS_ADDR_MULTICAST(&address.Ipv6.sin6_addr))
        return false;
    address.si_family = AF_INET6;
    address.Ipv6.sin6_scope_id = scope;
    return !IN6_IS_ADDR_LINKLOCAL(&address.Ipv6.sin6_addr) || scope;
}
bool validPingTarget(const std::wstring &target) {
    SOCKADDR_INET address{};
    return target.empty() || parseTarget(target, address);
}
static std::vector<SOCKADDR_INET> gateways() {
    PMIB_IPFORWARD_TABLE2 table = nullptr;
    std::map<ADDRESS_FAMILY, std::pair<ULONG64, SOCKADDR_INET>> best;
    if (GetIpForwardTable2(AF_UNSPEC, &table) != NO_ERROR)
        return {};
    for (ULONG i = 0; i < table->NumEntries; ++i) {
        auto &row = table->Table[i];
        if (row.DestinationPrefix.PrefixLength != 0 || row.Loopback)
            continue;
        MIB_IF_ROW2 nic{};
        nic.InterfaceLuid = row.InterfaceLuid;
        MIB_IPINTERFACE_ROW ip{};
        ip.Family = row.NextHop.si_family;
        ip.InterfaceLuid = row.InterfaceLuid;
        if (GetIfEntry2(&nic) || nic.OperStatus != IfOperStatusUp || GetIpInterfaceEntry(&ip))
            continue;
        auto address = row.NextHop;
        if (address.si_family == AF_INET && !address.Ipv4.sin_addr.S_un.S_addr)
            continue;
        if (address.si_family == AF_INET6) {
            if (IN6_IS_ADDR_UNSPECIFIED(&address.Ipv6.sin6_addr))
                continue;
            if (IN6_IS_ADDR_LINKLOCAL(&address.Ipv6.sin6_addr))
                address.Ipv6.sin6_scope_id = row.InterfaceIndex;
        }
        ULONG64 metric = (ULONG64)row.Metric + ip.Metric;
        if (!best.contains(address.si_family) || metric < best[address.si_family].first)
            best[address.si_family] = {metric, address};
    }
    FreeMibTable(table);
    std::vector<SOCKADDR_INET> result;
    for (auto &[family, item] : best)
        result.push_back(item.second);
    return result;
}
static std::wstring targetText(const SOCKADDR_INET &address) {
    wchar_t text[64]{};
    if (address.si_family == AF_INET)
        InetNtopW(AF_INET, (void *)&address.Ipv4.sin_addr, text, 64);
    else
        InetNtopW(AF_INET6, (void *)&address.Ipv6.sin6_addr, text, 64);
    return std::wstring(text) + (address.si_family == AF_INET6 && address.Ipv6.sin6_scope_id
                                     ? L"%" + std::to_wstring(address.Ipv6.sin6_scope_id)
                                     : L"");
}
static DWORD probe(const SOCKADDR_INET &destination, int &milliseconds,
                   const std::function<bool()> &cancelled) {
    bool v6 = destination.si_family == AF_INET6;
    HANDLE icmp = v6 ? Icmp6CreateFile() : IcmpCreateFile();
    if (icmp == INVALID_HANDLE_VALUE)
        return GetLastError();
    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!event) {
        DWORD e = GetLastError();
        IcmpCloseHandle(icmp);
        return e;
    }
    char payload[] = "WinIsland";
    std::vector<BYTE> reply(256);
    DWORD status = IP_REQ_TIMED_OUT;
    SOCKADDR_INET target = destination, source{};
    source.si_family = destination.si_family;
    MIB_IPFORWARD_ROW2 route{};
    DWORD routed = GetBestRoute2(nullptr, 0, nullptr, &target, 0, &route, &source);
    if (routed)
        status = ERROR_NETWORK_UNREACHABLE;
    else {
        DWORD count =
            v6 ? Icmp6SendEcho2(icmp, event, nullptr, nullptr, &source.Ipv6, &target.Ipv6, payload,
                                sizeof(payload), nullptr, reply.data(), (DWORD)reply.size(), 650)
               : IcmpSendEcho2(icmp, event, nullptr, nullptr, target.Ipv4.sin_addr.S_un.S_addr, payload,
                               sizeof(payload), nullptr, reply.data(), (DWORD)reply.size(), 650);
        status = count ? NO_ERROR : GetLastError();
        if (!count && status == ERROR_IO_PENDING) {
            DWORD wait = WAIT_TIMEOUT;
            double until = now() + 1;
            while (!cancelled() && now() < until && (wait = WaitForSingleObject(event, 50)) == WAIT_TIMEOUT) {
            }
            if (wait == WAIT_OBJECT_0) {
                count = v6 ? Icmp6ParseReplies(reply.data(), (DWORD)reply.size())
                           : IcmpParseReplies(reply.data(), (DWORD)reply.size());
                status = count ? NO_ERROR : GetLastError();
            } else
                status = cancelled() ? ERROR_CANCELLED : IP_REQ_TIMED_OUT;
        }
        if (count) {
            status =
                v6 ? ((ICMPV6_ECHO_REPLY *)reply.data())->Status : ((ICMP_ECHO_REPLY *)reply.data())->Status;
            if (status == IP_SUCCESS)
                milliseconds = (int)(v6 ? ((ICMPV6_ECHO_REPLY *)reply.data())->RoundTripTime
                                        : ((ICMP_ECHO_REPLY *)reply.data())->RoundTripTime);
        }
    }
    // Close before freeing overlapped buffers, including cancellation paths.
    IcmpCloseHandle(icmp);
    CloseHandle(event);
    return status;
}
static const GUID DxgiProvider{0xca11c036, 0x0102, 0x4a2d, {0xa6, 0xad, 0xf0, 0x3c, 0xfe, 0xd5, 0xd3, 0xc9}};
// Counts successful, non-test DXGI presents for the foreground process. This is
// app submission FPS, not monitor Hz or a claim about scanout/frame generation.
class PresentCounter {
    TRACEHANDLE session = 0, consumer = INVALID_PROCESSTRACE_HANDLE;
    std::thread reader;
    std::vector<BYTE> storage;
    std::wstring name = L"WinIsland.FPS." + std::to_wstring(GetCurrentProcessId());
    std::mutex mutex;
    struct Start {
        uint64_t chain;
        bool test;
    };
    struct Count {
        uint64_t frames = 0;
        LONGLONG first = 0, last = 0;
    };
    std::map<DWORD, Start> pending;
    std::map<uint64_t, Count> frames;
    DWORD process = 0;
    std::atomic_bool lost = false;
    static void WINAPI event(EVENT_RECORD *event) {
        auto &self = *(PresentCounter *)event->UserContext;
        if (event->EventHeader.ProcessId != self.process || event->EventHeader.EventDescriptor.Version != 0)
            return;
        auto id = event->EventHeader.EventDescriptor.Id;
        auto bytes = (BYTE *)event->UserData;
        std::lock_guard lock(self.mutex);
        if (id == 42) {
            size_t pointerSize = (event->EventHeader.Flags & EVENT_HEADER_FLAG_32_BIT_HEADER) ? 4 : 8;
            if (event->UserDataLength < pointerSize + 8)
                return;
            uint64_t chain = 0;
            DWORD flags = 0;
            memcpy(&chain, bytes, pointerSize);
            memcpy(&flags, bytes + pointerSize, 4);
            if (self.pending.size() < 128)
                self.pending[event->EventHeader.ThreadId] = {chain, (flags & 1) != 0};
        } else if (id == 43 && event->UserDataLength >= 4) {
            auto found = self.pending.find(event->EventHeader.ThreadId);
            if (found == self.pending.end())
                return;
            DWORD result;
            memcpy(&result, bytes, 4);
            if (result == S_OK && !found->second.test && self.frames.size() < 64) {
                auto &count = self.frames[found->second.chain];
                ++count.frames;
                if (!count.first)
                    count.first = event->EventHeader.TimeStamp.QuadPart;
                count.last = event->EventHeader.TimeStamp.QuadPart;
            }
            self.pending.erase(found);
        }
    }
    static ULONG WINAPI buffer(EVENT_TRACE_LOGFILEW *log) {
        if (log->EventsLost)
            ((PresentCounter *)log->Context)->lost = true;
        return TRUE;
    }

  public:
    DWORD error = ERROR_SUCCESS;
    explicit PresentCounter(DWORD pid) : process(pid) {
        storage.resize(sizeof(EVENT_TRACE_PROPERTIES) + (name.size() + 1) * sizeof(wchar_t));
        auto p = (EVENT_TRACE_PROPERTIES *)storage.data();
        p->Wnode.BufferSize = (ULONG)storage.size();
        p->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
        p->Wnode.ClientContext = 1;
        p->LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
        p->BufferSize = 16;
        p->MinimumBuffers = 2;
        p->MaximumBuffers = 8;
        p->FlushTimer = 1;
        p->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
        error = StartTraceW(&session, name.c_str(), p);
        if (error != ERROR_SUCCESS) {
            session = 0;
            return;
        }
        // Provider-side PID and event filters avoid consuming every application's
        // graphics stream. Failure is reported; no unrestricted fallback trace.
        ULONG pidFilter = pid;
        struct EventIds {
            BOOLEAN filterIn;
            UCHAR reserved;
            USHORT count;
            USHORT ids[2];
        } ids{TRUE, 0, 2, {42, 43}};
        EVENT_FILTER_DESCRIPTOR filters[2]{{(ULONGLONG)&pidFilter, sizeof(pidFilter), EVENT_FILTER_TYPE_PID},
                                           {(ULONGLONG)&ids, sizeof(ids), EVENT_FILTER_TYPE_EVENT_ID}};
        ENABLE_TRACE_PARAMETERS parameters{};
        parameters.Version = ENABLE_TRACE_PARAMETERS_VERSION_2;
        parameters.EnableFilterDesc = filters;
        parameters.FilterDescCount = 2;
        error = EnableTraceEx2(session, &DxgiProvider, EVENT_CONTROL_CODE_ENABLE_PROVIDER,
                               TRACE_LEVEL_VERBOSE, 2, 0, 0, &parameters);
        if (error != ERROR_SUCCESS)
            return;
        EVENT_TRACE_LOGFILEW log{};
        log.LoggerName = name.data();
        log.ProcessTraceMode =
            PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD | PROCESS_TRACE_MODE_RAW_TIMESTAMP;
        log.EventRecordCallback = event;
        log.BufferCallback = buffer;
        log.Context = this;
        consumer = OpenTraceW(&log);
        if (consumer == INVALID_PROCESSTRACE_HANDLE) {
            error = GetLastError();
            return;
        }
        reader = std::thread([this] { ProcessTrace(&consumer, 1, nullptr, nullptr); });
    }
    ~PresentCounter() {
        if (session)
            ControlTraceW(session, name.c_str(), (EVENT_TRACE_PROPERTIES *)storage.data(),
                          EVENT_TRACE_CONTROL_STOP);
        if (consumer != INVALID_PROCESSTRACE_HANDLE)
            CloseTrace(consumer);
        if (reader.joinable())
            reader.join();
    }
    int sample() {
        std::lock_guard lock(mutex);
        Count best;
        for (auto &[chain, count] : frames)
            if (count.frames > best.frames)
                best = count;
        frames.clear();
        if (lost.exchange(false) || best.frames < 3 || best.last <= best.first)
            return -1;
        LARGE_INTEGER frequency;
        QueryPerformanceFrequency(&frequency);
        double value = (best.frames - 1) * (double)frequency.QuadPart / (best.last - best.first);
        return std::isfinite(value) && value >= 1 && value <= 2000 ? (int)std::lround(value) : -1;
    }
};
struct Telemetry::Impl {
    std::mutex mutex;
    std::condition_variable cv;
    bool stopping = false, fps = false, ping = false;
    uint64_t generation = 0;
    std::wstring target;
    TelemetryData data;
    std::thread worker;
    Impl() : worker([this] { run(); }) {}
    ~Impl() {
        {
            std::lock_guard lock(mutex);
            stopping = true;
        }
        cv.notify_all();
        worker.join();
    }
    void run() {
        std::unique_ptr<PresentCounter> counter;
        DWORD tracked = 0;
        uint64_t seen = 0;
        double nextPing = 0;
        DWORD error = 0;
        while (true) {
            bool wantFps, wantPing;
            std::wstring destination;
            uint64_t version;
            {
                std::unique_lock lock(mutex);
                if (!fps && !ping) {
                    lock.unlock();
                    counter.reset();
                    tracked = 0;
                    lock.lock();
                    cv.wait(lock, [&] { return stopping || fps || ping; });
                }
                if (stopping)
                    break;
                wantFps = fps;
                wantPing = ping;
                destination = target;
                version = generation;
            }
            TelemetryData current;
            if (version != seen) {
                nextPing = 0;
                seen = version;
            }
            if (wantFps) {
                HWND front = GetForegroundWindow();
                DWORD pid = 0;
                if (front && !IsIconic(front))
                    GetWindowThreadProcessId(front, &pid);
                if (pid == GetCurrentProcessId() || front == GetShellWindow() || front == GetDesktopWindow())
                    pid = 0;
                current.foreground = pid;
                if (pid != tracked) {
                    counter.reset();
                    tracked = pid;
                    if (pid) {
                        counter = std::make_unique<PresentCounter>(pid);
                        error = counter->error;
                    }
                }
                current.fps = counter && error == ERROR_SUCCESS ? counter->sample() : -1;
                current.fpsStatus = !pid                           ? L"无前台应用"
                                    : error == ERROR_ACCESS_DENIED ? L"系统未允许图形事件采集"
                                    : error                        ? L"图形事件不可用"
                                    : current.fps < 0              ? L"未获得 DXGI 帧数据"
                                                                   : L"前台 DXGI 提交帧率（非屏幕刷新率）";
            } else {
                counter.reset();
                tracked = 0;
            }
            if (wantPing) {
                if (now() >= nextPing) {
                    nextPing = now() + 3;
                    std::vector<SOCKADDR_INET> targets;
                    if (destination.empty())
                        targets = gateways();
                    else {
                        SOCKADDR_INET ip{};
                        if (parseTarget(destination, ip))
                            targets.push_back(ip);
                    }
                    current.pingTarget = targets.empty() ? L"无可用默认网关" : targetText(targets.front());
                    DWORD status = ERROR_NETWORK_UNREACHABLE;
                    auto cancelled = [&] {
                        std::lock_guard lock(mutex);
                        return stopping || generation != version;
                    };
                    // At most two real ICMP probes per 3-second sample. In gateway
                    // mode prefer IPv4, then IPv6; never substitute HTTP timings.
                    for (int attempt = 0; attempt < 2 && !targets.empty() && !cancelled(); ++attempt) {
                        auto &target = targets[std::min<size_t>(attempt, targets.size() - 1)];
                        current.pingTarget = targetText(target);
                        status = probe(target, current.ping, cancelled);
                        if (status == IP_SUCCESS || status == ERROR_ACCESS_DENIED ||
                            status == ERROR_CANCELLED)
                            break;
                    }
                    current.pingStatus = status == IP_SUCCESS ? L"ICMP 往返延迟"
                                         : status == IP_REQ_TIMED_OUT || status == ERROR_TIMEOUT ? L"超时"
                                         : status == ERROR_NETWORK_UNREACHABLE ? L"网络断开或无可用路由"
                                         : status == ERROR_ACCESS_DENIED       ? L"系统未允许 ICMP 探测"
                                                                               : L"目标不可达或 ICMP 不可用";
                    if (status != IP_SUCCESS)
                        current.ping = -1;
                } else {
                    std::lock_guard lock(mutex);
                    current.ping = data.ping;
                    current.pingStatus = data.pingStatus;
                    current.pingTarget = data.pingTarget;
                }
            }
            std::unique_lock lock(mutex);
            if (version == generation)
                data = current;
            cv.wait_for(lock, std::chrono::seconds(1), [&] { return stopping || generation != version; });
            if (stopping)
                break;
        }
    }
};
Telemetry::Telemetry() : impl(std::make_unique<Impl>()) {}
Telemetry::~Telemetry() = default;
void Telemetry::configure(bool fps, bool ping, const std::wstring &target) {
    std::lock_guard lock(impl->mutex);
    if (impl->fps == fps && impl->ping == ping && impl->target == target)
        return;
    impl->fps = fps;
    impl->ping = ping;
    impl->target = target;
    ++impl->generation;
    impl->data = {};
    impl->cv.notify_one();
}
TelemetryData Telemetry::snapshot() {
    std::lock_guard lock(impl->mutex);
    return impl->data;
}
} // namespace wi

