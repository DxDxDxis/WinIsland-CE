#pragma once
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bcrypt.h>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <dwmapi.h>
#include <dwrite.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <utility>
#include <wrl.h>
#include <wtsapi32.h>
#include <xmllite.h>
namespace wi {
using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
inline constexpr wchar_t Version[] = L"1.2.4beta_2";
inline constexpr char BuildId[] = "1.2.4beta_2-capsule95-20260907-r2";
inline constexpr double TimelineExtrapolationSeconds = 15;
inline constexpr size_t MusicBarCount = 12;
inline constexpr UINT WM_DATA = WM_APP + 1, WM_NOTICE = WM_APP + 2, WM_HEALTH = WM_APP + 3,
                      WM_TRAY = WM_APP + 4, WM_FEEDBACK = WM_APP + 5;
inline double now() {
    return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
}
inline double ease(double t) {
    t = std::clamp(t, 0., 1.);
    return (1 - (1 + 9 * t) * std::exp(-9 * t)) / (1 - 10 * std::exp(-9.));
}
inline double mix(double a, double b, double p) {
    return a + (b - a) * p;
}
std::string utf8(const std::wstring &);
std::wstring wide(const std::string &);
fs::path dataDir();
fs::path exePath();
std::string readFile(const fs::path &, size_t limit = 1024 * 1024);
void writeAtomic(const fs::path &, const std::string &);
std::string sha256(const std::string &);
std::wstring xmlEscape(std::wstring);
std::map<std::wstring, std::wstring> xmlFields(const std::string &);
struct Settings {
    bool resident = true, hideNative = true;
    double seconds = 4;
    int fps = 0;
    int songSource = 0;  // 0 SMTC first with title fallback, 1 SMTC only, 2 window title only
    int lyricSource = 0; // 0 automatic, 1 LRCLIB only, 2 disabled, 3 NetEase only, 4 LRCLIB search, 5 QCloudMusicApi
    bool showFps = false, showPing = false;
    std::wstring lyricApi = L"https://lrclib.net";
    std::wstring pingTarget; // empty = active default gateway; otherwise IPv4/IPv6 literal
    void load(const fs::path &);
    void save(const fs::path &) const;
};
struct Lyrics {
    std::vector<std::pair<double, std::wstring>> lines;
    std::wstring plain, preview, provider, format;
    bool hasText() const {
        return !lines.empty() || !plain.empty();
    }
    double compact = 460.46, expanded = 460.46, recordingDuration = 0;
    std::wstring at(double p) const;
    static std::shared_ptr<Lyrics> parse(const std::string &);
    void measure();
};
struct Music {
    std::wstring source, platform, title, artist, album, infoSource = L"SMTC", controlKey, lyricStatus;
    bool playing = false, paused = false, loading = false, timeline = false, prev = false, toggle = false,
         next = false, mode = false;
    int repeat = -1;
    bool shuffle = false;
    double position = 0, duration = 0, rate = 1, stamp = 0;
    double timelineExpires = 0;
    bool testSource = false;
    uint64_t lyricRequest = 0, timelineRevision = 0;
    long long timelineUpdated = 0;
    double timelineRawPosition = 0;
    std::string sessionTag, songTag, timelineReason = "not_reported", lyricState = "loading";
    std::wstring playerLyric;
    std::wstring retainedLyric;
    double playerLyricStamp = 0;
    int playerLyricIndex = -1;
    std::string lyricSyncSource = "none";
    std::shared_ptr<std::vector<uint8_t>> cover;
    std::shared_ptr<Lyrics> lyrics;
    std::wstring key() const {
        return source + L"\n" + title + L"\n" + artist + L"\n" + album;
    }
    double progress() const {
        if (!timeline)
            return 0;
        double time = timelineExpires > 0 ? std::min(now(), timelineExpires) : now();
        double p = std::max(0., position + (playing ? std::max(0., time - stamp) * rate : 0.));
        return duration > 0 ? std::min(p, duration) : p;
    }
};
struct TimelineSample {
    double start = 0, end = 0, minimum = 0, maximum = 0, position = 0;
    long long updated = 0;
    uint64_t revision = 0;
};
// A background accessibility snapshot is live only after an observed line change.
// This is line-level evidence, never a synthetic playback position.
struct ObservedLyricLine {
    std::wstring key, text;
    double changedAt = 0;
    bool wasVisible = false, observed = false;
    bool accept(const std::wstring &, const std::wstring &, bool visible, double stamp, double maxAge);
};
struct TimelineAnchor {
    std::wstring key;
    TimelineSample raw;
    double position = 0, stamp = 0, rate = 1, validUntil = 0;
    bool playing = false, valid = false, awaitingTrackTimeline = false, reportExpired = false;
};
void applyTimeline(Music &, const TimelineSample &, TimelineAnchor &, double steady, long long utcTicks);
std::wstring lyricDisplay(const Music &, int &line);
std::wstring normalizeSong(std::wstring);
bool parseSongTitle(const std::wstring &, const std::wstring &, Music &);
std::vector<std::shared_ptr<Music>> windowSongs(bool fixtures);
std::shared_ptr<Lyrics> matchedApiLyrics(const std::string &, const Music &);
std::shared_ptr<Lyrics> parseNeteaseLyrics(const std::string &, const Music &);
std::string selectNeteaseSong(const std::string &, const Music &, std::string &reason);
int lyricProvidersNetworkTest(const fs::path &);
int lyricQcloudNetworkTest(const fs::path &);
class LyricProvider {
    struct Impl;
    std::unique_ptr<Impl> impl;

  public:
    LyricProvider(const fs::path &, bool diagnostic);
    ~LyricProvider();
    void request(const std::shared_ptr<Music> &, int mode, bool force = false, bool clear = false,
                 const std::wstring &api = L"https://lrclib.net");
    void attach(Music &);
};
struct TelemetryData {
    int fps = -1, ping = -1;
    DWORD foreground = 0;
    std::wstring fpsStatus = L"未开启", pingStatus = L"未开启", pingTarget;
};
bool validPingTarget(const std::wstring &);
bool validLyricApi(const std::wstring &);
class Telemetry {
    struct Impl;
    std::unique_ptr<Impl> impl;

  public:
    Telemetry();
    ~Telemetry();
    void configure(bool fps, bool ping, const std::wstring &target);
    TelemetryData snapshot();
};
struct Notice {
    long long id = 0, handler = 0, arrival = 0;
    std::wstring title, body;
    std::string fingerprint;
    std::string origin = "test-injection", application = "synthetic", correlation;
    bool testSource = true;
    std::string key() const {
        return std::to_string(handler) + ":" + std::to_string(id) + ":" + std::to_string(arrival);
    }
};
void traceNotice(const Notice &, const char *stage, const std::string &detail = {});
class Jobs {
    std::mutex mu;
    std::condition_variable cv;
    std::deque<std::function<void()>> queue;
    bool stop = false;
    std::thread thread;

  public:
    Jobs();
    ~Jobs();
    void finish();
    void post(std::function<void()>);
};
struct Metric {
    std::atomic<uint64_t> count{}, total{}, max{};
    std::array<std::atomic<uint64_t>, 1002> bins{};
    void add(double ms);
};
enum MetricId {
    Draw,
    FrameGap,
    LyricMeasure,
    MediaWork,
    NoticeWork,
    AudioWork,
    UiWork,
    LayoutWork,
    DiagnosticIO,
    AccessibilityWork,
    MetricCount
};
class Monitor {
    struct Session;
    std::atomic<std::shared_ptr<Session>> current;
    std::shared_ptr<Session> last;
    std::mutex control;

  public:
    void start(const std::string &renderEnvironment);
    void stop();
    bool active() const {
        return current.load() != nullptr;
    }
    bool hasData();
    void add(MetricId, double);
    void event(const std::string &);
    void sample();
    std::string report();
    fs::path exportTo(const fs::path &directory);
};
extern Monitor monitor;
class Perf {
    std::array<Metric, MetricCount> metrics;
    fs::path folder;
    double started = now();
    std::mutex mu;

  public:
    std::atomic_bool enabled = false;

  private:
    std::string phase = "startup";

  public:
    void setPhase(std::string p) {
        std::lock_guard l(mu);
        phase = std::move(p);
    }
    void start(const fs::path &);
    void add(MetricId id, double ms) {
        if (enabled)
            metrics[id].add(ms);
        monitor.add(id, ms);
    }
    void flush();
};
extern Perf perf;
struct Measure {
    MetricId id;
    double start;
    explicit Measure(MetricId id) : id(id), start(now()) {}
    ~Measure() {
        perf.add(id, (now() - start) * 1000);
    }
};
std::wstring platformOf(std::wstring);
std::set<DWORD> processIds(const std::wstring &);
struct AudioFrame {
    std::wstring source;
    bool available = false, muted = false;
    std::array<float, MusicBarCount> peaks{};
};
class Audio {
    std::mutex mu;
    std::wstring source, platform;
    AudioFrame frame;
    std::atomic_bool stop = false;
    std::thread thread;
    void run();

  public:
    Audio();
    ~Audio();
    void select(const Music *);
    AudioFrame snapshot();
};
class Media {
    std::mutex mu;
    std::condition_variable cv;
    std::atomic_bool stop = false;
    bool suspended = false, posted = false;
    HWND owner;
    std::shared_ptr<Music> latest;
    std::deque<std::pair<std::wstring, std::wstring>> commands;
    std::thread thread;
    fs::path store;
    std::unique_ptr<LyricProvider> lyrics;
    int songSource = 1, lyricSource = 0;
    std::wstring lyricApi = L"https://lrclib.net";
    bool settingsPending = false;
    void run();

  public:
    std::atomic_bool fixtures = false;
    Media(HWND, const fs::path &, bool);
    ~Media();
    std::shared_ptr<Music> take();
    void command(const std::wstring &, const std::wstring &);
    void pause(bool);
    void reload(bool clear = false);
    void configure(int song, int lyric, const std::wstring &api);
};
bool parseToast(const std::string &, Notice &);
class Notices {
    HWND owner;
    fs::path path;
    HANDLE stopEvent = nullptr;
    uint64_t started = 0;
    std::atomic_bool stop = false, paused = false;
    std::thread thread;
    std::mutex mu;
    std::deque<Notice> pending;
    uint64_t generation = 0;
    void run();

  public:
    explicit Notices(HWND, const fs::path & = {});
    ~Notices();
    std::deque<Notice> take();
    void pause(bool);
};
class QqNotices {
    struct Impl;
    std::unique_ptr<Impl> impl;
  public:
    QqNotices(HWND, bool fixtures = false);
    ~QqNotices();
    void pause(bool);
    std::deque<Notice> take();
};
class ToastHelper {
    HANDLE process = nullptr, event = nullptr;

  public:
    ~ToastHelper() {
        enable(false);
    }
    void enable(bool);
};
int runToastHelper(DWORD, uint64_t, uint64_t);
int selfTest(const fs::path &);
int lyricNetworkTest(const fs::path &);
} // namespace wi
