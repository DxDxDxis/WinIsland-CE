#include "core.h"
#include "outline.h"
namespace wi {
int lyricNetworkTest(const fs::path &output) {
    LyricProvider provider(output.parent_path() / L"live-api-cache", false);
    auto music = std::make_shared<Music>();
    music->source = L"PublicApiVerification";
    music->title = L"Yellow";
    music->artist = L"Coldplay";
    music->album = L"Yellow - Single";
    music->timeline = true;
    music->duration = 267;
    provider.request(music, 1);
    double deadline = now() + 15;
    while (now() < deadline) {
        provider.attach(*music);
        if (music->lyrics || music->lyricStatus != L"正在获取歌词")
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    bool ok = music->lyrics && !music->lyrics->lines.empty();
    writeAtomic(
        output,
        std::string(ok ? "PASS" : "FAIL") +
            ": native WinHTTP HTTPS to lrclib.net; exact title/artist and recording duration checked; " +
            utf8(music->lyricStatus) + "\n");
    return ok ? 0 : 1;
}
int selfTest(const fs::path &output) {
    std::vector<std::string> checks;
    auto check = [&](bool ok, const char *name) {
        if (!ok)
            throw std::runtime_error(name);
        checks.push_back(std::string("PASS: ") + name);
    };
    try {
        for (double height : {26., 40., 120., 280.}) {
            for (double dpi : {1., 1.25, 1.5, 2.}) {
                for (double top : {-6., -4., -2., -1., 0., 1., height/2}) {
                    auto path = islandOutline(100*dpi, 380*dpi, height*dpi, height*dpi/2, top*dpi);
                    for (int i=0;i<=100;++i) {
                        auto right=outlineAt(path[0],i/100.);
                        auto left=outlineAt(path[3],1-i/100.);
                        if (std::abs(right.x+left.x-580*dpi)>1e-7 || std::abs(right.y-left.y)>1e-7)
                            throw std::runtime_error("outline symmetry");
                        if (top<=0 && (right.x<480*dpi-1e-7 || left.x>100*dpi+1e-7))
                            throw std::runtime_error("outward shoulder must never cut into body");
                        if (right.y<0 || right.y>std::min(std::abs(top*dpi)*(top<0 ? 1.1 : 1.), height*dpi/2)+1e-7)
                            throw std::runtime_error("shoulder must remain within top band");
                    }
                }
            }
        }
        check(true, "Symmetric outward contour and continuous zero-radius boundary across states and DPI");
        auto arcConfig = output.parent_path() / L"arc-settings.xml";
        Settings arc;
        writeAtomic(arcConfig, "<WinIsland><IslandZoom>1.5</IslandZoom></WinIsland>");
        arc.load(arcConfig);
        check(arc.topArcScale==1., "Legacy config uses new fixed arc baseline");
        for (auto invalid : {"-1", "0.49", "1.51", "nan", "inf", "1.2oops"}) {
            writeAtomic(arcConfig, std::string("<WinIsland><TopArcScale>")+invalid+"</TopArcScale></WinIsland>");
            arc.load(arcConfig);
            if (arc.topArcScale!=1.) throw std::runtime_error("invalid arc config accepted");
        }
        check(true, "Invalid and nonfinite arc multipliers rejected");
        for (double scale : {.5, 1., 1.5}) {
            arc.topArcScale=scale; arc.save(arcConfig);
            Settings loaded; loaded.load(arcConfig);
            if (loaded.topArcScale!=scale || loaded.islandZoom!=1.5)
                throw std::runtime_error("arc persistence or unrelated setting changed");
            loaded.resetLayout();
            if (loaded.topArcScale!=1.) throw std::runtime_error("arc reset failed");
        }
        check(true, "Arc endpoints persist and reset without cumulative scaling");
        auto metricTest = std::make_unique<Perf>();
        auto metricFolder = output.parent_path() / L"metric-name-regression";
        metricTest->start(metricFolder);
        metricTest->add(AccessibilityWork, 1.0);
        metricTest->flush();
        auto csv = readFile(metricFolder / L"latest.csv");
        check(csv.find("\nAccessibilityWork,1,") != std::string::npos &&
                  std::count(csv.begin(), csv.end(), '\n') == MetricCount + 3,
              "Performance export names every metric including accessibility without out-of-bounds reads");
        ObservedLyricLine observed;
        check(observed.accept(L"one",L"line 1",true,1,5),"Visible verified lyric is accepted");
        check(!observed.accept(L"one",L"line 1",false,2,5),"Minimization cannot refresh a frozen foreground snapshot");
        check(observed.accept(L"one",L"line 2",false,3,5),"Actual background line changes are accepted without restoring the player");
        check(!observed.accept(L"one",L"line 2",false,9,5),"Frozen background line expires instead of claiming continuous sync");
        check(!observed.accept(L"two",L"line 2",false,10,5),"New song cannot inherit old background current line");
        check(observed.accept(L"two",L"new line",false,11,5),"Background line recovery requires real new evidence");
        check(observed.accept(L"two",L"same",true,12,5),"Restored window reacquires current line");
        Music clockMusic;
        clockMusic.source=L"test";clockMusic.title=L"one";clockMusic.playing=true;
        TimelineAnchor anchor;
        TimelineSample sample{0,180,0,180,10,1000000000,1};
        applyTimeline(clockMusic,sample,anchor,10,1020000000);
        check(clockMusic.timeline && clockMusic.position==12,"Timeline uses 100ns UTC timestamp and seconds position");
        clockMusic.playing=false;
        applyTimeline(clockMusic,sample,anchor,12,1040000000);
        check(clockMusic.position==14,"Pause freezes previous running clock without a new timeline timestamp");
        applyTimeline(clockMusic,sample,anchor,22,1140000000);
        check(clockMusic.position==14,"Paused wall-clock time never advances lyric progress");
        clockMusic.playing=true;
        applyTimeline(clockMusic,sample,anchor,22,1140000000);
        check(clockMusic.position==14,"Resume rebases instead of including the pause interval");
        sample.position=50;sample.updated=1150000000;++sample.revision;
        applyTimeline(clockMusic,sample,anchor,23,1150000000);
        check(clockMusic.position==50,"Seek reanchors immediately from new SMTC timeline");
        sample.end=sample.maximum=0;sample.position=20;sample.updated=1160000000;
        applyTimeline(clockMusic,sample,anchor,24,1160000000);
        check(clockMusic.timeline && clockMusic.duration==0 && clockMusic.position==20,"Reported timestamped position can sync without an end time");
        sample={};applyTimeline(clockMusic,sample,anchor,25,1170000000);
        check(!clockMusic.timeline,"All-zero SMTC timeline is unavailable, never synthesized");
        sample={0,180,0,180,32,1180000000,4};
        applyTimeline(clockMusic,sample,anchor,26,1180000000);
        check(clockMusic.timeline && clockMusic.position==32,"Timeline recovery uses new actual position");
        clockMusic.title=L"two";
        applyTimeline(clockMusic,sample,anchor,27,1190000000);
        check(!clockMusic.timeline && clockMusic.timelineReason=="awaiting_new_track_timeline","Track change rejects stale timeline from previous track");
        applyTimeline(clockMusic,sample,anchor,28,1200000000);
        check(!clockMusic.timeline,"Old unchanged timeline stays unavailable after track change");
        sample.position=0;sample.updated=1210000000;++sample.revision;
        applyTimeline(clockMusic,sample,anchor,29,1210000000);
        check(clockMusic.timeline && clockMusic.position==0,"New track timeline event restores synchronized start");
        Music bounded;bounded.source=L"test";bounded.title=L"bounded";bounded.playing=true;
        TimelineAnchor boundedAnchor;TimelineSample boundedSample{0,180,0,180,10,1000000000,1};
        applyTimeline(bounded,boundedSample,boundedAnchor,100,1000000000);
        applyTimeline(bounded,boundedSample,boundedAnchor,108,1080000000);
        check(bounded.timeline && bounded.position==18 && bounded.timelineExpires==115,"Sparse SMTC updates use a bounded monotonic projection");
        applyTimeline(bounded,boundedSample,boundedAnchor,116,1160000000);
        check(!bounded.timeline && bounded.timelineReason=="timeline_report_stale","Polling identical stale reports never renews progress validity");
        bounded.playing=false;applyTimeline(bounded,boundedSample,boundedAnchor,116.5,1165000000);
        check(!bounded.timeline,"Pausing after a stale report cannot revive an old playback cursor");
        bounded.playing=true;
        boundedSample.position=45;boundedSample.updated=1170000000;++boundedSample.revision;
        applyTimeline(bounded,boundedSample,boundedAnchor,117,1170000000);
        check(bounded.timeline && bounded.position==45,"Fresh timeline after expiry reanchors to actual playback position");
        check(sha256("abc") == "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD",
              "Legacy SHA256 lyric file identity");
        auto l = Lyrics::parse(
            "[offset:500]\n[00:01.00][00:03.00]first\n[00:02.50]second\n[00:04.00]\n[00:65.00]invalid");
        check(l && l->lines.size() == 4 && l->at(0) == L"" && l->at(.5) == L"first" &&
                  l->at(2.1) == L"second" && l->at(2.5) == L"first" && l->at(3.6).empty(),
              "LRC timestamp, offset, repeated tags and empty intervals");
        auto p = output.parent_path() / L"self-settings.xml";
        Settings s;
        s.resident = false;
        s.seconds = .7;
        s.fps = 144;
        s.save(p);
        Settings loaded;
        loaded.load(p);
        check(!loaded.resident && loaded.seconds == .7 && loaded.fps == 144,
              "Settings preserve resident, duration and manual FPS");
        s.islandZoom=2; s.widthRatio=.75; s.heightRatio=1.5; s.dpiCorrection=1.1;
        s.radiusMode=2; s.topAttach=false; s.monitorDevice=L"\\\\.\\DISPLAY2"; s.save(p);
        loaded.load(p);
        check(loaded.islandZoom==2 && loaded.widthRatio==.75 && loaded.heightRatio==1.5 &&
              loaded.dpiCorrection==1.1 && loaded.radiusMode==2 && !loaded.topAttach &&
              loaded.monitorDevice==s.monitorDevice,"Independent layout settings survive XML reload");
        loaded.resetLayout();
        check(loaded.islandZoom==1.5 && loaded.widthRatio==1 && loaded.heightRatio==1 && loaded.topAttach &&
              loaded.monitorDevice.empty() && loaded.fps==144 && !loaded.resident,
              "Restore layout defaults preserves unrelated user settings");
        writeAtomic(p,"<WinIsland><IslandZoom>nan</IslandZoom><WidthRatio>-1</WidthRatio>"
                      "<HeightRatio>999</HeightRatio><DpiCorrection>1junk</DpiCorrection><RadiusMode>9</RadiusMode></WinIsland>");
        loaded.load(p);
        check(loaded.islandZoom==1.5 && loaded.widthRatio==1 && loaded.heightRatio==1 && loaded.dpiCorrection==1 &&
              loaded.radiusMode==0,"Invalid nonfinite and oversized layout values preserve valid defaults");
        writeAtomic(p, "<WinIsland><Resident>false</Resident><HideNative>true</HideNative><DwellSeconds>3</"
                       "DwellSeconds></WinIsland>");
        loaded = Settings{};
        loaded.load(p);
        check(!loaded.resident && loaded.hideNative && loaded.seconds == 3 && loaded.fps == 0 &&
              loaded.islandZoom == 1.5 && loaded.topAttach,
              "Legacy settings use default system cadence");
        fs::remove(p);
        size_t beforeEasing=checks.size();
        double last = 0;
        for (int i = 0; i <= 1000; i++) {
            double v = ease(i / 1000.);
            check(v >= last && v <= 1, "bounded easing");
            last = v;
        }
        checks.resize(beforeEasing);
        checks.push_back("PASS: easing is continuous, bounded and monotonic");
        Music lineMusic;lineMusic.lyrics=Lyrics::parse("[00:00.00]first\n[00:08.00]current");
        lineMusic.playerLyric=L"current";lineMusic.playerLyricIndex=1;lineMusic.playerLyricStamp=now();
        lineMusic.lyricState="player_line";int lineIndex=-1;
        check(lyricDisplay(lineMusic,lineIndex)==L"current" && lineIndex==1 && !lineMusic.timeline && lineMusic.progress()==0,
              "Fresh real player current line displays without inventing a seconds timeline");
        lineMusic.playerLyricStamp=now()-3;
        check(lyricDisplay(lineMusic,lineIndex)!=L"current" && lineIndex==-1,"Expired player line never remains synchronized");
        lineMusic.playerLyricStamp=now();lineMusic.lyricState="disabled";lineMusic.lyrics.reset();
        check(lyricDisplay(lineMusic,lineIndex).empty(),"Disabling lyrics suppresses player-line fallback");
        check(Settings{}.songSource == 0 && Settings{}.lyricSource == 0 && !Settings{}.showFps &&
                  !Settings{}.showPing,
              "New defaults use SMTC-first automatic metadata and cache-first lyrics");
        s.songSource = 2;
        s.lyricSource = 1;
        s.showFps = true;
        s.showPing = true;
        s.pingTarget = L"127.0.0.1";
        s.lyricApi = L"https://example.org/lyrics";
        s.save(p);
        loaded.load(p);
        check(loaded.songSource == 2 && loaded.lyricSource == 1 && loaded.showFps && loaded.showPing &&
                  loaded.pingTarget == L"127.0.0.1" && loaded.lyricApi == s.lyricApi,
              "Data source, telemetry switches and target survive reload");
        fs::remove(p);
        check(validPingTarget(L"") && validPingTarget(L"192.168.1.1") && validPingTarget(L"::1") &&
                  validPingTarget(L"fe80::1%12") && !validPingTarget(L"bad.example") &&
                  !validPingTarget(L"999.1.1.1") && !validPingTarget(L"0.0.0.0"),
              "Ping accepts IPv4/IPv6 literals, scoped link-local and gateway mode");
        check(validLyricApi(L"https://lrclib.net") && validLyricApi(L"https://example.org/lyrics") &&
                  !validLyricApi(L"http://example.org") && !validLyricApi(L"https://user:pass@example.org") &&
                  !validLyricApi(L"https://example.org/?token=secret"),
              "Lyric endpoints require HTTPS and exclude credentials/query fragments");
        check(MusicBarCount == 12, "Real audio history has twelve bars");
        Music title;
        check(parseSongTitle(L"正在播放：歌曲 - 歌手 - 网易云音乐", L"网易云音乐", title) &&
                  title.title == L"歌曲" && title.artist == L"歌手",
              "Window title strips only known player decorations");
        check(parseSongTitle(L"QQ音乐 - 歌曲 - 歌手", L"QQ 音乐", title) &&
                  parseSongTitle(L"歌曲 - 歌手 - 汽水音乐", L"汽水音乐", title),
              "Three platform title formats");
        check(!parseSongTitle(L"网易云音乐", L"网易云音乐", title) &&
                  !parseSongTitle(L"广告 - 立即领取", L"网易云音乐", title) &&
                  !parseSongTitle(L"歌名 - 无法确认 - 更多", L"网易云音乐", title),
              "Invalid or ambiguous player captions are rejected");
        auto bilingual =
            Lyrics::parse("[offset:-500]\n[00:01.00]first\n[00:01.00]translation\n[00:03.00][00:04.00]next");
        check(bilingual->at(1).empty() && bilingual->at(1.5) == L"first / translation" &&
                  bilingual->at(4.6) == L"next",
              "LRC translation lines and signed offset remain synchronized");
        Music api;
        api.title = L"Song";
        api.artist = L"Artist";
        api.timeline = true;
        api.duration = 180;
        std::string json =
            R"({"trackName":"Song","artistName":"Artist","duration":180,"syncedLyrics":"[00:00.00]first\n[00:03.00]second"})";
        check(matchedApiLyrics(json, api) && matchedApiLyrics(json, api)->at(4) == L"second",
              "API validates metadata and parses actual timed lyrics");
        api.album = L"Different compilation";
        check(matchedApiLyrics(json, api) != nullptr,
              "Matching recording duration tolerates different compilation album");
        api.timeline = false;
        api.duration = 0;
        check(!matchedApiLyrics(json, api), "Missing duration requires matching album");
        api.timeline = true;
        api.duration = 180;
        api.album.clear();
        api.artist = L"Other";
        check(!matchedApiLyrics(json, api), "API rejects a different artist");
        api.artist = L"Artist";
        api.duration = 200;
        check(!matchedApiLyrics(json, api), "API rejects a different recording duration");
        check(!matchedApiLyrics("not json", api) && !matchedApiLyrics(R"({"syncedLyrics":null})", api),
              "Malformed/empty API responses cannot leave lyrics");
        api.duration = 180;
        auto yrc = parseNeteaseLyrics(
            R"({"code":200,"lrc":{"lyric":""},"yrc":{"lyric":"[1000,2000](1000,800,0)first (1800,1200,0)line\n[3500,1000](3500,1000,0)second"}})",
            api);
        check(yrc && yrc->format == L"YRC（逐行）" && yrc->at(2) == L"first line" &&
                  yrc->at(3.5) == L"second",
              "YRC empty LRC fallback strips every word tag and preserves millisecond line times");
        auto plain = parseNeteaseLyrics(R"({"code":200,"lrc":{"lyric":"plain first\nplain second"}})", api);
        check(plain && plain->lines.empty() && plain->preview == L"plain first",
              "Plain lyrics have no invented timeline");
        std::string why;
        auto candidate = selectNeteaseSong(
            R"({"code":200,"result":{"songs":[{"id":1,"name":"Song","artists":[{"name":"Artist"}],"duration":210000,"album":{"name":"Other"}},{"id":3000000001,"name":"Song","artists":[{"name":"Artist"}],"duration":180000,"album":{"name":"Album"}}]}})",
            api, why);
        check(candidate.find("3000000001") != candidate.npos && why == "unique_match",
              "Search rejects wrong first recording and preserves 64-bit song ID");
        api.album = L"Album";
        check(
            selectNeteaseSong(
                R"json({"code":200,"result":{"songs":[{"id":1,"name":"Song (Live)","artists":[{"name":"Artist"}],"duration":180000,"album":{"name":"Album"}},{"id":2,"name":"Song","artists":[{"name":"Other"}],"duration":180000,"album":{"name":"Album"}}]}})json",
                api, why)
                .empty(),
            "Cross-catalog live or cover recording cannot match original");
        check(
            selectNeteaseSong(
                R"({"code":200,"result":{"songs":[{"id":1,"name":"Song","artists":[{"name":"Artist"}],"album":{"name":"Album"}}]}})",
                api, why)
                .empty(),
            "Missing candidate duration cannot pass a known timeline");
        check(
            selectNeteaseSong(
                R"({"code":200,"result":{"songs":[{"id":1,"name":"Song","artists":[{"name":"Artist"}],"duration":180000,"album":{"name":"Album"}},{"id":2,"name":"Song","artists":[{"name":"Artist"}],"duration":180000,"album":{"name":"Album"}}]}})",
                api, why)
                    .empty() &&
                why == "ambiguous_recording",
            "Duplicate matching recordings are rejected rather than choosing first");
        Music sparse = api;
        sparse.album.clear();
        sparse.duration = 0;
        sparse.timeline = false;
        auto unique = selectNeteaseSong(
            R"({"code":200,"result":{"songs":[{"id":9,"name":"Song","artists":[{"name":"Artist"}],"duration":180000,"album":{"name":"Album"}}]}})",
            sparse, why);
        check(
            !unique.empty() && why.find("player_album_duration_unavailable") != why.npos,
            "Missing player album/duration permits only unique exact candidate and reports limited evidence");
        check(
            selectNeteaseSong(
                R"({"code":200,"result":{"songs":[{"id":9,"name":"Song","artists":[{"name":"Artist"}],"duration":180000,"album":{"name":"Album"}},{"id":10,"name":"Song","artists":[{"name":"Artist"}],"duration":230000,"album":{"name":"Other"}}]}})",
                sparse, why)
                .empty(),
            "Unknown player duration cannot choose between two exact recordings");
        check(platformOf(L"cloudmusic.exe") == L"网易云音乐" && platformOf(L"QQMusic.exe") == L"QQ 音乐" &&
                  platformOf(L"com.luna.music") == L"汽水音乐" && platformOf(L"QQ.exe").empty(),
              "Supported players exclude QQ chat");
        Music m;
        m.timeline = true;
        m.playing = false;
        m.position = 10;
        m.duration = 20;
        check(m.progress() == 10, "Paused timeline is stable");
        m.playing = true;
        m.stamp = now() - 30;
        check(m.progress() == 20, "Playing timeline clamps at track duration");
        Notice toast;
        std::string xml = "<?xml version=\"1.0\" encoding=\"utf-8\"?><toast><visual><binding "
                          "template=\"ToastGeneric\"><text>test</text><text>body &amp; more</text><text "
                          "placement=\"attribution\">ignored</text></binding></"
                          "visual><actions><input><text>ignored</text></input></actions></toast>";
        check(parseToast(xml, toast) && toast.title == L"test" && toast.body == L"body & more",
              "Toast XML decoding excludes actions and attribution");
        auto unicode = wide(xml);
        std::string utf16((const char *)unicode.data(), unicode.size() * 2);
        utf16.append(2, 0);
        check(parseToast(utf16, toast) && toast.body == L"body & more",
              "Legacy UTF16 notification payload with trailing NUL");
        check(!parseToast(
                  "<!DOCTYPE toast [<!ENTITY x SYSTEM "
                  "'file:///missing'>]><toast><visual><binding><text>&x;</text></binding></visual></toast>",
                  toast),
              "External entities are prohibited");
        check(parseToast("<toast><visual><binding template='ToastText01'><text>old</text></binding><binding "
                         "template='ToastGeneric'><text>preferred</text></binding></visual></toast>",
                         toast) &&
                  toast.title == L"preferred",
              "ToastGeneric chosen once among alternate bindings");
        std::ostringstream o;
        for (auto &c : checks)
            o << c << '\n';
        writeAtomic(output, o.str());
        return 0;
    } catch (const std::exception &e) {
        std::ostringstream o;
        for (auto &c : checks)
            o << c << '\n';
        o << "FAIL: " << e.what() << '\n';
        writeAtomic(output, o.str());
        return 1;
    }
}
} // namespace wi

