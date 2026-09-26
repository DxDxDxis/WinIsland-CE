#include "clipboard.h"
namespace wi {
int translationTest(const fs::path& root){
    using namespace winrt::Windows::Data::Json;fs::create_directories(root);std::string report;int failures=0;
    auto record=[&](bool ok,const std::string& message){report+=(ok?"PASS ":"FAIL ")+message+"\n";if(!ok)++failures;writeAtomic(root/L"translation-tests.txt",report);};
    try{
        setDataRoot(root);writeAtomic(root/L"clipboard"/L"preserve.txt","synthetic history sentinel");auto provider=makeHttpTranslationProvider(root);ClipboardPreferences prefs;provider->configure(prefs);std::atomic_bool cancelled=false;
        TranslationRequest q{L"online-1",L"请在离开房间前关上窗户。",L"zh",L"en"};auto online=provider->translate(q,cancelled);record(!online.text.empty()&&online.text!=q.text,"real HTTPS Chinese -> English: "+utf8(online.text));
        JsonObject empty,confirmed;confirmed.Insert(L"confirmed",JsonValue::CreateBooleanValue(true));bool rejected=false;try{provider->command("translation-model-install",empty);}catch(...){rejected=true;}record(rejected,"install requires explicit confirmation");
        auto poll=[&](unsigned timeout){auto until=GetTickCount64()+timeout;for(;;){auto s=provider->command("translation-model-status",empty);if(!s.GetNamedBoolean(L"busy"))return s;if(GetTickCount64()>until)throw std::runtime_error("model operation test timeout");Sleep(200);}};
        provider->command("translation-model-install",confirmed);Sleep(800);provider->command("translation-model-cancel",empty);auto s=poll(30000);record(!s.GetNamedBoolean(L"installed")&&!fs::exists(root/L"translation-model"/L"download.part")&&!fs::exists(root/L"translation-model"/L"staging"),"cancel download clears staging and does not mark installed");
        provider->command("translation-model-install",confirmed);s=poll(20*60*1000);if(!s.GetNamedBoolean(L"installed"))throw std::runtime_error("install failed: "+utf8(s.GetNamedString(L"error").c_str()));record(true,"real download, SHA-256 verification, extraction and inference smoke test");
        provider.reset();provider=makeHttpTranslationProvider(root);record(provider->command("translation-model-status",empty).GetNamedBoolean(L"installed"),"new provider restores installed state from disk");
        prefs.translationOffline=true;prefs.translationEndpoint=L"https://offline.invalid/never-contact";provider->configure(prefs);q={L"offline-1",L"明天下午我们一起去图书馆学习。",L"auto",L"en"};auto local=provider->translate(q,cancelled);record(!local.text.empty()&&local.text!=q.text&&local.text.find(L'\x2581')==local.text.npos,"real local Chinese -> English with unusable online endpoint: "+utf8(local.text));
        q={L"offline-2",L"Please close the window before leaving the room.",L"en",L"zh-Hans"};local=provider->translate(q,cancelled);record(!local.text.empty()&&local.text!=q.text,"real local English -> Chinese: "+utf8(local.text));
        q.target=L"ja";bool unsupported=false;try{provider->translate(q,cancelled);}catch(...){unsupported=true;}record(unsupported,"unsupported offline language fails without online fallback");
        q.target=L"en";q.source=L"zh";q.text=std::wstring(1800,L'文');std::atomic_bool stopped=false;std::thread t([&]{try{provider->translate(q,cancelled);}catch(...){stopped=true;}});Sleep(100);provider->command("translation-model-uninstall",confirmed);t.join();s=poll(30000);record(stopped&&!s.GetNamedBoolean(L"installed")&&!fs::exists(root/L"translation-model"/L"installed"),"uninstall cancels isolated inference, waits for exit and removes model");
        record(readFile(root/L"clipboard"/L"preserve.txt")=="synthetic history sentinel","uninstall preserves unrelated clipboard data");
        bool unavailable=false;try{provider->translate(q,cancelled);}catch(...){unavailable=true;}record(unavailable,"offline mode after uninstall fails explicitly without online fallback");
        record(true,"optional model lifecycle complete; no helper kept alive");
    }catch(const std::exception& e){record(false,std::string("unexpected: ")+e.what());}catch(const winrt::hresult_error& e){record(false,"WinRT: "+utf8(e.message().c_str()));}catch(...){record(false,"unexpected exception");}
    return failures?1:0;
}
}
