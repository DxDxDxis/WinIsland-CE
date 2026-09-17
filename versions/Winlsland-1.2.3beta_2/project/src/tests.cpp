#include "core.h"
namespace wi {
int selfTest(const fs::path &output) {
    std::vector<std::string> checks;
    auto check = [&](bool ok, const char *name) {
        if (!ok)
            throw std::runtime_error(name);
        checks.push_back(std::string("PASS: ") + name);
    };
    try {
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
        writeAtomic(p, "<WinIsland><Resident>false</Resident><HideNative>true</HideNative><DwellSeconds>3</"
                       "DwellSeconds></WinIsland>");
        loaded = Settings{};
        loaded.load(p);
        check(!loaded.resident && loaded.hideNative && loaded.seconds == 3 && loaded.fps == 0,
              "Legacy settings use default system cadence");
        fs::remove(p);
        double last = 0;
        for (int i = 0; i <= 1000; i++) {
            double v = ease(i / 1000.);
            check(v >= last && v <= 1, "bounded easing");
            last = v;
        }
        checks.erase(checks.begin() + 4, checks.end());
        checks.push_back("PASS: easing is continuous, bounded and monotonic");
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
