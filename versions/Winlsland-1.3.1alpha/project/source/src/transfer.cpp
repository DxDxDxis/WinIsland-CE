#include "transfer.h"
namespace wi {
using namespace winrt::Windows::Data::Json;
static void str(JsonObject& o,const wchar_t* k,const std::wstring& v){o.Insert(k,JsonValue::CreateStringValue(v));}
static void num(JsonObject& o,const wchar_t* k,double v){o.Insert(k,JsonValue::CreateNumberValue(v));}
static void boolean(JsonObject& o,const wchar_t* k,bool v){o.Insert(k,JsonValue::CreateBooleanValue(v));}
static std::wstring get(const JsonObject& o,const wchar_t* k){return o.GetNamedString(k,L"").c_str();}
static JsonObject encode(const TransferEntry& e){JsonObject o;str(o,L"id",e.id);str(o,L"name",e.name);str(o,L"extension",e.extension);str(o,L"original",e.original);str(o,L"saved",e.saved);str(o,L"mode",e.mode);str(o,L"state",e.state);str(o,L"error",e.error);num(o,L"size",e.size);num(o,L"imported",e.imported);num(o,L"progress",e.progress);return o;}
static TransferEntry decode(const JsonObject& o){TransferEntry e;e.id=get(o,L"id");e.name=get(o,L"name");e.extension=get(o,L"extension");e.original=get(o,L"original");e.saved=get(o,L"saved");e.mode=get(o,L"mode");e.state=get(o,L"state");e.error=get(o,L"error");e.size=o.GetNamedNumber(L"size",0);e.imported=o.GetNamedNumber(L"imported",0);return e;}
static std::wstring newId(){GUID id{};CoCreateGuid(&id);wchar_t s[40];StringFromGUID2(id,s,40);std::wstring v=s;return v.substr(1,36);}
static fs::path longPath(const fs::path& p){auto normalized=p.lexically_normal();normalized.make_preferred();auto s=normalized.wstring();if(s.starts_with(L"\\\\?\\"))return normalized;if(s.starts_with(L"\\\\"))return L"\\\\?\\UNC\\"+s.substr(2);return L"\\\\?\\"+s;}
static bool regular(const fs::path& p){std::error_code ec;if(!p.is_absolute()||!fs::is_regular_file(longPath(p),ec))return false;auto handle=CreateFileW(longPath(p).c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(handle==INVALID_HANDLE_VALUE)return false;CloseHandle(handle);return true;}
TransferStore::TransferStore(const fs::path& data):root(data/L"file-transfer") {
    auto key=fs::absolute(root).wstring();CharLowerBuffW(key.data(),(DWORD)key.size());auto lockName=L"Local\\WinIsland.Transfer."+wide(sha256(utf8(key)));ownership=CreateMutexW(nullptr,FALSE,lockName.c_str());auto acquired=ownership?WaitForSingleObject(ownership,0):WAIT_FAILED;
    if(acquired!=WAIT_OBJECT_0&&acquired!=WAIT_ABANDONED){if(ownership)CloseHandle(ownership);ownership=nullptr;storageFault=true;state.error=L"另一 WinIsland 实例正在管理此中转目录。本实例不会修改中转数据，请关闭另一实例后重启。";return;}
    for(auto name:{L"files",L"staging"})fs::create_directories(root/name);
    bool loaded=false;
    for(auto file:{L"index.json",L"index.backup.json"}){try{
        if(!fs::exists(root/file))continue;
        auto o=JsonObject::Parse(wide(readFile(root/file,64*1024*1024)));
        if(o.GetNamedNumber(L"version")!=1)throw std::runtime_error("Unsupported transfer index version");
        TransferSnapshot candidate;candidate.enabled=o.GetNamedBoolean(L"enabled",false);candidate.remember=get(o,L"remember");candidate.preferences=o.GetNamedObject(L"preferences",JsonObject{});
        std::set<std::wstring> ids;
        for(auto v:o.GetNamedArray(L"entries")){auto e=decode(v.GetObject());if(e.name.empty()||e.name==L"."||e.name==L".."||e.name.find_first_of(L"<>:\"/\\|?*")!=e.name.npos)throw std::runtime_error("Invalid transfer filename");if(e.id.size()!=36||e.id.find_first_not_of(L"0123456789abcdefABCDEF-")!=e.id.npos||!ids.insert(e.id).second)throw std::runtime_error("Invalid transfer entry ID");
            // Saved paths are derived, never trusted from a modified index.
            if(!e.saved.empty())e.saved=(root/L"files"/e.id/e.name).wstring();
            if(fs::path(e.name).filename()!=fs::path(e.name))throw std::runtime_error("Invalid transfer filename");
            if(e.state==L"copying"||e.state==L"queued"){e.state=L"cancelled";e.error=L"上次复制未完成，可重新选择保存模式重试";}
            if(e.state==L"ready"&&!regular(e.mode==L"saved"?e.saved:e.original)){e.state=L"invalid";e.error=L"文件不存在或磁盘不可用，请重新定位";}
            candidate.entries.push_back(std::move(e));}
        state=std::move(candidate);loaded=true;break;
    }catch(...){state.error=L"中转索引损坏；尝试上一份有效记录。原文件和保存副本均保留。";}}
    if(!loaded&&fs::exists(root/L"index.json")){storageFault=true;state.error=L"中转索引及备份无法读取；已停止接收和写入，原文件和副本未删除。请保留索引排查。";}
    ++state.revision;
    // Orphaned staging files are retained for explicit recovery, never reported as saved.
}
TransferStore::~TransferStore(){ {std::lock_guard lock(mutex);closing=true;for(auto& [id,c]:cancellations)*c=true;}copies.finish();if(ownership){ReleaseMutex(ownership);CloseHandle(ownership);}}
TransferSnapshot TransferStore::snapshot(){std::lock_guard lock(mutex);return state;}
bool TransferStore::refreshSnapshot(TransferSnapshot& current){std::lock_guard lock(mutex);if(current.revision==state.revision)return false;current=state;return true;}
bool TransferStore::enabled(){std::lock_guard lock(mutex);return state.enabled&&!storageFault;}
TransferEntry& TransferStore::find(const std::wstring& id){auto it=std::find_if(state.entries.begin(),state.entries.end(),[&](auto& e){return e.id==id;});if(it==state.entries.end())throw std::runtime_error("中转条目不存在");return *it;}
void TransferStore::persist(){if(storageFault)throw std::runtime_error(utf8(state.error));JsonObject o;num(o,L"version",1);boolean(o,L"enabled",state.enabled);str(o,L"remember",state.remember);o.Insert(L"preferences",state.preferences);JsonArray a;for(auto& e:state.entries)a.Append(encode(e));o.Insert(L"entries",a);
    auto previous=readFile(root/L"index.json",64*1024*1024);if(!previous.empty()){try{JsonObject::Parse(wide(previous));writeAtomic(root/L"index.backup.json",previous);}catch(...){}}
    writeAtomic(root/L"index.json",utf8(o.Stringify().c_str()));++state.revision;
}
std::vector<std::wstring> TransferStore::import(const std::vector<fs::path>& paths){
    if(transferDragActive())return {}; // Our OLE source returned to a host view; it is not an import.
    if(paths.empty()||paths.size()>2000)throw std::runtime_error("请选择 1–2000 个普通文件");
    std::vector<TransferEntry> entries;
    for(auto& p:paths){if(!p.is_absolute()||p.wstring().find(L'\0')!=p.wstring().npos||!regular(p))throw std::runtime_error("无法读取普通文件（目录不支持）："+utf8(p.wstring()));
        TransferEntry e;e.id=newId();e.original=p.lexically_normal().wstring();e.name=p.filename().wstring();e.extension=p.extension().wstring();e.size=(double)fs::file_size(longPath(p));e.imported=(double)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();entries.push_back(std::move(e));}
    std::vector<std::wstring> result;std::wstring remembered;
    {std::lock_guard lock(mutex);auto old=state;remembered=state.remember;
        for(auto& e:entries){auto it=std::find_if(state.entries.begin(),state.entries.end(),[&](auto& x){return _wcsicmp(x.original.c_str(),e.original.c_str())==0&&x.state!=L"invalid"&&x.state!=L"failed"&&x.state!=L"cancelled";});if(it!=state.entries.end()){result.push_back(it->id);continue;}result.push_back(e.id);state.entries.push_back(std::move(e));}
        try{persist();}catch(...){state=std::move(old);throw;}}
    if(remembered==L"reference"||remembered==L"saved")choose(result,remembered);
    return result;
}
void TransferStore::choose(const std::vector<std::wstring>& ids,const std::wstring& mode,bool remember){
    if(ids.empty()||ids.size()>2000)throw std::runtime_error("中转条目选择为空或过多");
    if(mode!=L"reference"&&mode!=L"saved")throw std::runtime_error("请选择仅中转或保存并中转");
    std::vector<std::wstring> copying;
    {std::lock_guard lock(mutex);auto old=state;
        for(auto& id:ids){auto& e=find(id);if(e.state==L"copying"||e.state==L"queued"||e.state==L"ready")continue;e.mode=mode;e.error.clear();e.progress=0;e.state=mode==L"saved"?L"queued":L"ready";if(mode==L"reference"&&!regular(e.original)){e.state=L"invalid";e.error=L"原文件不存在或磁盘不可用";}if(mode==L"saved")copying.push_back(id);}
        if(remember)state.remember=mode;
        try{persist();}catch(...){state=std::move(old);throw;}}
    for(auto& id:copying)queueCopy(id);
}
void TransferStore::queueCopy(const std::wstring& id){auto cancel=std::make_shared<std::atomic_bool>(false);{std::lock_guard lock(mutex);cancellations[id]=cancel;}
    copies.post([this,id,cancel]{{std::lock_guard lock(mutex);if(closing){cancellations.erase(id);return;}}TransferEntry entry;fs::path stage=root/L"staging"/(id+L".partial");
        try{{std::lock_guard lock(mutex);entry=find(id);if(closing||*cancel)throw std::runtime_error("已取消复制");find(id).state=L"copying";persist();}
            ULARGE_INTEGER free{};if(!GetDiskFreeSpaceExW(root.c_str(),&free,nullptr,nullptr)||double(free.QuadPart)<entry.size+1024*1024)throw std::runtime_error("数据盘不可用或空间不足");
            struct Progress{TransferStore* self;std::wstring id;std::shared_ptr<std::atomic_bool> cancel;double stamp=0;}progress{this,id,cancel};
            auto callback=[](LARGE_INTEGER total,LARGE_INTEGER done,LARGE_INTEGER,LARGE_INTEGER,DWORD,DWORD,HANDLE,HANDLE,LPVOID data)->DWORD{
                auto& p=*(Progress*)data;if(*p.cancel)return PROGRESS_CANCEL;if(now()-p.stamp>.08){p.stamp=now();std::lock_guard lock(p.self->mutex);auto& e=p.self->find(p.id);e.progress=total.QuadPart?double(done.QuadPart)/total.QuadPart:1;++p.self->state.revision;}return PROGRESS_CONTINUE;};
            if(!CopyFileExW(longPath(entry.original).c_str(),longPath(stage).c_str(),callback,&progress,nullptr,0))throw std::runtime_error(*cancel?"已取消复制":"复制失败，Windows 错误 "+std::to_string(GetLastError()));
            if(*cancel)throw std::runtime_error("已取消复制");
            if(fs::file_size(stage)!=(uint64_t)entry.size||fileSha256(stage)!=fileSha256(longPath(entry.original)))throw std::runtime_error("复制校验失败，源文件可能在复制期间改变");
            if(*cancel)throw std::runtime_error("已取消复制");
            auto dest=root/L"files"/id/entry.name;fs::create_directories(dest.parent_path());
            HANDLE handle=CreateFileW(longPath(stage).c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);if(handle!=INVALID_HANDLE_VALUE){FlushFileBuffers(handle);CloseHandle(handle);}
            if(!MoveFileExW(longPath(stage).c_str(),longPath(dest).c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("无法发布保存副本");
            {std::lock_guard lock(mutex);auto& e=find(id);e.saved=dest.wstring();e.state=L"ready";e.progress=1;e.error.clear();persist();}
        }catch(const std::exception& e){std::error_code ec;fs::remove(stage,ec);std::lock_guard lock(mutex);try{auto& item=find(id);item.state=*cancel?L"cancelled":L"failed";item.error=wide(e.what());persist();}catch(...){state.error=L"无法保存复制结果，请检查数据盘";++state.revision;}}
        std::lock_guard lock(mutex);cancellations.erase(id);
    });
}
fs::path TransferStore::resolve(const std::wstring& id,bool original){TransferEntry e;{std::lock_guard lock(mutex);e=find(id);}auto p=fs::path(original?e.original:e.mode==L"saved"?e.saved:e.original);if((!original&&e.state!=L"ready")||!regular(p)){if(!original&&e.state==L"ready"){std::lock_guard lock(mutex);auto& item=find(id);item.state=L"invalid";item.error=L"文件暂不可用，请检查磁盘或重新定位";persist();}throw std::runtime_error("文件暂不可用，请检查磁盘或重新定位");}return p;}
void TransferStore::requestView(const std::wstring& id){std::lock_guard lock(mutex);state.selected=id;++state.viewRequest;++state.revision;}
TransferJson TransferStore::command(const std::string& action,const TransferJson& p){JsonObject out;boolean(out,L"ok",true);
    if(action=="interaction"){viewInteractionUntil=p.GetNamedBoolean(L"active",false)?now()+5:0;return out;}
    if(action=="import"){std::vector<fs::path> paths;for(auto v:p.GetNamedArray(L"paths"))paths.emplace_back(v.GetString().c_str());JsonArray ids;for(auto& id:import(paths))ids.Append(JsonValue::CreateStringValue(id));out.Insert(L"ids",ids);return out;}
    if(action=="choose"){std::vector<std::wstring> ids;for(auto v:p.GetNamedArray(L"ids"))ids.emplace_back(v.GetString().c_str());choose(ids,get(p,L"mode"),p.GetNamedBoolean(L"remember",false));return out;}
    if(action=="resolve"||action=="location"){auto path=resolve(get(p,L"id"),p.GetNamedBoolean(L"original",false));if(action=="resolve")str(out,L"path",path.wstring());else{PIDLIST_ABSOLUTE item=ILCreateFromPathW(path.c_str());if(!item)throw std::runtime_error("无法打开文件位置");auto parent=ILCloneFull(item);ILRemoveLastID(parent);PCUITEMID_CHILD child=ILFindLastID(item);auto hr=SHOpenFolderAndSelectItems(parent,1,&child,0);ILFree(parent);ILFree(item);if(FAILED(hr))throw std::runtime_error("资源管理器拒绝打开位置");}return out;}
    if(action=="refresh"){std::vector<std::pair<std::wstring,bool>> status;auto snap=snapshot();for(auto& e:snap.entries)if(e.state==L"ready"||e.state==L"invalid")status.push_back({e.id,regular(e.mode==L"saved"?e.saved:e.original)});std::lock_guard lock(mutex);for(auto& [id,valid]:status){try{auto& e=find(id);e.state=valid?L"ready":L"invalid";e.error=valid?L"":L"文件已移动、删除或存储盘不可用";}catch(...){}}persist();return out;}
    std::lock_guard lock(mutex);
    if(action=="list"){if(p.GetNamedBoolean(L"interaction",false))viewInteractionUntil=now()+5;num(out,L"revision",double(state.revision));num(out,L"viewRequest",double(state.viewRequest));str(out,L"selected",state.selected);boolean(out,L"enabled",state.enabled);str(out,L"remember",state.remember);str(out,L"error",state.error);out.Insert(L"preferences",state.preferences);JsonArray entries;for(auto& e:state.entries)entries.Append(encode(e));out.Insert(L"entries",entries);return out;}
    auto before=state;
    if(action=="enable")state.enabled=p.GetNamedBoolean(L"enabled");
    else if(action=="forget")state.remember.clear();
    else if(action=="side-auto"){auto next=JsonObject::Parse(state.preferences.Stringify());next.Insert(L"sideAutoExpand",JsonValue::CreateBooleanValue(p.GetNamedBoolean(L"enabled")));state.preferences=next;}
    else if(action=="preferences"){auto next=JsonObject::Parse(state.preferences.Stringify());for(auto value:p.GetNamedObject(L"preferences"))next.Insert(value.Key(),value.Value());state.preferences=next;}
    else if(action=="cancel"){auto id=get(p,L"id");auto it=cancellations.find(id);if(it!=cancellations.end())*it->second=true;else {auto& e=find(id);e.state=L"cancelled";e.error=L"已取消";}}
    else if(action=="remove"){auto id=get(p,L"id");if(cancellations.contains(id))throw std::runtime_error("请先取消复制，待任务结束后移除");find(id);std::erase_if(state.entries,[&](auto& e){return e.id==id;});}
    else if(action=="relink"){auto& e=find(get(p,L"id"));if(cancellations.contains(e.id))throw std::runtime_error("请先结束复制");fs::path path=get(p,L"path");if(!regular(path))throw std::runtime_error("重新定位的文件不可读");e.original=path.wstring();e.name=path.filename().wstring();e.extension=path.extension().wstring();e.size=(double)fs::file_size(longPath(path));e.mode=L"pending";e.state=L"pending";e.error.clear();e.saved.clear();}
    else throw std::runtime_error("未知中转操作");
    try{persist();}catch(...){state=std::move(before);throw;}return out;
}
}
