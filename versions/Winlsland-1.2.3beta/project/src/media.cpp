#include "core.h"
#include <wincodec.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
namespace wi {
using namespace winrt;
using namespace winrt::Windows::Media;
using namespace winrt::Windows::Media::Control;
using namespace winrt::Windows::Storage::Streams;
template <class T> auto waitOp(T op) {
    if (op.wait_for(std::chrono::milliseconds(2500)) != winrt::Windows::Foundation::AsyncStatus::Completed) {
        op.Cancel();
        throw std::runtime_error("SMTC timeout");
    }
    return op.GetResults();
}
// Decoding and resizing happen on this worker; the render thread only uploads 36 KB.
static std::shared_ptr<std::vector<uint8_t>> decodeCover(const std::vector<uint8_t> &bytes) {
    ComPtr<IWICImagingFactory> factory;
    if (FAILED(
            CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
        return {};
    ComPtr<IStream> stream;
    stream.Attach(SHCreateMemStream(bytes.data(), (UINT)bytes.size()));
    ComPtr<IWICBitmapDecoder> decoder;
    if (!stream || FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr,
                                                           WICDecodeMetadataCacheOnLoad, &decoder)))
        return {};
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame)))
        return {};
    UINT w = 0, h = 0;
    frame->GetSize(&w, &h);
    if (!w || !h || w > 16384 || h > 16384)
        return {};
    ComPtr<IWICBitmapClipper> crop;
    factory->CreateBitmapClipper(&crop);
    WICRect rect{(INT)(w - std::min(w, h)) / 2, (INT)(h - std::min(w, h)) / 2, (INT)std::min(w, h),
                 (INT)std::min(w, h)};
    if (FAILED(crop->Initialize(frame.Get(), &rect)))
        return {};
    ComPtr<IWICBitmapScaler> scaler;
    factory->CreateBitmapScaler(&scaler);
    if (FAILED(scaler->Initialize(crop.Get(), 96, 96, WICBitmapInterpolationModeFant)))
        return {};
    ComPtr<IWICFormatConverter> converter;
    factory->CreateFormatConverter(&converter);
    if (FAILED(converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
                                     nullptr, 0, WICBitmapPaletteTypeCustom)))
        return {};
    auto pixels = std::make_shared<std::vector<uint8_t>>(96 * 96 * 4);
    if (FAILED(converter->CopyPixels(nullptr, 96 * 4, (UINT)pixels->size(), pixels->data())))
        return {};
    return pixels;
}
Media::Media(HWND h, const fs::path &p, bool f) : owner(h), store(p), fixtures(f) {
    thread = std::thread([this] { run(); });
}
Media::~Media() {
    stop = true;
    cv.notify_all();
    thread.join();
}
void Media::pause(bool p) {
    {
        std::lock_guard l(mu);
        suspended = p;
        latest.reset();
        commands.clear();
    }
    cv.notify_all();
}
void Media::reload() {
    command(L"reload", L"");
}
void Media::command(const std::wstring &a, const std::wstring &key) {
    {
        std::lock_guard l(mu);
        if (commands.size() < 8)
            commands.emplace_back(a, key);
    }
    cv.notify_one();
}
std::shared_ptr<Music> Media::take() {
    std::lock_guard l(mu);
    posted = false;
    return latest;
}
void Media::run() {
    init_apartment(apartment_type::multi_threaded);
    struct Watch {
        GlobalSystemMediaTransportControlsSession s{nullptr};
        event_token token{};
        std::atomic_int revision = 0;
        int read = -1;
        double stamp = 0;
        std::shared_ptr<Music> cache;
    };
    std::map<std::wstring, std::shared_ptr<Watch>> watches;
    GlobalSystemMediaTransportControlsSessionManager manager{nullptr};
    std::wstring selected;
    int consecutiveErrors = 0;
    std::map<std::wstring, std::wstring> failed;
    auto publish = [&](std::shared_ptr<Music> m) {
        std::lock_guard l(mu);
        if (stop || suspended)
            return;
        latest = m;
        if (!posted) {
            posted = true;
            PostMessageW(owner, WM_DATA, 0, 0);
        }
    };
    while (!stop) {
        bool paused;
        std::deque<std::pair<std::wstring, std::wstring>> actions;
        {
            std::lock_guard l(mu);
            paused = suspended;
            actions.swap(commands);
        }
        if (!paused)
            try {
                Measure work(MediaWork);
                if (!manager)
                    manager = waitOp(GlobalSystemMediaTransportControlsSessionManager::RequestAsync());
                for (auto &[action, key] : actions) {
                    if (action == L"reload") {
                        for (auto &[id, w] : watches) {
                            if (w->cache) {
                                auto lyr =
                                    Lyrics::parse(readFile(store / (sha256(utf8(w->cache->key())) + ".lrc")));
                                if (lyr)
                                    lyr->measure();
                                w->cache = std::make_shared<Music>(*w->cache);
                                w->cache->lyrics = lyr;
                            }
                        }
                        continue;
                    }
                    bool accepted = false;
                    std::wstring source;
                    try {
                        for (auto s : manager.GetSessions()) {
                            auto props = waitOp(s.TryGetMediaPropertiesAsync());
                            source = s.SourceAppUserModelId();
                            std::wstring current = source + L"\n" + props.Title().c_str() + L"\n" +
                                                   props.Artist().c_str() + L"\n" +
                                                   props.AlbumTitle().c_str();
                            if (current != key)
                                continue;
                            auto before = s.GetPlaybackInfo();
                            auto status = before.PlaybackStatus();
                            auto position = s.GetTimelineProperties().Position();
                            int desiredRepeat = -1;
                            bool desiredShuffle = false;
                            if (action == L"mode") {
                                auto r = before.AutoRepeatMode();
                                auto sh = before.IsShuffleActive();
                                if (!r || !sh)
                                    break;
                                desiredRepeat = (int)r.Value();
                                desiredShuffle = sh.Value();
                                if (desiredShuffle) {
                                    desiredShuffle = false;
                                    if (before.Controls().IsRepeatEnabled())
                                        desiredRepeat = 0;
                                } else if (before.Controls().IsRepeatEnabled()) {
                                    if (desiredRepeat == 0)
                                        desiredRepeat = 2;
                                    else if (desiredRepeat == 2)
                                        desiredRepeat = 1;
                                    else if (before.Controls().IsShuffleEnabled())
                                        desiredShuffle = true;
                                    else
                                        desiredRepeat = 0;
                                } else if (before.Controls().IsShuffleEnabled())
                                    desiredShuffle = true;
                                accepted = true;
                                if (desiredShuffle != sh.Value())
                                    accepted = waitOp(s.TryChangeShuffleActiveAsync(desiredShuffle));
                                if (accepted && desiredRepeat != (int)r.Value())
                                    accepted = waitOp(s.TryChangeAutoRepeatModeAsync(
                                        (MediaPlaybackAutoRepeatMode)desiredRepeat));
                            } else if (action == L"previous")
                                accepted = waitOp(s.TrySkipPreviousAsync());
                            else if (action == L"next")
                                accepted = waitOp(s.TrySkipNextAsync());
                            else
                                accepted =
                                    status == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing
                                        ? waitOp(s.TryPauseAsync())
                                        : waitOp(s.TryPlayAsync());
                            if (accepted) {
                                accepted = false;
                                double until = now() + 2;
                                while (now() < until && !stop) {
                                    std::this_thread::sleep_for(std::chrono::milliseconds(120));
                                    auto info = s.GetPlaybackInfo();
                                    if (action == L"toggle") {
                                        bool playing =
                                            info.PlaybackStatus() ==
                                            GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
                                        accepted = playing !=
                                                   (status ==
                                                    GlobalSystemMediaTransportControlsSessionPlaybackStatus::
                                                        Playing);
                                    } else if (action == L"mode")
                                        accepted = info.AutoRepeatMode() && info.IsShuffleActive() &&
                                                   (int)info.AutoRepeatMode().Value() == desiredRepeat &&
                                                   info.IsShuffleActive().Value() == desiredShuffle;
                                    else {
                                        auto p = waitOp(s.TryGetMediaPropertiesAsync());
                                        accepted = source + L"\n" + p.Title().c_str() + L"\n" +
                                                           p.Artist().c_str() + L"\n" +
                                                           p.AlbumTitle().c_str() !=
                                                       key ||
                                                   (position.count() > 20000000 &&
                                                    s.GetTimelineProperties().Position().count() < 10000000);
                                    }
                                    if (accepted)
                                        break;
                                }
                            }
                            break;
                        }
                    } catch (...) {
                        accepted = false;
                    }
                    if (!accepted && !source.empty())
                        failed[source + L":" + action] = key;
                    PostMessageW(owner, WM_FEEDBACK, accepted ? 1 : 0, 0);
                }
                std::vector<std::shared_ptr<Music>> candidates;
                std::set<std::wstring> live;
                for (auto s : manager.GetSessions()) {
                    std::wstring id(s.SourceAppUserModelId());
                    auto platform = platformOf(id);
                    if (fixtures) {
                        if (id.find(L"WinIsland.MediaFixture") == id.npos)
                            continue;
                        platform = L"媒体集成测试";
                    }
                    if (platform.empty() || (!fixtures && processIds(platform).empty()))
                        continue;
                    live.insert(id);
                    auto &w = watches[id];
                    if (w && w->s != s) {
                        try {
                            w->s.MediaPropertiesChanged(w->token);
                        } catch (...) {
                        }
                        w.reset();
                    }
                    if (!w) {
                        w = std::make_shared<Watch>();
                        w->s = s;
                        std::weak_ptr<Watch> weak = w;
                        w->token = s.MediaPropertiesChanged([weak](auto &&, auto &&) {
                            if (auto p = weak.lock())
                                ++p->revision;
                        });
                    }
                    auto info = s.GetPlaybackInfo();
                    bool playing = info.PlaybackStatus() ==
                                   GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
                    bool isPaused = info.PlaybackStatus() ==
                                        GlobalSystemMediaTransportControlsSessionPlaybackStatus::Paused ||
                                    info.PlaybackStatus() ==
                                        GlobalSystemMediaTransportControlsSessionPlaybackStatus::Stopped;
                    if (!playing && !isPaused)
                        continue;
                    if (w->read != w->revision || now() - w->stamp > 5) {
                        int revision = w->revision;
                        if (w->cache && w->read != revision && id == selected) {
                            auto loading = std::make_shared<Music>();
                            loading->source = id;
                            loading->platform = platform;
                            loading->playing = playing;
                            loading->loading = true;
                            publish(loading);
                        }
                        auto p = waitOp(s.TryGetMediaPropertiesAsync());
                        auto m = std::make_shared<Music>();
                        m->source = id;
                        m->platform = platform;
                        m->title = p.Title();
                        m->artist = p.Artist();
                        m->album = p.AlbumTitle();
                        bool same = w->cache && w->cache->key() == m->key();
                        if (same) {
                            m->cover = w->cache->cover;
                            m->lyrics = w->cache->lyrics;
                        }
                        if (!same || w->read != revision) {
                            m->cover.reset();
                            try {
                                if (auto thumb = p.Thumbnail()) {
                                    auto stream = waitOp(thumb.OpenReadAsync());
                                    if (stream.Size() > 0 && stream.Size() < 4 * 1024 * 1024) {
                                        DataReader r(stream.GetInputStreamAt(0));
                                        unsigned n = (unsigned)stream.Size();
                                        if (waitOp(r.LoadAsync(n)) == n) {
                                            m->cover = std::make_shared<std::vector<uint8_t>>(n);
                                            r.ReadBytes(*m->cover);
                                            m->cover = decodeCover(*m->cover);
                                        }
                                        r.Close();
                                    }
                                    stream.Close();
                                }
                            } catch (...) {
                                monitor.event("封面读取失败，使用占位图");
                            }
                        }
                        if (!same) {
                            m->lyrics = Lyrics::parse(readFile(store / (sha256(utf8(m->key())) + ".lrc")));
                            if (m->lyrics)
                                m->lyrics->measure();
                        }
                        if (revision != w->revision)
                            continue;
                        w->cache = m;
                        w->read = revision;
                        w->stamp = now();
                    }
                    if (!w->cache || w->cache->title.empty())
                        continue;
                    auto m = std::make_shared<Music>(*w->cache);
                    auto timeline = s.GetTimelineProperties();
                    m->playing = playing;
                    m->paused = isPaused;
                    m->timeline = timeline.EndTime() > timeline.StartTime();
                    m->duration = (timeline.EndTime() - timeline.StartTime()).count() / 1e7;
                    m->position = (timeline.Position() - timeline.StartTime()).count() / 1e7;
                    m->rate = info.PlaybackRate() ? info.PlaybackRate().Value() : 1;
                    if (!std::isfinite(m->rate) || m->rate < 0 || m->rate > 8)
                        m->rate = 1;
                    auto age = winrt::clock::now() - timeline.LastUpdatedTime();
                    if (playing && age.count() > 0 && age.count() < 864000000000)
                        m->position += age.count() / 1e7 * m->rate;
                    m->stamp = now();
                    auto c = info.Controls();
                    auto available = [&](const wchar_t *a) { return failed[id + L":" + a] != m->key(); };
                    m->prev = c.IsPreviousEnabled() && available(L"previous");
                    m->next = c.IsNextEnabled() && available(L"next");
                    m->toggle = (playing ? c.IsPauseEnabled() : c.IsPlayEnabled()) && available(L"toggle");
                    m->mode = info.AutoRepeatMode() && info.IsShuffleActive() &&
                              (c.IsRepeatEnabled() || c.IsShuffleEnabled()) && available(L"mode");
                    if (info.AutoRepeatMode())
                        m->repeat = (int)info.AutoRepeatMode().Value();
                    if (info.IsShuffleActive())
                        m->shuffle = info.IsShuffleActive().Value();
                    candidates.push_back(m);
                }
                for (auto i = watches.begin(); i != watches.end();)
                    if (!live.contains(i->first)) {
                        i->second->s.MediaPropertiesChanged(i->second->token);
                        i = watches.erase(i);
                    } else
                        ++i;
                std::shared_ptr<Music> chosen;
                for (auto &m : candidates)
                    if (m->source == selected && m->playing)
                        chosen = m;
                if (!chosen) {
                    auto preferred = manager.GetCurrentSession();
                    std::wstring id = preferred ? std::wstring(preferred.SourceAppUserModelId()) : L"";
                    for (auto &m : candidates)
                        if (m->playing) {
                            chosen = m;
                            if (m->source == id)
                                break;
                        }
                }
                if (!chosen)
                    for (auto &m : candidates)
                        if (m->source == selected && m->paused)
                            chosen = m;
                selected = chosen ? chosen->source : L"";
                if (consecutiveErrors)
                    monitor.event("SMTC 连接已恢复");
                consecutiveErrors = 0;
                publish(chosen);
            } catch (...) {
                if (++consecutiveErrors == 1)
                    monitor.event("SMTC 读取失败/超时");
                if (consecutiveErrors >= 3) {
                    manager = nullptr;
                    publish(nullptr);
                }
            }
        std::unique_lock l(mu);
        cv.wait_for(l, std::chrono::milliseconds(400), [&] { return stop || !commands.empty(); });
    }
    for (auto &[id, w] : watches)
        try {
            w->s.MediaPropertiesChanged(w->token);
        } catch (...) {
        }
    watches.clear();
    manager = nullptr;
    uninit_apartment();
}
} // namespace wi
