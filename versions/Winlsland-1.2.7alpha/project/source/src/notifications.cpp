#include "core.h"
namespace wi {
void traceNotice(const Notice &n, const char *stage, const std::string &detail) {
    if (!monitor.active()) return;
    monitor.event("通知链路；id=" + n.correlation + ";stage=" + stage +
                  ";source=" + n.origin + ";app=" + n.application +
                  ";data=" + (n.testSource ? "test" : "real") + (detail.empty() ? "" : ";" + detail));
}
static uint64_t fileTime() {
    FILETIME f;
    GetSystemTimeAsFileTime(&f);
    return ((uint64_t)f.dwHighDateTime << 32) | f.dwLowDateTime;
}

bool parseToast(const std::string &raw, Notice &notice) {
    if (raw.empty() || raw.size() > 1024 * 1024)
        return false;
    std::wstring xml;
    if (raw.size() > 1 && ((BYTE)raw[0] == 255 || raw[1] == 0)) {
        size_t start = (BYTE)raw[0] == 255 ? 2 : 0;
        for (size_t i = start; i + 1 < raw.size(); i += 2)
            xml.push_back((BYTE)raw[i] | ((BYTE)raw[i + 1] << 8));
    } else if (raw.size() > 1 && ((BYTE)raw[0] == 254 || raw[0] == 0)) {
        size_t start = (BYTE)raw[0] == 254 ? 2 : 0;
        for (size_t i = start; i + 1 < raw.size(); i += 2)
            xml.push_back(((BYTE)raw[i] << 8) | (BYTE)raw[i + 1]);
    } else
        xml = wide(raw);
    while (!xml.empty() && (xml.back() == 0 || xml.back() == 0xFEFF))
        xml.pop_back();
    if (!xml.empty() && xml.front() == 0xFEFF)
        xml.erase(0, 1);
    // Feed UTF-16 with a BOM. XmlLite then handles both the declaration and Unicode text.
    xml.insert(xml.begin(), 0xFEFF);
    ComPtr<IStream> stream;
    stream.Attach(SHCreateMemStream((BYTE *)xml.data(), (UINT)xml.size() * 2));
    ComPtr<IXmlReader> r;
    if (!stream || FAILED(CreateXmlReader(__uuidof(IXmlReader), &r, nullptr)))
        return false;
    r->SetProperty(XmlReaderProperty_DtdProcessing, DtdProcessing_Prohibit);
    ComPtr<IXmlReaderInput> input;
    if (FAILED(
            CreateXmlReaderInputWithEncodingName(stream.Get(), nullptr, L"utf-16", FALSE, nullptr, &input)))
        return false;
    r->SetInput(input.Get());
    XmlNodeType type;
    bool toast = false, visual = false, binding = false, inText = false, generic = false,
         selectedGeneric = false;
    std::vector<std::wstring> lines, chosen;
    std::wstring value;
    UINT textDepth = 0;
    HRESULT hr;
    while ((hr = r->Read(&type)) == S_OK) {
        const wchar_t *p = nullptr;
        UINT n = 0, depth = 0;
        r->GetLocalName(&p, &n);
        r->GetDepth(&depth);
        std::wstring name(p ? p : L"", n);
        if (type == XmlNodeType_Element) {
            if (depth == 0) {
                if (name != L"toast")
                    return false;
                toast = true;
            }
            if (depth == 1 && name == L"visual")
                visual = true;
            if (visual && depth == 2 && name == L"binding") {
                binding = true;
                lines.clear();
                generic = false;
                if (r->MoveToAttributeByName(L"template", nullptr) == S_OK) {
                    r->GetValue(&p, &n);
                    generic = std::wstring(p, n) == L"ToastGeneric";
                    r->MoveToElement();
                }
            }
            if (binding && name == L"text") {
                inText = !r->IsEmptyElement();
                value.clear();
                textDepth = depth;
                if (r->MoveToAttributeByName(L"placement", nullptr) == S_OK) {
                    r->GetValue(&p, &n);
                    if (std::wstring(p, n) == L"attribution")
                        inText = false;
                    r->MoveToElement();
                }
            }
        } else if (type == XmlNodeType_Text || type == XmlNodeType_CDATA || type == XmlNodeType_Whitespace) {
            if (inText) {
                r->GetValue(&p, &n);
                if (value.size() < 12000)
                    value.append(p, std::min<size_t>(n, 12000 - value.size()));
            }
        } else if (type == XmlNodeType_EndElement) {
            // XmlLite reports the end tag one level deeper than its matching start tag.
            if (depth)
                --depth;
            if (name == L"text" && depth == textDepth && inText) {
                size_t first = value.find_first_not_of(L" \r\n\t"), last = value.find_last_not_of(L" \r\n\t");
                if (first != value.npos)
                    lines.push_back(value.substr(first, last - first + 1));
                inText = false;
            }
            if (name == L"binding" && depth == 2) {
                if (!selectedGeneric && (chosen.empty() || generic)) {
                    chosen = lines;
                    selectedGeneric = generic;
                }
                binding = false;
            }
            if (name == L"visual" && depth == 1)
                visual = false;
        }
    }
    if (FAILED(hr) || !toast || chosen.empty())
        return false;
    notice.title = chosen.front();
    notice.body.clear();
    for (size_t i = 1; i < chosen.size(); i++) {
        if (i > 1)
            notice.body += L"\n";
        notice.body += chosen[i];
    }
    return true;
}
struct SQLite {
    HMODULE lib = nullptr;
    void *db = nullptr;
    int(__cdecl *open)(const char *, void **, int, const char *);
    int(__cdecl *close)(void *);
    int(__cdecl *prepare)(void *, const char *, int, void **, const char **);
    int(__cdecl *step)(void *);
    int(__cdecl *finalize)(void *);
    long long(__cdecl *integer)(void *, int);
    int(__cdecl *bytes)(void *, int);
    const void *(__cdecl *blob)(void *, int);
    const unsigned char *(__cdecl *text)(void *, int);
    int(__cdecl *timeout)(void *, int);
    SQLite() {
        lib = LoadLibraryExW(L"winsqlite3.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!lib)
            throw std::runtime_error("Windows SQLite missing");
#define LOAD(field, name)                                                                                    \
    field = (decltype(field))GetProcAddress(lib, name);                                                      \
    if (!field)                                                                                              \
    throw std::runtime_error("SQLite export missing")
        LOAD(open, "sqlite3_open_v2");
        LOAD(close, "sqlite3_close");
        LOAD(prepare, "sqlite3_prepare_v2");
        LOAD(step, "sqlite3_step");
        LOAD(finalize, "sqlite3_finalize");
        LOAD(integer, "sqlite3_column_int64");
        LOAD(bytes, "sqlite3_column_bytes");
        LOAD(blob, "sqlite3_column_blob");
        LOAD(text, "sqlite3_column_text");
        LOAD(timeout, "sqlite3_busy_timeout");
#undef LOAD
    }
    ~SQLite() {
        if (db)
            close(db);
        if (lib)
            FreeLibrary(lib);
    }
    long long version() {
        void *s = nullptr;
        if (prepare(db, "PRAGMA data_version", -1, &s, nullptr))
            throw std::runtime_error("data_version");
        long long v = step(s) == 100 ? integer(s, 0) : -1;
        finalize(s);
        return v;
    }
    bool handlers() {
        void *s = nullptr;
        if (prepare(db, "SELECT 1 FROM sqlite_master WHERE name='NotificationHandler'", -1, &s, nullptr)) return false;
        bool result = step(s) == 100;
        finalize(s);
        return result;
    }
};
Notices::Notices(HWND h, const fs::path &p)
    : owner(h),
      path(p.empty() ? dataDir().parent_path() / L"Microsoft/Windows/Notifications/wpndatabase.db" : p),
      started(fileTime()) {
    stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    thread = std::thread([this] { run(); });
}
Notices::~Notices() {
    stop = true;
    SetEvent(stopEvent);
    thread.join();
    CloseHandle(stopEvent);
}
void Notices::pause(bool p) {
    std::lock_guard l(mu);
    paused = p;
    ++generation;
    pending.clear();
}
std::deque<Notice> Notices::take() {
    std::lock_guard l(mu);
    std::deque<Notice> r;
    r.swap(pending);
    return r;
}
void Notices::run() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    uint64_t since = started, epoch = 0;
    double opened = 0;
    HANDLE change = FindFirstChangeNotificationW(path.parent_path().c_str(), FALSE,
                                                 FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE |
                                                     FILE_NOTIFY_CHANGE_FILE_NAME);
    bool ready = false, healthy = false, hasHandlers = false;
    const bool testing = path.filename() == L"notifications.db";
    long long version = -1;
    std::set<std::string> seen;
    std::unique_ptr<SQLite> sql;
    while (!stop) {
        if (!paused)
            try {
                Measure work(NoticeWork);
                {
                    std::lock_guard l(mu);
                    if (epoch != generation) {
                        epoch = generation;
                        seen.clear();
                        ready = false;
                        since = fileTime();
                        version = -1;
                    }
                }
                if (sql && now() - opened > 15) {
                    sql.reset();
                    version = -1;
                }
                if (!sql) {
                    opened = now();
                    sql = std::make_unique<SQLite>();
                    if (sql->open(utf8(path.wstring()).c_str(), &sql->db, 1, nullptr) != 0)
                        throw std::runtime_error("read-only db open");
                    sql->timeout(sql->db, 150);
                    hasHandlers = sql->handlers();
                }
                auto v = sql->version();
                if (v != version) {
                    void *stmt = nullptr;
                    if (sql->prepare(sql->db, hasHandlers ?
                                     "SELECT n.Id,n.HandlerId,n.ArrivalTime,n.Payload,h.PrimaryId FROM Notification n "
                                     "LEFT JOIN NotificationHandler h ON h.RecordId=n.HandlerId "
                                     "WHERE n.Type='toast' ORDER BY n.[Order] ASC" :
                                     "SELECT Id,HandlerId,ArrivalTime,Payload,'' FROM Notification WHERE "
                                     "Type='toast' ORDER BY [Order] ASC",
                                     -1, &stmt, nullptr))
                        throw std::runtime_error("Windows notification schema");
                    std::vector<Notice> added;
                    std::set<std::string> retained;
                    int rc;
                    while ((rc = sql->step(stmt)) == 100) {
                        Notice n;
                        n.id = sql->integer(stmt, 0);
                        n.handler = sql->integer(stmt, 1);
                        n.arrival = sql->integer(stmt, 2);
                        n.origin = "windows-toast-db";
                        n.testSource = testing;
                        auto appId = sql->text(stmt, 4);
                        n.application = appId && *appId ? (const char *)appId : testing ? "synthetic" : "unknown-handler";
                        int len = sql->bytes(stmt, 3);
                        if (len <= 0 || len > 1024 * 1024) {
                            n.correlation = sha256(n.key()).substr(0, 16);
                            traceNotice(n, "filtered", "reason=payload_size_invalid");
                            continue;
                        }
                        std::string payload((const char *)sql->blob(stmt, 3), len);
                        n.fingerprint = n.key() + ":" + sha256(payload);
                        n.correlation = sha256(n.fingerprint).substr(0, 16);
                        retained.insert(n.fingerprint);
                        if (seen.contains(n.fingerprint))
                            continue;
                        seen.insert(n.fingerprint);
                        traceNotice(n, "discovered");
                        if ((uint64_t)n.arrival < since) {
                            // Startup history is suppressed, including newly discovered
                            // historical rows after a database reconnect.
                            traceNotice(n, "filtered", "reason=historical_before_listener");
                        } else if (parseToast(payload, n)) {
                            traceNotice(n, "parsed", "format=toast_xml");
                            added.push_back(std::move(n));
                        } else traceNotice(n, "filtered", "reason=invalid_xml_or_no_visible_text");
                    }
                    sql->finalize(stmt);
                    if (rc != 101)
                        throw std::runtime_error("notification read");
                    if (seen.size() > 8192)
                        seen = std::move(retained);
                    ready = true;
                    version = v;
                    {
                        std::lock_guard l(mu);
                        if (epoch == generation && !paused) {
                            bool empty = pending.empty();
                            for (auto &n : added) {
                                traceNotice(n, "listener_queued");
                                pending.push_back(std::move(n));
                            }
                            if (empty && !pending.empty())
                                PostMessageW(owner, WM_NOTICE, 0, 0);
                        } else for (auto &n : added) traceNotice(n, "filtered", "reason=listener_paused_or_generation_changed");
                    }
                }
                if (!healthy) {
                    monitor.event("通知读取已就绪；backend=windows-toast-db;health=1;QQ自绘弹窗不属于此来源");
                    healthy = true;
                    PostMessageW(owner, WM_HEALTH, 1, 0);
                }
            } catch (const std::exception &e) {
                sql.reset();
                version = -1;
                if (healthy || !ready) {
                    monitor.event(std::string("通知读取不可用，恢复系统横幅；原因=") + e.what());
                    healthy = false;
                    PostMessageW(owner, WM_HEALTH, 0, 0);
                }
            }
        HANDLE waits[] = {stopEvent, change};
        if (WaitForMultipleObjects(change == INVALID_HANDLE_VALUE ? 1 : 2, waits, FALSE, 650) ==
            WAIT_OBJECT_0 + 1)
            FindNextChangeNotification(change);
    }
    if (change != INVALID_HANDLE_VALUE)
        FindCloseChangeNotification(change);
    sql.reset();
    CoUninitialize();
}
void ToastHelper::enable(bool yes) {
    if (yes && process && WaitForSingleObject(process, 0) == WAIT_TIMEOUT)
        return;
    if (!yes) {
        if (event)
            SetEvent(event);
        if (process) {
            CloseHandle(process);
            process = nullptr;
        }
        if (event) {
            CloseHandle(event);
            event = nullptr;
        }
        return;
    }
    enable(false);
    uint64_t token = GetTickCount64();
    std::wstring name = L"Local\\WinIsland.NativeStop." + std::to_wstring(GetCurrentProcessId()) + L"." +
                        std::to_wstring(token);
    event = CreateEventW(nullptr, TRUE, FALSE, name.c_str());
    FILETIME c, e, k, u;
    GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u);
    uint64_t ticks = ((uint64_t)c.dwHighDateTime << 32) | c.dwLowDateTime;
    std::wstring cmd = L"\"" + exePath().wstring() + L"\" --native-helper " +
                       std::to_wstring(GetCurrentProcessId()) + L" " + std::to_wstring(ticks) + L" " +
                       std::to_wstring(token);
    STARTUPINFOW si{sizeof(si)};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si,
                       &pi)) {
        process = pi.hProcess;
        CloseHandle(pi.hThread);
    }
}
struct Mask {
    DWORD pid;
    HRGN region = nullptr;
    bool cloak = false;
};
static std::map<HWND, Mask> masks;
static void mask(HWND h) {
    wchar_t cls[256], name[256];
    if (GetAncestor(h, GA_ROOT) != h)
        return;
    GetClassNameW(h, cls, 256);
    GetWindowTextW(h, name, 256);
    bool toast = (wcscmp(cls, L"Windows.UI.Core.CoreWindow") == 0 ||
                  wcscmp(cls, L"Windows.UI.Composition.DesktopWindowContentBridge") == 0) &&
                 (wcscmp(name, L"新通知") == 0 || wcscmp(name, L"新的通知") == 0 ||
                  _wcsicmp(name, L"New notification") == 0);
    toast |= wcscmp(cls, L"ToastWnd") == 0 || wcscmp(cls, L"ToastWindowClass") == 0 ||
             wcscmp(cls, L"Windows.UI.Notifications.ToastWindow") == 0;
    if (!toast)
        return;
    DWORD pid;
    GetWindowThreadProcessId(h, &pid);
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p)
        return;
    wchar_t path[32768]{};
    DWORD n = 32768;
    BOOL ok = QueryFullProcessImageNameW(p, 0, path, &n);
    CloseHandle(p);
    wchar_t win[MAX_PATH];
    GetWindowsDirectoryW(win, MAX_PATH);
    std::wstring file = fs::path(path).filename().wstring();
    if (!ok || _wcsnicmp(path, win, wcslen(win)) ||
        (file != L"ShellExperienceHost.exe" && file != L"ShellHost.exe" && file != L"explorer.exe"))
        return;
    if (masks.contains(h)) {
        int yes = 1;
        if (masks[h].cloak)
            DwmSetWindowAttribute(h, 13, &yes, 4);
        return;
    }
    Mask m{pid};
    int already = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(h, 14, &already, 4)) && (already & 1))
        return;
    int yes = 1;
    if (SUCCEEDED(DwmSetWindowAttribute(h, 13, &yes, 4)))
        m.cloak = true;
    else {
        HRGN original = CreateRectRgn(0, 0, 0, 0);
        int kind = GetWindowRgn(h, original);
        if (kind == NULLREGION) {
            DeleteObject(original);
            return;
        }
        if (!kind) {
            DeleteObject(original);
            original = nullptr;
        }
        m.region = original;
        HRGN empty = CreateRectRgn(0, 0, 0, 0);
        if (!SetWindowRgn(h, empty, TRUE)) {
            DeleteObject(empty);
            if (original)
                DeleteObject(original);
            return;
        }
    }
    masks[h] = m;
}
static void CALLBACK windowEvent(HWINEVENTHOOK, DWORD event, HWND h, LONG obj, LONG child, DWORD, DWORD) {
    if (h && !obj && !child && event != EVENT_OBJECT_DESTROY && event != EVENT_OBJECT_HIDE)
        mask(h);
}
int runToastHelper(DWORD pid, uint64_t ticks, uint64_t token) {
    HANDLE parent = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!parent)
        return 0;
    FILETIME c, e, k, u;
    GetProcessTimes(parent, &c, &e, &k, &u);
    if ((((uint64_t)c.dwHighDateTime << 32) | c.dwLowDateTime) != ticks) {
        CloseHandle(parent);
        return 0;
    }
    std::wstring name =
        L"Local\\WinIsland.NativeStop." + std::to_wstring(pid) + L"." + std::to_wstring(token);
    HANDLE end = OpenEventW(SYNCHRONIZE, FALSE, name.c_str());
    if (!end) {
        CloseHandle(parent);
        return 0;
    }
    auto hook = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_NAMECHANGE, nullptr, windowEvent, 0, 0,
                                WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    HANDLE events[] = {parent, end};
    bool done = false;
    while (!done) {
        DWORD w = MsgWaitForMultipleObjects(2, events, FALSE, 500, QS_ALLINPUT);
        if (w < WAIT_OBJECT_0 + 2)
            break;
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        EnumWindows(
            [](HWND h, LPARAM) -> BOOL {
                mask(h);
                return TRUE;
            },
            0);
        for (auto i = masks.begin(); i != masks.end();)
            if (!IsWindow(i->first)) {
                if (i->second.region)
                    DeleteObject(i->second.region);
                i = masks.erase(i);
            } else
                ++i;
    }
    if (hook)
        UnhookWinEvent(hook);
    for (auto &[h, m] : masks) {
        DWORD id;
        GetWindowThreadProcessId(h, &id);
        if (IsWindow(h) && id == m.pid) {
            if (m.cloak) {
                int no = 0;
                DwmSetWindowAttribute(h, 13, &no, 4);
            } else if (SetWindowRgn(h, m.region, TRUE))
                m.region = nullptr;
        }
        if (m.region)
            DeleteObject(m.region);
    }
    masks.clear();
    CloseHandle(end);
    CloseHandle(parent);
    return 0;
}
} // namespace wi


