#include "clipboard.h"
#include <winhttp.h>
#define MINIZ_NO_STDIO
#define MINIZ_NO_TIME
#define MINIZ_NO_DEFLATE_APIS
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "../third_party/miniz/miniz.h"

namespace wi { namespace {
using namespace winrt::Windows::Data::Json;
#include "translation_package.h"
using Cancel=std::function<bool()>;
void put(JsonObject& o,const wchar_t* k,const std::wstring& v){o.Insert(k,JsonValue::CreateStringValue(v));}
void put(JsonObject& o,const wchar_t* k,const wchar_t* v){put(o,k,std::wstring(v));}
void put(JsonObject& o,const wchar_t* k,bool v){o.Insert(k,JsonValue::CreateBooleanValue(v));}
void put(JsonObject& o,const wchar_t* k,double v){o.Insert(k,JsonValue::CreateNumberValue(v));}
struct Handle {HANDLE h=INVALID_HANDLE_VALUE;~Handle(){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);}operator HANDLE()const{return h;}void close(){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);h=INVALID_HANDLE_VALUE;}};
struct Internet {HINTERNET h=nullptr;~Internet(){if(h)WinHttpCloseHandle(h);}operator HINTERNET()const{return h;}};
void checkCancel(const Cancel& cancel){if(cancel())throw std::runtime_error("操作已取消");}
void noReparse(const fs::path& path){fs::path current;for(auto& p:fs::absolute(path)){current/=p;auto a=GetFileAttributesW(current.c_str());if(a!=INVALID_FILE_ATTRIBUTES&&(a&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("模型目录包含链接，已停止操作");}}
void removeOwned(const fs::path& path){noReparse(path);if(!fs::exists(path))return;for(auto& e:fs::recursive_directory_iterator(path))noReparse(e.path());fs::remove_all(path);}
struct Url {std::wstring host,path;INTERNET_PORT port;bool tls;};
Url urlParts(const std::wstring& value){
    URL_COMPONENTS c{sizeof(c)};c.dwHostNameLength=c.dwUrlPathLength=c.dwExtraInfoLength=c.dwUserNameLength=c.dwPasswordLength=(DWORD)-1;
    if(!WinHttpCrackUrl(value.c_str(),(DWORD)value.size(),0,&c)||!c.dwHostNameLength||c.dwUserNameLength||c.dwPasswordLength)throw std::runtime_error("翻译地址无效，请填写 HTTPS 接口地址");
    std::wstring host(c.lpszHostName,c.dwHostNameLength),path(c.lpszUrlPath,c.dwUrlPathLength),extra(c.lpszExtraInfo,c.dwExtraInfoLength);
    bool tls=c.nScheme==INTERNET_SCHEME_HTTPS,local=host==L"127.0.0.1"||host==L"localhost"||host==L"::1";
    if((!tls&&!(local&&c.nScheme==INTERNET_SCHEME_HTTP))||extra.find(L'#')!=extra.npos)throw std::runtime_error("远程翻译服务必须使用 HTTPS；仅本机测试允许 HTTP");
    return {host,(path.empty()?L"/":path)+extra,c.nPort,tls};
}
// Network work runs only on the translation/model worker. A watchdog bounds the
// entire operation and closes the request on cancellation, including receive waits.
std::string http(const std::wstring& url,const std::string* body,uint64_t limit,const Cancel& cancel,
                 std::function<void(const char*,size_t,uint64_t)> sink={}){
    checkCancel(cancel);auto u=urlParts(url);
    Internet session{WinHttpOpen(L"WinIsland/translation-v1",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0)};
    if(!session.h)throw std::runtime_error("无法创建网络会话");WinHttpSetTimeouts(session,5000,8000,10000,body?55000:15000);
    Internet connection{WinHttpConnect(session,u.host.c_str(),u.port,0)};
    if(!connection.h)throw std::runtime_error("无法连接翻译服务器");
    auto request=WinHttpOpenRequest(connection,body?L"POST":L"GET",u.path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,u.tls?WINHTTP_FLAG_SECURE:0);
    if(!request)throw std::runtime_error("无法创建网络请求");
    struct Guard {std::atomic<HINTERNET> request;std::atomic_bool done=false;std::thread watch;
        Guard(HINTERNET r,const Cancel& cancel,ULONGLONG ms):request(r),watch([this,cancel,ms]{auto until=GetTickCount64()+ms;while(!done){if(cancel()||GetTickCount64()>=until){auto h=request.exchange(nullptr);if(h)WinHttpCloseHandle(h);break;}Sleep(40);}}){}
        ~Guard(){done=true;if(watch.joinable())watch.join();auto h=request.exchange(nullptr);if(h)WinHttpCloseHandle(h);}
    } guard(request,cancel,sink?30*60*1000:60000);
    DWORD redirects=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;WinHttpSetOption(request,WINHTTP_OPTION_REDIRECT_POLICY,&redirects,sizeof(redirects));
    const wchar_t* headers=L"Content-Type: application/json\r\nAccept: application/json\r\n";
    auto failed=[&]{checkCancel(cancel);throw std::runtime_error("网络请求失败或超时，请检查地址与网络后重试");};
    if(!WinHttpSendRequest(request,body?headers:nullptr,body?(DWORD)-1:0,body?(void*)body->data():nullptr,body?(DWORD)body->size():0,body?(DWORD)body->size():0,0)||!WinHttpReceiveResponse(request,nullptr))failed();
    DWORD status=0,n=sizeof(status);if(!WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,nullptr,&status,&n,nullptr))failed();
    if(status!=200){if(status==429)throw std::runtime_error("翻译请求过于频繁，请稍后重试");throw std::runtime_error("服务器返回 HTTP "+std::to_string(status)+"，请检查服务地址、语言或服务器状态");}
    std::string out;std::array<char,64*1024> buf;uint64_t total=0;
    for(;;){checkCancel(cancel);DWORD got=0;if(!WinHttpReadData(request,buf.data(),(DWORD)buf.size(),&got))failed();if(!got)break;
        if(got>limit-total)throw std::runtime_error("服务器响应超过允许大小");total+=got;if(sink)sink(buf.data(),got,total);else out.append(buf.data(),got);}
    checkCancel(cancel);return out;
}
fs::path safePath(const std::string& raw){
    if(raw.empty()||raw.size()>400||raw.find('\0')!=raw.npos||raw.find('\\')!=raw.npos)throw std::runtime_error("模型包路径无效");
    auto s=wide(raw);fs::path p(s);if(utf8(s)!=raw||p.has_root_path()||s.find_first_of(L":<>\"|?*")!=s.npos)throw std::runtime_error("模型包路径无效");
    for(auto& part:p){auto name=part.wstring();if(name==L"."||name==L".."||name.empty()||name.back()==L'.'||name.back()==L' ')throw std::runtime_error("模型包包含不安全路径");
        for(auto c:name)if(c<32)throw std::runtime_error("模型包包含控制字符");auto base=name.substr(0,name.find(L'.'));CharLowerBuffW(base.data(),(DWORD)base.size());
        if(base==L"con"||base==L"prn"||base==L"aux"||base==L"nul"||base==L"conin$"||base==L"conout$"||((base.rfind(L"com",0)==0||base.rfind(L"lpt",0)==0)&&base.size()==4))throw std::runtime_error("模型包包含设备名");}
    return p;
}
void extract(const fs::path& archive,const fs::path& dest,const Cancel& cancel){
    std::ifstream input(archive,std::ios::binary);mz_zip_archive z{};
    z.m_pIO_opaque=&input;z.m_pRead=[](void* opaque,mz_uint64 offset,void* buffer,size_t count)->size_t{auto& f=*(std::ifstream*)opaque;f.clear();f.seekg(offset);f.read((char*)buffer,count);return(size_t)f.gcount();};
    if(!mz_zip_reader_init(&z,fs::file_size(archive),0))throw std::runtime_error("模型压缩包损坏");
    struct Close{mz_zip_archive* z;~Close(){mz_zip_reader_end(z);}} close{&z};
    auto count=mz_zip_reader_get_num_files(&z);if(!count||count>8192)throw std::runtime_error("模型包条目数量无效");uint64_t expanded=0;std::set<std::wstring> names;
    for(mz_uint i=0;i<count;i++){checkCancel(cancel);mz_zip_archive_file_stat s{};
        if(!mz_zip_reader_file_stat(&z,i,&s)||!s.m_is_supported||s.m_is_encrypted||s.m_uncomp_size>128*1024*1024ULL||s.m_uncomp_size>768*1024*1024ULL-expanded)throw std::runtime_error("模型包解压大小或格式无效");expanded+=s.m_uncomp_size;
        if(((s.m_external_attr>>16)&0170000)==0120000||(s.m_external_attr&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("模型包不允许链接");
        auto n=mz_zip_reader_get_filename(&z,i,nullptr,0);if(n<2||n>401)throw std::runtime_error("模型文件名无效");std::string name(n,'\0');mz_zip_reader_get_filename(&z,i,name.data(),n);name.pop_back();if(s.m_is_directory&&name.back()=='/')name.pop_back();
        auto relative=safePath(name);auto key=relative.generic_wstring();CharLowerBuffW(key.data(),(DWORD)key.size());if(!names.insert(key).second)throw std::runtime_error("模型包存在重复路径");auto target=dest/relative;noReparse(target);
        if(s.m_is_directory){fs::create_directories(target);continue;}fs::create_directories(target.parent_path());std::ofstream output(target,std::ios::binary|std::ios::trunc);
        struct Writer{std::ofstream& out;const Cancel& cancel;} writer{output,cancel};
        auto write=[](void* opaque,mz_uint64,const void* data,size_t size)->size_t{auto& w=*(Writer*)opaque;if(w.cancel())return 0;w.out.write((const char*)data,size);return w.out?size:0;};
        if(!output||!mz_zip_reader_extract_to_callback(&z,i,write,&writer,0))throw std::runtime_error("模型包解压失败或已取消");output.close();if(!output)throw std::runtime_error("模型文件写入失败");}
}
JsonObject infer(const fs::path& installed,const JsonObject& q,const Cancel& cancel){
    checkCancel(cancel);noReparse(installed);auto executable=installed/L"python.exe";noReparse(executable);
    SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};Handle inRead,inWrite,outRead,outWrite,err;
    if(!CreatePipe(&inRead.h,&inWrite.h,&sa,65536)||!CreatePipe(&outRead.h,&outWrite.h,&sa,65536))throw std::runtime_error("无法创建离线推理通道");
    SetHandleInformation(inWrite,HANDLE_FLAG_INHERIT,0);SetHandleInformation(outRead,HANDLE_FLAG_INHERIT,0);
    err.h=CreateFileW(L"NUL",GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,nullptr);
    SIZE_T bytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&bytes);std::vector<unsigned char> storage(bytes);auto attributes=(LPPROC_THREAD_ATTRIBUTE_LIST)storage.data();
    if(!InitializeProcThreadAttributeList(attributes,1,0,&bytes))throw std::runtime_error("无法初始化离线进程");struct Attr{LPPROC_THREAD_ATTRIBUTE_LIST p;~Attr(){DeleteProcThreadAttributeList(p);}} attr{attributes};
    HANDLE inherited[]={inRead.h,outWrite.h,err.h};if(!UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherited,sizeof(inherited),nullptr,nullptr))throw std::runtime_error("无法限制离线进程句柄");
    Handle job{CreateJobObjectW(nullptr,nullptr)};JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE|JOB_OBJECT_LIMIT_ACTIVE_PROCESS|JOB_OBJECT_LIMIT_PROCESS_MEMORY;limits.BasicLimitInformation.ActiveProcessLimit=1;limits.ProcessMemoryLimit=768*1024*1024;
    if(!job.h||!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))throw std::runtime_error("无法管理离线进程生命周期");
    STARTUPINFOEXW si{};si.StartupInfo.cb=sizeof(si);si.StartupInfo.dwFlags=STARTF_USESTDHANDLES;si.StartupInfo.hStdInput=inRead;si.StartupInfo.hStdOutput=outWrite;si.StartupInfo.hStdError=err;si.lpAttributeList=attributes;
    auto command=L"\""+executable.wstring()+L"\" -I -B \""+(installed/L"infer.py").wstring()+L"\"";auto environment=childEnvironment(installed.parent_path()/L"runtime-cache");PROCESS_INFORMATION pi{};
    if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED|CREATE_UNICODE_ENVIRONMENT|EXTENDED_STARTUPINFO_PRESENT,environment.data(),installed.c_str(),&si.StartupInfo,&pi))throw std::runtime_error("离线推理程序无法启动，请重新安装模型");
    Handle process{pi.hProcess},thread{pi.hThread};if(!AssignProcessToJobObject(job,process)){TerminateProcess(process,1);WaitForSingleObject(process,5000);throw std::runtime_error("无法接管离线进程，已停止启动");}
    ResumeThread(thread);inRead.close();outWrite.close();auto payload=utf8(q.Stringify().c_str());DWORD wrote=0;if(!WriteFile(inWrite,payload.data(),(DWORD)payload.size(),&wrote,nullptr)||wrote!=payload.size())throw std::runtime_error("离线请求提交失败");inWrite.close();
    std::string output;auto until=GetTickCount64()+90000;std::array<char,8192> buf;
    try{for(;;){checkCancel(cancel);if(GetTickCount64()>until)throw std::runtime_error("离线翻译超过 90 秒，请缩短文本后重试");DWORD available=0;
            if(PeekNamedPipe(outRead,nullptr,0,nullptr,&available,nullptr)&&available){DWORD got=0;if(!ReadFile(outRead,buf.data(),std::min<DWORD>((DWORD)buf.size(),available),&got,nullptr))throw std::runtime_error("离线结果读取失败");output.append(buf.data(),got);if(output.size()>256*1024)throw std::runtime_error("离线结果过大");continue;}
            if(WaitForSingleObject(process,20)==WAIT_OBJECT_0){DWORD remaining=0;if(PeekNamedPipe(outRead,nullptr,0,nullptr,&remaining,nullptr)&&remaining)continue;break;}}
        DWORD exit=0;GetExitCodeProcess(process,&exit);if(exit||output.empty())throw std::runtime_error("离线推理失败，请检查 CPU 兼容性或重新安装模型");
        auto result=JsonObject::Parse(wide(output));if(result.HasKey(L"error"))throw std::runtime_error(utf8(result.GetNamedString(L"error").c_str()));return result;
    }catch(...){TerminateJobObject(job,1);WaitForSingleObject(process,5000);throw;}
}
struct Provider final:TranslationProvider {
    fs::path root,installed;std::mutex stateMutex,useMutex,workerMutex;std::thread worker;
    std::atomic_bool stop=false,cancelModel=false,cancelInference=false;
    bool busy=false,present=false;uint64_t received=0;std::wstring phase=L"idle",failure;ClipboardPreferences prefs;
    explicit Provider(const fs::path& data):root(data/L"translation-model"),installed(root/L"installed"){
        try{present=complete();}catch(...){failure=L"本地模型不可用，请检查数据盘或卸载后重新下载";}
    }
    ~Provider(){stop=true;cancelModel=true;cancelInference=true;if(worker.joinable())worker.join();}
    bool complete(){noReparse(root);if(!fs::exists(installed/L"installed.json"))return false;
        auto marker=JsonObject::Parse(wide(readFile(installed/L"installed.json")));if(marker.GetNamedString(L"sha256",L"")!=wide(ModelHash))return false;
        auto inventory=JsonObject::Parse(wide(readFile(installed/L"inventory.json",4*1024*1024))).GetNamedObject(L"files");
        for(auto pair:inventory){auto p=installed/safePath(utf8(pair.Key().c_str()));noReparse(p);if(!fs::is_regular_file(p)||fs::file_size(p)!=(uint64_t)pair.Value().GetObject().GetNamedNumber(L"bytes"))return false;}return inventory.Size()>10;
    }
    JsonObject status(){std::lock_guard lock(stateMutex);JsonObject r;put(r,L"ok",true);put(r,L"installed",present);put(r,L"busy",busy);put(r,L"phase",phase);put(r,L"error",failure);put(r,L"received",double(received));put(r,L"total",double(ModelBytes));put(r,L"version",ModelVersion);put(r,L"path",installed.wstring());return r;}
    void progress(const wchar_t* value){std::lock_guard lock(stateMutex);phase=value;}
    void run(bool uninstall){
        Handle operation;
        try{std::lock_guard use(useMutex);noReparse(root);fs::create_directories(root);
            operation.h=CreateFileW((root/L"operation.lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(operation.h==INVALID_HANDLE_VALUE)throw std::runtime_error("另一个 WinIsland 正在使用模型，请稍后重试");
            if(uninstall){progress(L"uninstalling");removeOwned(installed);removeOwned(root/L"staging");removeOwned(root/L"runtime-cache");noReparse(root/L"download.part");fs::remove(root/L"download.part");std::lock_guard lock(stateMutex);present=false;phase=L"removed";}
            else{Cancel cancel=[this]{return stop||cancelModel;};checkCancel(cancel);if(complete()){std::lock_guard lock(stateMutex);present=true;phase=L"installed";busy=false;return;}
                auto staging=root/L"staging",archive=root/L"download.part";removeOwned(staging);noReparse(archive);fs::create_directories(staging);
                auto space=fs::space(root);if(space.available<1024*1024*1024ULL)throw std::runtime_error("安装模型需要至少 1 GB 可用空间");
                progress(L"downloading");std::ofstream output(archive,std::ios::binary|std::ios::trunc);if(!output)throw std::runtime_error("无法创建模型下载文件");
                http(ModelUrl,nullptr,ModelBytes,cancel,[&](const char* bytes,size_t size,uint64_t total){output.write(bytes,size);if(!output)throw std::runtime_error("模型下载写入失败，请检查磁盘空间");std::lock_guard lock(stateMutex);received=total;});output.close();if(!output||fs::file_size(archive)!=ModelBytes)throw std::runtime_error("模型下载不完整，请重试");
                progress(L"verifying");checkCancel(cancel);if(fileSha256(archive)!=ModelHash)throw std::runtime_error("模型 SHA-256 校验失败，未安装");
                progress(L"extracting");extract(archive,staging,cancel);progress(L"testing");JsonObject q;put(q,L"q",L"Hello, world.");put(q,L"source",L"en");put(q,L"target",L"zh");auto result=infer(staging,q,cancel);if(result.GetNamedString(L"translatedText",L"").empty())throw std::runtime_error("模型试运行未返回结果");checkCancel(cancel);
                JsonObject marker;put(marker,L"sha256",wide(ModelHash));put(marker,L"version",ModelVersion);writeAtomic(staging/L"installed.json",utf8(marker.Stringify().c_str()));
                removeOwned(installed);fs::rename(staging,installed);fs::remove(archive);std::lock_guard lock(stateMutex);present=true;phase=L"installed";
            }
        }catch(const std::exception& e){std::lock_guard lock(stateMutex);failure=wide(e.what());phase=(stop||cancelModel)?L"cancelled":L"failed";}
         catch(...){std::lock_guard lock(stateMutex);failure=L"模型操作失败，请检查磁盘与安装包后重试";phase=L"failed";}
        if(!uninstall&&operation.h!=INVALID_HANDLE_VALUE)try{removeOwned(root/L"staging");noReparse(root/L"download.part");fs::remove(root/L"download.part");}catch(...){}
        std::lock_guard lock(stateMutex);busy=false;cancelInference=false;
    }
    ClipboardJson command(const std::string& action,const ClipboardJson& p) override{
        if(action=="translation-model-status")return status();
        if(action=="translation-model-cancel"){cancelModel=true;return status();}
        if(action!="translation-model-install"&&action!="translation-model-uninstall")throw std::runtime_error("未知模型管理操作");
        if(!p.GetNamedBoolean(L"confirmed",false))throw std::runtime_error("请先确认模型下载或卸载");
        std::lock_guard workerLock(workerMutex);{std::lock_guard lock(stateMutex);if(busy)throw std::runtime_error("模型任务正在进行，请等待或取消下载");}
        if(worker.joinable())worker.join();bool uninstall=action=="translation-model-uninstall";
        {std::lock_guard lock(stateMutex);busy=true;failure.clear();received=0;phase=uninstall?L"uninstalling":L"preparing";cancelModel=false;cancelInference=uninstall;}
        try{worker=std::thread([this,uninstall]{winrt::init_apartment(winrt::apartment_type::multi_threaded);run(uninstall);winrt::uninit_apartment();});}catch(...){std::lock_guard lock(stateMutex);busy=false;throw;}return status();
    }
    void configure(const ClipboardPreferences& p)override{prefs=p;}
    TranslationResult translate(const TranslationRequest& q,const std::atomic_bool& cancelled)override{
        if(q.text.empty()||q.text.size()>4000)throw std::runtime_error("翻译一次支持 1–4000 个字符，请选择一段文字后重试");
        JsonObject request;put(request,L"q",q.text);put(request,L"source",q.source);put(request,L"target",q.target==L"en-US"?L"en":q.target);put(request,L"format",L"text");JsonObject reply;
        if(prefs.translationOffline){std::lock_guard use(useMutex);{std::lock_guard lock(stateMutex);if(busy||!present)throw std::runtime_error("离线模型未就绪，请先下载模型；未发送任何文本");cancelInference=false;}
            noReparse(root);Handle operation{CreateFileW((root/L"operation.lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr)};
            if(operation.h==INVALID_HANDLE_VALUE)throw std::runtime_error("离线模型正被另一实例使用，请稍后重试");
            reply=infer(installed,request,[&]{return stop||cancelInference||cancelled;});
        }else{if(prefs.translationEndpoint.empty())throw std::runtime_error("请先填写在线翻译服务器地址，或下载模型后选择离线翻译");auto body=utf8(request.Stringify().c_str());reply=JsonObject::Parse(wide(http(prefs.translationEndpoint,&body,256*1024,[&]{return stop||cancelled;})));}
        if(cancelled)throw std::runtime_error("翻译已取消");auto text=reply.GetNamedString(L"translatedText",L"");if(text.empty())throw std::runtime_error("服务器没有返回译文");std::wstring detected;
        if(reply.HasKey(L"detectedLanguage")){auto v=reply.GetNamedValue(L"detectedLanguage");if(v.ValueType()==JsonValueType::Object)detected=v.GetObject().GetNamedString(L"language",L"");else if(v.ValueType()==JsonValueType::String)detected=v.GetString();}
        return {q.id,std::wstring(text),detected};
    }
};
} std::unique_ptr<TranslationProvider> makeHttpTranslationProvider(const fs::path& root){return std::make_unique<Provider>(root);} }
