// Small native bootstrap UI: Electron itself is still inside the EXE here.
// The path stays editable; filesystem work and hashing run on a worker thread.
namespace {
struct InstallWindow {
    Bundle& bundle;fs::path root;std::wstring reason;bool automatic=false,running=false,done=false,success=false;
    HWND window=nullptr,pathEdit=nullptr;HFONT font=nullptr,title=nullptr;HBRUSH white=CreateSolidBrush(RGB(255,255,255));
    float dpi=1;Progress progress;std::thread worker;std::string error;bool workerSuccess=false;
    InstallWindow(Bundle& b,fs::path p,std::wstring why,bool a):bundle(b),root(std::move(p)),reason(std::move(why)),automatic(a){}
    ~InstallWindow(){if(worker.joinable())worker.join();if(font)DeleteObject(font);if(title)DeleteObject(title);DeleteObject(white);}
    int px(int n){return (int)std::lround(n*dpi);}
    void layout(){RECT r;GetClientRect(window,&r);int width=r.right;
        if(font)DeleteObject(font);if(title)DeleteObject(title);
        font=CreateFontW(-px(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
        title=CreateFontW(-px(25),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
        SetWindowPos(pathEdit,nullptr,px(42),px(162),width-px(212),px(32),SWP_NOZORDER);
        SetWindowPos(GetDlgItem(window,1002),nullptr,width-px(157),px(155),px(119),px(46),SWP_NOZORDER);
        SetWindowPos(GetDlgItem(window,1003),nullptr,width-px(214),r.bottom-px(74),px(176),px(44),SWP_NOZORDER);
        SetWindowPos(GetDlgItem(window,1004),nullptr,px(38),r.bottom-px(74),px(104),px(44),SWP_NOZORDER);
        for(int id:{1001,1002,1003,1004})SendMessageW(GetDlgItem(window,id),WM_SETFONT,(WPARAM)font,TRUE);
        InvalidateRect(window,nullptr,FALSE);
    }
    void text(HDC dc,const std::wstring& text,int x,int y,int w,int h,COLORREF color,HFONT f=nullptr){RECT r{px(x),px(y),px(x+w),px(y+h)};SelectObject(dc,f?f:font);SetTextColor(dc,color);SetBkMode(dc,TRANSPARENT);DrawTextW(dc,text.c_str(),-1,&r,DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);}
    void begin(){if(running)return;if(workerSuccess){success=done=true;DestroyWindow(window);return;}int length=GetWindowTextLengthW(pathEdit);std::wstring value(length+1,0);GetWindowTextW(pathEdit,value.data(),(int)value.size());value.resize(length);
        try{root=checkedRoot(value);writable(root,bundle.size*2+Margin);}catch(const std::exception& e){reason=wide(e.what());InvalidateRect(window,nullptr,FALSE);return;}
        running=true;error.clear();progress.cancel=false;progress.committing=false;progress.done=0;progress.say(L"检查安装目录…");
        EnableWindow(pathEdit,FALSE);EnableWindow(GetDlgItem(window,1002),FALSE);EnableWindow(GetDlgItem(window,1003),FALSE);
        SetWindowTextW(GetDlgItem(window,1004),L"取消安装");
        worker=std::thread([this]{winrt::init_apartment(winrt::apartment_type::multi_threaded);try{install(root,bundle,progress);workerSuccess=true;}catch(const std::exception& e){error=e.what();}catch(const winrt::hresult_error& e){error=utf8(e.message().c_str());}catch(...){error="安装失败，原数据与定位记录保留";}winrt::uninit_apartment();PostMessageW(window,WM_APP+70,0,0);});
    }
    void cancel(){if(running){if(!progress.committing){progress.cancel=true;progress.say(L"正在安全取消；原文件保留…");}return;}done=true;DestroyWindow(window);}
    LRESULT message(UINT msg,WPARAM w,LPARAM l){
        if(msg==WM_CREATE){pathEdit=CreateWindowExW(0,L"EDIT",root.c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,0,0,0,0,window,(HMENU)1001,nullptr,nullptr);SendMessageW(pathEdit,EM_SETLIMITTEXT,30000,0);
            for(auto item:std::vector<std::pair<int,const wchar_t*>>{{1002,L"选择文件夹"},{1003,L"安装并启动"},{1004,L"取消"}})CreateWindowExW(0,L"BUTTON",item.second,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,window,(HMENU)(INT_PTR)item.first,nullptr,nullptr);
            dpi=GetDpiForWindow(window)/96.f;layout();SetTimer(window,1,80,nullptr);if(automatic)PostMessageW(window,WM_COMMAND,1003,0);return 0;}
        if(msg==WM_COMMAND){switch(LOWORD(w)){case 1002:{auto p=chooseFolder(window);if(!p.empty())SetWindowTextW(pathEdit,p.c_str());break;}case 1003:begin();break;case 1004:cancel();break;case 1001:if(HIWORD(w)==EN_CHANGE)InvalidateRect(window,nullptr,FALSE);break;}return 0;}
        if(msg==WM_APP+70){if(worker.joinable())worker.join();running=false;if(workerSuccess){if(progress.conflicts){reason=L"安装完成；检测到 "+std::to_wstring(progress.conflicts.load())+L" 个历史文件冲突。保留目标文件，源副本和详情位于数据目录 migration/conflicts。请核对后启动。";EnableWindow(GetDlgItem(window,1003),TRUE);SetWindowTextW(GetDlgItem(window,1003),L"启动主程序");SetWindowTextW(GetDlgItem(window,1004),L"关闭");EnableWindow(GetDlgItem(window,1004),TRUE);InvalidateRect(window,nullptr,FALSE);}else{success=done=true;DestroyWindow(window);}}else{reason=wide(error)+L"。原位置仍可回退，修改路径后可重试。";EnableWindow(pathEdit,TRUE);EnableWindow(GetDlgItem(window,1002),TRUE);EnableWindow(GetDlgItem(window,1003),TRUE);SetWindowTextW(GetDlgItem(window,1004),L"取消");InvalidateRect(window,nullptr,FALSE);}return 0;}
        if(msg==WM_CLOSE){cancel();return 0;}
        if(msg==WM_DPICHANGED){dpi=HIWORD(w)/96.f;auto r=(RECT*)l;SetWindowPos(window,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER);layout();return 0;}
        if(msg==WM_SIZE){if(pathEdit)layout();return 0;}
        if(msg==WM_TIMER){if(running){EnableWindow(GetDlgItem(window,1004),!progress.committing);InvalidateRect(window,nullptr,FALSE);}return 0;}
        if(msg==WM_ERASEBKGND)return 1;
        if(msg==WM_CTLCOLOREDIT||msg==WM_CTLCOLORSTATIC){SetTextColor((HDC)w,RGB(32,39,55));SetBkColor((HDC)w,RGB(255,255,255));return (LRESULT)white;}
        if(msg==WM_DRAWITEM){auto d=(DRAWITEMSTRUCT*)l;auto canvas=CreateSolidBrush(d->CtlID==1002?RGB(255,255,255):RGB(245,246,249));FillRect(d->hDC,&d->rcItem,canvas);DeleteObject(canvas);bool disabled=d->itemState&ODS_DISABLED,pressed=d->itemState&ODS_SELECTED,primary=d->CtlID==1003;auto color=disabled?RGB(238,240,244):primary?(pressed?RGB(38, 70,180):RGB(49,93,221)):(pressed?RGB(225,234,255):RGB(255,255,255));
            auto brush=CreateSolidBrush(color);auto pen=CreatePen(PS_SOLID,px(d->itemState&ODS_FOCUS?2:1),d->itemState&ODS_FOCUS?RGB(49,93,221):RGB(220,225,235));auto oldB=SelectObject(d->hDC,brush),oldP=SelectObject(d->hDC,pen);RoundRect(d->hDC,d->rcItem.left+1,d->rcItem.top+1,d->rcItem.right-1,d->rcItem.bottom-1,px(16),px(16));SelectObject(d->hDC,oldB);SelectObject(d->hDC,oldP);DeleteObject(brush);DeleteObject(pen);SelectObject(d->hDC,font);SetBkMode(d->hDC,TRANSPARENT);SetTextColor(d->hDC,disabled?RGB(120,128,143):primary?RGB(255,255,255):RGB(32,39,55));wchar_t label[80];GetWindowTextW(d->hwndItem,label,80);DrawTextW(d->hDC,label,-1,&d->rcItem,DT_CENTER|DT_VCENTER|DT_SINGLELINE);return TRUE;}
        if(msg==WM_PAINT){PAINTSTRUCT ps;HDC paint=BeginPaint(window,&ps);RECT area;GetClientRect(window,&area);auto dc=CreateCompatibleDC(paint);auto bitmap=CreateCompatibleBitmap(paint,area.right,area.bottom);auto old=SelectObject(dc,bitmap);auto bg=CreateSolidBrush(RGB(245,246,249));FillRect(dc,&area,bg);DeleteObject(bg);int width=(int)(area.right/dpi);
            auto pen=CreatePen(PS_SOLID,1,RGB(220,225,235));auto oldP=SelectObject(dc,pen),oldB=SelectObject(dc,white);RoundRect(dc,px(24),px(118),area.right-px(24),px(268),px(24),px(24));SelectObject(dc,oldP);SelectObject(dc,oldB);DeleteObject(pen);
            text(dc,L"WinIsland 1.3.0 社区版",30,24,width-60,42,RGB(32,39,55),title);
            text(dc,L"选择一个数据家目录。组件、插件、设置和缓存都保存在这里，主 EXE 可自由移动。",32, 70,width-64,44,RGB(98,108,126));
            text(dc,L"安装与数据位置",38,128,width-76,26,RGB(32,39,55));
            std::wstring space=L"预计预留："+std::to_wstring((bundle.size*2+Margin)/1024/1024)+L" MiB（含暂存和更新余量）";
            wchar_t value[32768];GetWindowTextW(pathEdit,value,32768);try{space+=L"  ·  可用："+std::to_wstring(freeBytes(value)/1024/1024)+L" MiB";}catch(...){space+=L"  ·  目标磁盘不可用";}
            text(dc,space,38,218,width-76,42,RGB(98,108,126));
            text(dc,reason,32,280,width-64, 70, error.empty()?RGB(98,108,126):RGB(171,53,69));
            if(running){text(dc,progress.read(),32,350,width-64, 50,RGB(32,39,55));RECT track{px(32),px(407),area.right-px(32),px(414)};auto b=CreateSolidBrush(RGB(220,225,235));FillRect(dc,&track,b);DeleteObject(b);track.right=track.left+(int)((area.right-px(64))*std::min(1.,double(progress.done)/std::max<uint64_t>(1,progress.total)));b=CreateSolidBrush(RGB(49,93,221));FillRect(dc,&track,b);DeleteObject(b);}
            BitBlt(paint,0,0,area.right,area.bottom,dc,0,0,SRCCOPY);SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);EndPaint(window,&ps);return 0;}
        if(msg==WM_DESTROY){KillTimer(window,1);done=true;return 0;}
        return DefWindowProcW(window,msg,w,l);
    }
    static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){auto p=(InstallWindow*)GetWindowLongPtrW(h,GWLP_USERDATA);if(m==WM_NCCREATE){p=(InstallWindow*)((CREATESTRUCTW*)l)->lpCreateParams;p->window=h;SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)p);}return p?p->message(m,w,l):DefWindowProcW(h,m,w,l);}
    bool run(){WNDCLASSEXW wc{sizeof(wc)};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=proc;wc.lpszClassName=L"WinIsland.Installation";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassExW(&wc);
        dpi=GetDpiForSystem()/96.f;RECT r{0,0,px(720),px(516)};AdjustWindowRectExForDpi(&r,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU, FALSE,WS_EX_CONTROLPARENT,(UINT)(96*dpi));
        window=CreateWindowExW(WS_EX_CONTROLPARENT,wc.lpszClassName,L"WinIsland · 安装与数据目录",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,CW_USEDEFAULT,CW_USEDEFAULT,r.right-r.left,r.bottom-r.top,nullptr,nullptr,wc.hInstance,this);if(!window)return false;
        ShowWindow(window,SW_SHOW);UpdateWindow(window);SetFocus(pathEdit);MSG msg;while(!done&&GetMessageW(&msg,nullptr,0,0)>0){if(msg.message==WM_KEYDOWN&&msg.wParam==VK_ESCAPE){cancel();continue;}if(msg.message==WM_KEYDOWN&&msg.wParam==VK_RETURN&&msg.hwnd==pathEdit){begin();continue;}if(!IsDialogMessageW(window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
        return success;
    }
};
}
