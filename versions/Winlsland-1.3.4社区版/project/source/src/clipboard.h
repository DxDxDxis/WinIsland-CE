#pragma once
#include "core.h"
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
namespace wi {
using ClipboardJson = winrt::Windows::Data::Json::JsonObject;
inline constexpr UINT ClipboardChangedMessage = WM_APP + 73;
inline constexpr size_t ClipboardTextLimit = 256 * 1024;
struct ClipboardEntry {
    std::wstring id,text,original,translation,target,source;
    double created=0; uint64_t version=1; bool modified=false;
};
struct ClipboardPreferences {
    bool listening=true,show=true,save=true,clearOnExit=false;
    int maximum=20;
    std::wstring excluded=L"1password.exe;keepass.exe;keepassxc.exe;bitwarden.exe",target=L"auto";
    // Translation is opt-in at the provider level.  The endpoint is persisted
    // with the clipboard preferences so a user can point the client at a
    // private server without changing the host binary.
    std::wstring translationEndpoint=L"https://154-219-99-106.sslip.io/translation/translate";
    bool translationOffline=false;
};
struct ClipboardSnapshot {
    uint64_t revision=0,viewRequest=0; std::wstring selected,error;
    ClipboardPreferences preferences; std::vector<ClipboardEntry> entries;
};
// A replaceable, host-owned service. No provider is silently contacted.
struct TranslationRequest { std::wstring id,text,source=L"auto",target; };
struct TranslationResult { std::wstring requestId,text,detectedLanguage; };
struct TranslationProvider {
    virtual ~TranslationProvider()=default;
    virtual void configure(const ClipboardPreferences&) {}
    virtual ClipboardJson command(const std::string&,const ClipboardJson&){throw std::runtime_error("此翻译提供者不支持模型管理");}
    virtual TranslationResult translate(const TranslationRequest&,const std::atomic_bool& cancelled)=0;
};
std::unique_ptr<TranslationProvider> makeHttpTranslationProvider(const fs::path& dataRoot);
class ClipboardStore {
    fs::path file; HWND owner=nullptr,listener=nullptr; HANDLE ready=nullptr;
    std::thread thread; std::mutex mutex; ClipboardSnapshot state;
    std::shared_ptr<TranslationProvider> provider;
    std::mutex providerMutex;
    std::atomic_bool stopping=false,cancelTranslation=false;
    Jobs translations; std::mutex translationStateMutex; ClipboardJson translationState; bool translationBusy=false,registered=false;
    std::mutex translationMutex; DWORD ownSequence=0,lastSequence=0; int retries=0;
    bool recoveryRequired=false;
    void persistLocked(); void capture(); void loop();
    static LRESULT CALLBACK proc(HWND,UINT,WPARAM,LPARAM);
    ClipboardEntry& findLocked(const std::wstring&);
public:
    ClipboardStore(const fs::path&,HWND owner,bool listen=true);
    ~ClipboardStore();
    ClipboardSnapshot snapshot(bool full=true);
    ClipboardJson command(const std::string&,const ClipboardJson&);
    void captureText(const std::wstring&,const std::wstring& source=L"");
    void copy(const std::wstring&); void copyText(const std::wstring&);
    void requestView(const std::wstring&);
    void setProvider(std::unique_ptr<TranslationProvider>);
    void cancel(){cancelTranslation=true;}
};
int clipboardTest(const fs::path&);
int translationTest(const fs::path&);
}
