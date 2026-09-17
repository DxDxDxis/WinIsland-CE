#include "mod_system.h"
#include "ui_theme.h"
#include <commctrl.h>
#include <uxtheme.h>
namespace wi {
namespace {
constexpr COLORREF Background=ui_theme::Background, Card=ui_theme::Card, Text=ui_theme::Text, Muted=ui_theme::MutedText;
enum { Back=600, ListPage, Add, Folder, Scan, ReloadAll, Market, Filter, List, Details, Status,
       Enable, Disable, Unload, Reload, SettingsPage, Logs, DisableAll, UnloadAll, Title, Help,
       ResourceList, Value, Apply, Run, ValueChoice };
struct Manager {
    ModLoader& loader; HWND hwnd=nullptr,hostParent=nullptr; bool embedded=false; HFONT font=nullptr,heading=nullptr;
    HBRUSH bg=CreateSolidBrush(Background),card=CreateSolidBrush(Card);
    float dpi=1; int page=0; uint64_t revision=~0ull; bool refreshing=false;
    ModSnapshot state; std::vector<std::string> ids; std::vector<uint64_t> handles;
    std::string selected; std::wstring listSignature,resourceSignature;
    std::map<int,std::array<int,4>> positions;
    explicit Manager(ModLoader& l,HWND host=nullptr):loader(l),hostParent(host),embedded(host!=nullptr){}
    ~Manager(){DeleteObject(font);DeleteObject(heading);DeleteObject(bg);DeleteObject(card);}
    void beginCollapse(){
        if(!embedded)return;
        EnableWindow(hwnd,FALSE); DestroyWindow(hwnd);
    }
    static LRESULT CALLBACK filterProc(HWND h,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR, DWORD_PTR ref){
        auto* p=(Manager*)ref;
        if(msg==WM_ERASEBKGND)return 1;
        if(msg==WM_MOUSEMOVE){if(!GetPropW(h,L"hover")){SetPropW(h,L"hover",(HANDLE)1);InvalidateRect(h,nullptr,FALSE);}TRACKMOUSEEVENT t{sizeof(t),TME_LEAVE,h,0};TrackMouseEvent(&t);}
        if(msg==WM_MOUSELEAVE){RemovePropW(h,L"hover");InvalidateRect(h,nullptr,FALSE);}
        if(msg==WM_PAINT){
            PAINTSTRUCT ps{};HDC dc=BeginPaint(h,&ps);RECT r{};GetClientRect(h,&r);FillRect(dc,&r,p->bg);
            COLORREF fill=GetPropW(h,L"hover")?ui_theme::Hover:ui_theme::Input,edge=GetFocus()==h?ui_theme::Focus:ui_theme::Border;
            auto brush=CreateSolidBrush(fill);auto pen=CreatePen(PS_SOLID,1,edge);auto oldBrush=SelectObject(dc,brush),oldPen=SelectObject(dc,pen);
            RoundRect(dc,r.left,r.top,r.right,r.bottom,int(10*p->dpi),int(10*p->dpi));SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(brush);DeleteObject(pen);
            int sel=(int)SendMessageW(h,CB_GETCURSEL,0,0);wchar_t value[256]{};if(sel>=0)SendMessageW(h,CB_GETLBTEXT,sel,(LPARAM)value);
            RECT text=r;text.left+=int(12*p->dpi);text.right-=int(34*p->dpi);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,Text);auto old=SelectObject(dc,p->font);DrawTextW(dc,value,-1,&text,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);SelectObject(dc,old);
            HPEN arrow=CreatePen(PS_SOLID,std::max(1,(int)p->dpi),Text);oldPen=SelectObject(dc,arrow);int x=r.right-int(17*p->dpi),y=r.top+r.bottom/2;MoveToEx(dc,x-int(4*p->dpi),y-int(2*p->dpi),nullptr);LineTo(dc,x,y+int(2*p->dpi));LineTo(dc,x+int(4*p->dpi),y-int(2*p->dpi));SelectObject(dc,oldPen);DeleteObject(arrow);
            if(GetFocus()==h){RECT focus=r;InflateRect(&focus,-3,-3);DrawFocusRect(dc,&focus);}EndPaint(h,&ps);return 0;
        }
        if(msg==WM_SETFOCUS||msg==WM_KILLFOCUS)InvalidateRect(h,nullptr,FALSE);
        if(msg==WM_NCDESTROY){RemovePropW(h,L"hover");RemoveWindowSubclass(h,filterProc,1);}
        return DefSubclassProc(h,msg,wp,lp);
    }
    HWND item(int id){return GetDlgItem(hwnd,id);}
    void text(int id,const std::wstring& value){auto h=item(id);int n=GetWindowTextLengthW(h);std::wstring old(n+1,L'\0');GetWindowTextW(h,old.data(),n+1);old.resize(n);if(old!=value)SetWindowTextW(h,value.c_str());}
    static LRESULT CALLBACK buttonProc(HWND h,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
        if(msg==WM_MOUSEMOVE){TRACKMOUSEEVENT t{sizeof(t),TME_LEAVE,h,0};TrackMouseEvent(&t);if(!GetPropW(h,L"hover")){SetPropW(h,L"hover",(HANDLE)1);InvalidateRect(h,nullptr,FALSE);}}
        if(msg==WM_MOUSELEAVE){RemovePropW(h,L"hover");InvalidateRect(h,nullptr,FALSE);}
        if(msg==WM_SETFOCUS||msg==WM_KILLFOCUS)InvalidateRect(h,nullptr,FALSE);
        if(msg==WM_NCDESTROY){RemovePropW(h,L"hover");RemoveWindowSubclass(h,buttonProc,1);}
        return DefSubclassProc(h,msg,wp,lp);
    }
    void create(int id,const wchar_t* cls,const wchar_t* label,DWORD style=0){
        HWND h=CreateWindowExW(0,cls,label,WS_CHILD|style,0,0,1,1,hwnd,(HMENU)(INT_PTR)id,GetModuleHandleW(nullptr),nullptr);
        if(!wcscmp(cls,L"BUTTON"))SetWindowSubclass(h,buttonProc,1,0);
    }
    void init(){
        create(Title,L"STATIC",L"插件管理");create(Help,L"STATIC",L"");
        for(auto [id,label]:{std::pair{Back,L"返回"},{ListPage,L"插件模组管理"},{Add,L"添加模组"},{Folder,L"打开模组目录"},{Scan,L"刷新插件列表"},{ReloadAll,L"重新加载插件"},{Market,L"插件市场"},{Enable,L"启用"},{Disable,L"禁用"},{Unload,L"卸载"},{Reload,L"重新加载"},{SettingsPage,L"插件设置"},{Logs,L"查看日志"},{DisableAll,L"禁用全部插件"},{UnloadAll,L"卸载全部插件"},{Apply,L"保存设置"},{Run,L"执行插件按钮"}})
            create(id,L"BUTTON",label,BS_OWNERDRAW|WS_TABSTOP);
        create(Filter,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL);
        SetWindowSubclass(item(Filter),filterProc,1,(DWORD_PTR)this);SetWindowTheme(item(Filter),L"",L"");
        for(auto label:{L"全部",L"已启用",L"已禁用",L"加载失败",L"已加载",L"已发现"})SendMessageW(item(Filter),CB_ADDSTRING,0,(LPARAM)label);
        SendMessageW(item(Filter),CB_SETCURSEL,0,0);
        create(List,L"LISTBOX",L"插件模组列表",LBS_NOTIFY|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS|LBS_NOINTEGRALHEIGHT|WS_VSCROLL|WS_TABSTOP);
        create(Details,L"EDIT",L"",ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL|WS_TABSTOP);
        create(Status,L"STATIC",L"正在读取插件状态…");
        create(ResourceList,L"LISTBOX",L"插件设置与按钮",LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|WS_VSCROLL|WS_TABSTOP);
        create(Value,L"EDIT",L"",ES_AUTOHSCROLL|WS_TABSTOP|WS_BORDER);
        create(ValueChoice,L"COMBOBOX",L"插件设置选项",CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL);
        SetWindowSubclass(item(ValueChoice),filterProc,1,(DWORD_PTR)this);SetWindowTheme(item(ValueChoice),L"",L"");
        SendMessageW(item(Value),EM_SETLIMITTEXT,4096,0);
        SetWindowTheme(item(List),L"",L"");SetWindowTheme(item(ResourceList),L"",L"");
        fonts();layout();refresh();SetTimer(hwnd,1,50,nullptr);
    }
    void fonts(){
        HFONT old=font,oldHeading=heading;
        font=CreateFontW(-int(14*dpi),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
        heading=CreateFontW(-int(24*dpi),0,0,0,FW_SEMIBOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
        for(HWND h=GetWindow(hwnd,GW_CHILD);h;h=GetWindow(h,GW_HWNDNEXT))SendMessageW(h,WM_SETFONT,(WPARAM)(GetDlgCtrlID(h)==Title?heading:font),FALSE);
        SendMessageW(item(List),LB_SETITEMHEIGHT,0,int(78*dpi));
        if(old)DeleteObject(old);if(oldHeading)DeleteObject(oldHeading);
    }
    void layout(){
        RECT rc;GetClientRect(hwnd,&rc);int w=int(rc.right/dpi),h=int(rc.bottom/dpi);
        std::set<int> shown;
        HDWP batch=BeginDeferWindowPos(32);
        auto put=[&](int id,int x,int y,int width,int height){shown.insert(id);std::array<int,4> rect{int(x*dpi),int(y*dpi),int(std::max(1,width)*dpi),int(std::max(1,height)*dpi)};
            if(positions[id]!=rect||!(GetWindowLongW(item(id),GWL_STYLE)&WS_VISIBLE)){positions[id]=rect;batch=DeferWindowPos(batch,item(id),nullptr,rect[0],rect[1],rect[2],rect[3],SWP_NOZORDER|SWP_NOACTIVATE|SWP_SHOWWINDOW);}};
        put(Title,24,20,w-170,36);put(Back,w-126,20,102,34);
        put(Help,24,66,w-48,48);put(Status,24,h-64,w-48,48);
        if(page==0){
            text(Title,L"插件管理");text(Help,L"管理插件系统与文件。进入“插件模组管理”查看每个模组的功能、状态和操作。");
            int width=(w-60)/2,y=136;
            for(auto pair:{std::pair{ListPage,Folder},{Add,Scan},{ReloadAll,Market}}){put(pair.first,24,y,width,46);put(pair.second,36+width,y,width,46);y+=62;}
        }else if(page==1){
            text(Title,L"插件模组管理");text(Help,L"导入单个 .wimod 文件即可使用。卸载不删除模组包、缓存、日志或配置。");
            int lw=std::clamp(w*2/5,220,370),x=36+lw,rw=w-x-24;
            int tb=(w-88)/6,index=0;for(int id:{Add,Folder,DisableAll,UnloadAll,Market,Scan})put(id,24+(tb+8)*index++,118,tb,36);
            put(Filter,24,166,lw,230);put(List,24,206,lw,h-288);
            put(Details,x,166,rw,h-336);
            int bw=(rw-16)/3;
            for(int i=0;i<6;++i)put(Enable+i,x+(i%3)*(bw+8),h-154+(i/3)*44,bw,36);
        }else if(page==2){
            text(Title,L"插件设置");text(Help,L"选择插件注册的设置或按钮。修改设置后点击保存；未注册设置的模组不会出现虚假选项。");
            put(ResourceList,24,120,w-48,h-324);put(Value,24,h-188,w-48,34);put(ValueChoice,24,h-188,w-48,230);
            put(Apply,24,h-136,(w-60)/2,36);put(Run,36+(w-60)/2,h-136,(w-60)/2,36);
        }else {text(Title,L"插件市场");text(Help,L"插件市场暂未开放。本页面预留未来接入入口，当前不会连接任何市场服务器。");}
        for(HWND c=GetWindow(hwnd,GW_CHILD);c;c=GetWindow(c,GW_HWNDNEXT))if(!shown.contains(GetDlgCtrlID(c))&&(GetWindowLongW(c,GWL_STYLE)&WS_VISIBLE))batch=DeferWindowPos(batch,c,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_HIDEWINDOW);
        if(batch)EndDeferWindowPos(batch);
        if(page==2)resourceSelection(false);
        InvalidateRect(hwnd,nullptr,FALSE);
    }
    ModInfo* current(){for(auto& m:state.mods)if(m.id==selected)return &m;return nullptr;}
    void pageTo(int p){if(page==p)return;page=p;layout();refresh(true);SetFocus(item(p==1?Filter:p==2?ResourceList:ListPage));}
    void refresh(bool force=false){
        auto next=loader.snapshot();if(!force&&next.revision==revision)return;
        revision=next.revision;state=std::move(next);refreshing=true;
        int filter=(int)SendMessageW(item(Filter),CB_GETCURSEL,0,0);
        std::vector<std::string> visible;std::wstring signature;
        for(auto& m:state.mods){bool show=filter==0||(filter==1&&m.enabled)||(filter==2&&m.status=="已禁用")||(filter==3&&m.status=="加载失败")||(filter==4&&m.loaded)||(filter==5&&m.status=="未加载");
            if(show){visible.push_back(m.id);signature+=wide(m.id+"|"+m.name+"|"+m.version+"|"+m.author+"|"+m.status)+L"\n";}}
        if(signature!=listSignature){
            listSignature=signature;int top=(int)SendMessageW(item(List),LB_GETTOPINDEX,0,0);
            SendMessageW(item(List),WM_SETREDRAW,FALSE,0);SendMessageW(item(List),LB_RESETCONTENT,0,0);ids=std::move(visible);
            int sel=-1;for(size_t i=0;i<ids.size();++i){auto it=std::find_if(state.mods.begin(),state.mods.end(),[&](auto& m){return m.id==ids[i];});auto label=wide(it->name+" — "+it->status);SendMessageW(item(List),LB_ADDSTRING,0,(LPARAM)label.c_str());if(ids[i]==selected)sel=(int)i;}
            if(sel<0&&!ids.empty()){sel=0;selected=ids[0];}if(ids.empty())selected.clear();
            SendMessageW(item(List),LB_SETCURSEL,sel,0);SendMessageW(item(List),LB_SETTOPINDEX,std::max(0,top),0);SendMessageW(item(List),WM_SETREDRAW,TRUE,0);InvalidateRect(item(List),nullptr,FALSE);
        }
        auto m=current();std::wstring detail;
        if(m){detail=L"模组名称："+wide(m->name)+L"\r\n\r\n功能简介："+wide(m->description)+L"\r\n\r\n作者名称："+wide(m->author)+L"\r\n版本号："+wide(m->version)+L"\r\n当前状态："+wide(m->status)+L"\r\n\r\n模组 ID："+wide(m->id)+L"\r\n依赖：";
            if(m->dependencies.empty())detail+=L"无";for(auto& d:m->dependencies)detail+=wide(d+" "+m->dependencyVersions[d])+L"  ";
            detail+=L"\r\n\r\n模组文件："+wide(m->packageFile)+L"\r\n签名："+wide(m->signature);
            if(!m->error.empty())detail+=L"\r\n\r\n错误原因："+wide(m->error);
            detail+=L"\r\n\r\nDLL 加载 / 释放次数："+std::to_wstring(m->loads)+L" / "+std::to_wstring(m->unloads);
        }else detail=state.mods.empty()?L"暂未发现插件模组\r\n请导入 .wimod 文件，或将模组放入 mods 文件夹。":L"此筛选下没有插件模组。";
        text(Details,detail);
        bool any=std::any_of(state.mods.begin(),state.mods.end(),[](auto& r){return r.loaded||r.enabled;});
        text(Status,(state.busy?L"正在处理，请稍候… ":L"")+wide(state.message)+(any?L"":L"\r\n当前没有启用的插件模组"));
        for(int id:{Add,Scan,ReloadAll,DisableAll,UnloadAll,Enable,Disable,Unload,Reload,Apply,Run})EnableWindow(item(id),!state.busy);
        EnableWindow(item(Enable),!state.busy&&m&&m->valid&&!m->enabled);
        EnableWindow(item(Disable),!state.busy&&m&&(m->loaded||m->enabled));
        EnableWindow(item(Unload),!state.busy&&m&&m->status!="已卸载");
        EnableWindow(item(Reload),!state.busy&&m&&m->valid);
        EnableWindow(item(SettingsPage),m!=nullptr);EnableWindow(item(Logs),m!=nullptr);
        EnableWindow(item(DisableAll),!state.busy&&any);
        EnableWindow(item(UnloadAll),!state.busy&&std::any_of(state.mods.begin(),state.mods.end(),[](auto& r){return r.status!="已卸载";}));
        if(page==2)resources(force);
        refreshing=false;
    }
    uint64_t resourceHandle(){int i=(int)SendMessageW(item(ResourceList),LB_GETCURSEL,0,0);return i>=0&&i<(int)handles.size()?handles[i]:0;}
    void resources(bool force){
        std::wstring signature=wide(selected)+L"\n";std::vector<const ModResourceView*> entries;
        for(auto& r:state.resources)if(r.owner==selected&&(r.kind==WI_SETTING||r.kind==WI_BUTTON||r.kind==WI_LAYER)){entries.push_back(&r);signature+=std::to_wstring(r.handle)+wide(r.label+r.value)+L"\n";}
        if(signature!=resourceSignature){auto chosen=resourceHandle();resourceSignature=signature;handles.clear();SendMessageW(item(ResourceList),WM_SETREDRAW,FALSE,0);SendMessageW(item(ResourceList),LB_RESETCONTENT,0,0);int sel=0;
            for(auto r:entries){if(r->handle==chosen)sel=(int)handles.size();handles.push_back(r->handle);auto label=wide(r->label)+(r->kind==WI_SETTING?L"  · 设置":r->kind==WI_BUTTON?L"  · 按钮":L"  · 图层");SendMessageW(item(ResourceList),LB_ADDSTRING,0,(LPARAM)label.c_str());}
            if(entries.empty())SendMessageW(item(ResourceList),LB_ADDSTRING,0,(LPARAM)L"此模组尚未注册设置或按钮，或已经禁用。");
            SendMessageW(item(ResourceList),LB_SETCURSEL,sel,0);SendMessageW(item(ResourceList),WM_SETREDRAW,TRUE,0);InvalidateRect(item(ResourceList),nullptr,FALSE);resourceSelection();
        }else resourceSelection(false);
    }
    void resourceSelection(bool update=true){uint64_t h=resourceHandle();auto it=std::find_if(state.resources.begin(),state.resources.end(),[&](auto& r){return r.handle==h;});bool valid=it!=state.resources.end();
        EnableWindow(item(Value),valid&&it->kind==WI_SETTING&&!state.busy);EnableWindow(item(Apply),valid&&it->kind==WI_SETTING&&!state.busy);EnableWindow(item(Run),valid&&it->kind==WI_BUTTON&&!state.busy);
        bool choice=valid&&(it->settingType==WI_SETTING_SWITCH||it->settingType==WI_SETTING_CHOICE);
        ShowWindow(item(ValueChoice),page==2&&choice?SW_SHOW:SW_HIDE);ShowWindow(item(Value),page==2&&!choice?SW_SHOW:SW_HIDE);
        EnableWindow(item(ValueChoice),valid&&!state.busy);
        if(update){text(Value,valid?wide(it->value):L"");if(choice){SendMessageW(item(ValueChoice),CB_RESETCONTENT,0,0);auto options=it->settingType==WI_SETTING_SWITCH?std::vector<std::string>{"0","1"}:it->choices;int selectedIndex=-1;for(size_t i=0;i<options.size();++i){auto label=it->settingType==WI_SETTING_SWITCH?(i?L"开启":L"关闭"):wide(options[i]);SendMessageW(item(ValueChoice),CB_ADDSTRING,0,(LPARAM)label.c_str());if(options[i]==it->value)selectedIndex=(int)i;}SendMessageW(item(ValueChoice),CB_SETCURSEL,selectedIndex,0);}}

    }
    void openPath(const fs::path& path){auto result=(INT_PTR)ShellExecuteW(hwnd,L"open",path.c_str(),nullptr,nullptr,SW_SHOWNORMAL);if(result<=32)MessageBoxW(hwnd,(L"无法打开："+path.wstring()+L"\n系统错误："+std::to_wstring(result)).c_str(),L"插件管理",MB_ICONERROR);}
    void operate(const std::string& action,bool all){
        if(state.busy)return;
        auto m=current();if(!all&&!m)return;
        std::string id=all?"":selected;bool cascade=all;
        if(all&&action!="reload"){
            size_t count=std::count_if(state.mods.begin(),state.mods.end(),[&](auto& r){return action=="disable"?(r.loaded||r.enabled):r.status!="已卸载";});
            auto message=L"将"+std::wstring(action=="disable"?L"禁用":L"卸载")+L" "+std::to_wstring(count)+L" 个插件。\n插件修改会还原；.wimod、缓存、日志和设置文件全部保留。";
            if(MessageBoxW(hwnd,message.c_str(),action=="disable"?L"禁用全部插件":L"卸载全部插件",MB_YESNO|MB_DEFBUTTON2|MB_ICONWARNING)!=IDYES)return;
        }else if(!all&&action!="enable"){
            auto affected=loader.affected(id);
            if(affected.size()>1){std::wstring names;for(auto& x:affected)names+=L"\n• "+wide(x);
                TASKDIALOG_BUTTON buttons[]={{101,L"仅关闭当前插件（保留依赖，拒绝此次操作）"},{102,L"连带关闭依赖插件"}};
                auto body=L"运行中的依赖关系要求一起处理："+names+L"\n不受影响的插件将保持运行。";
                TASKDIALOGCONFIG config{sizeof(config)};config.hwndParent=hwnd;config.dwFlags=TDF_ALLOW_DIALOG_CANCELLATION|TDF_USE_COMMAND_LINKS;config.pszWindowTitle=L"插件依赖关系";config.pszMainInstruction=L"选择处理范围";config.pszContent=body.c_str();config.cButtons=2;config.pButtons=buttons;config.dwCommonButtons=TDCBF_CANCEL_BUTTON;int result=0;
                // Resolve dynamically: Comctl32 v5 has no ordinal 345. An optional
                // dependency dialog must never prevent the EXE from starting.
                using TaskDialogFn=HRESULT(WINAPI*)(const TASKDIALOGCONFIG*,int*,int*,BOOL*);
                auto taskDialog=(TaskDialogFn)GetProcAddress(GetModuleHandleW(L"comctl32.dll"),"TaskDialogIndirect");
                if(!taskDialog||FAILED(taskDialog(&config,&result,nullptr,nullptr))){
                    result=MessageBoxW(hwnd,(body+L"\n\n是：连带关闭；否：仅当前（拒绝破坏依赖）；取消：不操作。").c_str(),L"插件依赖关系",MB_YESNOCANCEL|MB_DEFBUTTON3|MB_ICONWARNING)==IDYES?102:0;
                }
                if(result!=102)return;cascade=true;
            }else if(action=="unload"&&MessageBoxW(hwnd,L"从当前程序卸载此插件？模组包、缓存、日志和设置会保留。",L"卸载插件",MB_YESNO|MB_DEFBUTTON2|MB_ICONQUESTION)!=IDYES)return;
        }
        if(!loader.request(action,id,cascade))text(Status,L"已有操作正在进行，请等待完成。");refresh(true);
    }
    void command(int id,int code){
        if(refreshing)return;
        if(id==Filter&&code==CBN_SELCHANGE){refresh(true);return;}
        if(id==List&&code==LBN_SELCHANGE){int i=(int)SendMessageW(item(List),LB_GETCURSEL,0,0);if(i>=0&&i<(int)ids.size())selected=ids[i];refresh(true);return;}
        if(id==ResourceList&&code==LBN_SELCHANGE){resourceSelection();return;}
        if(code!=BN_CLICKED)return;
        switch(id){
        case Back: if(page==0){if(embedded)beginCollapse();else DestroyWindow(hwnd);}else pageTo(page==2?1:0);break;
        case ListPage:pageTo(1);break;case Market:pageTo(3);break;case SettingsPage:pageTo(2);break;
        case Folder:openPath(loader.directory());break;case Logs:openPath(loader.logPath(selected));break;
        case Scan:loader.request("scan");refresh(true);break;
        case ReloadAll:operate("reload",true);break;case Enable:operate("enable",false);break;
        case Disable:operate("disable",false);break;case Unload:operate("unload",false);break;case Reload:operate("reload",false);break;
        case DisableAll:operate("disable",true);break;case UnloadAll:operate("unload",true);break;
        case Apply:{wchar_t value[4097]{};GetWindowTextW(item(Value),value,4097);std::string chosen=utf8(value);auto id=resourceHandle();auto it=std::find_if(state.resources.begin(),state.resources.end(),[&](auto& r){return r.handle==id;});if(it!=state.resources.end()&&IsWindowVisible(item(ValueChoice))){int index=(int)SendMessageW(item(ValueChoice),CB_GETCURSEL,0,0);if(index<0)break;chosen=it->settingType==WI_SETTING_SWITCH?(index?"1":"0"):it->choices.at(index);}loader.invoke(id,chosen);refresh();break;}
        case Run:loader.invoke(resourceHandle());refresh();break;
        case Add:{ComPtr<IFileOpenDialog> dialog;if(SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)))){
            dialog->SetOptions(FOS_FORCEFILESYSTEM|FOS_FILEMUSTEXIST|FOS_PATHMUSTEXIST);dialog->SetTitle(L"选择 WinIsland 模组包");
            COMDLG_FILTERSPEC filter{L"WinIsland 模组 (*.wimod)",L"*.wimod"};dialog->SetFileTypes(1,&filter);dialog->SetDefaultExtension(L"wimod");
            if(SUCCEEDED(dialog->Show(hwnd))){ComPtr<IShellItem> file;PWSTR path=nullptr;if(SUCCEEDED(dialog->GetResult(&file))&&SUCCEEDED(file->GetDisplayName(SIGDN_FILESYSPATH,&path))){loader.install(path);CoTaskMemFree(path);refresh(true);}}
        }break;}
        }
    }
    void draw(DRAWITEMSTRUCT& d){
        HDC dc=d.hDC;RECT r=d.rcItem;SetBkMode(dc,TRANSPARENT);SelectObject(dc,font);
        if(d.CtlID==List){FillRect(dc,&r,card);if(d.itemID>=ids.size())return;auto it=std::find_if(state.mods.begin(),state.mods.end(),[&](auto& m){return m.id==ids[d.itemID];});if(it==state.mods.end())return;
            COLORREF color=(d.itemState&ODS_SELECTED)?ui_theme::Hover:Card;auto b=CreateSolidBrush(color);auto old=SelectObject(dc,b);auto pen=SelectObject(dc,GetStockObject(NULL_PEN));RoundRect(dc,r.left+3,r.top+2,r.right-3,r.bottom-2,int(12*dpi),int(12*dpi));SelectObject(dc,pen);SelectObject(dc,old);DeleteObject(b);
            RECT line=r;line.left+=int(12*dpi);line.right-=int(12*dpi);line.top+=int(8*dpi);line.bottom=line.top+int(25*dpi);SetTextColor(dc,Text);auto title=wide(it->name);DrawTextW(dc,title.c_str(),-1,&line,DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
            line.top+=int(27*dpi);line.bottom=r.bottom-int(8*dpi);SetTextColor(dc,it->error.empty()?Muted:ui_theme::Error);auto sub=wide(it->version+" · "+it->status);DrawTextW(dc,sub.c_str(),-1,&line,DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        }else{FillRect(dc,&r,bg);bool disabled=d.itemState&ODS_DISABLED;COLORREF color=disabled?ui_theme::Disabled:(d.itemState&ODS_SELECTED)?ui_theme::Pressed:GetPropW(d.hwndItem,L"hover")?ui_theme::Hover:ui_theme::Surface;
            auto brush=CreateSolidBrush(color);auto border=CreatePen(PS_SOLID,1,GetFocus()==d.hwndItem?ui_theme::Focus:ui_theme::Border);auto old=SelectObject(dc,brush),oldPen=SelectObject(dc,border);RoundRect(dc,r.left,r.top,r.right,r.bottom,int(12*dpi),int(12*dpi));SelectObject(dc,old);SelectObject(dc,oldPen);DeleteObject(brush);DeleteObject(border);
            wchar_t label[256]{};GetWindowTextW(d.hwndItem,label,256);SetTextColor(dc,disabled?ui_theme::MutedText:Text);DrawTextW(dc,label,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        }
        if((d.itemState&ODS_FOCUS)&&!(d.itemState&ODS_NOFOCUSRECT)){InflateRect(&r,-4,-4);DrawFocusRect(dc,&r);}
    }
};
HWND managerWindow=nullptr;
int testAnswer=IDNO;
bool testConfirmed=false;
void CALLBACK confirmTestDialog(HWND,UINT,UINT_PTR timer,DWORD){
    EnumThreadWindows(GetCurrentThreadId(),[](HWND h,LPARAM data)->BOOL{
        wchar_t name[64]{};GetClassNameW(h,name,64);
        if(!wcscmp(name,L"#32770")&&GetWindow(h,GW_OWNER)==managerWindow){
            testConfirmed=true;KillTimer(nullptr,(UINT_PTR)data);PostMessageW(h,WM_COMMAND,testAnswer,0);return FALSE;
        }return TRUE;
    },(LPARAM)timer);
}
LRESULT CALLBACK managerProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    auto p=(Manager*)GetWindowLongPtrW(hwnd,GWLP_USERDATA);
    if(msg==WM_NCCREATE){p=(Manager*)((CREATESTRUCTW*)lp)->lpCreateParams;p->hwnd=hwnd;SetWindowLongPtrW(hwnd,GWLP_USERDATA,(LONG_PTR)p);}
    if(!p)return DefWindowProcW(hwnd,msg,wp,lp);
    switch(msg){
    case WM_CREATE:p->dpi=GetDpiForWindow(hwnd)/96.f;p->init();return 0;
    case WM_SIZE:p->layout();return 0;
    case WM_DPICHANGED:{
        p->dpi=HIWORD(wp)/96.f;p->fonts();
        if(!p->embedded){auto r=(RECT*)lp;SetWindowPos(hwnd,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);}
        else {
            RECT client{};GetClientRect(p->hostParent,&client);
            SetWindowPos(hwnd,nullptr,0,0,client.right,client.bottom,SWP_NOZORDER|SWP_NOACTIVATE);
        }
        p->layout();return 0;}
    case WM_GETMINMAXINFO:{auto m=(MINMAXINFO*)lp;m->ptMinTrackSize={LONG(680*p->dpi),LONG(460*p->dpi)};return 0;}
    case WM_TIMER:
        p->refresh();return 0;
    
    case WM_COMMAND:p->command(LOWORD(wp),HIWORD(wp));return 0;
    case WM_DRAWITEM:p->draw(*(DRAWITEMSTRUCT*)lp);return TRUE;
    case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:{HDC dc=(HDC)wp;SetBkMode(dc,TRANSPARENT);SetTextColor(dc,Text);bool card=msg!=WM_CTLCOLORSTATIC||(HWND)lp==p->item(Details);SetBkColor(dc,card?Card:Background);return (LRESULT)(card?p->card:p->bg);}
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);FillRect(dc,&ps.rcPaint,p->bg);EndPaint(hwnd,&ps);return 0;}
    case WM_CLOSE:DestroyWindow(hwnd);return 0;
    case WM_NCDESTROY:KillTimer(hwnd,1);if(p->embedded&&p->hostParent){
            ShowWindow(GetDlgItem(p->hostParent,300),SW_SHOW);
            for(int id:{200,201,202,250,251,252,253,254})ShowWindow(GetDlgItem(p->hostParent,id),SW_SHOW);
            RedrawWindow(p->hostParent,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
        }managerWindow=nullptr;SetWindowLongPtrW(hwnd,GWLP_USERDATA,0);delete p;return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
}
void openModManager(HWND parent,ModLoader& loader){
    if(IsWindow(managerWindow)){ShowWindow(managerWindow,SW_RESTORE);SetForegroundWindow(managerWindow);return;}
    WNDCLASSEXW c{sizeof(c)};c.lpfnWndProc=managerProc;c.hInstance=GetModuleHandleW(nullptr);c.hCursor=LoadCursorW(nullptr,IDC_ARROW);c.hIcon=LoadIconW(c.hInstance,MAKEINTRESOURCEW(1));c.hIconSm=c.hIcon;c.lpszClassName=L"WinIsland.ModManager";RegisterClassExW(&c);
    float dpi=parent?GetDpiForWindow(parent)/96.f:1.f;RECT anchor{};if(parent)GetWindowRect(parent,&anchor);
    MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(parent,MONITOR_DEFAULTTONEAREST),&monitor);
    int width=std::min<int>(int(860*dpi),monitor.rcWork.right-monitor.rcWork.left),height=std::min<int>(int(660*dpi),monitor.rcWork.bottom-monitor.rcWork.top);
    int left=std::clamp<int>(anchor.left+int(20*dpi),monitor.rcWork.left,monitor.rcWork.right-width),top=std::clamp<int>(anchor.top+int(20*dpi),monitor.rcWork.top,monitor.rcWork.bottom-height);
    auto model=new Manager(loader,parent);
    if(parent){
        RECT client{};GetClientRect(parent,&client);
        managerWindow=CreateWindowExW(WS_EX_CONTROLPARENT,c.lpszClassName,L"插件管理",WS_CHILD|WS_CLIPCHILDREN|WS_CLIPSIBLINGS,0,0,client.right,client.bottom,parent,nullptr,c.hInstance,model);
        ShowWindow(GetDlgItem(parent,300),SW_HIDE);
        for(int id:{200,201,202,250,251,252,253,254})ShowWindow(GetDlgItem(parent,id),SW_HIDE);
        // Commit the parent surface immediately after its settings controls
        // are hidden.  The embedded manager then expands over a stable dark
        // canvas instead of exposing one frame of the old navigation layer.
        RedrawWindow(parent,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);
        if(managerWindow) ShowWindow(managerWindow,SW_SHOW);
    }
    else managerWindow=CreateWindowExW(WS_EX_CONTROLPARENT,c.lpszClassName,L"WinIsland · 插件管理 · 1.3.1alpha 社区版",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,left,top,width,height,parent,nullptr,c.hInstance,model);
    if(managerWindow){BOOL dark=FALSE;DwmSetWindowAttribute(managerWindow,20,&dark,sizeof(dark));ShowWindow(managerWindow,SW_SHOW);SetForegroundWindow(managerWindow);}
}
void closeModManager(){if(IsWindow(managerWindow))DestroyWindow(managerWindow);}
bool modManagerMessage(MSG& message){return IsWindow(managerWindow)&&IsDialogMessageW(managerWindow,&message);}

// Integration checks operate the real native controls and the real loader, in an
// isolated --mods-test process. Diagnostic DPI messages are not physical monitors.
int modUiTest(ModLoader& loader,const fs::path& output){
    std::ostringstream report;int failed=0;
    auto check=[&](bool value,const char* label){report<<(value?"PASS ":"FAIL ")<<label<<'\n';if(!value)++failed;};
    auto pump=[](double seconds){double until=now()+seconds;do{MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){if(!modManagerMessage(msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}Sleep(1);}while(now()<until);};
    auto click=[&](int id){SendMessageW(GetDlgItem(managerWindow,id),BM_CLICK,0,0);pump(.01);};
    openModManager(nullptr,loader);ShowWindow(managerWindow,SW_SHOWNORMAL);pump(.08);
    auto p=(Manager*)GetWindowLongPtrW(managerWindow,GWLP_USERDATA);
    if(!p){check(false,"window creation");writeAtomic(output/L"mod-ui-tests.txt",report.str());return 1;}
    check(IsWindowVisible(p->item(ListPage)),"hub has independent module-management entry");
    click(ListPage);check(p->page==1&&IsWindowVisible(p->item(List)),"entry opens real module list");
    check((size_t)SendMessageW(p->item(List),LB_GETCOUNT,0,0)==loader.snapshot().mods.size(),"visible list comes from loader records");
    auto capture=[&](const fs::path& file){
        RECT rc;GetClientRect(managerWindow,&rc);HDC dc=GetDC(managerWindow),mem=CreateCompatibleDC(dc);BITMAPINFO bi{};bi.bmiHeader={sizeof(BITMAPINFOHEADER),rc.right,-rc.bottom,1,32,BI_RGB};void* bits=nullptr;
        HBITMAP bitmap=CreateDIBSection(dc,&bi,DIB_RGB_COLORS,&bits,nullptr,0);auto old=SelectObject(mem,bitmap);PrintWindow(managerWindow,mem,PW_CLIENTONLY);DWORD size=rc.right*rc.bottom*4;
        BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);header.bfSize=header.bfOffBits+size;
        std::ofstream out(file,std::ios::binary);out.write((char*)&header,sizeof(header));out.write((char*)&bi.bmiHeader,sizeof(BITMAPINFOHEADER));out.write((char*)bits,size);
        SelectObject(mem,old);DeleteObject(bitmap);DeleteDC(mem);ReleaseDC(managerWindow,dc);
    };
    for(auto scale:{1.f,1.25f,1.5f,2.f}){
        UINT dpi=UINT(scale*96);RECT rect{30,30,30+LONG(800*scale),30+LONG(640*scale)};
        SendMessageW(managerWindow,WM_DPICHANGED,MAKELONG(dpi,dpi),(LPARAM)&rect);pump(.04);
        RECT client;GetClientRect(managerWindow,&client);bool inside=true,overlap=false;
        std::vector<RECT> rects;
        for(HWND c=GetWindow(managerWindow,GW_CHILD);c;c=GetWindow(c,GW_HWNDNEXT))if(IsWindowVisible(c)){
            RECT r;GetWindowRect(c,&r);MapWindowPoints(nullptr,managerWindow,(POINT*)&r,2);
            if(r.left<0||r.top<0||r.right>client.right||r.bottom>client.bottom)inside=false;
            for(auto other:rects){RECT intersection;if(IntersectRect(&intersection,&r,&other))overlap=true;}rects.push_back(r);
        }
        check(inside&&!overlap,("DPI "+std::to_string(dpi)+" visible controls inside client with no overlap").c_str());
        capture(output/wide("mod-manager-"+std::to_string(dpi)+".bmp"));
    }
    SendMessageW(p->item(Filter),CB_SETCURSEL,1,0);SendMessageW(managerWindow,WM_COMMAND,MAKEWPARAM(Filter,CBN_SELCHANGE),(LPARAM)p->item(Filter));
    check(p->ids.size()==(size_t)std::count_if(p->state.mods.begin(),p->state.mods.end(),[](auto& m){return m.enabled;}),"enabled filter uses runtime state");
    SendMessageW(p->item(Filter),CB_SETCURSEL,0,0);SendMessageW(managerWindow,WM_COMMAND,MAKEWPARAM(Filter,CBN_SELCHANGE),(LPARAM)p->item(Filter));
    auto a=std::find(p->ids.begin(),p->ids.end(),"a");if(a!=p->ids.end()){SendMessageW(p->item(List),LB_SETCURSEL,a-p->ids.begin(),0);SendMessageW(managerWindow,WM_COMMAND,MAKEWPARAM(List,LBN_SELCHANGE),0);}
    click(SettingsPage);check(IsWindowVisible(p->item(Value))&&!p->handles.empty(),"plugin settings page shows registered resources");
    SendMessageW(p->item(ResourceList),LB_SETCURSEL,0,0);SendMessageW(managerWindow,WM_COMMAND,MAKEWPARAM(ResourceList,LBN_SELCHANGE),0);
    SetWindowTextW(p->item(Value),L"saved from native UI");click(Apply);double deadline=now()+8;while(loader.busy()&&now()<deadline)pump(.02);
    auto state=loader.snapshot();check(std::any_of(state.resources.begin(),state.resources.end(),[](auto& r){return r.owner=="a"&&r.kind==WI_SETTING&&r.value=="saved from native UI";}),"native edit and save invokes real setting callback");
    click(Back);SetFocus(p->item(Filter));MSG tab{};tab.hwnd=p->item(Filter);tab.message=WM_KEYDOWN;tab.wParam=VK_TAB;modManagerMessage(tab);check(GetFocus()!=p->item(Filter),"native Tab navigation advances focus");
    SendMessageW(p->item(List),WM_VSCROLL,SB_BOTTOM,0);check(SendMessageW(p->item(List),LB_GETTOPINDEX,0,0)>0,"native list scroll reaches bottom");SendMessageW(p->item(List),WM_VSCROLL,SB_TOP,0);
    auto batchClick=[&](int id,int answer){testAnswer=answer;testConfirmed=false;auto timer=SetTimer(nullptr,0,40,confirmTestDialog);click(id);KillTimer(nullptr,timer);check(testConfirmed,"batch action displays actual confirmation dialog");double end=now()+8;while(loader.busy()&&now()<end)pump(.02);p->refresh(true);};
    auto liveBefore=std::count_if(p->state.mods.begin(),p->state.mods.end(),[](auto& m){return m.enabled;});
    batchClick(DisableAll,IDNO);check(std::count_if(p->state.mods.begin(),p->state.mods.end(),[](auto& m){return m.enabled;})==liveBefore,"cancel disable-all retains live plugins");
    batchClick(DisableAll,IDYES);check(p->state.resources.empty(),"disable-all button really restores resources");
    check(std::any_of(p->state.mods.begin(),p->state.mods.end(),[](auto& m){return m.packageFile=="bad-json.wimod"&&m.status=="加载失败";}),"disable-all preserves pre-existing failure diagnosis");
    batchClick(UnloadAll,IDYES);check(p->state.message=="全部插件已卸载","unload-all button reaches completion state");
    DWORD before=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);double start=now();
    for(int i=0;i<100;++i){p->pageTo(0);p->pageTo(1);p->pageTo(2);p->pageTo(1);}
    DWORD after=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);check(after<=before+2,"400 page transitions do not grow GDI objects");report<<"400 transitions elapsed ms="<<(now()-start)*1000<<" GDI before="<<before<<" after="<<after<<'\n';
    closeModManager();check(!IsWindow(managerWindow),"closing manager destroys controls");
    openModManager(nullptr,loader);click(ListPage);p=(Manager*)GetWindowLongPtrW(managerWindow,GWLP_USERDATA);check(!p->ids.empty(),"reopen retains loader state");closeModManager();
    writeAtomic(output/L"mod-ui-tests.txt",report.str());return failed?1:0;
}
} // namespace wi


