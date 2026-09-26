#include "core.h"
#include "browser_access.h"
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
// Some CloudMusic builds publish an all-zero SMTC timeline, while their
// accessibility tree exposes the actual highlighted lyric. Read it only on
// this worker, verify song + artist, and never convert a line into fake seconds.
struct PlayerLyricReader {
    struct View {
        ComPtr<IAccessible> root, title, artist, lyric;
        std::vector<ComPtr<IAccessible>> search;
        ObservedLyricLine freshness;
        double refresh=0;
    };
    std::map<HWND,View> views;
    double discover=0;
    std::wstring retainedKey, lastVerifiedLine;
    std::shared_ptr<Lyrics> retainedDocument;
    void apply(Music &m) {
        m.lyricSyncSource=m.timeline?"smtc_timeline":"none";
        if(retainedKey!=m.key() || retainedDocument!=m.lyrics || !m.lyrics || m.lyricState=="disabled") {
            lastVerifiedLine.clear();
            for(auto &[h,v]:views)v.freshness={};
        }
        retainedKey=m.key();
        retainedDocument=m.lyrics;
        if(m.timeline && m.lyrics)lastVerifiedLine=m.lyrics->at(m.progress());
        m.retainedLyric=lastVerifiedLine;
        if(m.timeline || m.testSource || m.platform!=L"网易云音乐" || !m.lyrics || m.lyricState=="disabled")return;
        Measure work(AccessibilityWork);
        if(now()>=discover) {
            discover=now()+3;auto live=browserWindows(L"cloudmusic.exe");
            std::erase_if(views,[&](auto &v){return std::find(live.begin(),live.end(),v.first)==live.end();});
            for(auto h:live)views.try_emplace(h);
        }
        for(auto &[hwnd,v]:views) {
            auto top=GetAncestor(hwnd,GA_ROOT);
            const bool visible=!IsIconic(top)&&IsWindowVisible(top);
            if(!visible) {
                m.lyricStatus=m.lyrics->provider+L"；歌词已保留，播放器未提供后台进度，同步暂不可用";
            }
            if(!v.root)AccessibleObjectFromWindow(hwnd,OBJID_CLIENT,IID_PPV_ARGS(&v.root));
            if(!v.root)continue;
            if(v.search.empty() && now()>=v.refresh) {
                v.search.push_back(v.root);v.refresh=now()+5;
            }
            scanAccessible(v.search,[&](IAccessible *a,const std::wstring &at) {
                if(accessibleClass(at,L"lyric")){v.lyric=a;return false;}
                if(accessibleClass(at,L"title") && accessibleClass(at,L"cmd-typography")){v.title=a;return false;}
                if(accessibleClass(at,L"info") && accessibleClass(at,L"artist")){v.artist=a;return false;}
                return true;
            });
            auto title=accessibleName(v.title.Get());auto artist=accessibleText(v.artist.Get());
            if(artist.starts_with(L"歌手：")||artist.starts_with(L"歌手:"))artist.erase(0,3);
            if(title.empty() || artist.empty() || normalizeSong(title)!=normalizeSong(m.title) ||
                normalizeSong(artist)!=normalizeSong(m.artist))continue;
            std::wstring current;
            for(auto &line:accessibleChildren(v.lyric.Get(),256)) {
                if(accessibleClass(accessibleAttributes(line.Get()),L"current")) {
                    current=accessibleText(line.Get());break;
                }
            }
            // The accessibility line must also belong to the independently
            // matched lyric document. No "first search result" or old lyrics.
            auto key=normalizeSong(current);if(key.empty())continue;
            int matches=0,matchedIndex=-1;
            for(size_t i=0;i<m.lyrics->lines.size();++i)
                if(normalizeSong(m.lyrics->lines[i].second)==key) {
                    ++matches;matchedIndex=(int)i;
                }
            if(matches) {
                    double maxAge=3;
                    if(matches==1 && matchedIndex+1<(int)m.lyrics->lines.size())
                        maxAge=m.lyrics->lines[matchedIndex+1].first-m.lyrics->lines[matchedIndex].first+1;
                    if(!v.freshness.accept(m.key(),current,visible,now(),maxAge))continue;
                    m.playerLyric=current;m.playerLyricStamp=now();m.playerLyricIndex=matches==1?matchedIndex:-1;
                    lastVerifiedLine=current;m.retainedLyric=current;
                    m.lyricSyncSource=visible?"player_accessibility_line":"player_accessibility_background";m.lyricState="player_line";
                    m.lyricStatus=m.lyrics->provider+(visible?L"；跟随播放器当前行（未提供秒级进度）":L"；跟随后台实际变化的歌词行（无秒级进度）");
                    return;
            }
        }
    }
};
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
    lyrics = std::make_unique<LyricProvider>(p, f);
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
void Media::reload(bool clear) {
    command(clear ? L"clear-lyrics" : L"reload", L"");
}
void Media::configure(int song, int lyric, const std::wstring &api, int player) {
    std::lock_guard lock(mu);
    if (song == songSource && lyric == lyricSource && api == lyricApi && player == playerFilter)
        return;
    bool sourceChanged = songSource != song;
    songSource = song;
    lyricSource = lyric;
    lyricApi = api;
    playerFilter = player;
    settingsPending = true;
    if (sourceChanged)
        latest.reset();
    else if (latest) {
        latest = std::make_shared<Music>(*latest);
        latest->lyrics.reset();
    }
    if (!posted) {
        posted = true;
        PostMessageW(owner, WM_DATA, 0, 0);
    }
    cv.notify_one();
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
        event_token timelineToken{}, playbackToken{};
        std::atomic_int revision = 0;
        std::atomic_uint64_t timelineRevision = 0, playbackRevision = 0;
        TimelineAnchor anchor;
        std::string sessionTag, songTag;
        int read = -1;
        double stamp = 0;
        std::shared_ptr<Music> cache;
        ~Watch() {
            try { s.MediaPropertiesChanged(token); } catch (...) {}
            try { s.TimelinePropertiesChanged(timelineToken); } catch (...) {}
            try { s.PlaybackInfoChanged(playbackToken); } catch (...) {}
        }
    };
    std::map<std::wstring, std::shared_ptr<Watch>> watches;
    GlobalSystemMediaTransportControlsSessionManager manager{nullptr};
    std::wstring selected;
    std::map<std::wstring, double> audibleAt;
    std::shared_ptr<Music> previous;
    int songMode = 1, lyricMode = 0, playerMode = 0;
    std::wstring api;
    int consecutiveErrors = 0;
    PlayerLyricReader playerLyrics;
    const auto sourceSalt=std::to_string(GetCurrentProcessId())+std::to_string(now());
    std::map<std::wstring, std::wstring> failed;
    auto publish = [&](std::shared_ptr<Music> m) {
        std::lock_guard l(mu);
        if (stop || suspended || songMode != songSource || lyricMode != lyricSource || api != lyricApi)
            return;
        latest = m;
        if (!posted) {
            posted = true;
            PostMessageW(owner, WM_DATA, 0, 0);
        }
    };
    while (!stop) {
        bool paused, changed;
        bool reloadLyrics = false, clearLyrics = false;
        std::deque<std::pair<std::wstring, std::wstring>> actions;
        {
            std::lock_guard l(mu);
            paused = suspended;
            songMode = songSource;
            lyricMode = lyricSource;
            playerMode = playerFilter;
            api = lyricApi;
            changed = std::exchange(settingsPending, false);
            actions.swap(commands);
        }
        for (auto i = actions.begin(); i != actions.end();) {
            if (i->first == L"reload" || i->first == L"clear-lyrics") {
                reloadLyrics = true;
                clearLyrics = i->first == L"clear-lyrics";
                i = actions.erase(i);
            } else
                ++i;
        }
        std::vector<std::shared_ptr<Music>> candidates;
        std::wstring preferredId;
        size_t actionsReplied = 0;
        if (songMode == 2 || paused) {
            for (auto &[action, key] : actions)
                PostMessageW(owner, WM_FEEDBACK, 0, 0);
            watches.clear();
            manager = nullptr;
        }
        if (!paused && songMode != 2)
            try {
                Measure work(MediaWork);
                if (!manager)
                    manager = waitOp(GlobalSystemMediaTransportControlsSessionManager::RequestAsync());
                for (auto &[action, key] : actions) {
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
                    ++actionsReplied;
                }
                std::set<std::wstring> live;
                for (auto s : manager.GetSessions()) {
                    std::wstring id(s.SourceAppUserModelId());
                    auto platform = platformOf(id);
                    if (playerMode > 0) {
                        const std::wstring wanted = playerMode == 1 ? L"网易云音乐" : playerMode == 2 ? L"QQ 音乐" : L"汽水音乐";
                        if (platform != wanted) continue;
                    }
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
                        w.reset();
                    }
                    if (!w) {
                        w = std::make_shared<Watch>();
                        w->s = s;
                        w->sessionTag = sha256(utf8(id) + std::to_string(GetCurrentProcessId()) +
                                              std::to_string(now())).substr(0, 16);
                        std::weak_ptr<Watch> weak = w;
                        w->token = s.MediaPropertiesChanged([weak](auto &&, auto &&) {
                            if (auto p = weak.lock())
                                ++p->revision;
                        });
                        w->timelineToken = s.TimelinePropertiesChanged([weak](auto &&, auto &&) {
                            if (auto p = weak.lock()) ++p->timelineRevision;
                        });
                        w->playbackToken = s.PlaybackInfoChanged([weak](auto &&, auto &&) {
                            if (auto p = weak.lock()) ++p->playbackRevision;
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
                        if (!same)
                            w->songTag = sha256(w->sessionTag + utf8(m->key())).substr(0, 16);
                        m->sessionTag = w->sessionTag;
                        m->songTag = w->songTag;
                        m->testSource = fixtures;
                        if (!same && w->cache && id == selected) {
                            auto loading = std::make_shared<Music>();
                            loading->source = id;
                            loading->platform = platform;
                            loading->playing = playing;
                            loading->loading = true;
                            publish(loading);
                        }
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
                        if (revision != w->revision)
                            continue;
                        w->cache = m;
                        w->read = revision;
                        w->stamp = now();
                    }
                    if (!w->cache)
                        continue;
                    auto m = std::make_shared<Music>(*w->cache);
                    m->controlKey = m->key();
                    auto timeline = s.GetTimelineProperties();
                    m->playing = playing;
                    m->paused = isPaused;
                    m->rate = info.PlaybackRate() ? info.PlaybackRate().Value() : 1;
                    if (!std::isfinite(m->rate) || m->rate < 0 || m->rate > 8)
                        m->rate = 1;
                    TimelineSample sample{timeline.StartTime().count() / 1e7,
                                          timeline.EndTime().count() / 1e7,
                                          timeline.MinSeekTime().count() / 1e7,
                                          timeline.MaxSeekTime().count() / 1e7,
                                          timeline.Position().count() / 1e7,
                                          timeline.LastUpdatedTime().time_since_epoch().count(),
                                          w->timelineRevision.load()};
                    applyTimeline(*m, sample, w->anchor, now(), winrt::clock::now().time_since_epoch().count());
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
                        i = watches.erase(i);
                    } else
                        ++i;
                auto preferred = manager.GetCurrentSession();
                preferredId = preferred ? std::wstring(preferred.SourceAppUserModelId()) : L"";
                if (consecutiveErrors)
                    monitor.event("SMTC 连接已恢复");
                consecutiveErrors = 0;
            } catch (...) {
                // RequestAsync/GetSessions can fail before a queued action is
                // reached. Always release the UI's busy state in that case.
                for (; actionsReplied < actions.size(); ++actionsReplied)
                    PostMessageW(owner, WM_FEEDBACK, 0, 0);
                if (++consecutiveErrors == 1)
                    monitor.event("SMTC 读取失败/超时");
                if (consecutiveErrors >= 3) {
                    manager = nullptr;
                }
            }
        if (!paused) {
            try {
                bool needTitles =
                    songMode == 2 ||
                    (songMode == 0 && (std::none_of(candidates.begin(), candidates.end(),
                                                    [](auto &m) { return m->playing; }) ||
                                       std::any_of(candidates.begin(), candidates.end(), [](auto &m) {
                                           return m->title.empty() || m->artist.empty();
                                       })));
                if (needTitles) {
                        auto titles = windowSongs(fixtures);
                        for (auto &title : titles) {
                            if (playerMode > 0) {
                                const std::wstring wanted = playerMode == 1 ? L"网易云音乐" : playerMode == 2 ? L"QQ 音乐" : L"汽水音乐";
                                if (title->platform != wanted) continue;
                            }
                        auto found = std::find_if(candidates.begin(), candidates.end(),
                                                  [&](auto &m) { return m->platform == title->platform; });
                        if (found != candidates.end()) {
                            auto &m = *found;
                            if ((m->title.empty() ||
                                 normalizeSong(m->title) == normalizeSong(title->title)) &&
                                (m->artist.empty() ||
                                 normalizeSong(m->artist) == normalizeSong(title->artist))) {
                                if (m->title.empty()) {
                                    m->title = title->title;
                                    m->cover.reset();
                                }
                                if (m->artist.empty())
                                    m->artist = title->artist;
                                m->infoSource = L"SMTC + 窗口标题";
                            }
                        } else {
                            if (title->playing)
                                audibleAt[title->source] = now();
                            if (!title->playing && audibleAt.contains(title->source) &&
                                now() - audibleAt[title->source] < 1.2) {
                                title->playing = true;
                                title->paused = false;
                            }
                            candidates.push_back(title);
                        }
                    }
                }
                std::shared_ptr<Music> chosen;
                for (auto &m : candidates)
                    if (!m->title.empty() && m->playing && m->source == preferredId)
                        chosen = m;
                if (!chosen)
                    for (auto &m : candidates)
                        if (!m->title.empty() && m->playing && m->source == selected)
                            chosen = m;
                if (!chosen)
                    for (auto &m : candidates)
                        if (!m->title.empty() && m->playing) {
                            chosen = m;
                            break;
                        }
                if (!chosen)
                    for (auto &m : candidates)
                        if (!m->title.empty() && m->paused && m->source == selected)
                            chosen = m;
                if (chosen && previous && chosen->key() != previous->key()) {
                    chosen->lyrics.reset();
                }
                selected = chosen ? chosen->source : L"";
                if(chosen) {
                    if(chosen->sessionTag.empty())chosen->sessionTag=sha256(sourceSalt+utf8(chosen->source)).substr(0,16);
                    chosen->songTag=sha256(chosen->sessionTag+utf8(chosen->key())).substr(0,16);
                }
                previous = chosen;
                std::erase_if(audibleAt, [&](auto &item) { return now() - item.second > 15; });
                lyrics->request(chosen, lyricMode, changed || reloadLyrics, clearLyrics, api);
                if (chosen) {
                    lyrics->attach(*chosen);
                    playerLyrics.apply(*chosen);
                }
                publish(chosen);
            } catch (...) {
                monitor.event("窗口标题/歌词来源读取失败");
                publish(nullptr);
            }
        } else
            lyrics->request(nullptr, lyricMode, false, false, api);
        std::unique_lock l(mu);
        cv.wait_for(l, std::chrono::milliseconds(400),
                    [&] { return stop || settingsPending || !commands.empty(); });
    }
    watches.clear();
    playerLyrics.views.clear();
    manager = nullptr;
    uninit_apartment();
}
} // namespace wi


