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
#include <wrl.h>
#include <wtsapi32.h>
#include <xmllite.h>
namespace wi {
using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
inline constexpr wchar_t Version[] = L"1.2.3beta_2";
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
    void load(const fs::path &);
    void save(const fs::path &) const;
};
struct Lyrics {
    std::vector<std::pair<double, std::wstring>> lines;
    double compact = 460.46, expanded = 460.46;
    std::wstring at(double p) const;
    static std::shared_ptr<Lyrics> parse(const std::string &);
    void measure();
};
struct Music {
    std::wstring source, platform, title, artist, album;
    bool playing = false, paused = false, loading = false, timeline = false, prev = false, toggle = false,
         next = false, mode = false;
    int repeat = -1;
    bool shuffle = false;
    double position = 0, duration = 0, rate = 1, stamp = 0;
    std::shared_ptr<std::vector<uint8_t>> cover;
    std::shared_ptr<Lyrics> lyrics;
    std::wstring key() const {
        return source + L"\n" + title + L"\n" + artist + L"\n" + album;
    }
    double progress() const {
        return timeline
                   ? std::clamp(position + (playing ? std::max(0., now() - stamp) * rate : 0.), 0., duration)
                   : 0;
    }
};
struct Notice {
    long long id = 0, handler = 0, arrival = 0;
    std::wstring title, body;
    std::string fingerprint;
    std::string key() const {
        return std::to_string(handler) + ":" + std::to_string(id) + ":" + std::to_string(arrival);
    }
};
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
    std::array<float, 6> peaks{};
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
    void run();

  public:
    std::atomic_bool fixtures = false;
    Media(HWND, const fs::path &, bool);
    ~Media();
    std::shared_ptr<Music> take();
    void command(const std::wstring &, const std::wstring &);
    void pause(bool);
    void reload();
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
} // namespace wi
