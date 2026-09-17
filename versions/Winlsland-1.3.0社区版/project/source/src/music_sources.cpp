#include "core.h"
#include "embedded_components.h"
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <tlhelp32.h>
#include <winhttp.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

namespace wi {
static std::wstring trim(std::wstring s) {
    auto first = s.find_first_not_of(L" \t\r\n\u200b\ufeff");
    return first == s.npos ? L"" : s.substr(first, s.find_last_not_of(L" \t\r\n\u200b\ufeff") - first + 1);
}
std::wstring normalizeSong(std::wstring s) {
    s = trim(s);
    for (auto &c : s) {
        if (c >= 0xff01 && c <= 0xff5e)
            c -= 0xfee0;
        if (c == 0x2019 || c == 0x2018)
            c = L'\'';
    }
    std::transform(s.begin(), s.end(), s.begin(), towlower);
    s.erase(std::remove_if(s.begin(), s.end(), [](wchar_t c) { return iswspace(c) || c == 0x200b; }),
            s.end());
    return s;
}
bool parseSongTitle(const std::wstring &caption, const std::wstring &platform, Music &out) {
    if (caption.size() > 500)
        return false;
    auto s = trim(caption);
    for (const auto &name :
         {platform, std::wstring(L"QQ音乐"), std::wstring(L"网易云音乐"), std::wstring(L"汽水音乐")}) {
        for (const auto &separator : {L" - ", L" | ", L" — ", L" · "}) {
            auto prefix = name + separator, suffix = separator + name;
            if (s.starts_with(prefix))
                s = trim(s.substr(prefix.size()));
            if (s.ends_with(suffix))
                s = trim(s.substr(0, s.size() - suffix.size()));
        }
    }
    // Only strip known player decorations. An ambiguous/advertising caption is
    // rejected rather than guessed (e.g. interpreting a login page as a song).
    for (auto prefix : {L"正在播放：", L"正在播放: ", L"▶ ", L"♪ "})
        if (s.starts_with(prefix))
            s = trim(s.substr(wcslen(prefix)));
    auto invalid = [](const std::wstring &v) {
        auto n = normalizeSong(v);
        if (n.empty() || n.size() > 160)
            return true;
        for (auto word : {L"网易云音乐", L"qq音乐", L"汽水音乐", L"登录", L"广告", L"开通会员", L"立即领取",
                          L"音乐馆", L"听见好时光", L"http://", L"https://"})
            if (n.find(word) != n.npos)
                return true;
        return false;
    };
    size_t split = s.find(L" - ");
    size_t length = 3;
    if (split == s.npos)
        split = s.find(L" — ");
    if (split == s.npos)
        return false;
    auto title = trim(s.substr(0, split)), artist = trim(s.substr(split + length));
    if (invalid(title) || invalid(artist) || artist.find(L" - ") != artist.npos ||
        artist.find(L" | ") != artist.npos)
        return false;
    out.title = title;
    out.artist = artist;
    out.platform = platform;
    out.infoSource = L"窗口标题";
    return true;
}

std::vector<std::shared_ptr<Music>> windowSongs(bool fixtures) {
    struct Scan {
        std::map<DWORD, std::wstring> processes;
        std::map<DWORD, std::shared_ptr<Music>> songs;
    } scan;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W p{sizeof(p)};
        if (Process32FirstW(snapshot, &p))
            do {
                auto platform = !_wcsicmp(p.szExeFile, L"cloudmusic.exe")  ? std::wstring(L"网易云音乐")
                                : !_wcsicmp(p.szExeFile, L"QQMusic.exe")   ? std::wstring(L"QQ 音乐")
                                : !_wcsicmp(p.szExeFile, L"SodaMusic.exe") ? std::wstring(L"汽水音乐")
                                                                           : L"";
                if (fixtures)
                    platform = !_wcsicmp(p.szExeFile, L"WinIsland-MediaFixture.exe") ? L"媒体集成测试" : L"";
                if (!platform.empty())
                    scan.processes[p.th32ProcessID] = platform;
            } while (Process32NextW(snapshot, &p));
        CloseHandle(snapshot);
    }
    // GetWindowText reads the cross-process caption stored by Windows; do not
    // send WM_GETTEXT to a potentially hung player or enumerate its UI tree.
    EnumWindows(
        [](HWND h, LPARAM value) -> BOOL {
            auto &scan = *(Scan *)value;
            DWORD pid = 0;
            GetWindowThreadProcessId(h, &pid);
            auto found = scan.processes.find(pid);
            if (found == scan.processes.end() || scan.songs.contains(pid))
                return TRUE;
            wchar_t caption[512]{};
            GetWindowTextW(h, caption, 512);
            auto m = std::make_shared<Music>();
            if (parseSongTitle(caption, found->second, *m)) {
                m->source = L"window:" + std::to_wstring(pid);
                m->stamp = now();
                scan.songs[pid] = m;
            }
            return TRUE;
        },
        (LPARAM)&scan);
    // A running process/title is not evidence of playback. Use its actual audio
    // session meter, including inactive/minimized windows. Silence is conservative.
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDeviceCollection> devices;
    if (!scan.songs.empty() &&
        SUCCEEDED(
            CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))) &&
        SUCCEEDED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices))) {
        UINT count = 0;
        devices->GetCount(&count);
        for (UINT i = 0; i < count; ++i) {
            ComPtr<IMMDevice> device;
            devices->Item(i, &device);
            ComPtr<IAudioSessionManager2> manager;
            ComPtr<IAudioSessionEnumerator> sessions;
            if (!device ||
                FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, &manager)) ||
                FAILED(manager->GetSessionEnumerator(&sessions)))
                continue;
            int n = 0;
            sessions->GetCount(&n);
            for (int j = 0; j < n; ++j) {
                ComPtr<IAudioSessionControl> base;
                ComPtr<IAudioSessionControl2> control;
                ComPtr<IAudioMeterInformation> meter;
                sessions->GetSession(j, &base);
                DWORD pid = 0;
                AudioSessionState state;
                float peak = 0;
                if (!base || FAILED(base.As(&control)) || FAILED(control->GetProcessId(&pid)) ||
                    !scan.songs.contains(pid))
                    continue;
                if (SUCCEEDED(control->GetState(&state)) && state == AudioSessionStateActive &&
                    SUCCEEDED(base.As(&meter)) && SUCCEEDED(meter->GetPeakValue(&peak)) && peak > .00001f)
                    scan.songs[pid]->playing = true;
            }
        }
    }
    std::vector<std::shared_ptr<Music>> result;
    for (auto &[pid, m] : scan.songs) {
        m->paused = !m->playing;
        result.push_back(m);
    }
    return result;
}

using namespace winrt::Windows::Data::Json;
static std::wstring stringValue(const JsonObject &o, const wchar_t *name) {
    auto v = o.GetNamedValue(name, JsonValue::CreateNullValue());
    return v.ValueType() == JsonValueType::String ? std::wstring(v.GetString().c_str()) : L"";
}
static std::wstring artistKey(std::wstring value) {
    for (auto &c : value)
        if (c == L'/' || c == L'&' || c == L'、' || c == L',' || c == L'；')
            c = L';';
    std::wistringstream in(value);
    std::wstring part;
    std::set<std::wstring> names;
    while (std::getline(in, part, L';'))
        if (!normalizeSong(part).empty())
            names.insert(normalizeSong(part));
    std::wstring out;
    for (auto &name : names)
        out += name + L";";
    return out;
}
static std::set<int> variants(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(), towlower);
    std::set<int> found;
    const std::vector<std::vector<std::wstring>> terms = {{L"live", L"现场", L"演唱会"},
                                                          {L"cover", L"翻唱"},
                                                          {L"sped", L"加速", L"speed up", L"nightcore"},
                                                          {L"slowed", L"降速", L"慢速"},
                                                          {L"remix", L"混音"},
                                                          {L"instrumental", L"伴奏", L"karaoke"},
                                                          {L"remaster", L"remastered", L"重制"},
                                                          {L"acoustic", L"不插电"},
                                                          {L"demo", L"小样"}};
    for (size_t i = 0; i < terms.size(); ++i)
        for (auto &term : terms[i]) {
            size_t pos = 0;
            while ((pos = text.find(term, pos)) != text.npos) {
                auto latin = [](wchar_t c) { return c >= L'a' && c <= L'z'; };
                bool english = latin(term[0]);
                if (!english || ((pos == 0 || !latin(text[pos - 1])) &&
                                 (pos + term.size() == text.size() || !latin(text[pos + term.size()]))))
                    found.insert((int)i);
                pos += term.size();
            }
        }
    return found;
}
static bool knownDuration(const Music &song) {
    return std::isfinite(song.duration) && song.duration > 0 && song.duration < 86400;
}
static bool metadataMatches(const JsonObject &record, const Music &song, bool uniqueSearchCandidate = false) {
    auto title = stringValue(record, L"trackName"), artist = stringValue(record, L"artistName"),
         album = stringValue(record, L"albumName");
    if (title.empty() || artist.empty() || normalizeSong(title) != normalizeSong(song.title) ||
        artistKey(artist) != artistKey(song.artist))
        return false;
    if (variants(title + L" " + album) != variants(song.title + L" " + song.album))
        return false;
    double duration = record.GetNamedNumber(L"duration", -1);
    bool timed = knownDuration(song);
    if (timed && (!std::isfinite(duration) || duration <= 0 || std::abs(duration - song.duration) > 3))
        return false;
    // Without duration, use the reported album. If both are absent, only
    // a unique search candidate may supply lyrics, never a player timeline.
    if (!timed) {
        if (!song.album.empty() && normalizeSong(album) != normalizeSong(song.album))
            return false;
        if (song.album.empty() &&
            (!uniqueSearchCandidate || album.empty() || !std::isfinite(duration) || duration <= 0))
            return false;
    }
    return true;
}
static bool useful(const std::shared_ptr<Lyrics> &l) {
    return l &&
           std::any_of(l->lines.begin(), l->lines.end(), [](auto &line) { return !line.second.empty(); });
}
static std::wstring previewOf(const std::wstring &text) {
    std::wistringstream in(text);
    std::wstring line;
    while (std::getline(in, line))
        if (!trim(line).empty())
            return trim(line).substr(0, 240);
    return L"";
}
static void prepareLyric(const std::shared_ptr<Lyrics> &l) {
    if (!l)
        return;
    if (l->plain.empty())
        for (auto &line : l->lines)
            if (!line.second.empty())
                l->plain += line.second + L"\n";
    l->preview = previewOf(l->plain);
    l->measure();
}
std::shared_ptr<Lyrics> matchedApiLyrics(const std::string &body, const Music &song) {
    try {
        if (body.empty() || body.size() > 1024 * 1024)
            return {};
        auto o = JsonObject::Parse(wide(body));
        if (!metadataMatches(o, song, stringValue(o, L"wiMatchBasis") == L"unique_exact_title_artist"))
            return {};
        auto l = Lyrics::parse(utf8(stringValue(o, L"syncedLyrics")));
        if (!useful(l)) {
            auto plain = trim(stringValue(o, L"plainLyrics"));
            if (plain.empty())
                return {};
            l = std::make_shared<Lyrics>();
            l->plain = plain;
            l->format = L"Plain";
        } else {
            if (knownDuration(song) && l->lines.back().first > song.duration + 10)
                return {};
            l->format = L"LRC";
        }
        l->provider = stringValue(o, L"wiProvider");
        l->recordingDuration = o.GetNamedNumber(L"duration", 0);
        if (l->provider.empty())
            l->provider = L"LRCLIB";
        auto format = stringValue(o, L"wiFormat");
        if (!format.empty())
            l->format = format;
        prepareLyric(l);
        return l;
    } catch (...) {
        return {};
    }
}
static std::shared_ptr<Lyrics> parseYrc(const std::wstring &text) {
    std::wistringstream input(text);
    std::wstring line, lrc;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] != L'[')
            continue;
        auto close = line.find(L']');
        if (close == line.npos)
            continue;
        auto tag = line.substr(1, close - 1);
        long long ms = 0, len = 0;
        wchar_t tail = 0;
        if (swscanf_s(tag.c_str(), L"%lld,%lld%c", &ms, &len, &tail, 1) != 2 || ms < 0 || len < 0 ||
            ms + len > 86400000)
            return {};
        std::wstring words;
        size_t pos = close + 1;
        while (pos < line.size()) {
            if (line[pos] == L'(') {
                auto end = line.find(L')', pos);
                if (end == line.npos)
                    return {};
                auto timing = line.substr(pos + 1, end - pos - 1);
                long long start = 0, duration = 0, extra = 0;
                if (swscanf_s(timing.c_str(), L"%lld,%lld,%lld%c", &start, &duration, &extra, &tail, 1) !=
                        3 ||
                    start < 0 || duration < 0)
                    return {};
                pos = end + 1;
            } else
                words += line[pos++];
        }
        wchar_t stamp[40];
        swprintf_s(stamp, L"[%02lld:%02lld.%03lld]", ms / 60000, (ms / 1000) % 60, ms % 1000);
        lrc += stamp + words + L"\n";
    }
    auto result = Lyrics::parse(utf8(lrc));
    if (useful(result))
        result->format = L"YRC（逐行）";
    return useful(result) ? result : nullptr;
}
std::shared_ptr<Lyrics> parseNeteaseLyrics(const std::string &body, const Music &song) {
    try {
        if (body.empty() || body.size() > 1024 * 1024)
            return {};
        auto o = JsonObject::Parse(wide(body));
        if (o.GetNamedNumber(L"code", 0) != 200 || o.GetNamedBoolean(L"nolyric", false))
            return {};
        std::shared_ptr<Lyrics> plain;
        for (auto key : {L"lrc", L"yrc"}) {
            auto v = o.GetNamedValue(key, JsonValue::CreateNullValue());
            if (v.ValueType() != JsonValueType::Object)
                continue;
            auto text = stringValue(v.GetObject(), L"lyric");
            if (trim(text).empty())
                continue;
            auto l = Lyrics::parse(utf8(text));
            if (useful(l))
                l->format = L"LRC";
            else if (text.find(L"(0,") != text.npos || text.find(L',') != text.npos)
                l = parseYrc(text);
            if (useful(l)) {
                if (knownDuration(song) && l->lines.back().first > song.duration + 10)
                    continue;
                prepareLyric(l);
                return l;
            }
            // Broken LRC/YRC syntax is not presented as plain lyrics.
            if (text.find_first_of(L"[]{}") == text.npos && !trim(text).empty()) {
                plain = std::make_shared<Lyrics>();
                plain->plain = trim(text);
                plain->format = L"Plain";
            }
        }
        if (plain)
            prepareLyric(plain);
        return plain;
    } catch (...) {
        return {};
    }
}
static JsonObject neteaseRecord(const JsonObject &o) {
    JsonObject out;
    out.Insert(L"trackName", JsonValue::CreateStringValue(stringValue(o, L"name")));
    auto a = o.GetNamedValue(L"artists", o.GetNamedValue(L"ar", JsonValue::CreateNullValue()));
    std::wstring artist;
    if (a.ValueType() == JsonValueType::Array)
        for (auto v : a.GetArray()) {
            auto name = stringValue(v.GetObject(), L"name");
            if (!artist.empty())
                artist += L" / ";
            artist += name;
        }
    out.Insert(L"artistName", JsonValue::CreateStringValue(artist));
    auto album = o.GetNamedValue(L"album", o.GetNamedValue(L"al", JsonValue::CreateNullValue()));
    out.Insert(L"albumName", JsonValue::CreateStringValue(album.ValueType() == JsonValueType::Object
                                                              ? stringValue(album.GetObject(), L"name")
                                                              : L""));
    out.Insert(L"duration", JsonValue::CreateNumberValue(
                                o.GetNamedNumber(L"duration", o.GetNamedNumber(L"dt", 0)) / 1000.));
    out.Insert(L"id", o.GetNamedValue(L"id", JsonValue::CreateNumberValue(0)));
    return out;
}
std::string selectNeteaseSong(const std::string &body, const Music &song, std::string &reason) {
    reason = "invalid_json";
    try {
        if (body.size() > 1024 * 1024)
            return {};
        auto root = JsonObject::Parse(wide(body));
        if (root.GetNamedNumber(L"code", 0) != 200) {
            reason = "api_code";
            return {};
        }
        auto array = root.GetNamedObject(L"result").GetNamedArray(L"songs");
        if (array.Size() > 200)
            return {};
        std::map<long long, JsonObject> matches;
        bool exactAlbum = false;
        for (auto item : array) {
            try {
                auto record = neteaseRecord(item.GetObject());
                if (!metadataMatches(record, song, true))
                    continue;
                double id = record.GetNamedNumber(L"id", 0);
                if (id <= 0 || id > 9007199254740991. || std::floor(id) != id)
                    continue;
                bool album = !song.album.empty() &&
                             normalizeSong(stringValue(record, L"albumName")) == normalizeSong(song.album);
                if (album && !exactAlbum) {
                    matches.clear();
                    exactAlbum = true;
                }
                if (!exactAlbum || album)
                    matches.emplace((long long)id, record);
            } catch (...) {
            }
        }
        reason = matches.empty()      ? "metadata_or_version_mismatch"
                 : matches.size() > 1 ? "ambiguous_recording"
                                      : "unique_match";
        if (matches.size() != 1)
            return {};
        auto selected = matches.begin()->second;
        if (!knownDuration(song) && song.album.empty()) {
            // No recording timing/album can be asserted about the player. Accept
            // only one exact title+all-artists ID with complete catalog metadata;
            // an ambiguous set is rejected above. Never copy this into SMTC.
            selected.Insert(L"wiMatchBasis", JsonValue::CreateStringValue(L"unique_exact_title_artist"));
            reason = "unique_exact_title_artist; player_album_duration_unavailable";
        }
        return utf8(selected.Stringify().c_str());
    } catch (...) {
        return {};
    }
}
static std::wstring urlEncode(const std::wstring &s) {
    std::wstring out;
    constexpr wchar_t hex[] = L"0123456789ABCDEF";
    for (unsigned char c : utf8(s)) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
            c == '_' || c == '.' || c == '~')
            out += wchar_t(c);
        else {
            out += L'%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}
struct InternetHandle {
    HINTERNET value = nullptr;
    explicit InternetHandle(HINTERNET v) : value(v) {}
    ~InternetHandle() {
        if (value)
            WinHttpCloseHandle(value);
    }
    operator HINTERNET() const {
        return value;
    }
};
bool validLyricApi(const std::wstring &base) {
    if (base.empty() || base.size() > 512 || base.find_first_of(L"\r\n\t ?#") != base.npos)
        return false;
    URL_COMPONENTS parts{sizeof(parts)};
    parts.dwHostNameLength = parts.dwUserNameLength = parts.dwPasswordLength = parts.dwUrlPathLength = -1;
    return WinHttpCrackUrl(base.c_str(), 0, 0, &parts) && parts.nScheme == INTERNET_SCHEME_HTTPS &&
           parts.dwHostNameLength && !parts.dwUserNameLength && !parts.dwPasswordLength;
}
static std::string selectSearch(const std::string &body, const Music &song) {
    try {
        auto array = JsonArray::Parse(wide(body));
        if (array.Size() > 200)
            return {};
        struct Candidate {
            std::string raw;
            std::shared_ptr<Lyrics> lyric;
            int score;
        };
        std::vector<Candidate> matches;
        int best = -1;
        for (auto item : array) {
            try {
                auto obj = item.GetObject();
                auto raw = utf8(obj.Stringify().c_str());
                auto l = matchedApiLyrics(raw, song);
                if (!l)
                    continue;
                int score = (useful(l) ? 2 : 0) +
                            (!song.album.empty() && normalizeSong(stringValue(obj, L"albumName")) ==
                                                        normalizeSong(song.album)
                                 ? 1
                                 : 0);
                best = std::max(best, score);
                matches.push_back({raw, l, score});
            } catch (...) {
            }
        }
        const Candidate *chosen = nullptr;
        for (auto &m : matches)
            if (m.score == best) {
                if (chosen &&
                    (chosen->lyric->lines != m.lyric->lines || chosen->lyric->plain != m.lyric->plain))
                    return {};
                chosen = &m;
            }
        return chosen ? chosen->raw : std::string{};
    } catch (...) {
        return {};
    }
}
struct LyricHttp {
    DWORD code = 0, error = 0;
    DWORD qtError = 0;
    DWORD businessCode = 0;
    bool cancelled = false, mock = false, qcloud = false;
    bool ok() const { return code == 200 && !error && !qtError && !cancelled && (!businessCode || businessCode == 200); }
    double cooldown = 60;
    std::string body;
};
// The Qt-based library is isolated from our window/animation process. Restrict the
// child to a numeric song ID, inherited pipe handles, a job lifetime and a deadline.
static LyricHttp qcloudHttp(const std::wstring &path, const std::function<bool()> &valid) {
    LyricHttp result;
    result.qcloud = true;
    const auto prefix = path.find(L"?id=");
    if (prefix == path.npos || !path.starts_with(L"/api/song/lyric")) {
        result.error = ERROR_INVALID_PARAMETER; return result;
    }
    const auto id = path.substr(prefix + 4, path.find(L'&', prefix) - (prefix + 4));
    if (id.empty() || id.size() > 16 || id.find_first_not_of(L"0123456789") != id.npos) {
        result.error = ERROR_INVALID_PARAMETER; return result;
    }
    const auto executable = lyricProviderDirectory() / L"WinIsland-LyricHelper.exe";
    struct Handles {
        HANDLE read=nullptr, write=nullptr, nul=nullptr, job=nullptr, process=nullptr, thread=nullptr;
        ~Handles() { for(auto h:{thread,process,job,nul,write,read}) if(h && h!=INVALID_HANDLE_VALUE)CloseHandle(h); }
    } h;
    SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};
    if (!CreatePipe(&h.read,&h.write,&sa,0) || !SetHandleInformation(h.read,HANDLE_FLAG_INHERIT,0)) {
        result.error=GetLastError();return result;
    }
    h.nul=CreateFileW(L"NUL",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,nullptr);
    h.job=CreateJobObjectW(nullptr,nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(h.nul==INVALID_HANDLE_VALUE || !h.job || !SetInformationJobObject(h.job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))) {
        result.error=GetLastError();return result;
    }
    SIZE_T bytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&bytes);
    std::vector<unsigned char> storage(bytes);
    auto attributes=(LPPROC_THREAD_ATTRIBUTE_LIST)storage.data();
    if(!InitializeProcThreadAttributeList(attributes,1,0,&bytes)){result.error=GetLastError();return result;}
    struct DeleteAttributes {LPPROC_THREAD_ATTRIBUTE_LIST p;~DeleteAttributes(){DeleteProcThreadAttributeList(p);}} cleanup{attributes};
    HANDLE inherited[]{h.write,h.nul};
    if(!UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherited,sizeof(inherited),nullptr,nullptr)) {
        result.error=GetLastError();return result;
    }
    STARTUPINFOEXW start{};start.StartupInfo.cb=sizeof(start);start.lpAttributeList=attributes;
    start.StartupInfo.dwFlags=STARTF_USESTDHANDLES;start.StartupInfo.hStdOutput=h.write;
    start.StartupInfo.hStdInput=start.StartupInfo.hStdError=h.nul;
    std::wstring command=L"\""+executable.wstring()+L"\" "+(path.find(L"/v1?")!=path.npos?L"lyric_new ":L"lyric ")+id;
    PROCESS_INFORMATION pi{};
    if(!valid()){result.cancelled=true;return result;}
    std::vector<wchar_t> environment;
    if(dataRootReady())environment=childEnvironment(dataDir()/L"lyrics"/L"provider-runtime");
    if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,TRUE,
        CREATE_NO_WINDOW|CREATE_SUSPENDED|EXTENDED_STARTUPINFO_PRESENT|CREATE_UNICODE_ENVIRONMENT,environment.empty()?nullptr:environment.data(),executable.parent_path().c_str(),&start.StartupInfo,&pi)) {
        result.error=GetLastError();return result;
    }
    h.process=pi.hProcess;h.thread=pi.hThread;
    if(!AssignProcessToJobObject(h.job,h.process)) {result.error=GetLastError();TerminateProcess(h.process,result.error);return result;}
    ResumeThread(h.thread);CloseHandle(h.write);h.write=nullptr;
    const double deadline=now()+7;
    std::string output;
    for(;;) {
        DWORD available=0,read=0;char buffer[8192];
        while(PeekNamedPipe(h.read,nullptr,0,nullptr,&available,nullptr) && available) {
            if(!ReadFile(h.read,buffer,std::min<DWORD>(available,sizeof(buffer)),&read,nullptr) || !read)break;
            output.append(buffer,read);
            if(output.size()>1024*1024){result.error=ERROR_FILE_TOO_LARGE;break;}
        }
        if(result.error || !valid() || now()>deadline) {
            result.cancelled=!valid();if(!result.error)result.error=result.cancelled?ERROR_CANCELLED:ERROR_TIMEOUT;
            TerminateProcess(h.process,result.error);WaitForSingleObject(h.process,1000);return result;
        }
        if(WaitForSingleObject(h.process,20)==WAIT_OBJECT_0) {
            DWORD remaining=0;if(PeekNamedPipe(h.read,nullptr,0,nullptr,&remaining,nullptr)&&remaining)continue;
            break;
        }
    }
    DWORD exitCode=0;GetExitCodeProcess(h.process,&exitCode);
    if(exitCode){result.error=ERROR_PROCESS_ABORTED;return result;}
    try {
        auto envelope=JsonObject::Parse(wide(output));
        result.code=(DWORD)envelope.GetNamedNumber(L"http_status",0);
        result.qtError=(DWORD)envelope.GetNamedNumber(L"network_error",0);
        auto body=envelope.GetNamedObject(L"body");
        result.body=utf8(body.Stringify().c_str());
        result.businessCode=(DWORD)body.GetNamedNumber(L"code",0);
    } catch(...) {result.error=ERROR_INVALID_DATA;}
    return result;
}
int lyricQcloudNetworkTest(const fs::path &output) {
    std::ostringstream report;bool ok=true;Music song;
    for(auto path:{L"/api/song/lyric?id=1408586353",L"/api/song/lyric/v1?id=1408586353"}) {
        auto response=qcloudHttp(path,[]{return true;});
        auto lyric=response.ok()?parseNeteaseLyrics(response.body,song):nullptr;
        bool passed=lyric && !lyric->lines.empty();ok=ok&&passed;
        report<<(passed?"PASS":"FAIL")<<": QCloudMusicApi actual child; id=1408586353; endpoint="<<utf8(path)
              <<"; HTTP="<<response.code<<"; API="<<response.businessCode<<"; QtNetwork="<<response.qtError
              <<"; ProcessError="<<response.error<<"; parsed_lines="<<(lyric?lyric->lines.size():0)<<'\n';
    }
    auto cancelled=qcloudHttp(L"/api/song/lyric?id=1408586353",[]{return false;});
    ok=ok&&cancelled.cancelled;
    report<<(cancelled.cancelled?"PASS":"FAIL")<<": cancelled request does not launch helper\n";
    report<<"Real interface and parser checks only; not a player synchronization test.\n";
    writeAtomic(output,report.str());return ok?0:1;
}
static LyricHttp lyricHttp(const std::wstring &base, const std::wstring &path,
                           const std::function<bool()> &valid, bool diagnostic) {
    LyricHttp result;
    if (!validLyricApi(base) || !valid())
    {
        result.cancelled = !valid();
        return result;
    }
    URL_COMPONENTS parts{sizeof(parts)};
    parts.dwHostNameLength = parts.dwUrlPathLength = -1;
    if (!WinHttpCrackUrl(base.c_str(), 0, 0, &parts))
        return result;
    std::wstring host(parts.lpszHostName, parts.dwHostNameLength),
        prefix(parts.lpszUrlPath, parts.dwUrlPathLength);
    while (!prefix.empty() && prefix.back() == L'/')
        prefix.pop_back();
    INTERNET_PORT port = parts.nPort;
    DWORD flags = WINHTTP_FLAG_SECURE;
    wchar_t testPort[16]{};
    if (diagnostic && GetEnvironmentVariableW(L"WINISLAND_TEST_LYRIC_PORT", testPort, 16)) {
        int n = _wtoi(testPort);
        if (n > 0 && n <= 65535) {
            host = L"127.0.0.1";
            port = (INTERNET_PORT)n;
            flags = 0;
            result.mock = true;
        }
    }
    InternetHandle session(WinHttpOpen((std::wstring(L"WinIsland/") + Version).c_str(),
                                       WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                       WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) {
        result.error = GetLastError();
        return result;
    }
    WinHttpSetTimeouts(session, 1800, 1800, 1800, 1800);
    InternetHandle connection(WinHttpConnect(session, host.c_str(), port, 0));
    if (!connection) {
        result.error = GetLastError();
        return result;
    }
    InternetHandle request(WinHttpOpenRequest(connection, L"GET", (prefix + path).c_str(), nullptr,
                                              WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (!request) {
        result.error = GetLastError();
        return result;
    }
    DWORD disabled = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES;
    WinHttpSetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled));
    auto headers = std::wstring(L"Accept: application/json\r\n") +
                   (base == L"https://music.163.com" ? L"Referer: https://music.163.com/\r\n" : L"");
    if (!valid()) { result.cancelled = true; return result; }
    if (!WinHttpSendRequest(request, headers.c_str(), (DWORD)-1L, nullptr, 0, 0, 0)) {
        result.error = GetLastError();
        return result;
    }
    if (!valid()) { result.cancelled = true; return result; }
    if (!WinHttpReceiveResponse(request, nullptr)) {
        result.error = GetLastError();
        return result;
    }
    DWORD size = sizeof(result.code);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &result.code, &size, WINHTTP_NO_HEADER_INDEX);
    if (result.code == 429) {
        wchar_t retry[64]{};
        size = sizeof(retry);
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, L"Retry-After", retry, &size,
                                WINHTTP_NO_HEADER_INDEX))
            result.cooldown = std::clamp((double)_wtoi(retry), 30., 300.);
    }
    if (result.code != 200)
        return result;
    char buffer[8192];
    DWORD read = 0;
    double deadline = now() + 4;
    while (valid() && now() < deadline) {
        if (!WinHttpReadData(request, buffer, sizeof(buffer), &read)) {
            result.error = GetLastError();
            break;
        }
        if (!read)
            return result;
        if (result.body.size() + read > 1024 * 1024) {
            result.error = ERROR_FILE_TOO_LARGE;
            break;
        }
        result.body.append(buffer, read);
    }
    if (!result.error)
        result.error = valid() ? ERROR_TIMEOUT : ERROR_CANCELLED;
    result.cancelled = !valid();
    result.body.clear();
    return result;
}
static std::wstring lrclibPath(const Music &song, bool search) {
    auto path = std::wstring(search ? L"/api/search?track_name=" : L"/api/get?track_name=") +
                urlEncode(trim(song.title)) + L"&artist_name=" + urlEncode(trim(song.artist));
    if (!search && !song.album.empty())
        path += L"&album_name=" + urlEncode(trim(song.album));
    if (!search && knownDuration(song))
        path += L"&duration=" + std::to_wstring((int)std::lround(song.duration));
    return path;
}
static std::string lyricCache(const JsonObject &metadata, const std::shared_ptr<Lyrics> &l) {
    auto copy = JsonObject::Parse(metadata.Stringify());
    std::wstring timed;
    for (auto &[seconds, line] : l->lines) {
        auto ms = (long long)std::llround(seconds * 1000);
        wchar_t tag[40];
        swprintf_s(tag, L"[%02lld:%02lld.%03lld]", ms / 60000, (ms / 1000) % 60, ms % 1000);
        timed += tag + line + L"\n";
    }
    copy.Insert(L"syncedLyrics", JsonValue::CreateStringValue(timed));
    copy.Insert(L"plainLyrics", JsonValue::CreateStringValue(l->plain));
    copy.Insert(L"wiProvider", JsonValue::CreateStringValue(l->provider));
    copy.Insert(L"wiFormat", JsonValue::CreateStringValue(l->format));
    return utf8(copy.Stringify().c_str());
}
int lyricProvidersNetworkTest(const fs::path &output) {
    std::ostringstream report;
    bool passed = true;
    Music known;
    known.title = L"海阔天空";
    known.artist = L"Beyond";
    known.album = L"海阔天空";
    known.duration = 326;
    known.timeline = true;
    for (auto path : {L"/api/song/lyric?id=347230&lv=-1&kv=-1&tv=-1",
                      L"/api/song/lyric/v1?id=347230&lv=-1&kv=-1&tv=-1&rv=-1&yv=-1"}) {
        auto r = lyricHttp(L"https://music.163.com", path, [] { return true; }, false);
        auto l = parseNeteaseLyrics(r.body, known);
        bool ok = r.code == 200 && useful(l);
        passed &= ok;
        report << (ok ? "PASS" : "FAIL") << ": known ID 347230; endpoint=" << utf8(path)
               << "; HTTP=" << r.code << "; WinHTTP=" << r.error
               << "; format=" << (l ? utf8(l->format) : "none") << "; lines=" << (l ? l->lines.size() : 0)
               << "\n";
    }
    auto r = lyricHttp(
        L"https://music.163.com",
        L"/api/search/get?s=" + urlEncode(known.title + L" " + known.artist) + L"&type=1&limit=30&offset=0",
        [] { return true; }, false);
    std::string why;
    auto selected = selectNeteaseSong(r.body, known, why);
    long long id = 0;
    if (!selected.empty())
        id = (long long)JsonObject::Parse(wide(selected)).GetNamedNumber(L"id");
    passed &= id == 347230;
    report << (id == 347230 ? "PASS" : "FAIL") << ": metadata search selects known recording ID=" << id
           << "; result=" << why << "; HTTP=" << r.code << "\n";
    report << "API checks only; these results do not prove player timeline availability or in-player "
              "synchronization.\n";
    writeAtomic(output, report.str());
    return passed ? 0 : 1;
}
struct LyricProvider::Impl {
    std::mutex mutex;
    std::condition_variable cv;
    std::atomic_bool stopping = false;
    std::atomic_uint64_t generation = 0;
    std::shared_ptr<Music> pending;
    std::wstring key, status;
    std::string resultState = "loading";
    double requestedDuration = 0;
    std::wstring api = L"https://lrclib.net", requestIdentity;
    double retryAt = 0;
    int retries = 0;
    std::shared_ptr<Lyrics> result;
    int mode = -1;
    bool clear = false, diagnostic;
    fs::path store;
    std::thread worker;
    explicit Impl(const fs::path &p, bool d) : diagnostic(d), store(p), worker([this] { run(); }) {}
    ~Impl() {
        stopping = true;
        ++generation;
        cv.notify_all();
        worker.join();
    }
    // Only the lyric worker touches backoff state. It is shared across tracks
    // so quick track changes do not hammer a service already returning 429.
    std::map<std::wstring, double> unavailableUntil;
    LyricHttp fetch(const std::wstring &base, const std::wstring &path, const char *stage, uint64_t version,
                    bool &transient, bool forceQcloud = false) {
        auto valid = [&] { return !stopping && generation == version; };
        LyricHttp r;
        if (unavailableUntil[base] > now()) {
            transient = true;
            monitor.event(std::string("歌词跳过 ") + stage + "；请求=" + std::to_string(version) +
                          "；原因=服务退避");
            return r;
        }
        for (int attempt = 0; attempt < 2 && valid(); ++attempt) {
            r = forceQcloud && !diagnostic ? qcloudHttp(path,valid) : lyricHttp(base, path, valid, diagnostic);
            // A transport failure may use the packaged C++ backend in the same
            // lyric stage. Never route around an HTTP/API rate limit or refusal.
            if (!forceQcloud && !diagnostic && r.error && valid() &&
                path.starts_with(L"/api/song/lyric") &&
                fs::exists(lyricProviderDirectory()/L"WinIsland-LyricHelper.exe")) {
                monitor.event("歌词传输回退；请求="+std::to_string(version)+"；WinHTTP="+std::to_string(r.error)+"；到=QCloudMusicApi");
                r=qcloudHttp(path,valid);
            }
            // API-level rate limiting matters even when HTTP is 200.
            if (r.code == 200 && !r.body.empty())
                try {
                    auto o = JsonObject::Parse(wide(r.body));
                    r.businessCode = (DWORD)o.GetNamedNumber(L"code", 0);
                } catch (...) {
                }
            monitor.event(std::string("歌词请求 ") + stage + "；请求=" + std::to_string(version) +
                          "；尝试=" + std::to_string(attempt + 1) + "；HTTP=" + std::to_string(r.code) +
                          "；业务码=" + std::to_string(r.businessCode) +
                          (r.qcloud ? "；ProcessError=" : "；WinHTTP=") + std::to_string(r.error) +
                          "；QtNetwork="+std::to_string(r.qtError)+"；传输="+(r.qcloud?"QCloudMusicApi":"WinHTTP")+
                          "；网络来源=" + (r.mock ? "test-loopback" : "real-https") +
                          "；取消=" + ((!valid() || r.cancelled) ? "superseded_or_shutdown" : "none"));
            bool retry = r.code == 0 || r.error || r.qtError || r.code >= 500 || r.businessCode >= 500;
            if (!retry || !valid() || attempt == 1)
                break;
            std::unique_lock lock(mutex);
            cv.wait_for(lock, std::chrono::milliseconds(250), [&] { return !valid(); });
        }
        // Cancellation belongs to the old track, not to the service's health.
        if (!valid())
            return r;
        if (r.code == 0 || r.error || r.qtError || r.code == 429 || r.code >= 500 || r.code == 403 ||
            r.businessCode == 429 || r.businessCode >= 500 || r.businessCode == 403) {
            transient = true;
            unavailableUntil[base] = now() + r.cooldown;
        }
        return r;
    }
    void pruneCache() {
        std::error_code ec;
        std::vector<fs::directory_entry> files;
        uintmax_t bytes = 0;
        for (auto &entry : fs::directory_iterator(store / L"api-cache", ec)) {
            if (entry.path().extension() != L".json" || !entry.is_regular_file(ec))
                continue;
            files.push_back(entry);
            bytes += entry.file_size(ec);
        }
        std::sort(files.begin(), files.end(),
                  [&](auto &a, auto &b) { return a.last_write_time(ec) < b.last_write_time(ec); });
        size_t count = files.size();
        for (auto &f : files) {
            if (count <= 128 && bytes <= 32 * 1024 * 1024)
                break;
            auto size = f.file_size(ec);
            fs::remove(f.path(), ec);
            if (!ec) {
                --count;
                bytes -= std::min(bytes, size);
            }
        }
    }
    void run() {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        while (!stopping) {
            std::shared_ptr<Music> song;
            int source;
            std::wstring base;
            bool erase;
            uint64_t version;
            {
                std::unique_lock lock(mutex);
                cv.wait(lock, [&] { return stopping || pending; });
                if (stopping)
                    break;
                song = std::move(pending);
                source = mode;
                base = api;
                erase = clear;
                version = generation;
            }
            auto valid = [&] { return !stopping && generation == version; };
            std::shared_ptr<Lyrics> lyric;
            std::wstring message = L"暂无歌词";
            bool transient = false;
            auto requestTag = std::to_string(version);
            monitor.event("歌词开始；请求=" + requestTag + "；播放器=" + utf8(song->platform) +
                          "；信息来源=" + utf8(song->infoSource) + "；timeline=" +
                          (song->timeline ? "1" : "0") + "；duration_s=" + std::to_string(song->duration) +
                          "；数据=" + (song->testSource ? "test" : "real") + "；session=" + song->sessionTag +
                          "；song=" + song->songTag);
            try {
                auto legacy = store / (sha256(utf8(song->key())) + ".lrc");
                auto cache = store / L"api-cache" /
                             (sha256(utf8(song->title + L"\n" + song->artist + L"\n" + song->album) +
                                     std::to_string((int)song->duration) +
                                     (base == L"https://lrclib.net" ? "" : utf8(base))) +
                              ".json");
                if (erase) {
                    std::error_code ec;
                    fs::remove(legacy, ec);
                    fs::remove(cache, ec);
                    message = L"已清除当前歌词";
                } else if (source == 2)
                    message = L"歌词已关闭";
                else {
                    auto raw = readFile(legacy);
                    if (!raw.empty()) {
                        lyric = Lyrics::parse(raw);
                        if (useful(lyric)) {
                            lyric->provider = L"本地导入";
                            lyric->format = L"LRC";
                            prepareLyric(lyric);
                            message = L"手动载入的歌词";
                        } else
                            lyric.reset();
                    }
                    if (!lyric && source != 1) {
                        auto cached = matchedApiLyrics(readFile(cache), *song);
                        bool permitted =
                            cached &&
                            (source == 0 || (source == 3 && cached->provider.starts_with(L"网易云")) ||
                             (source == 5 && cached->provider.starts_with(L"QCloudMusicApi")) ||
                             (source == 4 && cached->provider == L"LRCLIB 搜索"));
                        if (permitted) {
                            lyric = cached;
                            message = L"已校验缓存 · " + cached->provider;
                        }
                        monitor.event("歌词缓存；请求=" + requestTag + "；匹配=" + (lyric ? "1" : "0"));
                    }
                    if (!lyric && !song->title.empty() && !song->artist.empty() && !song->loading &&
                        valid()) {
                        std::string body, cacheBody, plainCache;
                        std::shared_ptr<Lyrics> plain;
                        auto accept = [&](std::shared_ptr<Lyrics> l, const JsonObject &metadata,
                                          const std::wstring &provider) {
                            monitor.event("歌词匹配；请求=" + requestTag + "；来源=" + utf8(provider) +
                                          "；结果=" + (l ? "accepted" : "metadata/format/missing"));
                            if (!l)
                                return;
                            l->provider = provider;
                            l->recordingDuration = metadata.GetNamedNumber(L"duration", 0);
                            auto serialized = lyricCache(metadata, l);
                            if (useful(l)) {
                                lyric = l;
                                cacheBody = serialized;
                            } else if (!plain) {
                                plain = l;
                                plainCache = serialized;
                            }
                        };
                        if (source == 0 || source == 1) {
                            auto r =
                                fetch(base, lrclibPath(*song, false), "LRCLIB exact", version, transient);
                            if (r.ok() && !r.body.empty())
                                try {
                                    accept(matchedApiLyrics(r.body, *song), JsonObject::Parse(wide(r.body)),
                                           L"LRCLIB 精确");
                                } catch (...) {
                                    monitor.event("歌词解析失败；请求=" + requestTag + "；来源=LRCLIB exact；原因=invalid_json");
                                }
                        }
                        if (!lyric && (source == 0 || source == 3 || source == 5) && valid()) {
                            auto r = fetch(L"https://music.163.com",
                                           L"/api/search/get?s=" +
                                               urlEncode(trim(song->title) + L" " + trim(song->artist)) +
                                               L"&type=1&limit=30&offset=0",
                                           "NetEase search", version, transient);
                            std::string reason;
                            auto record =
                                r.ok() ? selectNeteaseSong(r.body, *song, reason) : std::string{};
                            monitor.event("网易云搜索匹配；请求=" + requestTag +
                                          "；结果=" + (reason.empty() ? "request_failed" : reason));
                            if (!record.empty()) {
                                auto metadata = JsonObject::Parse(wide(record));
                                auto id = std::to_wstring((long long)metadata.GetNamedNumber(L"id"));
                                for (int newer = 0; newer < 2 && !lyric && valid(); ++newer) {
                                    auto path = std::wstring(newer ? L"/api/song/lyric/v1?id="
                                                                   : L"/api/song/lyric?id=") +
                                                id + L"&lv=-1&kv=-1&tv=-1&rv=-1&yv=-1";
                                    auto reply =
                                        fetch(L"https://music.163.com", path,
                                              newer ? "NetEase v1" : "NetEase standard", version, transient, source == 5);
                                    if (reply.ok())
                                        accept(parseNeteaseLyrics(reply.body, *song), metadata,
                                               reply.qcloud ? (newer ? L"QCloudMusicApi 新版" : L"QCloudMusicApi 普通") :
                                                   (newer ? L"网易云新版" : L"网易云普通"));
                                }
                            }
                        }
                        // Exactly one candidate search per generation. A failed or
                        // rate-limited exact query sets the service backoff above.
                        if (!lyric && (source == 0 || source == 1 || source == 4) && valid()) {
                            auto r =
                                fetch(base, lrclibPath(*song, true), "LRCLIB search", version, transient);
                            body = r.ok() ? selectSearch(r.body, *song) : std::string{};
                            if (!body.empty())
                                accept(matchedApiLyrics(body, *song), JsonObject::Parse(wide(body)),
                                       L"LRCLIB 搜索");
                            else
                                monitor.event("LRCLIB 候选未匹配/歧义/请求失败；请求=" + requestTag);
                        }
                        if (!valid()) {
                            monitor.event("歌词取消旧请求；请求=" + requestTag);
                            continue;
                        }
                        if (!lyric && plain) {
                            lyric = plain;
                            cacheBody = plainCache;
                        }
                        if (lyric) {
                            message = lyric->provider + L" · " + lyric->format;
                            if (!useful(lyric))
                                message += L"（纯文本，无同步时间戳）";
                            try {
                                writeAtomic(cache, cacheBody);
                                pruneCache();
                            } catch (...) {
                                monitor.event("歌词缓存写入失败；在线结果仍可用");
                            }
                        } else
                            message = transient ? L"暂无歌词（网络/限流，稍后有限重试）"
                                                : L"暂无歌词（无匹配录音或可用歌词）";
                    } else if (!lyric && song->artist.empty())
                        message = L"暂无歌词（缺少歌手，不能可靠匹配）";
                }
            } catch (...) {
                message = L"暂无歌词（读取或格式异常）";
                monitor.event("歌词异常；请求=" + requestTag);
            }
            if (!valid()) { monitor.event("歌词取消旧请求；请求=" + requestTag + "；原因=superseded_or_shutdown"); continue; }
            monitor.event("歌词完成；请求=" + requestTag +
                          "；来源=" + (lyric ? utf8(lyric->provider) : "none") +
                          "；格式=" + (lyric ? utf8(lyric->format) : "none") + "；时间戳歌词=" +
                          (lyric && useful(lyric) ? "1" : "0") + "；行数=" +
                          std::to_string(lyric ? lyric->lines.size() : 0) + "；结果=" + utf8(message));
            std::lock_guard lock(mutex);
            if (valid()) {
                result = lyric;
                status = message;
                resultState = source == 2 ? "disabled" : erase ? "cleared"
                              : lyric ? (useful(lyric) ? "timed" : "plain")
                              : transient ? "network_unavailable" : "no_match";
                retryAt = transient && !useful(lyric) && retries < 2 ? now() + 60 : 0;
            }
        }
        winrt::uninit_apartment();
    }
};
LyricProvider::LyricProvider(const fs::path &path, bool diagnostic)
    : impl(std::make_unique<Impl>(path, diagnostic)) {}
LyricProvider::~LyricProvider() = default;
void LyricProvider::request(const std::shared_ptr<Music> &song, int mode, bool force, bool clear,
                            const std::wstring &api) {
    std::lock_guard lock(impl->mutex);
    auto key = song ? song->key() : L"";
    auto identity = key;
    bool same = identity == impl->requestIdentity && mode == impl->mode && api == impl->api;
    // Position/timeline availability is presentation state, not song identity.
    // Revalidate only genuinely new matching evidence, never loss/recovery alone.
    if (same && song && knownDuration(*song)) {
        bool mismatch = impl->result && impl->result->recordingDuration > 0 &&
                        std::abs(impl->result->recordingDuration - song->duration) > 3;
        bool enriched = impl->resultState == "no_match" && impl->requestedDuration <= 0;
        if (mismatch || enriched) same = false;
    }
    bool retry = same && !force && impl->retryAt && now() >= impl->retryAt;
    if (!force && same && !retry)
        return;
    if (!same || force)
        impl->retries = 0;
    if (retry)
        ++impl->retries;
    impl->retryAt = 0;
    impl->requestIdentity = identity;
    impl->api = api;
    ++impl->generation;
    impl->key = key;
    impl->mode = mode;
    impl->requestedDuration = song ? song->duration : 0;
    impl->clear = clear;
    if (!retry)
        impl->result.reset();
    impl->status = mode == 2 ? L"歌词已关闭" : L"正在获取歌词";
    impl->resultState = mode == 2 ? "disabled" : "loading";
    impl->pending = song && !song->loading ? song : nullptr;
    impl->cv.notify_one();
}
void LyricProvider::attach(Music &song) {
    std::lock_guard lock(impl->mutex);
    song.lyrics.reset();
    song.lyricStatus.clear();
    if (song.key() == impl->key) {
        song.lyrics = impl->result;
        song.lyricStatus = impl->status;
        song.lyricRequest = impl->generation;
        song.lyricState = impl->resultState == "timed" ? (song.timeline ? "synced" : "no_progress")
                                                        : impl->resultState;
        if (song.lyricState == "no_progress")
            song.lyricStatus += song.timelineReason=="timeline_report_stale" ? L"；播放进度已过期，歌词保留为静态显示"
                                                                          : L"；无播放器进度，同步不可用";
    }
}
} // namespace wi


