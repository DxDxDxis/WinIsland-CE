#include "core.h"
#include <psapi.h>
#include <tlhelp32.h>
namespace wi {
bool ObservedLyricLine::accept(const std::wstring &song, const std::wstring &line,
                              bool visible, double stamp, double maxAge) {
    if (key != song || line.empty()) {
        key=song;text=line;changedAt=stamp;observed=visible&&!line.empty();wasVisible=visible;
        return observed;
    }
    bool changed = !text.empty() && text != line;
    if (wasVisible && !visible) observed=false;
    if (changed) {changedAt=stamp;observed=true;}
    text=line;wasVisible=visible;
    if (visible) {observed=true;return true;}
    return observed && stamp-changedAt <= std::clamp(maxAge,2.,15.);
}
Perf perf;
std::string utf8(const std::wstring &s) {
    if (s.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string r(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), r.data(), n, nullptr, nullptr);
    return r;
}
std::wstring wide(const std::string &s) {
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring r(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), r.data(), n);
    return r;
}
fs::path dataDir() {
    wchar_t p[MAX_PATH];
    SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, p);
    return fs::path(p) / L"WinIsland";
}
fs::path exePath() {
    wchar_t p[32768];
    GetModuleFileNameW(nullptr, p, 32768);
    return p;
}
std::string readFile(const fs::path &p, size_t limit) {
    std::error_code ec;
    auto n = fs::file_size(p, ec);
    if (ec || n > limit)
        return {};
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}
void writeAtomic(const fs::path &p, const std::string &s) {
    // A filename without a directory is valid (for example self-test.txt).
    // std::filesystem rejects create_directories("") with invalid argument.
    auto parent = p.parent_path();
    if (!parent.empty())
        fs::create_directories(parent);
    fs::path t = p;
    t += L".tmp";
    {
        std::ofstream f(t, std::ios::binary);
        f.write(s.data(), s.size());
        if (!f)
            throw std::runtime_error("file write");
    }
    if (!MoveFileExW(t.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("file replace");
}
std::string sha256(const std::string &s) {
    BCRYPT_ALG_HANDLE a = nullptr;
    BCryptOpenAlgorithmProvider(&a, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    std::array<UCHAR, 32> d{};
    BCryptHash(a, nullptr, 0, (PUCHAR)s.data(), (ULONG)s.size(), d.data(), 32);
    BCryptCloseAlgorithmProvider(a, 0);
    std::ostringstream o;
    for (auto b : d)
        o << std::uppercase << std::hex << std::setfill('0') << std::setw(2) << (int)b;
    return o.str();
}
void applyTimeline(Music &m, const TimelineSample &s, TimelineAnchor &a, double steady, long long utcTicks) {
    bool identical = s.updated == a.raw.updated && s.position == a.raw.position &&
                     s.start == a.raw.start && s.end == a.raw.end && s.maximum == a.raw.maximum;
    bool changedSong = !a.key.empty() && a.key != m.key();
    if (changedSong)
        a.awaitingTrackTimeline = identical && s.revision == a.raw.revision;
    if (!identical || s.revision != a.raw.revision)
        a.awaitingTrackTimeline = false;
    double duration = std::max(s.end, s.maximum) - s.start;
    double raw = s.position - s.start;
    double age = (utcTicks - s.updated) / 1e7;
    m.duration = std::isfinite(duration) && duration > 0 && duration < 86400 ? duration : 0;
    m.timelineRawPosition = raw;
    m.timelineUpdated = s.updated;
    m.timelineRevision = s.revision;
    m.timelineReason = a.awaitingTrackTimeline ? "awaiting_new_track_timeline"
                       : (!m.duration && raw <= 0) ? "player_did_not_report_timeline"
                       : s.updated <= 0 ? "timestamp_missing"
                       : !std::isfinite(raw) || raw < 0 || (m.duration && raw > m.duration + 1)
                           ? "invalid_position"
                       : age < -2 || age > 86400 ? "invalid_timeline_timestamp" : "valid";
    m.timeline = m.timelineReason == "valid";
    m.stamp = steady;
    // Reading the same SMTC object is not a fresh progress report. Bound the
    // monotonic projection, retaining a frozen confirmed cursor while paused.
    bool freshPosition = !a.key.empty() && !changedSong && s.position != a.raw.position && s.position > 0 &&
                         s.updated == a.raw.updated;
    if(!identical || changedSong)a.reportExpired=false;
    double validUntil = steady + TimelineExtrapolationSeconds - std::max(0., age);
    if (identical && a.valid && !changedSong)
        validUntil = a.playing ? a.validUntil : steady + TimelineExtrapolationSeconds;
    else if (freshPosition)
        validUntil = steady + TimelineExtrapolationSeconds;
    if (m.timeline && (a.reportExpired || (m.playing && steady >= validUntil) ||
        (!m.playing && a.playing && a.validUntil>0 && steady>=a.validUntil))) {
        m.timeline = false;
        m.timelineReason = "timeline_report_stale";
        a.reportExpired = true;
    }
    m.timelineExpires = m.timeline && m.playing ? validUntil : 0;
    if (m.timeline) {
        if (!changedSong && identical && a.valid) {
            // A pause/resume can arrive without a fresh timestamp. Rebase from
            // the previous playback clock, never add the paused wall-clock time.
            double time = a.validUntil > 0 ? std::min(steady, a.validUntil) : steady;
            m.position = a.position + (a.playing ? std::max(0., time - a.stamp) * a.rate : 0.);
        } else {
            m.position = raw + (m.playing && !freshPosition ? std::max(0., age) * m.rate : 0.);
        }
        m.position = std::max(0., m.position);
        if (m.duration)
            m.position = std::min(m.position, m.duration);
    } else
        m.position = 0;
    a.key = m.key();
    a.raw = s;
    a.position = m.position;
    a.stamp = steady;
    a.rate = m.rate;
    a.playing = m.playing;
    a.valid = m.timeline;
    a.validUntil = m.timelineExpires;
}
std::wstring lyricDisplay(const Music &m, int &index) {
    index = -1;
    if (!m.playerLyric.empty() && now() - m.playerLyricStamp < 2 && !m.timeline && m.lyricState != "disabled") {
        index = m.playerLyricIndex;
        return m.playerLyric;
    }
    if (!m.lyrics)
        return {};
    if (m.lyrics->lines.empty())
        return L"文本歌词 · " + m.lyrics->preview;
    if (!m.timeline || (m.timelineExpires > 0 && now() >= m.timelineExpires))
        return L"静态歌词 · " + (m.retainedLyric.empty() ? m.lyrics->preview : m.retainedLyric);
    const auto &lines = m.lyrics->lines;
    auto i = std::upper_bound(lines.begin(), lines.end(), m.progress(),
                              [](double p, const auto &line) { return p < line.first; });
    if (i == lines.begin())
        return {};
    index = (int)(i - lines.begin()) - 1;
    return m.lyrics->at(m.progress());
}
std::wstring xmlEscape(std::wstring s) {
    for (auto [a, b] : std::vector<std::pair<std::wstring, std::wstring>>{
             {L"&", L"&amp;"}, {L"<", L"&lt;"}, {L">", L"&gt;"}, {L"\"", L"&quot;"}}) {
        size_t p = 0;
        while ((p = s.find(a, p)) != s.npos) {
            s.replace(p, a.size(), b);
            p += b.size();
        }
    }
    return s;
}
std::map<std::wstring, std::wstring> xmlFields(const std::string &s) {
    std::map<std::wstring, std::wstring> m;
    ComPtr<IStream> stream;
    stream.Attach(SHCreateMemStream((BYTE *)s.data(), (UINT)s.size()));
    ComPtr<IXmlReader> r;
    if (FAILED(CreateXmlReader(__uuidof(IXmlReader), &r, nullptr)) || !stream)
        return m;
    r->SetProperty(XmlReaderProperty_DtdProcessing, DtdProcessing_Prohibit);
    r->SetInput(stream.Get());
    XmlNodeType type;
    std::wstring key;
    while (r->Read(&type) == S_OK) {
        const wchar_t *p = nullptr;
        UINT n = 0;
        if (type == XmlNodeType_Element) {
            r->GetLocalName(&p, &n);
            key.assign(p, n);
        } else if (type == XmlNodeType_Text || type == XmlNodeType_CDATA) {
            r->GetValue(&p, &n);
            m[key].append(p, n);
        }
    }
    return m;
}
void Settings::load(const fs::path &p) {
    try {
        auto m = xmlFields(readFile(p));
        auto number = [&](const wchar_t *key, double &value, double lo, double hi) {
            if (!m.contains(key)) return;
            try {
                size_t used = 0; double n = std::stod(m[key], &used);
                if (used == m[key].size() && std::isfinite(n) && n >= lo && n <= hi) value = n;
            } catch (...) {}
        };
        number(L"TopArcScale", topArcScale, .5, 1.5);
        number(L"IslandZoom", islandZoom, 1., 2.);
        number(L"WidthRatio", widthRatio, .75, 1.5);
        number(L"HeightRatio", heightRatio, .75, 1.5);
        number(L"DpiCorrection", dpiCorrection, .8, 1.25);
        if (m.contains(L"TopAttach")) topAttach = m[L"TopAttach"] != L"false";
        if (m[L"RadiusMode"] == L"0" || m[L"RadiusMode"] == L"1" || m[L"RadiusMode"] == L"2")
            radiusMode = std::stoi(m[L"RadiusMode"]);
        if (m[L"MonitorDevice"].size() < 64) monitorDevice = m[L"MonitorDevice"];
        if (m.contains(L"Resident"))
            resident = m[L"Resident"] == L"true";
        if (m.contains(L"HideNative"))
            hideNative = m[L"HideNative"] == L"true";
        if (m.contains(L"DwellSeconds")) {
            double n = std::stod(m[L"DwellSeconds"]);
            if (std::isfinite(n) && n >= .1 && n <= 3600)
                seconds = n;
        }
        if (m.contains(L"FrameRate")) {
            int n = std::stoi(m[L"FrameRate"]);
            if (n == 0 || (n >= 30 && n <= 240))
                fps = n;
        }
        if (m.contains(L"SongSource") &&
            (m[L"SongSource"] == L"0" || m[L"SongSource"] == L"1" || m[L"SongSource"] == L"2"))
            songSource = std::stoi(m[L"SongSource"]);
        if (m.contains(L"PlayerFilter")) {
            int n = std::stoi(m[L"PlayerFilter"]);
            if (n >= 0 && n <= 3) playerFilter = n;
        }
        if (m.contains(L"LyricSource") &&
            (m[L"LyricSource"] == L"0" || m[L"LyricSource"] == L"1" || m[L"LyricSource"] == L"2" ||
             m[L"LyricSource"] == L"3" || m[L"LyricSource"] == L"4" || m[L"LyricSource"] == L"5"))
            lyricSource = std::stoi(m[L"LyricSource"]);
        showFps = m[L"ShowFps"] == L"true";
        showPing = m[L"ShowPing"] == L"true";
        if (validLyricApi(m[L"LyricApi"]))
            lyricApi = m[L"LyricApi"];
        if (validPingTarget(m[L"PingTarget"]))
            pingTarget = m[L"PingTarget"];
    } catch (...) {
    }
}
void Settings::save(const fs::path &p) const {
    std::ostringstream s;
    s << "<?xml version=\"1.0\" encoding=\"utf-8\"?><WinIsland version=\"1.2.6alpha-c1\"><Resident>"
      << (resident ? "true" : "false") << "</Resident><HideNative>" << (hideNative ? "true" : "false")
      << "</HideNative><DwellSeconds>" << seconds << "</DwellSeconds><FrameRate>" << fps
      << "</FrameRate><SongSource>" << songSource << "</SongSource><PlayerFilter>" << playerFilter << "</PlayerFilter><LyricSource>" << lyricSource
      << "</LyricSource><ShowFps>" << (showFps ? "true" : "false") << "</ShowFps><ShowPing>"
      << (showPing ? "true" : "false") << "</ShowPing><PingTarget>" << utf8(xmlEscape(pingTarget))
      << "</PingTarget><LyricApi>" << utf8(xmlEscape(lyricApi)) << "</LyricApi>"
      << "<IslandZoom>" << islandZoom << "</IslandZoom><WidthRatio>" << widthRatio
      << "</WidthRatio><HeightRatio>" << heightRatio << "</HeightRatio><DpiCorrection>" << dpiCorrection
      << "</DpiCorrection><TopAttach>" << (topAttach ? "true" : "false")
      << "</TopAttach><TopArcScale>" << topArcScale << "</TopArcScale><RadiusMode>" << radiusMode << "</RadiusMode><MonitorDevice>"
      << utf8(xmlEscape(monitorDevice)) << "</MonitorDevice></WinIsland>";
    writeAtomic(p, s.str());
}
Jobs::Jobs()
    : thread([this] {
          CoInitializeEx(nullptr, COINIT_MULTITHREADED);
          for (;;) {
              std::function<void()> f;
              {
                  std::unique_lock l(mu);
                  cv.wait(l, [this] { return stop || !queue.empty(); });
                  if (stop && queue.empty())
                      break;
                  f = std::move(queue.front());
                  queue.pop_front();
              }
              try {
                  f();
              } catch (...) {
                  monitor.event("后台任务失败（文件/资源操作）；任务队列继续运行");
              }
          }
          CoUninitialize();
      }) {}
Jobs::~Jobs() {
    finish();
}
void Jobs::finish() {
    {
        std::lock_guard l(mu);
        stop = true;
    }
    cv.notify_one();
    if (thread.joinable())
        thread.join();
}
void Jobs::post(std::function<void()> f) {
    {
        std::lock_guard l(mu);
        if (!stop)
            queue.push_back(std::move(f));
    }
    cv.notify_one();
}
std::shared_ptr<Lyrics> Lyrics::parse(const std::string &raw) {
    if (raw.size() > 1024 * 1024)
        return {};
    std::wstring text;
    if (raw.size() > 2 && ((BYTE)raw[0] == 255 && (BYTE)raw[1] == 254)) {
        for (size_t i = 2; i + 1 < raw.size(); i += 2)
            text.push_back((BYTE)raw[i] | ((BYTE)raw[i + 1] << 8));
    } else if (raw.size() > 2 && (BYTE)raw[0] == 254 && (BYTE)raw[1] == 255) {
        for (size_t i = 2; i + 1 < raw.size(); i += 2)
            text.push_back(((BYTE)raw[i] << 8) | (BYTE)raw[i + 1]);
    } else
        text = wide(raw);

    auto result = std::make_shared<Lyrics>();
    double offset = 0;
    // LRC has a small grammar. Parse bounded numeric tags directly instead of
    // instantiating a regular-expression engine for every song.
    for (size_t p = 0; (p = text.find(L'[', p)) != text.npos; ++p) {
        auto end = text.find(L']', p);
        if (end == text.npos)
            break;
        auto tag = text.substr(p + 1, end - p - 1);
        if (tag.size() > 7 && tag.size() < 30 && _wcsnicmp(tag.c_str(), L"offset:", 7) == 0) {
            wchar_t *tail = nullptr;
            double v = wcstod(tag.c_str() + 7, &tail);
            if (tail != tag.c_str() + 7 && !*tail && std::isfinite(v)) {
                offset = v;
                break;
            }
        }
    }
    std::wistringstream stream(text);
    std::wstring line;
    while (std::getline(stream, line)) {
        std::vector<double> times;
        size_t last = 0;
        for (size_t p = 0; (p = line.find(L'[', p)) != line.npos; ++p) {
            auto end = line.find(L']', p);
            if (end == line.npos)
                break;
            std::wstring_view tag(line.data() + p + 1, end - p - 1);
            size_t colon = tag.find(L':');
            if (colon < 1 || colon > 3 || colon == tag.npos)
                continue;
            auto digits = [](std::wstring_view v) {
                return !v.empty() &&
                       std::all_of(v.begin(), v.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; });
            };
            auto number = [](std::wstring_view v) {
                int n = 0;
                for (auto c : v)
                    n = n * 10 + c - L'0';
                return n;
            };
            if (!digits(tag.substr(0, colon)) || tag.size() < colon + 3 || !digits(tag.substr(colon + 1, 2)))
                continue;
            double fraction = 0;
            if (tag.size() > colon + 3) {
                auto delimiter = tag[colon + 3];
                auto part = tag.substr(colon + 4);
                if ((delimiter != L'.' && delimiter != L':') || part.size() > 3 || !digits(part))
                    continue;
                fraction = number(part) / std::pow(10., (double)part.size());
            }
            last = end + 1;
            int seconds = number(tag.substr(colon + 1, 2));
            if (seconds >= 60)
                continue;
            times.push_back(
                std::max(0., number(tag.substr(0, colon)) * 60 + seconds + fraction - offset / 1000));
        }
        auto value = line.substr(last);
        auto end = value.find_last_not_of(L" \t\r\n");
        value = end == value.npos ? L"" : value.substr(0, end + 1);
        auto first = value.find_first_not_of(L" \t");
        if (first != value.npos)
            value.erase(0, first);
        if (value.size() > 500)
            value.resize(500);
        for (double t : times)
            result->lines.emplace_back(t, value);
    }
    std::stable_sort(result->lines.begin(), result->lines.end(),
                     [](auto &a, auto &b) { return a.first < b.first; });
    // Translations/duets often have two lines with the same timestamp. Retain both.
    std::vector<std::pair<double, std::wstring>> merged;
    for (auto &line : result->lines) {
        if (!merged.empty() && merged.back().first == line.first) {
            if (!line.second.empty() && merged.back().second != line.second) {
                if (!merged.back().second.empty())
                    merged.back().second += L" / ";
                merged.back().second += line.second;
                if (merged.back().second.size() > 500)
                    merged.back().second.resize(500);
            }
        } else
            merged.push_back(std::move(line));
    }
    result->lines = std::move(merged);
    return result;
}
std::wstring Lyrics::at(double p) const {
    auto i =
        std::upper_bound(lines.begin(), lines.end(), p, [](double p, const auto &l) { return p < l.first; });
    return i == lines.begin() ? L"" : std::prev(i)->second;
}
void Lyrics::measure() {
    Measure probe(LyricMeasure);
    ComPtr<IDWriteFactory> f;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   (IUnknown **)f.GetAddressOf())))
        return;

    for (int kind = 0; kind < 2; kind++) {
        ComPtr<IDWriteTextFormat> fmt;
        if (FAILED(f->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                       DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                       kind ? 12.f : 10.5f, L"zh-CN", &fmt)))
            continue;
        double need = 460.46;
        std::set<std::wstring> seen;
        for (auto &line : lines) {
            if (!seen.insert(line.second).second)
                continue;
            ComPtr<IDWriteTextLayout> layout;
            if (FAILED(f->CreateTextLayout(line.second.c_str(), (UINT)line.second.size(), fmt.Get(), 10000,
                                           100, &layout)))
                continue;
            DWRITE_TEXT_METRICS m{};
            layout->GetMetrics(&m);
            need = std::max(need, (m.widthIncludingTrailingWhitespace + (kind ? 40 : 118)) * 1.1);
            if (need >= 616)
                break;
        }
        (kind ? expanded : compact) = std::min(616., need <= 460.46 ? 460.46 : std::ceil(need / 26.4) * 26.4);
    }
}
std::wstring platformOf(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), towlower);
    if (s.find(L"cloudmusic") != s.npos || s.find(L"netease") != s.npos)
        return L"网易云音乐";
    if (s.find(L"qqmusic") != s.npos || s.find(L"qq音乐") != s.npos)
        return L"QQ 音乐";
    if (s.find(L"sodamusic") != s.npos || s.find(L"com.luna.music") != s.npos ||
        s.find(L"soda.music") != s.npos || s.find(L"汽水音乐") != s.npos ||
        s.find(L"douyin.soda") != s.npos)
        return L"汽水音乐";
    return {};
}
std::set<DWORD> processIds(const std::wstring &platform) {
    std::wstring name = platform == L"网易云音乐" ? L"cloudmusic.exe"
                        : platform == L"QQ 音乐"  ? L"QQMusic.exe"
                        : platform == L"汽水音乐" ? L"SodaMusic.exe"
                                                  : platform;
    std::set<DWORD> r;
    HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (h != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W p{sizeof(p)};
        if (Process32FirstW(h, &p))
            do {
                if (!_wcsicmp(p.szExeFile, name.c_str()))
                    r.insert(p.th32ProcessID);
            } while (Process32NextW(h, &p));
        CloseHandle(h);
    }
    return r;
}
void Metric::add(double ms) {
    uint64_t v = (uint64_t)std::max(0., ms * 1000);
    ++count;
    total += v;
    uint64_t old = max;
    while (v > old && !max.compare_exchange_weak(old, v)) {
    }
    ++bins[std::min<size_t>(1001, (size_t)std::ceil(ms))];
}
void Perf::start(const fs::path &p) {
    folder = p;
    if (!p.empty())
        fs::create_directories(p);
    enabled = true;
}
void Perf::flush() {
    if (!enabled)
        return;
    std::lock_guard l(mu);
    PROCESS_MEMORY_COUNTERS_EX mem{sizeof(mem)};
    GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&mem, sizeof(mem));
    FILETIME create, exit, kernel, user;
    GetProcessTimes(GetCurrentProcess(), &create, &exit, &kernel, &user);
    auto ft = [](FILETIME f) {
        ULARGE_INTEGER u;
        u.LowPart = f.dwLowDateTime;
        u.HighPart = f.dwHighDateTime;
        return u.QuadPart;
    };
    std::ostringstream s;
    s << "phase,elapsed_s,cpu_s,working_mb,private_mb\n"
      << phase << ',' << now() - started << ',' << (ft(kernel) + ft(user)) / 1e7 << ','
      << mem.WorkingSetSize / 1048576. << ',' << mem.PrivateUsage / 1048576.
      << "\npart,count,mean_ms,p95_ms_bucket,p99_ms_bucket,max_ms\n";
    const char *names[] = {"Draw",      "FrameGap", "LyricMeasure", "MediaWork",   "NoticeWork",
                           "AudioWork", "UiWork",   "LayoutWork",   "DiagnosticIO", "AccessibilityWork"};
    static_assert(std::size(names) == MetricCount, "Every performance metric needs a CSV name");
    for (int j = 0; j < MetricCount; j++) {
        auto &m = metrics[j];
        uint64_t n = m.count, seen = 0;
        int p95 = 0, p99 = 0;
        for (int i = 0; i < 1002; i++) {
            seen += m.bins[i];
            if (seen < n * .95)
                p95 = i + 1;
            if (seen < n * .99)
                p99 = i + 1;
        }
        s << names[j] << ',' << n << ',' << (n ? m.total / 1000. / n : 0) << ',' << p95 << ',' << p99 << ','
          << m.max / 1000. << '\n';
    }
    writeAtomic(folder / L"latest.csv", s.str());
    auto log = folder / L"performance.log";
    if (fs::exists(log) && fs::file_size(log) > 2 * 1024 * 1024) {
        fs::copy_file(log, folder / L"performance.previous.log", fs::copy_options::overwrite_existing);
        std::ofstream(log, std::ios::trunc);
    }
    std::ofstream(log, std::ios::app) << s.str() << '\n';
}
} // namespace wi


