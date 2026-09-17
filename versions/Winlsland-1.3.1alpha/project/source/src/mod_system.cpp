#include "mod_system.h"
#include "animation_runtime.h"
#include "media_runtime.h"
#include <winrt/base.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <unordered_set>
namespace wi {
using namespace winrt::Windows::Data::Json;
static bool safeId(const std::string& s) {
    return !s.empty() && s.size()<=80 && std::all_of(s.begin(),s.end(),[](unsigned char c){return std::isalnum(c)||c=='-'||c=='_';});
}
static std::string clean(std::string s) {
    auto value=wide(s);auto first=value.find_first_not_of(L" \t\r\n\v\f\u00a0\u3000");if(first==value.npos)return {};
    return utf8(value.substr(first,value.find_last_not_of(L" \t\r\n\v\f\u00a0\u3000")-first+1));
}
static std::string field(const JsonObject& j,const wchar_t* key,const std::string& fallback={}) {
    if(!j.HasKey(key) || j.GetNamedValue(key).ValueType()!=JsonValueType::String) return fallback;
    auto s=clean(utf8(j.GetNamedString(key).c_str())); return s.empty()?fallback:s;
}
static int cppCall(WinIslandCallback fn,void* ctx,const char* text) { try {return fn?fn(ctx,text):0;} catch(...) {return -1001;} }
static int guarded(WinIslandCallback fn,void* ctx,const char* text) {
    __try { return cppCall(fn,ctx,text); } __except(EXCEPTION_EXECUTE_HANDLER) {return -1002;}
}
static IWinIslandMod* cppCreate(WinIslandCreateMod fn,const WinIslandHostApi* api) {try{return fn(api);}catch(...){return nullptr;}}
static IWinIslandMod* guardedCreate(WinIslandCreateMod fn,const WinIslandHostApi* api) {
    __try{return cppCreate(fn,api);}__except(EXCEPTION_EXECUTE_HANDLER){return nullptr;}
}
static uint32_t guardedAbi(WinIslandModAbi fn) {__try{return fn();}__except(EXCEPTION_EXECUTE_HANDLER){return 0;}}
static bool guardedTable(IWinIslandMod* p) {
    __try {return p && p->size>=sizeof(IWinIslandMod) && p->version==WINISLAND_MOD_ABI && p->destroy;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
static void cppDestroy(IWinIslandMod* p) {try{p->destroy(p->context);}catch(...){}}
static void guardedDestroy(IWinIslandMod* p) {__try{cppDestroy(p);}__except(EXCEPTION_EXECUTE_HANDLER){}}

double ModSnapshot::replacement(const std::string& key,double fallback) const {
    for(auto i=resources.rbegin();i!=resources.rend();++i) if(i->kind==WI_REPLACE&&i->key==key) {
        try{return std::stod(i->value);}catch(...) {return fallback;}
    }
    return fallback;
}
std::string ModSnapshot::text(const std::string& key,std::string fallback) const {for(auto i=resources.rbegin();i!=resources.rend();++i)if(i->kind==WI_REPLACE&&i->key==key)return i->value;return fallback;}
struct ModLoader::Impl {
    struct Record {
        Impl* host=nullptr; ModInfo info; fs::path folder,entry,extractionRoot;
        std::shared_ptr<ModPackage> package;
        HMODULE dll=nullptr; IWinIslandMod* plugin=nullptr; WinIslandHostApi api{};
        bool accepting=false; float alpha=0;
        std::string desired="disabled";
        std::map<std::wstring,std::wstring> settings;
    };
    struct Resource { ModResourceView view; WinIslandCallback callback=nullptr; void* context=nullptr; uint32_t interval=0; double next=0; std::shared_ptr<AnimationRuntime> animation; double animationDuration=.24; uint64_t animationSequence=0; };
    struct Binding { std::string owner; WiElement element; WiInputCallback callback; void* context; };
    std::vector<Binding> bindings;
    struct DrawBinding {std::string owner;WiElement element;WiDrawCallback callback;void* context;};
    std::vector<DrawBinding> drawBindings;
    struct MediaAsset { WiMedia handle=0; std::string owner; std::set<WiElement> bound; uint64_t revision=0,eventSequence=0; bool hidden=false; };
    MediaEngine mediaEngine;
    std::map<WiMedia,MediaAsset> mediaAssets;
    uint64_t nextMedia=700000;
    WiInputEvent lastSceneInput{sizeof(WiInputEvent),1};
    SceneStore scenes;
    std::deque<int> sceneActions;
    WiSceneSnapshot hostScene{sizeof(WiSceneSnapshot),1};
    std::vector<SceneNode> hostElements,displayedElements;
    struct Work {std::string action,id,value; bool cascade=false; uint64_t handle=0;fs::path folder; WiInputEvent input{};};
    fs::path root,dataRoot,cacheRoot; std::map<std::string,std::unique_ptr<Record>> records;
    std::vector<Resource> resources; uint64_t nextHandle=50000;
    mutable std::mutex mutex; std::condition_variable cv; ModSnapshot published;
    std::deque<Work> queue; bool stopping=false; DWORD workerId=0; std::thread worker;
    explicit Impl(fs::path path,bool start,fs::path data):root(fs::absolute(path)),dataRoot(data.empty()?dataDir():fs::absolute(data)),cacheRoot(dataRoot/L"mod-cache") {
        scenes.base.push_back(sceneNode(1,"island",WI_CONTAINER,0,0,180,30));hostElements=scenes.base;
        published.busy=start;
        if(start)queue.push_back({"startup"});
        worker=std::thread([this]{run();});
    }
    ~Impl(){ {std::lock_guard l(mutex);stopping=true;}cv.notify_all();if(worker.joinable())worker.join(); }
    void log(Record* r,const std::string& text) {
        try {
            auto folder=r?r->folder:dataRoot/L"mods";fs::create_directories(folder);
            auto file=folder/L"mod.log";
            std::error_code ec;if(fs::file_size(file,ec)>1024*1024&&!ec){fs::copy_file(file,folder/L"mod.previous.log",fs::copy_options::overwrite_existing,ec);std::ofstream(file,std::ios::trunc);}
            SYSTEMTIME t;GetLocalTime(&t);std::ofstream out(file,std::ios::app|std::ios::binary);
            out<<t.wYear<<'-'<<t.wMonth<<'-'<<t.wDay<<' '<<t.wHour<<':'<<t.wMinute<<':'<<t.wSecond<<" ["<<(r?r->info.id:"loader")<<"] "<<text<<'\n';
            monitor.event("插件；"+(r?r->info.id:"loader")+"；"+text);
        }catch(...){}
    }
    void publish(std::string message={}) {
        std::lock_guard l(mutex);
        published.scene=scenes.committed;
        published.media.clear();for(auto& [id,a]:mediaAssets){MediaSnapshot s;if(mediaEngine.snapshot(id,s))published.media.push_back({id,a.owner,s.info});}
        ++published.revision;if(!message.empty())published.message=std::move(message);
        published.mods.clear();published.resources.clear();
        for(auto& [id,r]:records)published.mods.push_back(r->info);
        for(auto& res:resources){auto v=res.view;auto it=records.find(v.owner);if(it!=records.end())v.alpha=it->second->alpha;published.resources.push_back(v);}
    }
    void state(Record& r,const char* status){r.info.status=status;log(&r,status);publish(std::string(status)+"："+r.info.name);}
    void persist(Record& r){writeAtomic(r.folder/L"host-state.txt",r.desired);}
    void saveValues(Record& r) {
        std::ostringstream out;out<<"<ModSettings>";
        for(auto& [k,v]:r.settings)out<<'<'<<utf8(k)<<'>'<<utf8(xmlEscape(v))<<"</"<<utf8(k)<<'>';
        out<<"</ModSettings>";writeAtomic(r.folder/L"settings.xml",out.str());
    }
    static void hostLog(void* context,const char* text) {
        auto& r=*(Record*)context;if(GetCurrentThreadId()!=r.host->workerId)return;if(text)r.host->log(&r,std::string(text).substr(0,2048));
    }
    static uint64_t add(void* context,const WinIslandResource* in) {
        auto& r=*(Record*)context;auto& h=*r.host;
        if(GetCurrentThreadId()!=h.workerId || !r.accepting || !in || in->size<sizeof(*in)||in->version!=1||!in->key || h.resources.size()>=4096) return 0;
        auto keyText=std::string(in->key); auto keyValid=safeId(keyText) || ((in->kind==WI_EVENT||in->kind==WI_HOOK||in->kind==WI_ANIMATION) && keyText.size()<=120 && std::all_of(keyText.begin(),keyText.end(),[](unsigned char c){return std::isalnum(c)||c=='-'||c=='_'||c=='.';}));
        if(!keyValid)return 0;
        if(in->kind<WI_SETTING||in->kind>WI_ANIMATION)return 0;
        if(in->kind==WI_REPLACE && std::strlen(in->value?in->value:"")>8192)return 0;
        if((in->kind==WI_TIMER||in->kind==WI_TASK||in->kind==WI_EVENT||in->kind==WI_HOOK)&&!in->callback)return 0;
        if(in->kind==WI_REPLACE) {
            auto key=std::string(in->key),value=std::string(in->value?in->value:"");
            if(key=="music-action") {if(value!="next"&&value!="previous"&&value!="toggle")return 0;}
            else if(key=="animation-duration"||key=="island-tint")try {size_t end;double n=std::stod(value,&end);if(end!=value.size()||!std::isfinite(n)||(key=="animation-duration"?(n<.25||n>4):(n<0||n>0xffffff)))return 0;}catch(...){return 0;}
        }
        Resource res;res.view={h.nextHandle++,in->kind,r.info.id,in->key,in->label?in->label:in->key,in->value?in->value:"",r.alpha};
        res.callback=in->callback;res.context=in->context;res.interval=in->intervalMs;res.next=now()+res.interval/1000.;
        if(in->kind==WI_SETTING){auto k=wide(in->key);auto found=r.settings.find(k);if(found==r.settings.end())r.settings[k]=wide(res.view.value);else res.view.value=utf8(found->second);h.saveValues(r);}
        for(auto& prev:h.resources)if(prev.view.owner==r.info.id&&prev.view.key==res.view.key&&prev.view.kind==in->kind)return 0;
        h.resources.push_back(res);h.log(&r,"注册资源 kind="+std::to_string(in->kind)+" target="+res.view.key+"；修改应用");return res.view.handle;
    }
    static int remove(void* context,uint64_t handle) {
        auto& r=*(Record*)context;auto& all=r.host->resources;
        if(GetCurrentThreadId()!=r.host->workerId)return 0;
        auto it=std::find_if(all.begin(),all.end(),[&](auto& x){return x.view.handle==handle&&x.view.owner==r.info.id;});
        if(it==all.end())return 0;r.host->log(&r,"资源移除 target="+it->view.key);all.erase(it);return 1;
    }
    static int getSetting(void* context,const char* key,char* out,uint32_t size) {
        auto& r=*(Record*)context;if(GetCurrentThreadId()!=r.host->workerId||!key||!out||!size)return 0;
        auto it=r.settings.find(wide(key));if(it==r.settings.end())return 0;
        auto value=utf8(it->second);if(value.size()+1>size)return 0;memcpy(out,value.c_str(),value.size()+1);return 1;
    }
    static uint32_t animationCapabilities(void* context){auto& r=*(Record*)context;return r.host->workerId?1u:0u;}
    static uint64_t registerAnimation(void* context,const WinIslandAnimationDefinition* def){
        auto& r=*(Record*)context;auto& h=*r.host;
        if(GetCurrentThreadId()!=h.workerId||!r.accepting||!def||def->size<sizeof(*def)||def->version!=1||!def->key||!def->script||std::strlen(def->script)>256*1024)return 0;
        auto runtime=std::make_shared<AnimationRuntime>();std::string error;
        if(!runtime->load(def->key,def->script,&error)){h.log(&r,"动画脚本加载失败："+error);return 0;}
        WinIslandResource base{sizeof(base),1,WI_ANIMATION,def->key,"动画","registered",nullptr,nullptr,0};uint64_t handle=add(context,&base);if(!handle)return 0;
        for(auto& item:h.resources)if(item.view.handle==handle){item.animation=std::move(runtime);item.animationDuration=std::clamp(def->duration,.01,5.);break;}
        h.log(&r,"注册动画资源 target="+std::string(def->key));return handle;
    }
    static int animationCommand(void* context,uint64_t handle,uint32_t command,const WinIslandAnimationFrame* input,WinIslandAnimationFrame* output){
        auto& r=*(Record*)context;auto& h=*r.host;if(GetCurrentThreadId()!=h.workerId)return 0;
        auto it=std::find_if(h.resources.begin(),h.resources.end(),[&](auto& x){return x.view.handle==handle&&x.view.owner==r.info.id&&x.animation;});if(it==h.resources.end())return 0;auto& item=*it;AnimationValues from{},to{},value{};
        auto copy=[](const WinIslandAnimationFrame* f,AnimationValues& v){if(!f)return;float* p=&v.x;const float* q=&f->x;for(int i=0;i<12;++i)p[i]=q[i];};
        if(command==1){if(!input||!output||input->size<sizeof(*input)||output->size<sizeof(*output))return 0;copy(input,from);copy(output,to);return item.animation->start(item.view.key,from,to,item.animationDuration)?1:0;}
        if(command==2){item.animation->cancel();return 1;}if(command==3){item.animation->pause();return 1;}if(command==4){item.animation->resume();return 1;}if(command==5){item.animation->reverse();return 1;}
        if(command==6&&output){if(!item.animation->tick(input?input->x:0,value))return 0;output->size=sizeof(*output);output->version=1;output->sequence=++item.animationSequence;float* p=&output->x;const float* q=&value.x;for(int i=0;i<12;++i)p[i]=q[i];return 1;}return 0;
    }
#include "scene_host.inc"
    void scan() {
        fs::create_directories(root);
        struct Candidate {fs::path path;std::shared_ptr<ModPackage> package;std::string error;};
        std::vector<Candidate> candidates;std::map<std::string,int> counts;
        for(auto& e:fs::directory_iterator(root)){
            if(!e.is_regular_file())continue;auto ext=e.path().extension().wstring();CharLowerBuffW(ext.data(),(DWORD)ext.size());if(ext!=L".wimod")continue;
            Candidate c;c.path=e.path();log(nullptr,"扫描 .wimod："+utf8(e.path().filename().wstring()));
            try{c.package=std::make_shared<ModPackage>(e.path());c.package->validate();++counts[c.package->manifest.id];}
            catch(const std::exception& ex){c.error=ex.what();}catch(...){c.error="模组包读取异常";}
            candidates.push_back(std::move(c));
        }
        std::set<std::string> seen;
        for(auto& c:candidates){
            std::string id=c.package&&safeId(c.package->manifest.id)?c.package->manifest.id:"broken-"+sha256(utf8(c.path.filename().wstring())).substr(0,16);
            bool duplicate=c.package&&counts[id]>1;
            if(seen.contains(id))id="duplicate-"+sha256(utf8(c.path.filename().wstring())).substr(0,16);
            seen.insert(id);
            auto& pointer=records[id];if(!pointer)pointer=std::make_unique<Record>();auto& r=*pointer;
            if(r.dll){if(!c.error.empty()||duplicate||!c.package||r.info.packageHash!=c.package->hash){r.info.error="包已变化或存在 ID 冲突；当前实例保留，需关闭后重新加载";log(&r,r.info.error);}continue;}
            auto loads=r.info.loads,unloads=r.info.unloads;r.info=ModInfo{};r.info.id=id;r.info.loads=loads;r.info.unloads=unloads;
            r.info.packageFile=utf8(c.path.filename().wstring());r.host=this;r.folder=dataRoot/L"mods"/wide(id);r.package=c.package;
            r.api={sizeof(WinIslandHostApi),WINISLAND_MOD_ABI,&r,&hostLog,&add,&remove,&getSetting,1,&animationCapabilities,&registerAnimation,&animationCommand,&queryInterface};
            log(&r,"读取清单与模组信息");
            if(c.package){auto& m=c.package->manifest;r.info.name=m.name;r.info.description=m.description;r.info.author=m.author;r.info.version=m.version;r.info.dependencies=m.dependencies;r.info.dependencyVersions=m.dependencyVersions;r.info.packageHash=c.package->hash;r.info.signature=c.package->signature;r.info.entry=m.entry;r.info.apiVersion=m.apiVersion;r.info.gameVersion=m.gameVersion;}
            r.info.error=duplicate?"模组 ID 重复：请保留一个包后刷新":c.error;
            if(!r.info.error.empty()){r.info.status="加载失败";log(&r,r.info.error);continue;}
            r.info.valid=true;r.desired=clean(readFile(r.folder/L"host-state.txt"));
            if(r.desired!="enabled"&&r.desired!="disabled"&&r.desired!="unloaded"){
                r.desired="disabled";log(&r,"启用记录缺失或损坏：安全恢复为禁用，未执行 DLL/回调；请手动启用");
                r.info.error="启用记录缺失或损坏，已保持禁用；请确认后手动启用";
            }
            r.info.status=r.desired=="disabled"?"已禁用":r.desired=="unloaded"?"已卸载":"未加载";
            r.settings=xmlFields(readFile(r.folder/L"settings.xml"));log(&r,"清单检查通过；缺失字段使用统一默认文本；"+r.info.signature);
        }
        for(auto it=records.begin();it!=records.end();){if(!seen.contains(it->first)){if(it->second->dll){it->second->info.error="模组文件已移除；当前已加载实例仍保留";++it;}else it=records.erase(it);}else ++it;}
        for(auto& [id,r]:records)if(r->info.valid&&!r->dll)for(auto& dep:r->info.dependencies){
            auto found=records.find(dep);std::string error;
            if(found==records.end())error="依赖模组缺失："+dep;
            else if(!found->second->info.valid)error="依赖模组无效："+dep;
            else if(!modVersionMatches(found->second->info.version,r->info.dependencyVersions[dep]))error="依赖版本不满足："+dep+" "+r->info.dependencyVersions[dep];
            if(!error.empty()){r->info.valid=false;r->info.error=error;r->info.status="加载失败";log(r.get(),error);break;}
        }
        publish("模组包列表已刷新");
    }
    void fade(Record& r,bool up) {
        BOOL animations=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animations,0);
        if(!animations){r.alpha=up?1.f:0.f;publish();return;}
        // Host snapshots, not plugin code, animate UI. Renderer uses the same alpha.
        for(int n=0;n<=10;++n){r.alpha=up?n/10.f:1-n/10.f;publish();std::unique_lock l(mutex);cv.wait_for(l,std::chrono::milliseconds(16),[&]{return stopping;});}
    }
    void revoke(Record& r) {
        scenes.revoke(r.info.id);std::erase_if(drawBindings,[&](auto& b){return b.owner==r.info.id;});std::erase_if(bindings,[&](auto& b){return b.owner==r.info.id||!scenes.exists(b.element);});
        // Media buffers are owner scoped; release them before the plugin DLL can unload.
        std::erase_if(mediaAssets,[&](const auto& item){if(item.second.owner!=r.info.id)return false;mediaEngine.release(item.first);return true;});
        for(auto it=resources.rbegin();it!=resources.rend();++it)if(it->view.owner==r.info.id)log(&r,"修改恢复/资源移除 target="+it->view.key);
        resources.erase(std::remove_if(resources.begin(),resources.end(),[&](auto& v){return v.view.owner==r.info.id;}),resources.end());
        log(&r,"插件 UI 移除；替换链恢复到前一个有效 owner 或默认实现");publish();
    }
    bool enable(Record& r,std::set<std::string>& visiting,bool restoring=false) {
        if(restoring && fs::exists(r.folder/L"quarantine.txt")){r.info.error="上次回调失败，自动恢复已暂停："+readFile(r.folder/L"quarantine.txt");state(r,"加载失败");return false;}
        if(r.info.enabled)return true;
        if(!r.info.valid)return false;
        if(!visiting.insert(r.info.id).second){r.info.error="循环依赖";state(r,"加载失败");return false;}
        for(auto& dep:r.info.dependencies){auto it=records.find(dep);if(it==records.end()||(restoring&&it->second->desired!="enabled")||!enable(*it->second,visiting,restoring)){r.info.error="依赖无法恢复（缺失、禁用、卸载或故障）："+dep;state(r,"加载失败");visiting.erase(r.info.id);return false;}}
        visiting.erase(r.info.id);r.info.error.clear();state(r,"正在加载");
        try {
            if(!r.dll){
                if(!r.package)throw std::runtime_error("缺少有效 .wimod 包");
                log(&r,"ZIP 解压到内部缓存；hash="+r.package->hash);r.entry=r.package->extract(r.extractionRoot.empty()?cacheRoot:r.extractionRoot);
                r.dll=LoadLibraryExW(r.entry.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
                if(!r.dll)throw std::runtime_error("DLL 加载失败，Win32="+std::to_string(GetLastError()));
                auto abi=(WinIslandModAbi)GetProcAddress(r.dll,"WinIsland_ModAbi");
                auto factory=(WinIslandCreateMod)GetProcAddress(r.dll,"WinIsland_CreateMod");
                r.info.actualAbi=abi?guardedAbi(abi):0;
                if(!abi||!factory||r.info.actualAbi!=WINISLAND_MOD_ABI)throw std::runtime_error("DLL ABI 不兼容；需使用本版 POD 接口重新编译");
                r.plugin=guardedCreate(factory,&r.api);
                if(!guardedTable(r.plugin))throw std::runtime_error("无效插件接口表");
                r.accepting=true;r.info.loaded=true;++r.info.loads;
                if(guarded(r.plugin->onLoad,r.plugin->context,"")!=0)throw std::runtime_error("onLoad 异常或返回失败");
                state(r,"已加载");
            }
            state(r,"正在启用");r.accepting=true;
            if(guarded(r.plugin->onEnable,r.plugin->context,"")!=0)throw std::runtime_error("onEnable 异常或返回失败");
            if(!restoring){auto old=r.desired;r.desired="enabled";try{persist(r);}catch(...){r.desired=old;throw;}std::error_code ec;fs::remove(r.folder/L"quarantine.txt",ec);}
            r.info.enabled=true;state(r,"已启用");fade(r,true);return true;
        }catch(const std::exception& ex){r.info.error=ex.what();r.accepting=false;revoke(r);destroy(r);state(r,"加载失败");return false;}
    }
    void destroy(Record& r) {
        if(guardedTable(r.plugin)){
            int result=guarded(r.plugin->onUnload,r.plugin->context,"");if(result)log(&r,"onUnload 异常");
            guardedDestroy(r.plugin);
        }
        r.plugin=nullptr;
        if(r.dll){if(!FreeLibrary(r.dll))log(&r,"DLL 卸载失败 "+std::to_string(GetLastError()));else ++r.info.unloads;r.dll=nullptr;log(&r,"DLL 卸载");}
        r.info.loaded=false;
    }
    void disable(Record& r,bool unload,bool userChoice=false) {
        if(userChoice){auto old=r.desired;r.desired=unload?"unloaded":"disabled";try{persist(r);}catch(...){r.desired=old;throw;}}
        if(!r.info.loaded&&!r.info.enabled&&!r.info.error.empty())return; // Preserve manifest/ABI/dependency diagnosis.
        // Single worker is the callback gate: earlier callback is finished before
        // this step; accepting=false cancels all future timers and queued events.
        r.accepting=false;
        if(r.info.enabled||r.dll){state(r,"正在关闭");fade(r,false);state(r,"正在还原");revoke(r);
            if(guardedTable(r.plugin)){int rc=guarded(r.plugin->onDisable,r.plugin->context,"");if(rc){r.info.error="onDisable 异常";log(&r,r.info.error);}}
        }
        r.info.enabled=false;r.alpha=0;
        if(unload){state(r,"正在卸载");destroy(r);state(r,"已卸载");}
        else {destroy(r);state(r,"已禁用");}
    }
    void quarantine(Record& r){writeAtomic(r.folder/L"quarantine.txt",r.info.error);disable(r,false);}
    std::vector<std::string> closeOrder(const std::string& target) {
        std::vector<std::string> out;std::set<std::string> seen;
        std::function<void(const std::string&)> visit=[&](const std::string& id){if(!seen.insert(id).second)return;
            for(auto& [other,r]:records)if((r->info.enabled||r->info.loaded)&&std::find(r->info.dependencies.begin(),r->info.dependencies.end(),id)!=r->info.dependencies.end())visit(other);
            if(records.contains(id))out.push_back(id);
        };
        if(target.empty())for(auto& [id,r]:records)visit(id);else visit(target);
        return out;
    }
    void process(Work& w) {
        if(w.action=="scene-input"){lastSceneInput=w.input;dispatchSceneInput(w.input);runSceneDraws(0);return;}
        if(w.action=="scene-draw"){runSceneDraws(w.handle);return;}
        if(w.action=="event"){
            if(w.id=="scene.changed"||w.id=="scene.layout.changed")runSceneDraws(0);
            std::vector<std::string> failed;
            auto listeners=resources; // callbacks may add/remove resources
            for(auto& res:listeners)if((res.view.kind==WI_EVENT||res.view.kind==WI_HOOK)&&res.view.key==w.id){auto it=records.find(res.view.owner);if(it!=records.end()&&it->second->accepting&&guarded(res.callback,res.context,w.value.c_str())!=0)failed.push_back(res.view.owner);}
            for(auto& id:failed){auto it=records.find(id);if(it!=records.end()){it->second->info.error="事件回调失败，已恢复该模组";quarantine(*it->second);}}
            return;
        }
        if(w.action=="scan"||w.action=="startup"){
            scan();if(w.action=="startup")for(auto& [id,r]:records)if(r->desired=="enabled"){std::set<std::string> seen;enable(*r,seen,true);}return;
        }
        if(w.action=="install") {
            ModPackage package(w.folder);package.validate();auto id=package.manifest.id;
            if(records.contains(id))throw std::runtime_error("模组 ID 重复："+id);
            for(auto& dep:package.manifest.dependencies){auto it=records.find(dep);if(it==records.end()||!it->second->info.valid)throw std::runtime_error("依赖模组缺失或无效："+dep);
                if(!modVersionMatches(it->second->info.version,package.manifest.dependencyVersions[dep]))throw std::runtime_error("依赖版本不满足："+dep);}
            auto dest=root/(wide(id)+L".wimod");if(fs::exists(dest))throw std::runtime_error("同名模组文件已存在，不覆盖文件");
            writeAtomic(dataRoot/L"mods"/wide(id)/L"host-state.txt","disabled");writeAtomic(dest,package.bytes());scan();log(records.at(id).get(),"添加模组完成（默认禁用）");return;
        }        if(w.action=="invoke"){
            auto it=std::find_if(resources.begin(),resources.end(),[&](auto& r){return r.view.handle==w.handle;});
            if(it==resources.end())throw std::runtime_error("插件资源已失效，请刷新后重试");auto res=*it;auto& r=*records.at(res.view.owner);if(!r.accepting||!r.info.enabled)throw std::runtime_error("插件未启用，设置资源不可用");
            if(res.view.kind==WI_SETTING){if(!validSetting(res.view,w.value))throw std::runtime_error("插件设置无效：请检查类型、数值范围或选项");auto previous=r.settings;r.settings[wide(res.view.key)]=wide(w.value);try{saveValues(r);}catch(...){r.settings=std::move(previous);throw;}it->view.value=w.value;}
            auto beforeHandle=nextHandle,beforeSize=resources.size();
            int rc=guarded(res.callback,res.context,w.value.c_str());
            if(rc){r.info.error="插件回调异常；已停用并恢复";auto order=closeOrder(r.info.id);for(auto& id:order){auto& affected=*records.at(id);affected.info.error=r.info.error;quarantine(affected);}state(r,"加载失败");throw std::runtime_error(r.info.error);}
            else if(res.view.kind!=WI_TIMER&&res.view.kind!=WI_TASK&&res.view.kind!=WI_EVENT&&res.view.kind!=WI_HOOK)log(&r,"事件/设置已处理 target="+res.view.key);
            if(res.view.kind!=WI_TIMER&&res.view.kind!=WI_TASK||rc||beforeHandle!=nextHandle||beforeSize!=resources.size())publish();return;
        }
        if(w.action=="reinstall") {
            auto found=records.find(w.id);if(found==records.end())throw std::runtime_error("插件不存在");auto& r=*found->second;
            auto package=std::make_shared<ModPackage>(root/wide(r.info.packageFile));package->validate();
            if(package->manifest.id!=r.info.id||package->hash!=r.info.packageHash)throw std::runtime_error("已安装包已变化，请先刷新并核对来源；重装只修复当前原包");
            if(!r.info.valid)throw std::runtime_error(r.info.error.empty()?"请先解决清单或依赖错误":r.info.error);
            auto order=closeOrder(w.id);if(order.size()>1&&!w.cascade)throw std::runtime_error("重装需要暂时关闭运行中的依赖者");
            // Prepare verified, isolated files before stopping anything. Never overwrite a loaded DLL.
            auto staging=cacheRoot/L"repairs"/wide(r.info.id)/(std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
            package->extract(staging);
            auto oldPackage=r.package;auto oldRoot=r.extractionRoot;
            std::map<std::string,std::string> desired;std::vector<std::string> restart;
            for(auto& id:order){auto& p=*records.at(id);desired[id]=p.desired;if(p.info.enabled)restart.push_back(id);}
            auto restoreChoices=[&]{for(auto& [id,value]:desired){auto& p=*records.at(id);p.desired=value;persist(p);if(!p.info.enabled)p.info.status=value=="disabled"?"已禁用":value=="unloaded"?"已卸载":"未加载";}};
            try{for(auto& id:order)disable(*records.at(id),true);r.package=package;r.extractionRoot=staging;
                for(auto i=restart.rbegin();i!=restart.rend();++i){std::set<std::string> seen;if(!enable(*records.at(*i),seen))throw std::runtime_error(records.at(*i)->info.error);}restoreChoices();}
            catch(const std::exception& e){auto error=std::string(e.what());bool recovered=true;
                for(auto& id:order)try{disable(*records.at(id),true);}catch(...){recovered=false;}
                r.package=oldPackage;r.extractionRoot=oldRoot;
                for(auto i=restart.rbegin();i!=restart.rend();++i){std::set<std::string> seen;try{if(!enable(*records.at(*i),seen))recovered=false;}catch(...){recovered=false;}}
                try{restoreChoices();}catch(...){recovered=false;}publish();
                throw std::runtime_error("重装失败："+error+(recovered?"；已恢复原实例与设置":"；原缓存及设置保留，但原实例也未能启动，请查看日志"));}
            log(&r,"重装完成：原包校验并释放到独立缓存；保留设置与原启用状态；旧缓存保留便于恢复");publish("重装完成");return;
        }
        if(w.action=="enable") {auto it=records.find(w.id);if(it==records.end())throw std::runtime_error("插件不存在");std::set<std::string> seen;if(!enable(*it->second,seen))throw std::runtime_error(it->second->info.error.empty()?"插件清单无效":it->second->info.error);return;}
        if(w.action=="disable"||w.action=="unload"||w.action=="reload") {
            auto order=closeOrder(w.id);
            if(!w.id.empty()&&order.size()>1&&!w.cascade)throw std::runtime_error("存在运行中的依赖者，不能仅关闭当前插件；请选择连带关闭");
            log(nullptr,w.id.empty()?"批量"+w.action+"开始":"依赖处理 "+w.id);
            std::vector<std::string> restart;
            for(auto& id:order){auto& r=*records.at(id);
                
                if(r.info.enabled||id==w.id)restart.push_back(id);disable(r,w.action!="disable",w.action!="reload");}
            if(w.action=="reload") {scan();std::string failures;for(auto i=restart.rbegin();i!=restart.rend();++i){std::set<std::string> seen;auto found=records.find(*i);if(found==records.end()||!enable(*found->second,seen))failures+=*i+"："+(found==records.end()?"插件包已移除":found->second->info.error)+"；";}if(!failures.empty())throw std::runtime_error("部分插件重新加载失败："+failures);}
            log(nullptr,w.id.empty()?"批量"+w.action+"完成":"操作完成 "+w.id);
            publish(w.id.empty()?(w.action=="unload"?"全部插件已卸载":w.action=="disable"?"全部插件已禁用":"重新加载完成"):"操作完成");return;
        }
    }
    void run() {
        workerId=GetCurrentThreadId();
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        for(;;){Work work;bool have=false;
            {std::unique_lock l(mutex);cv.wait_for(l,std::chrono::milliseconds(mediaAssets.empty()?50:16),[&]{return stopping||!queue.empty();});if(stopping)break;
             scenes.base=hostElements;scenes.displayed=displayedElements;scenes.state=hostScene;
             if(!queue.empty()){work=queue.front();queue.pop_front();have=true;}}
            if(have){std::string failure;try{process(work);}catch(const winrt::hresult_error&){failure="JSON/WinRT 操作失败";log(nullptr,failure);publish(failure);}catch(const std::exception& ex){failure=ex.what();log(nullptr,failure);publish("操作失败："+failure);}catch(...){failure="插件异常";publish(failure);}
                {std::lock_guard l(mutex);if(work.action!="event"&&work.action!="scene-input"&&work.action!="scene-draw"){published.busy=false;published.completedOperation=published.operation;published.operationError=failure;}++published.revision;}}
            std::vector<uint64_t> due;double t=now();for(auto& res:resources)if((res.view.kind==WI_TIMER||res.view.kind==WI_TASK)&&res.next<=t){res.next=t+res.interval/1000.;due.push_back(res.view.handle);}
            for(auto handle:due){Work event{"invoke"};event.handle=handle;try{process(event);}catch(...){log(nullptr,"定时回调异常");}}
            pollMedia();
        }
        auto order=closeOrder("");for(auto& id:order){auto& r=*records.at(id);try{disable(r,true);}catch(...){r.accepting=false;revoke(r);destroy(r);log(&r,"退出清理时文件写入失败");}} // shutdown preserves choices
        winrt::uninit_apartment();
    }
    bool submit(Work w){std::lock_guard l(mutex);if(stopping||published.busy)return false;published.busy=true;++published.operation;published.operationError.clear();++published.revision;queue.push_back(std::move(w));cv.notify_all();return true;}
};
ModLoader::ModLoader(fs::path r,bool start,fs::path data):impl(std::make_unique<Impl>(std::move(r),start,std::move(data))){}
ModLoader::~ModLoader()=default;
ModSnapshot ModLoader::snapshot()const{std::lock_guard l(impl->mutex);return impl->published;}
bool ModLoader::snapshotSince(uint64_t revision,ModSnapshot& out)const{std::lock_guard l(impl->mutex);if(impl->published.revision==revision)return false;out=impl->published;return true;}
bool ModLoader::request(std::string action,std::string id,bool cascade){return impl->submit({std::move(action),std::move(id),{},cascade});}
bool ModLoader::invoke(uint64_t h,std::string value){Impl::Work w{"invoke"};w.handle=h;w.value=std::move(value);return impl->submit(std::move(w));}
bool ModLoader::emit(std::string topic,std::string payload){std::lock_guard l(impl->mutex);if(impl->stopping||impl->queue.size()>=256)return false;impl->queue.push_back({"event",std::move(topic),std::move(payload)});impl->cv.notify_all();return true;}
std::string ModLoader::replacementText(const std::string& key,std::string fallback)const{return snapshot().text(key,std::move(fallback));}
bool ModLoader::install(fs::path path){Impl::Work w{"install"};w.folder=std::move(path);return impl->submit(std::move(w));}
fs::path ModLoader::directory()const{return impl->root;}
fs::path ModLoader::logPath(const std::string& id)const{return impl->dataRoot/L"mods"/wide(safeId(id)?id:"")/L"mod.log";}
bool ModLoader::busy()const{return snapshot().busy;}
void ModLoader::updateScene(WiSceneSnapshot state,std::vector<SceneNode> nodes,std::vector<SceneNode> visible){
    std::lock_guard l(impl->mutex);state.generation=impl->hostScene.generation+1;state.elementCount=(uint32_t)visible.size();impl->hostScene=state;impl->hostElements=std::move(nodes);impl->displayedElements=std::move(visible);
}
std::vector<int> ModLoader::takeSceneActions(){std::lock_guard l(impl->mutex);std::vector<int> out(impl->sceneActions.begin(),impl->sceneActions.end());impl->sceneActions.clear();return out;}
bool ModLoader::sceneInput(WiInputEvent e){std::lock_guard l(impl->mutex);if(impl->stopping||impl->queue.size()>=256)return false;Impl::Work w{"scene-input"};w.input=e;impl->queue.push_back(w);impl->cv.notify_all();return true;}
std::vector<std::string> ModLoader::affected(const std::string& id)const{
    auto s=snapshot();std::vector<std::string> result;std::set<std::string> seen;
    std::function<void(std::string)> visit=[&](std::string target){if(!seen.insert(target).second)return;for(auto& m:s.mods)if((m.loaded||m.enabled)&&std::find(m.dependencies.begin(),m.dependencies.end(),target)!=m.dependencies.end())visit(m.id);result.push_back(target);};if(id.empty()){for(auto& m:s.mods)if(m.loaded||m.enabled)visit(m.id);}else visit(id);return result;
}
} // namespace wi

