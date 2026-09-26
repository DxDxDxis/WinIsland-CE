#include "clipboard.h"
#include <wincrypt.h>
#include <cwctype>
namespace wi {
namespace {
using namespace winrt::Windows::Data::Json;
void put(JsonObject& o,const wchar_t* k,const std::wstring& v){o.Insert(k,JsonValue::CreateStringValue(v));}
void put(JsonObject& o,const wchar_t* k,bool v){o.Insert(k,JsonValue::CreateBooleanValue(v));}
void put(JsonObject& o,const wchar_t* k,double v){o.Insert(k,JsonValue::CreateNumberValue(v));}
std::wstring str(const JsonObject& p,const wchar_t* k){return std::wstring(p.GetNamedString(k,L""));}
std::wstring lower(std::wstring s){std::transform(s.begin(),s.end(),s.begin(),[](wchar_t c){return (wchar_t)towlower(c);});return s;}
std::wstring newId(){GUID g{};if(FAILED(CoCreateGuid(&g)))throw std::runtime_error("无法创建记录 ID");wchar_t s[40];StringFromGUID2(g,s,40);return s;}
size_t characters(const std::wstring& s){size_t n=0;for(auto c:s)if(c<0xdc00||c>0xdfff)++n;return n;}
JsonObject entryJson(const ClipboardEntry& e,bool full){JsonObject o;put(o,L"id",e.id);put(o,L"text",full?e.text:e.text.substr(0,240));put(o,L"created",e.created);put(o,L"characters",double(characters(e.text)));put(o,L"source",e.source);put(o,L"modified",e.modified);put(o,L"translated",!e.translation.empty());put(o,L"version",double(e.version));put(o,L"target",e.target);if(full){put(o,L"original",e.original);put(o,L"translation",e.translation);}return o;}
JsonObject prefsJson(const ClipboardPreferences& p){JsonObject o;put(o,L"listening",p.listening);put(o,L"show",p.show);put(o,L"save",p.save);put(o,L"clearOnExit",p.clearOnExit);put(o,L"maximum",double(p.maximum));put(o,L"excluded",p.excluded);put(o,L"target",p.target);put(o,L"translationEndpoint",p.translationEndpoint);put(o,L"translationOffline",p.translationOffline);return o;}
ClipboardPreferences parsePrefs(const JsonObject& o){ClipboardPreferences p;p.listening=o.GetNamedBoolean(L"listening",true);p.show=o.GetNamedBoolean(L"show",true);p.save=o.GetNamedBoolean(L"save",true);p.clearOnExit=o.GetNamedBoolean(L"clearOnExit",false);auto n=o.GetNamedNumber(L"maximum",20);if(!std::isfinite(n)||n<4||n>200||floor(n)!=n)throw std::runtime_error("历史数量必须为 4–200 的整数");p.maximum=(int)n;p.excluded=o.GetNamedString(L"excluded",p.excluded);p.target=o.GetNamedString(L"target",L"auto");p.translationEndpoint=o.GetNamedString(L"translationEndpoint",p.translationEndpoint);p.translationOffline=o.GetNamedBoolean(L"translationOffline",false);if(p.excluded.size()>4096||p.translationEndpoint.size()>1024)throw std::runtime_error("翻译服务地址过长");return p;}
void checkText(const std::wstring& s){if(s.size()>ClipboardTextLimit||s.find(L'\0')!=s.npos)throw std::runtime_error("文本超过 262144 个 UTF-16 单元或包含空字符；未截断保存");}
std::string protect(const std::string& data,bool decrypt){DATA_BLOB in{(DWORD)data.size(),(BYTE*)data.data()},out{};BOOL ok=decrypt?CryptUnprotectData(&in,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&out):CryptProtectData(&in,L"WinIsland clipboard",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&out);if(!ok)throw std::runtime_error("剪贴板历史加密或解密失败");std::string result((char*)out.pbData,out.cbData);SecureZeroMemory(out.pbData,out.cbData);LocalFree(out.pbData);return result;}
std::wstring processName(HWND h){DWORD pid=0;GetWindowThreadProcessId(h,&pid);auto p=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(!p)return {};wchar_t name[32768];DWORD size=32768;std::wstring result;if(QueryFullProcessImageNameW(p,0,name,&size))result=fs::path(name).filename().wstring();CloseHandle(p);return result;}
bool excluded(const std::wstring& name,const std::wstring& list){std::wistringstream in(lower(list));std::wstring part;while(std::getline(in,part,L';')){part.erase(0,part.find_first_not_of(L" \r\n\t"));auto end=part.find_last_not_of(L" \r\n\t");if(end!=part.npos)part.resize(end+1);if(!part.empty()&&part==lower(name))return true;}return false;}
void trim(ClipboardSnapshot& s){size_t size=0;size_t keep=0;for(auto& e:s.entries){size+=e.text.size()+e.original.size()+e.translation.size();if(keep>=(size_t)s.preferences.maximum||size>8*1024*1024)break;++keep;}s.entries.resize(keep);}
}
ClipboardStore::ClipboardStore(const fs::path& root,HWND w,bool listen):file(root/L"clipboard"/L"history.dat"),owner(w){
    if(fs::exists(file))try{auto o=JsonObject::Parse(wide(protect(readFile(file,64*1024*1024),true)));if(o.GetNamedNumber(L"format")!=1)throw std::runtime_error("历史格式不支持");state.preferences=parsePrefs(o.GetNamedObject(L"preferences"));
        if(state.preferences.save&&!state.preferences.clearOnExit)for(auto value:o.GetNamedArray(L"entries")){auto j=value.GetObject();ClipboardEntry e;e.id=str(j,L"id");e.text=str(j,L"text");e.original=str(j,L"original");e.translation=str(j,L"translation");e.target=str(j,L"target");e.source=str(j,L"source");e.created=j.GetNamedNumber(L"created");e.version=(uint64_t)j.GetNamedNumber(L"version",1);e.modified=j.GetNamedBoolean(L"modified",false);checkText(e.text);checkText(e.original);checkText(e.translation);if(e.id.empty()||std::any_of(state.entries.begin(),state.entries.end(),[&](auto& x){return x.id==e.id;}))throw std::runtime_error("历史 ID 无效");state.entries.push_back(e);if(state.entries.size()>200)throw std::runtime_error("历史索引超出上限");}trim(state);
    }catch(...){state.entries.clear();state.error=L"历史无法读取或解密，原文件已保留；暂停监听。请备份后在管理页面明确重置。";state.preferences.listening=false;recoveryRequired=true;}
    state.revision=1;
    if(listen){ready=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!ready)throw std::runtime_error("剪贴板事件创建失败");thread=std::thread([this]{loop();});WaitForSingleObject(ready,INFINITE);CloseHandle(ready);ready=nullptr;}
}
ClipboardStore::~ClipboardStore(){stopping=true;cancelTranslation=true;translations.finish();if(listener)PostMessageW(listener,WM_CLOSE,0,0);if(thread.joinable())thread.join();}
void ClipboardStore::loop(){winrt::init_apartment(winrt::apartment_type::multi_threaded);WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"WinIsland.Clipboard.Listener";RegisterClassW(&wc);listener=CreateWindowW(wc.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,wc.hInstance,this);
    if(listener&&state.preferences.listening)registered=AddClipboardFormatListener(listener)!=FALSE;
    if(!listener||(state.preferences.listening&&!registered)){std::lock_guard lock(mutex);state.error=L"无法注册系统剪贴板监听";state.preferences.listening=false;}
    SetEvent(ready);if(listener){MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}}winrt::uninit_apartment();}
LRESULT CALLBACK ClipboardStore::proc(HWND h,UINT msg,WPARAM wp,LPARAM lp){auto self=(ClipboardStore*)GetWindowLongPtrW(h,GWLP_USERDATA);if(msg==WM_NCCREATE){self=(ClipboardStore*)((CREATESTRUCTW*)lp)->lpCreateParams;SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)self);}if(!self)return DefWindowProcW(h,msg,wp,lp);
    if(msg==WM_CLIPBOARDUPDATE){self->retries=0;SetTimer(h,1,35,nullptr);return 0;}if(msg==WM_TIMER){KillTimer(h,1);self->capture();return 0;}
    if(msg==WM_APP+1){std::lock_guard lock(self->mutex);if(self->state.preferences.listening&&!self->registered){self->lastSequence=GetClipboardSequenceNumber();self->registered=AddClipboardFormatListener(h)!=FALSE;if(!self->registered){self->state.error=L"无法恢复剪贴板监听";++self->state.revision;}}else if(!self->state.preferences.listening&&self->registered){RemoveClipboardFormatListener(h);self->registered=false;KillTimer(h,1);}return 0;}
    if(msg==WM_CLOSE){if(self->registered)RemoveClipboardFormatListener(h);KillTimer(h,1);DestroyWindow(h);return 0;}if(msg==WM_DESTROY){PostQuitMessage(0);return 0;}return DefWindowProcW(h,msg,wp,lp);}
void ClipboardStore::capture(){if(stopping)return;std::wstring text,source;DWORD seq=0;
    {std::lock_guard lock(mutex);if(!state.preferences.listening||recoveryRequired)return;seq=GetClipboardSequenceNumber();if(seq==ownSequence||seq==lastSequence||GetClipboardOwner()==listener)return;
        source=processName(GetForegroundWindow());auto writer=processName(GetClipboardOwner());if(excluded(source,state.preferences.excluded)||excluded(writer,state.preferences.excluded))return;if(!writer.empty())source=writer;
        if(!OpenClipboard(listener)){if(++retries<=10)SetTimer(listener,1,50,nullptr);return;}bool ignore=IsClipboardFormatAvailable(RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing"));
        if(!ignore&&IsClipboardFormatAvailable(CF_UNICODETEXT)){auto mem=GetClipboardData(CF_UNICODETEXT);auto size=GlobalSize(mem);if(size&&size<=(ClipboardTextLimit+1)*sizeof(wchar_t)){auto p=(wchar_t*)GlobalLock(mem);if(p){size_t n=0;while(n<size/2&&p[n])++n;if(n<size/2)text.assign(p,n);GlobalUnlock(mem);}}else{state.error=L"剪贴板文本过大，已忽略；原剪贴板不受影响";++state.revision;}}
        lastSequence=seq;CloseClipboard();}
    if(!text.empty())try{captureText(text,source);}catch(...){std::lock_guard lock(mutex);state.error=L"历史写入失败，原索引未覆盖；请检查数据盘";++state.revision;}
}
void ClipboardStore::persistLocked(){if(recoveryRequired)throw std::runtime_error("历史损坏，需明确重置后才能写入");JsonObject o;put(o,L"format",1.);o.Insert(L"preferences",prefsJson(state.preferences));JsonArray entries;if(state.preferences.save&&!state.preferences.clearOnExit)for(auto& e:state.entries)entries.Append(entryJson(e,true));o.Insert(L"entries",entries);writeAtomic(file,protect(utf8(o.Stringify().c_str()),false));}
ClipboardSnapshot ClipboardStore::snapshot(bool full){std::lock_guard lock(mutex);auto result=state;if(!full)for(auto& e:result.entries){e.text=e.text.substr(0,240);e.original.clear();e.translation.clear();}return result;}
ClipboardEntry& ClipboardStore::findLocked(const std::wstring& id){auto it=std::find_if(state.entries.begin(),state.entries.end(),[&](auto& e){return e.id==id;});if(it==state.entries.end())throw std::runtime_error("记录已删除，请刷新");return *it;}
void ClipboardStore::captureText(const std::wstring& text,const std::wstring& source){checkText(text);if(text.empty())return;std::lock_guard lock(mutex);if(!state.preferences.listening||recoveryRequired)return;if(!state.entries.empty()&&state.entries.front().text==text)return;auto before=state;ClipboardEntry e;e.id=newId();e.text=text;e.source=source;e.created=std::chrono::duration<double,std::milli>(std::chrono::system_clock::now().time_since_epoch()).count();state.entries.insert(state.entries.begin(),e);trim(state);state.error.clear();try{persistLocked();}catch(...){state=std::move(before);throw;}++state.revision;if(owner)PostMessageW(owner,ClipboardChangedMessage,0,0);}
void ClipboardStore::copyText(const std::wstring& text){checkText(text);auto mem=GlobalAlloc(GMEM_MOVEABLE,(text.size()+1)*sizeof(wchar_t));if(!mem)throw std::runtime_error("剪贴板内存不足");auto p=GlobalLock(mem);if(!p){GlobalFree(mem);throw std::runtime_error("剪贴板锁定失败");}memcpy(p,text.c_str(),(text.size()+1)*sizeof(wchar_t));GlobalUnlock(mem);std::lock_guard lock(mutex);if(!OpenClipboard(listener?listener:owner)){GlobalFree(mem);throw std::runtime_error("剪贴板正忙，请重试");}if(!EmptyClipboard()||!SetClipboardData(CF_UNICODETEXT,mem)){CloseClipboard();GlobalFree(mem);throw std::runtime_error("剪贴板写入失败");}CloseClipboard();ownSequence=GetClipboardSequenceNumber();}
void ClipboardStore::copy(const std::wstring& id){std::wstring text;{std::lock_guard lock(mutex);text=findLocked(id).text;}copyText(text);}
void ClipboardStore::requestView(const std::wstring& id){std::lock_guard lock(mutex);state.selected=id;++state.viewRequest;++state.revision;}
void ClipboardStore::setProvider(std::unique_ptr<TranslationProvider> p){std::lock_guard lock(providerMutex);provider=std::move(p);}
ClipboardJson ClipboardStore::command(const std::string& action,const JsonObject& p){
    JsonObject result;put(result,L"ok",true);auto id=str(p,L"id");
    if(action=="copy"||action=="copy-text"){if(action=="copy")copy(id);else copyText(str(p,L"text"));return result;}
    if(action=="cancel-translation"){cancel();return result;}
    std::shared_ptr<TranslationProvider> activeProvider;{std::lock_guard lock(providerMutex);activeProvider=provider;}
    if(action.rfind("translation-model-",0)==0){if(!activeProvider)throw std::runtime_error("翻译提供者不可用");return activeProvider->command(action,p);}
    if(action=="translate-start"){
        {std::lock_guard lock(translationStateMutex);auto request=str(p,L"requestId");if(request.empty())throw std::runtime_error("缺少翻译请求 ID");if(request==str(translationState,L"requestId")){auto existing=JsonObject::Parse(translationState.Stringify());put(existing,L"ok",true);return existing;}if(translationBusy)throw std::runtime_error("翻译正在进行，请先取消");if(!activeProvider)throw std::runtime_error("翻译服务未配置；未发送任何文本。需要接入 TranslationProvider 接口。");translationState=JsonObject{};put(translationState,L"requestId",request);put(translationState,L"pending",true);translationBusy=true;cancelTranslation=false;}
        translations.post([this,p]{try{auto reply=command("translate",p);std::lock_guard lock(translationStateMutex);translationState=reply;put(translationState,L"pending",false);translationBusy=false;}catch(const std::exception& e){std::lock_guard lock(translationStateMutex);put(translationState,L"error",wide(e.what()));put(translationState,L"pending",false);translationBusy=false;}catch(...){std::lock_guard lock(translationStateMutex);put(translationState,L"error",std::wstring(L"翻译服务请求失败"));put(translationState,L"pending",false);translationBusy=false;}});
        put(result,L"requestId",str(p,L"requestId"));put(result,L"pending",true);return result;
    }
    if(action=="translation-read"){std::lock_guard lock(translationStateMutex);if(str(p,L"requestId")!=str(translationState,L"requestId"))throw std::runtime_error("翻译请求已过期");auto r=JsonObject::Parse(translationState.Stringify());put(r,L"ok",true);return r;}
    if(action=="translate"){
        std::unique_lock task(translationMutex,std::try_to_lock);if(!task.owns_lock())throw std::runtime_error("翻译正在进行，请先取消");if(!activeProvider)throw std::runtime_error("翻译服务未配置；未发送任何文本。需要接入 TranslationProvider 接口。可继续复制、编辑或导出原文。");
        {std::lock_guard lock(mutex);activeProvider->configure(state.preferences);}
        ClipboardEntry entry;{std::lock_guard lock(mutex);entry=findLocked(id);}TranslationRequest q;q.id=str(p,L"requestId");q.text=p.HasKey(L"text")?str(p,L"text"):entry.text;checkText(q.text);q.source=str(p,L"source");q.target=str(p,L"target");if(q.id.empty())throw std::runtime_error("缺少翻译请求 ID");if(q.source.empty())q.source=L"auto";if(q.target.empty()||q.target==L"auto"){bool chinese=std::any_of(q.text.begin(),q.text.end(),[](wchar_t c){return c>=0x3400&&c<=0x9fff;});q.target=chinese?L"en":L"zh-Hans";}
        const std::set<std::wstring> languages{L"zh-Hans",L"zh-Hant",L"en",L"en-US",L"ja",L"ko",L"ru",L"hi",L"de"};if(!languages.contains(q.target))throw std::runtime_error("目标语言不支持");auto translated=activeProvider->translate(q,cancelTranslation);if(cancelTranslation||stopping||translated.requestId!=q.id)throw std::runtime_error("翻译已取消或响应已失效");checkText(translated.text);
        std::lock_guard lock(mutex);auto& current=findLocked(id);if(current.version!=entry.version)throw std::runtime_error("原文已修改，旧翻译结果已丢弃");if(translated.text.empty())throw std::runtime_error("翻译没有返回有效文本");put(result,L"translation",translated.text);put(result,L"requestId",q.id);put(result,L"target",q.target);
        auto before=state;
        try{
            // Keep the existing export metadata, but make the result a normal,
            // separate history item. Explicit translation works with listening off.
            if(q.text==current.text){current.translation=translated.text;current.target=q.target;++current.version;}
            put(result,L"sourceVersion",double(current.version));
            ClipboardEntry output;output.id=newId();output.text=translated.text;output.target=q.target;output.source=L"快捷翻译 · "+q.target;output.created=std::chrono::duration<double,std::milli>(std::chrono::system_clock::now().time_since_epoch()).count();
            put(result,L"entryId",output.id);state.entries.insert(state.entries.begin(),std::move(output));trim(state);state.error.clear();persistLocked();++state.revision;
        }catch(...){state=std::move(before);throw;}
        if(owner)PostMessageW(owner,ClipboardChangedMessage,1,0);return result;
    }
    std::lock_guard lock(mutex);
    if(action=="list"){JsonArray entries;auto query=lower(str(p,L"query"));for(auto& e:state.entries)if(query.empty()||lower(e.text).find(query)!=std::wstring::npos)entries.Append(entryJson(e,false));result.Insert(L"entries",entries);result.Insert(L"preferences",prefsJson(state.preferences));put(result,L"revision",double(state.revision));put(result,L"viewRequest",double(state.viewRequest));put(result,L"selected",state.selected);put(result,L"error",state.error);put(result,L"recoveryRequired",recoveryRequired);put(result,L"translationAvailable",activeProvider!=nullptr);return result;}
    if(action=="get"){result.Insert(L"entry",entryJson(findLocked(id),true));return result;}
    auto before=state;
    try {
        if(action=="preferences"){auto candidate=parsePrefs(p.GetNamedObject(L"preferences"));if(candidate.maximum<state.preferences.maximum&&state.entries.size()>(size_t)candidate.maximum&&!p.GetNamedBoolean(L"confirmed",false))throw std::runtime_error("降低上限将移除超出的最旧记录，请确认");state.preferences=candidate;trim(state);}
        else if(action=="save"){auto& e=findLocked(id);if(p.GetNamedNumber(L"version")!=double(e.version))throw std::runtime_error("记录已更新，请重新打开；当前草稿仍保留");auto text=str(p,L"text");checkText(text);if(!e.modified)e.original=e.text;e.text=text;e.modified=true;e.translation.clear();e.target.clear();++e.version;}
        else if(action=="delete"){std::set<std::wstring> ids;if(!id.empty())ids.insert(id);if(p.HasKey(L"ids"))for(auto v:p.GetNamedArray(L"ids"))ids.insert(std::wstring(v.GetString()));std::erase_if(state.entries,[&](auto& e){return ids.contains(e.id);});}
        else if(action=="clear"){if(!p.GetNamedBoolean(L"confirmed",false))throw std::runtime_error("清空历史需要明确确认");state.entries.clear();}
        else if(action=="reset-corrupt"){if(!recoveryRequired||!p.GetNamedBoolean(L"confirmed",false))throw std::runtime_error("需要明确确认损坏索引重置");fs::copy_file(file,file.wstring()+L".corrupt-"+std::to_wstring(GetTickCount64()));recoveryRequired=false;state={};state.revision=before.revision;}
        else throw std::runtime_error("未知复制粘贴操作");
        persistLocked();++state.revision;state.error.clear();if(listener&&action=="preferences")PostMessageW(listener,WM_APP+1,0,0);
    }catch(...){state=std::move(before);throw;}
    if(owner)PostMessageW(owner,ClipboardChangedMessage,1,0);return result;
}
}
