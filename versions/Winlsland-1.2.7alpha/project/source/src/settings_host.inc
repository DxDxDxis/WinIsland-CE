// Included in namespace wi, beside App. No renderer or DLL pointers cross IPC.
namespace settings_json {
using namespace winrt::Windows::Data::Json;
inline JsonValue value(bool v){return JsonValue::CreateBooleanValue(v);}
inline JsonValue value(double v){return JsonValue::CreateNumberValue(v);}
inline JsonValue value(int v){return value(double(v));}
inline JsonValue value(long v){return value(double(v));}
inline JsonValue value(const std::string& v){return JsonValue::CreateStringValue(wide(v));}
inline JsonValue value(const std::wstring& v){return JsonValue::CreateStringValue(v);}
inline JsonValue value(const char* v){return value(std::string(v));}
inline void put(JsonObject& o,const wchar_t* k,auto v){o.Insert(k,value(v));}
inline JsonObject ok(){JsonObject o;put(o,L"ok",true);return o;}
inline JsonObject error(const char* code,const std::string& message){JsonObject o;put(o,L"ok",false);put(o,L"code",code);put(o,L"error",message);return o;}
inline std::string text(const JsonObject& o,const wchar_t* k){return utf8(o.GetNamedString(k).c_str());}
inline double number(IJsonValue v,double lo,double hi,bool integer=false){
    if(v.ValueType()!=JsonValueType::Number)throw std::runtime_error("需要数值");auto n=v.GetNumber();
    if(!std::isfinite(n)||n<lo||n>hi||(integer&&std::floor(n)!=n))throw std::runtime_error("数值超出允许范围");return n;
}
inline std::wstring string(IJsonValue v,size_t max){if(v.ValueType()!=JsonValueType::String)throw std::runtime_error("需要文本");std::wstring s=v.GetString().c_str();if(s.size()>max||s.find(L'\0')!=s.npos)throw std::runtime_error("文本过长或无效");return s;}
inline JsonObject fields(const Settings& s){JsonObject o;
    put(o,L"resident",s.resident);put(o,L"hideNative",s.hideNative);put(o,L"seconds",s.seconds);put(o,L"fps",s.fps);
    put(o,L"songSource",s.songSource);put(o,L"lyricSource",s.lyricSource);put(o,L"playerFilter",s.playerFilter);
    put(o,L"showFps",s.showFps);put(o,L"showPing",s.showPing);put(o,L"pingTarget",s.pingTarget);put(o,L"lyricApi",s.lyricApi);
    put(o,L"topArcScale",s.topArcScale);put(o,L"islandZoom",s.islandZoom);put(o,L"widthRatio",s.widthRatio);put(o,L"heightRatio",s.heightRatio);put(o,L"dpiCorrection",s.dpiCorrection);
    put(o,L"topAttach",s.topAttach);put(o,L"radiusMode",s.radiusMode);put(o,L"monitorDevice",s.monitorDevice);return o;
}
inline void set(Settings& s,const std::wstring& k,IJsonValue v){
    if(k==L"resident")s.resident=v.GetBoolean();else if(k==L"hideNative")s.hideNative=v.GetBoolean();
    else if(k==L"showFps")s.showFps=v.GetBoolean();else if(k==L"showPing")s.showPing=v.GetBoolean();else if(k==L"topAttach")s.topAttach=v.GetBoolean();
    else if(k==L"seconds")s.seconds=number(v,.1,3600);else if(k==L"fps"){auto n=number(v,0,240,true);if(n!=0&&n<30)throw std::runtime_error("帧率必须为 0 或 30–240 的整数");s.fps=(int)n;}
    else if(k==L"songSource")s.songSource=(int)number(v,0,2,true);else if(k==L"lyricSource")s.lyricSource=(int)number(v,0,5,true);
    else if(k==L"playerFilter")s.playerFilter=(int)number(v,0,3,true);else if(k==L"radiusMode")s.radiusMode=(int)number(v,0,2,true);
    else if(k==L"topArcScale")s.topArcScale=number(v,.5,1.5);else if(k==L"islandZoom")s.islandZoom=number(v,1,2);
    else if(k==L"widthRatio")s.widthRatio=number(v,.75,1.5);else if(k==L"heightRatio")s.heightRatio=number(v,.75,1.5);else if(k==L"dpiCorrection")s.dpiCorrection=number(v,.8,1.25);
    else if(k==L"lyricApi"){auto x=string(v,512);if(!validLyricApi(x))throw std::runtime_error("歌词 API 必须为有效 HTTPS 地址");s.lyricApi=x;}
    else if(k==L"pingTarget"){auto x=string(v,63);if(!validPingTarget(x))throw std::runtime_error("Ping 目标必须为 IPv4、IPv6 或留空");s.pingTarget=x;}
    else if(k==L"monitorDevice"){auto x=string(v,63);bool found=x.empty();for(auto& d:displays())found|=x==d.info.szDevice;if(!found)throw std::runtime_error("该显示器已断开，请重新选择");s.monitorDevice=x;}
    else throw std::runtime_error("未知或只读设置字段："+utf8(k));
}
}
void App::openSettings(){
    if(electronProcess&&WaitForSingleObject(electronProcess,0)==WAIT_TIMEOUT){
        AllowSetForegroundWindow(electronPid);
        EnumWindows([](HWND h,LPARAM p)->BOOL{DWORD pid;GetWindowThreadProcessId(h,&pid);if(pid==(DWORD)p&&IsWindowVisible(h)){ShowWindow(h,SW_RESTORE);SetForegroundWindow(h);}return TRUE;},electronPid);
        return;
    }
    if(electronProcess){CloseHandle(electronProcess);electronProcess=nullptr;}
    auto executable=exePath().parent_path()/L"settings"/L"WinIslandSettings.exe";
    if(!fs::exists(executable)){MessageBoxW(hwnd,L"缺少 Electron 设置组件。请使用 release 中包含 settings 文件夹的完整构建。",L"WinIsland 设置组件缺失",MB_ICONERROR);return;}
    auto command=L"\""+executable.wstring()+L"\" --pipe=\""+settingsBridge->pipeName+L"\" --token=\""+settingsBridge->token+L"\" --core-pid="+std::to_wstring(GetCurrentProcessId());
    if(diagnostic)command+=L" --session-dir=\""+testDir.wstring()+L"\"";
    STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};
    if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,executable.parent_path().c_str(),&si,&pi)){
        MessageBoxW(hwnd,L"Electron 设置程序启动失败，请查看发布目录完整性和系统运行权限。",L"WinIsland",MB_ICONERROR);return;}
    electronProcess=pi.hProcess;electronPid=pi.dwProcessId;CloseHandle(pi.hThread);AllowSetForegroundWindow(electronPid);
}
void App::dispatchSettings(){
    using namespace settings_json;
    for(auto& request:settingsBridge->take()){
        {std::lock_guard lock(request->mutex);if(request->cancelled)continue;request->started=true;}
        JsonObject response;
        try{
            auto q=JsonObject::Parse(wide(request->input));
            if(q.GetNamedNumber(L"protocol")!=1||q.GetNamedString(L"token")!=settingsBridge->token){response=error("AUTH","设置协议或访问令牌无效");}
            else{
                auto command=text(q,L"command");auto p=q.GetNamedObject(L"params",JsonObject{});response=ok();
                auto snapshot=[&]{auto o=ok();o.Insert(L"settings",fields(settings));put(o,L"revision",double(settingsRevision));return o;};
                if(command=="settings.read")response=snapshot();
                else if(command=="settings.write"||command=="layout.reset"){
                    if(p.GetNamedNumber(L"revision")!=double(settingsRevision))response=error("CONFLICT","设置已被更新，请重新读取后重试");
                    else{auto candidate=settings;if(command=="layout.reset")candidate.resetLayout();else{auto patch=p.GetNamedObject(L"patch");if(patch.Size()==0||patch.Size()>20)throw std::runtime_error("设置提交为空或过大");for(auto pair:patch)set(candidate,std::wstring(pair.Key()),pair.Value());}
                        // The small atomic configuration write must succeed before publishing a new revision.
                        {std::lock_guard lock(settingsSaveMutex);candidate.save(config);settings=candidate;loggedSettings=settings;++settingsRevision;}
                        position();applySettings();monitor.event("Electron 设置已应用并持久化");response=snapshot();}
                }
                else if(command=="capabilities.read"){
                    put(response,L"version",utf8(Version));put(response,L"build",BuildId);put(response,L"protocol",1);put(response,L"settingsAnimation",false);put(response,L"reducedMotion",reducedMotion);
                    put(response,L"reducedMotionReason","跟随 Windows 的动画效果设置；当前设置界面不使用过渡动画");put(response,L"dpi",double(dpi*96));
                    put(response,L"renderer",renderer->fallback()?"Direct2D software":"DirectComposition");JsonArray list;
                    for(auto d:displays()){JsonObject x;put(x,L"id",std::wstring(d.info.szDevice));put(x,L"width",d.info.rcMonitor.right-d.info.rcMonitor.left);put(x,L"height",d.info.rcMonitor.bottom-d.info.rcMonitor.top);list.Append(x);}response.Insert(L"displays",list);
                }
                else if(command=="media.read"){
                    put(response,L"active",bool(scene.music));put(response,L"playing",bool(scene.music&&scene.music->playing));put(response,L"title",scene.music?scene.music->title:L"");put(response,L"artist",scene.music?scene.music->artist:L"");
                    put(response,L"key",scene.music?scene.music->key():L"");put(response,L"source",scene.music?scene.music->infoSource:L"");put(response,L"lyricStatus",scene.music?scene.music->lyricStatus:L"暂无音乐");put(response,L"lyric",scene.lyric);
                    put(response,L"monitorActive",monitor.active());put(response,L"monitorPending",monitorPending);put(response,L"monitorMessage",monitorMessage);put(response,L"exportPath",exportPath);
                }
                else if(command=="lyrics.import"||command=="lyrics.clear"){
                    if(!scene.music||text(p,L"key")!=utf8(scene.music->key()))throw std::runtime_error("没有当前歌曲，或歌曲已切换，请刷新后重试");
                    auto key=scene.music->key();auto file=command=="lyrics.import"?fs::path(wide(text(p,L"path"))):fs::path{};
                    if(!file.empty()&&(_wcsicmp(file.extension().c_str(),L".lrc")!=0||!file.is_absolute()))throw std::runtime_error("请选择 .lrc 文件");
                    jobs.post([this,request,key,file]{try{auto dest=store/(sha256(utf8(key))+".lrc");if(file.empty())fs::remove(dest);else{auto data=readFile(file);auto lyrics=Lyrics::parse(data);if(!lyrics||lyrics->lines.empty())throw std::runtime_error("文件没有有效时间轴歌词");writeAtomic(dest,data);}media->reload(file.empty());SettingsBridge::reply(request,utf8(ok().Stringify().c_str()));}catch(const std::exception& e){SettingsBridge::reply(request,utf8(error("LYRICS",e.what()).Stringify().c_str()));}});continue;
                }
                else if(command=="diagnostics.action"){
                    auto action=text(p,L"action");if(monitorPending)throw std::runtime_error("诊断任务正在处理中");
                    if(action=="export"&&!monitor.hasData())throw std::runtime_error("没有可导出的记录，请先启动监测");
                    if(action!="start"&&action!="stop"&&action!="export")throw std::runtime_error("未知诊断操作");monitoring(action=="start"?0:action=="stop"?1:2);put(response,L"pending",true);
                }
                else if(command=="mods.list"){
                    auto s=mods->snapshot();put(response,L"busy",s.busy);put(response,L"operation",double(s.operation));put(response,L"completedOperation",double(s.completedOperation));put(response,L"operationError",s.operationError);put(response,L"message",s.message);JsonArray list,resources;
                    for(auto& m:s.mods){JsonObject x;put(x,L"id",m.id);put(x,L"name",m.name);put(x,L"author",m.author);put(x,L"description",m.description);put(x,L"version",m.version);put(x,L"status",m.status);put(x,L"error",m.error);put(x,L"path",(mods->directory()/wide(m.packageFile)).wstring());put(x,L"signature",m.signature);put(x,L"valid",m.valid);put(x,L"loaded",m.loaded);put(x,L"enabled",m.enabled);put(x,L"apiVersion",m.valid?1:0);put(x,L"loads",double(m.loads));put(x,L"unloads",double(m.unloads));JsonArray deps;for(auto& d:m.dependencies)deps.Append(value(d));x.Insert(L"dependencies",deps);list.Append(x);}
                    for(auto& r:s.resources)if(r.kind==WI_SETTING||r.kind==WI_BUTTON){JsonObject x;put(x,L"handle",std::to_string(r.handle));put(x,L"owner",r.owner);put(x,L"label",r.label);put(x,L"value",r.value);put(x,L"kind",int(r.kind));put(x,L"type",int(r.settingType));put(x,L"minimum",r.minimum);put(x,L"maximum",r.maximum);JsonArray choices;for(auto& c:r.choices)choices.Append(value(c));x.Insert(L"choices",choices);resources.Append(x);}response.Insert(L"mods",list);response.Insert(L"resources",resources);
                }
                else if(command=="mods.affected"){JsonArray ids;for(auto& id:mods->affected(text(p,L"id")))ids.Append(value(id));response.Insert(L"ids",ids);}
                else if(command=="mods.import"||command=="mods.action"||command=="mods.invoke"){
                    bool accepted=false;
                    if(command=="mods.import"){auto file=fs::path(wide(text(p,L"path")));if(!file.is_absolute()||_wcsicmp(file.extension().c_str(),L".wimod")!=0)throw std::runtime_error("请选择 .wimod 文件");accepted=mods->install(file);}
                    else if(command=="mods.invoke"){auto handle=std::stoull(text(p,L"handle"));auto s=mods->snapshot();auto it=std::find_if(s.resources.begin(),s.resources.end(),[&](auto& r){return r.handle==handle&&(r.kind==WI_SETTING||r.kind==WI_BUTTON);});if(it==s.resources.end())throw std::runtime_error("插件设置已失效，请刷新");accepted=mods->invoke(handle,text(p,L"value"));}
                    else{auto action=text(p,L"action"),id=text(p,L"id");if(action!="scan"&&action!="enable"&&action!="disable"&&action!="unload"&&action!="reload")throw std::runtime_error("未知模组操作");if(action=="enable"&&id.empty())throw std::runtime_error("请选择模组");if(!id.empty()){auto s=mods->snapshot();if(std::none_of(s.mods.begin(),s.mods.end(),[&](auto& m){return m.id==id;}))throw std::runtime_error("模组不存在");}accepted=mods->request(action,id,p.GetNamedBoolean(L"cascade",false));}
                    if(!accepted)response=error("BUSY","插件宿主正在处理中，请稍后重试");else{put(response,L"operation",double(mods->snapshot().operation));put(response,L"pending",true);}
                }
                else if(command=="mods.folder"){fs::create_directories(mods->directory());auto rc=(INT_PTR)ShellExecuteW(hwnd,L"open",mods->directory().c_str(),nullptr,nullptr,SW_SHOWNORMAL);if(rc<=32)throw std::runtime_error("无法打开模组目录");}
                else if(command=="mods.log"){auto id=text(p,L"id");auto s=mods->snapshot();if(std::none_of(s.mods.begin(),s.mods.end(),[&](auto& m){return m.id==id;}))throw std::runtime_error("模组不存在");put(response,L"text",readFile(mods->logPath(id),256*1024));}
                else response=error("COMMAND","不支持的设置命令");
            }
        }catch(const winrt::hresult_error&){response=error("INVALID","请求格式或字段类型无效");}
        catch(const std::exception& e){response=error("INVALID",e.what());}
        catch(...){response=error("INTERNAL","核心处理失败，请重新读取状态");}
        SettingsBridge::reply(request,utf8(response.Stringify().c_str()));
    }
}

