#include "embedded_components.h"
#include "mod_package.h"
#define MINIZ_NO_STDIO
#define MINIZ_NO_TIME
#define MINIZ_NO_DEFLATE_APIS
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "../third_party/miniz/miniz.h"
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <shobjidl.h>
#include <commctrl.h>
#include <uxtheme.h>

namespace wi { namespace {
using namespace winrt::Windows::Data::Json;
fs::path selected, testHome;
constexpr uint64_t Margin=256ull*1024*1024;
std::string js(const JsonObject& value){return utf8(value.Stringify().c_str());}
void str(JsonObject& j,const wchar_t* k,const std::string& v){j.Insert(k,JsonValue::CreateStringValue(wide(v)));}
void num(JsonObject& j,const wchar_t* k,double v){j.Insert(k,JsonValue::CreateNumberValue(v));}
std::string get(const JsonObject& j,const wchar_t* k){try{return utf8(j.GetNamedString(k,L"").c_str());}catch(...){throw std::runtime_error("安装记录字段类型损坏："+utf8(k));}}
JsonObject parse(const fs::path& p){auto bytes=readFile(p,8*1024*1024);JsonObject j;if(bytes.empty()||!JsonObject::TryParse(wide(bytes),j))throw std::runtime_error("JSON 记录损坏："+utf8(p.wstring()));return j;}
fs::path bootstrap(){return testHome.empty()?legacyDataDir()/L"installer":testHome/L"bootstrap";}
fs::path locator(){return bootstrap()/L"install-location.json";}
std::string guid(){GUID id;CoCreateGuid(&id);wchar_t s[64];StringFromGUID2(id,s,64);return utf8(s);}
fs::path relative(const std::string& text){
    auto p=fs::path(wide(text));if(text.empty()||p.has_root_path()||text.find_first_of(":\\\0")!=text.npos)throw std::runtime_error("无效组件相对路径");
    for(auto& part:p)if(part==L".."||part==L"."||part.empty())throw std::runtime_error("组件路径越界");return p;
}
void noLinks(const fs::path& p){fs::path current;for(auto& part:fs::absolute(p)){current/=part;auto a=GetFileAttributesW(current.c_str());if(a!=INVALID_FILE_ATTRIBUTES&&(a&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("数据目录不能包含链接或重解析点："+utf8(current.wstring()));}}
fs::path checkedRoot(const fs::path& p){if(!p.is_absolute()||p==p.root_path())throw std::runtime_error("请选择绝对路径下的专用 WinIsland 文件夹，不能使用磁盘根目录");auto value=p.lexically_normal();noLinks(value);return value;}
struct Lock {
    HANDLE handle=nullptr;
    explicit Lock(const fs::path& p){auto key=fs::absolute(p).wstring();CharLowerBuffW(key.data(),(DWORD)key.size());auto name=L"Local\\WinIsland.Install."+wide(sha256(utf8(key)));handle=CreateMutexW(nullptr,FALSE,name.c_str());if(!handle)throw std::runtime_error("无法创建安装锁");auto r=WaitForSingleObject(handle,30000);if(r!=WAIT_OBJECT_0&&r!=WAIT_ABANDONED){CloseHandle(handle);handle=nullptr;throw std::runtime_error("另一实例正在安装，请稍后重试");}}
    ~Lock(){if(handle){ReleaseMutex(handle);CloseHandle(handle);}}
};
struct Bundle {
    mz_zip_archive zip{};JsonObject manifest;std::string id;uint64_t size=0;
    Bundle(){auto r=FindResourceW(nullptr,MAKEINTRESOURCEW(100),RT_RCDATA);auto h=r?LoadResource(nullptr,r):nullptr;auto bytes=h?LockResource(h):nullptr;auto count=r?SizeofResource(nullptr,r):0;
        if(!bytes||!mz_zip_reader_init_mem(&zip,bytes,count,0))throw std::runtime_error("EXE 内嵌运行组件损坏，请重新获取构建");
        size_t length=0;void* data=mz_zip_reader_extract_file_to_heap(&zip,"bundle-manifest.json",&length,0);if(!data)throw std::runtime_error("缺少组件清单");
        std::string raw((char*)data,length);mz_free(data);manifest=JsonObject::Parse(wide(raw));id=sha256(raw);
        if(manifest.GetNamedNumber(L"format")!=2)throw std::runtime_error("组件清单协议不兼容");
        for(auto v:manifest.GetNamedArray(L"files")){auto f=v.GetObject();auto rel=relative(get(f,L"path"));auto n=f.GetNamedNumber(L"size");if(n<0||n>1024ull*1024*1024)throw std::runtime_error("组件尺寸无效");size+=(uint64_t)n;}
    }
    ~Bundle(){mz_zip_reader_end(&zip);}
    bool verify(const fs::path& folder){try{noLinks(folder);std::set<fs::path> expected{L"owner.json",L"component-manifest.json"};for(auto v:manifest.GetNamedArray(L"files")){auto f=v.GetObject();auto rel=relative(get(f,L"path"));expected.insert(rel);auto file=folder/rel;noLinks(file);if(!fs::is_regular_file(file)||fs::file_size(file)!=(uint64_t)f.GetNamedNumber(L"size")||fileSha256(file)!=get(f,L"sha256"))return false;}for(auto& e:fs::recursive_directory_iterator(folder)){noLinks(e.path());if(e.is_regular_file()&&!expected.contains(fs::relative(e.path(),folder)))return false;}return true;}catch(...){return false;}}
};
struct Progress {std::atomic<uint64_t> done{0},total{1};std::atomic<bool> cancel{false},committing{false};std::atomic<int> conflicts{0};std::mutex mutex;std::wstring message;void say(std::wstring s){std::lock_guard l(mutex);message=std::move(s);}std::wstring read(){std::lock_guard l(mutex);return message;}};
uint64_t freeBytes(const fs::path& p){auto ancestor=p;while(!ancestor.empty()&&!fs::exists(ancestor))ancestor=ancestor.parent_path();ULARGE_INTEGER n{};if(ancestor.empty()||!GetDiskFreeSpaceExW(ancestor.c_str(),&n,nullptr,nullptr))throw std::runtime_error("目标磁盘暂时不可用");return n.QuadPart;}
void writable(const fs::path& p,uint64_t required){auto root=checkedRoot(p);if(freeBytes(root)<required)throw std::runtime_error("目标磁盘空间不足，需要约 "+std::to_string(required/1024/1024)+" MiB；请选择其他目录");auto parent=root;while(!fs::exists(parent))parent=parent.parent_path();auto probe=parent/wide(".winisland-probe-"+guid());HANDLE f=CreateFileW(probe.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE,nullptr);if(f==INVALID_HANDLE_VALUE)throw std::runtime_error("目标目录不可写，Win32="+std::to_string(GetLastError()));CloseHandle(f);}
fs::path suggested(uint64_t required,std::wstring& reason){
    if(!testHome.empty()){reason=L"隔离验证：不会读取或修改日常安装记录。";return testHome/L"安装数据 中文 spaces";}
    for(wchar_t drive=L'D';drive<=L'Z';++drive){wchar_t volume[]{drive,L':',L'\\',0};if(GetDriveTypeW(volume)!=DRIVE_FIXED)continue;auto candidate=fs::path(volume)/L"WinIsland";try{writable(candidate,required);reason=drive==L'D'?L"优先使用可写且空间足够的 D 盘。":L"D 盘不可用、不可写或空间不足，已按盘符顺序选择可用本地磁盘。";return candidate;}catch(...){}}
    reason=L"没有可用数据盘，建议使用当前用户的本地应用数据目录。";return legacyDataDir();
}
struct Location {fs::path root;std::string instance;bool legacy=false;};
Location readLocationImpl(){auto bytes=readFile(locator());JsonObject j;
    if(!JsonObject::TryParse(wide(bytes),j)){
        // Only the exact legacy writer is accepted: it failed to escape Windows
        // backslashes. Never interpret an unknown/future format as a fresh install.
        if(!bytes.starts_with("{\"format\":1,\"instance\":\"winisland\",\"root\":\""))throw std::runtime_error("定位记录 JSON 损坏，请重新选择已有安装目录");
        auto start=bytes.find("\"root\":\"")+8,end=bytes.find("\",\"componentId\":",start);if(end==bytes.npos)throw std::runtime_error("旧定位记录不完整");
        return {checkedRoot(wide(bytes.substr(start,end-start))),"winisland",true};
    }
    auto format=j.GetNamedNumber(L"format");if(format!=1&&format!=2)throw std::runtime_error("安装定位协议版本无法识别，已停止安装操作；请使用兼容版本");
    return {checkedRoot(wide(get(j,L"root"))),get(j,L"instance"),format==1};
}
Location readLocation(){try{return readLocationImpl();}catch(const winrt::hresult_error&){throw std::runtime_error("定位记录字段损坏，请重新选择已有目录");}}
JsonObject validateStateImpl(const Location& loc){if(!fs::is_directory(loc.root))throw std::runtime_error("安装目录已移动、删除或磁盘离线；请恢复磁盘或重新选择已有目录");auto state=parse(loc.root/L"install-state.json");auto format=state.GetNamedNumber(L"format");if(format!=1&&format!=2)throw std::runtime_error("安装状态协议版本无法识别，不能覆盖");if(format==2&&(get(state,L"instance").empty()||(get(state,L"instance")!=loc.instance&&!(loc.legacy&&get(state,L"legacyInstance")==loc.instance))))throw std::runtime_error("定位记录与目录安装实例不匹配，不能覆盖");if(format==1&&!loc.legacy)throw std::runtime_error("安装状态与定位协议不匹配");return state;}
JsonObject validateState(const Location& loc){try{return validateStateImpl(loc);}catch(const winrt::hresult_error&){throw std::runtime_error("安装状态字段损坏，请恢复原安装状态或重新选择目录");}}
fs::path knownVersion(const fs::path& root,const JsonObject& state,const Bundle& b){JsonObject sets;if(state.HasKey(L"sets")){if(state.GetNamedValue(L"sets").ValueType()!=JsonValueType::Object)throw std::runtime_error("组件集合字段损坏");sets=state.GetNamedObject(L"sets");}auto path=get(sets,wide(b.id).c_str());return path.empty()?root/L"components"/L"runtime"/wide(b.id):root/relative(path);}
void copyVerified(const fs::path& source,const fs::path& target){
    noLinks(source);noLinks(target);fs::create_directories(target.parent_path());auto temp=target;temp+=wide(".migrating-"+guid());
    try{fs::copy_file(source,temp,fs::copy_options::none);if(fs::file_size(source)!=fs::file_size(temp)||fileSha256(source)!=fileSha256(temp))throw std::runtime_error("迁移文件校验失败："+utf8(source.wstring()));fs::rename(temp,target);}
    catch(...){std::error_code ec;fs::remove(temp,ec);throw;}
}
void migrate(const fs::path& root,Progress& progress){
    auto legacy=testHome.empty()?legacyDataDir():testHome/L"legacy-local";
    wchar_t roaming[MAX_PATH]{};SHGetFolderPathW(nullptr,CSIDL_APPDATA,nullptr,0,roaming);
    auto oldUI=testHome.empty()?fs::path(roaming)/L"WinIsland"/L"settings-ui":testHome/L"legacy-electron";
    std::vector<std::pair<fs::path,fs::path>> sources;
    for(auto from:{legacy,exePath().parent_path()}){
        for(auto name:{L"mods",L"mod-cache",L"lyrics",L"monitor",L"logs",L"settings.xml",L"daxian日志"})sources.push_back({from/name,root/name});
    }
    sources.push_back({oldUI,root/L"electron-data"});
    std::map<std::string,fs::path> ids;
    if(fs::exists(root/L"mods"))for(auto& e:fs::directory_iterator(root/L"mods"))if(e.path().extension()==L".wimod")try{ModPackage p(e.path());p.validate();ids[p.manifest.id]=e.path();}catch(...){}
    std::ostringstream report;
    for(auto& [source,dest]:sources){if(!fs::exists(source)||fs::equivalent(source,root))continue;if(fs::absolute(source).lexically_normal()==fs::absolute(dest).lexically_normal())continue;
        auto marker=root/L"migration"/(sha256(utf8(fs::absolute(source).wstring()))+".complete");if(fs::exists(marker))continue;noLinks(source);bool conflicts=false;
        std::vector<fs::path> files;if(fs::is_regular_file(source))files.push_back(source);else for(auto& e:fs::recursive_directory_iterator(source)){noLinks(e.path());if(e.is_regular_file())files.push_back(e.path());}
        for(auto& file:files){if(progress.cancel)throw std::runtime_error("已取消，原文件保留");auto target=fs::is_regular_file(source)?dest:dest/fs::relative(file,source);bool conflict=fs::exists(target)&&fileSha256(file)!=fileSha256(target);
            if(file.extension()==L".wimod")try{ModPackage p(file);p.validate();auto i=ids.find(p.manifest.id);if(i!=ids.end()){if(fileSha256(i->second)==fileSha256(file))continue;conflict=true;}else if(!conflict)ids[p.manifest.id]=target;}catch(...){}
            if(conflict){conflicts=true;++progress.conflicts;auto save=root/L"migration"/L"conflicts"/wide(sha256(utf8(file.wstring())))/file.filename();if(!fs::exists(save))copyVerified(file,save);report<<"冲突，保留目标且隔离源副本："<<utf8(file.wstring())<<" -> "<<utf8(save.wstring())<<'\n';}
            else if(!fs::exists(target))copyVerified(file,target);
        }
        writeAtomic(marker,conflicts?"copied-with-conflicts; destination wins; sources preserved":"copied-and-verified; sources preserved");
    }
    if(!report.str().empty())writeAtomic(root/L"migration"/("conflicts-"+std::to_string(GetTickCount64())+".txt"),report.str());
}
void install(const fs::path& candidate,Bundle& b,Progress& progress,bool persist=true){
    auto root=checkedRoot(candidate);Lock lock(root);writable(root,1024*1024);fs::create_directories(root);
    auto previousState=readFile(root/L"install-state.json",8*1024*1024);JsonObject state;std::string instance=guid();
    if(fs::exists(root/L"install-state.json")){state=parse(root/L"install-state.json");auto format=state.GetNamedNumber(L"format");if(format!=1&&format!=2)throw std::runtime_error("未知安装状态协议，不能写入");if(format==1)str(state,L"legacyInstance","winisland");if(format==2){instance=get(state,L"instance");if(instance.empty())throw std::runtime_error("安装实例标识缺失");}}
    auto version=knownVersion(root,state,b);progress.total=b.size;progress.say(L"校验已安装组件…");
    if(!b.verify(version)){
        writable(root,b.size*2+Margin);
        auto stage=root/L".component-staging"/wide(b.id+"-"+guid());noLinks(stage);fs::create_directories(stage);writeAtomic(stage/L"owner.json",instance);
        try{for(auto v:b.manifest.GetNamedArray(L"files")){if(progress.cancel)throw std::runtime_error("已取消安装，原安装与文件保留");auto f=v.GetObject();auto name=get(f,L"path");auto file=stage/relative(name);fs::create_directories(file.parent_path());progress.say(L"释放并校验："+wide(name));
                std::ofstream output(file,std::ios::binary);struct Sink{std::ofstream* stream;Progress* p;}sink{&output,&progress};
                auto callback=[](void* ctx,mz_uint64 offset,const void* data,size_t size)->size_t{auto& s=*(Sink*)ctx;if(s.p->cancel)return 0;s.stream->write((const char*)data,size);if(!*s.stream)return 0;s.p->done+=size;return size;};
                auto index=mz_zip_reader_locate_file(&b.zip,name.c_str(),nullptr,0);if(index<0||!mz_zip_reader_extract_to_callback(&b.zip,index,callback,&sink,0))throw std::runtime_error("组件解压失败或取消："+name);output.close();if(fileSha256(file)!=get(f,L"sha256"))throw std::runtime_error("组件 SHA256 校验失败："+name);
            }
            writeAtomic(stage/L"component-manifest.json",js(b.manifest));
            if(progress.cancel)throw std::runtime_error("已取消，原文件保留");
            // Never overwrite any published DLL/EXE, even if it is corrupt or in use.
            if(fs::exists(version))version=root/L"components"/L"runtime"/wide(b.id+"-"+guid());fs::create_directories(version.parent_path());fs::rename(stage,version);
        }catch(...){ // Delete only this invocation's uniquely created, owned staging tree.
            if(readFile(stage/L"owner.json")==instance){noLinks(stage);std::error_code ec;fs::remove_all(stage,ec);}throw;
        }
    }
    progress.done=b.size;progress.say(L"复制并校验已知历史数据（源文件保留）…");migrate(root,progress);
    if(progress.cancel)throw std::runtime_error("已取消；源文件和旧定位记录保留");
    progress.committing=true;progress.say(L"正在完成安装，请勿关闭…");
    for(auto name:{L"mods",L"mod-cache",L"lyrics",L"logs",L"temp",L"electron-data"}){noLinks(root/name);fs::create_directories(root/name);}
    auto sets=state.GetNamedObject(L"sets",JsonObject{});str(sets,wide(b.id).c_str(),utf8(fs::relative(version,root).generic_wstring()));
    num(state,L"format",2);str(state,L"instance",instance);str(state,L"componentId",b.id);state.Insert(L"sets",sets);state.Insert(L"files",b.manifest.GetNamedArray(L"files"));str(state,L"build",BuildId);writeAtomic(root/L"install-state.json",js(state));
    if(!previousState.empty()){
        writeAtomic(root/L"install-state.previous.json",previousState);
        if(!fs::exists(root/L"migration"/L"original-install-state.json"))writeAtomic(root/L"migration"/L"original-install-state.json",previousState);
    }
    if(persist&&fs::exists(locator())&&!fs::exists(root/L"migration"/L"original-install-location.json"))writeAtomic(root/L"migration"/L"original-install-location.json",readFile(locator()));
    if(persist){JsonObject loc;num(loc,L"format",2);str(loc,L"instance",instance);str(loc,L"root",utf8(root.wstring()));str(loc,L"componentId",b.id);str(loc,L"lastVersion",utf8(Version));num(loc,L"installedAt",(double)time(nullptr));try{writeAtomic(locator(),js(loc));}catch(...){if(!previousState.empty())writeAtomic(root/L"install-state.json",previousState);throw;}}
    selected=version;setDataRoot(root);
    writeAtomic(root/L"logs"/L"installation-last.json",js(state));
}
fs::path chooseFolder(HWND owner){ComPtr<IFileOpenDialog> dialog;if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog))))return {};DWORD options;dialog->GetOptions(&options);dialog->SetOptions(options|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST);dialog->SetTitle(L"选择 WinIsland 数据目录（可选择已有安装）");if(FAILED(dialog->Show(owner)))return {};ComPtr<IShellItem> item;PWSTR value=nullptr;if(FAILED(dialog->GetResult(&item))||FAILED(item->GetDisplayName(SIGDN_FILESYSPATH,&value)))return {};fs::path result(value);CoTaskMemFree(value);return result;}
} // namespace
#include "installation_ui.inc"
namespace {
bool runInstaller(Bundle& bundle,fs::path root,std::wstring reason,bool automatic=false){InstallWindow window(bundle,root,std::move(reason),automatic);return window.run();}
}
void setInstallerTestHome(const fs::path& p){testHome=checkedRoot(p);fs::create_directories(testHome);}
fs::path lyricProviderDirectory(){return selected.empty()?exePath().parent_path()/L"lyric-provider":selected/L"lyric-provider";}
fs::path settingsRuntimeDirectory(){return selected.empty()?exePath().parent_path()/L"settings":selected/L"settings";}
bool prepareEmbeddedComponents(bool diagnostic){if(!selected.empty())return true;if(diagnostic)return false;Bundle bundle;Lock locatorLock(bootstrap());
    if(fs::exists(locator())){
        Location location;try{location=readLocation();}catch(const std::exception& e){auto message=std::string(e.what());if(message.find("协议版本")!=message.npos)throw;std::wstring reason;auto root=suggested(bundle.size*2+Margin,reason);return runInstaller(bundle,root,wide(message)+L"。请选择已有安装目录，或确认新位置；原数据保持不变。");}
        try{auto state=validateState(location);writable(location.root,1024*1024);auto version=knownVersion(location.root,state,bundle);
            if(!location.legacy&&get(state,L"componentId")==bundle.id&&bundle.verify(version)){selected=version;setDataRoot(location.root);return true;}
        }catch(const std::exception& e){auto message=std::string(e.what());if(message.find("协议版本")!=message.npos)throw;return runInstaller(bundle,location.root,wide(message)+L"。请恢复该位置，或明确选择另一目录。");}
        return runInstaller(bundle,location.root,L"正在准备当前 EXE 所需组件，保留插件与用户设置。",true);
    }
    std::wstring reason;auto root=suggested(bundle.size*2+Margin,reason);return runInstaller(bundle,root,std::move(reason));
}
int storagePathTest(const fs::path& output){
    setInstallerTestHome(output);fs::create_directories(output);std::ostringstream report;int failures=0,count=0;
    auto check=[&](bool ok,const char* text){report<<(ok?"PASS ":"FAIL ")<<text<<'\n';++count;if(!ok)++failures;};
    auto rejects=[&](auto f){try{f();return false;}catch(...){return true;}};
    check(rejects([&]{checkedRoot(L"relative/path");}),"reject relative root");
    check(rejects([&]{checkedRoot(output.root_path());}),"reject volume root");
    check(rejects([&]{relative("../outside");}),"reject component path traversal");
    check(rejects([&]{writable(output,UINT64_MAX);}),"insufficient free space rejected before writes");
    auto root=output/L"中文 path";fs::create_directories(root);auto l=locator();
    writeAtomic(l,"{broken");check(rejects([&]{readLocation();}),"corrupt locator not fresh install");
    writeAtomic(l,"{\"format\":99}");check(rejects([&]{readLocation();}),"unknown future protocol stops");
    writeAtomic(l,"{\"format\":\"wrong type\"}");check(rejects([&]{readLocation();}),"invalid record field type stops");
    writeAtomic(l,"{\"format\":1,\"instance\":\"winisland\",\"root\":\""+utf8(root.wstring())+"\",\"componentId\":\"old\"}");
    check(readLocation().root==root,"legacy raw Windows path preserved");
    JsonObject state;num(state,L"format",2);str(state,L"instance","expected");writeAtomic(root/L"install-state.json",js(state));
    check(rejects([&]{validateState({root,"other",false});}),"wrong installation instance stops");
    check(rejects([&]{validateState({output/L"offline", "expected",false});}),"unavailable previous root not recreated");
    auto legacy=output/L"legacy-local";fs::create_directories(legacy/L"mods"/L"a");
    writeAtomic(legacy/L"settings.xml","source-settings");writeAtomic(legacy/L"mods"/L"a"/L"host-state.txt","disabled");writeAtomic(root/L"settings.xml","target-settings");
    Progress progress;migrate(root,progress);
    check(readFile(root/L"settings.xml")=="target-settings"&&progress.conflicts>0,"migration conflict keeps destination and reports");
    check(readFile(legacy/L"settings.xml")=="source-settings","migration preserves source");
    check(readFile(root/L"mods"/L"a"/L"host-state.txt")=="disabled","migration preserves desired plugin choice");
    fs::remove(root/L"mods"/L"a"/L"host-state.txt");Progress again;migrate(root,again);
    check(!fs::exists(root/L"mods"/L"a"/L"host-state.txt"),"completed migration does not resurrect removed data");
    report<<"Passed="<<count-failures<<" Failed="<<failures<<'\n';writeAtomic(output/L"storage-tests.txt",report.str());return failures?1:0;
}
int embeddedComponentsTest(const fs::path& output){try{setInstallerTestHome(output/L"test-bootstrap");Bundle bundle;Progress progress;install(output,bundle,progress,false);return 0;}catch(...){return 1;}}
} // namespace wi
