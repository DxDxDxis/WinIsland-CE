// Included inside App. The input-only island HWND has NOREDIRECTIONBITMAP:
// GDI children cannot paint there. A single owned, opaque editor surface stays
// inside the island; it has no independent lifetime, taskbar entry or frame.
static std::wstring clipboardForEdit(const std::wstring& text){
    std::wstring result;result.reserve(text.size());for(size_t i=0;i<text.size();++i){if(text[i]==L'\n'&&(i==0||text[i-1]!=L'\r'))result+=L'\r';result+=text[i];}return result;
}
std::wstring clipboardText() {
    if(!clipboardEdit)return clipboardDraft;
    int n=GetWindowTextLengthW(clipboardEdit);std::wstring text(n+1,L'\0');GetWindowTextW(clipboardEdit,text.data(),n+1);text.resize(n);
    // Win32 EDIT requires CRLF to draw line breaks. Keep the stored source
    // unchanged unless edited, and keep LF-only sources LF-only when saving.
    if(text==clipboardForEdit(clipboardDraft))return clipboardDraft;
    if(clipboardDraft.find(L'\r')==std::wstring::npos){std::wstring plain;plain.reserve(text.size());for(size_t i=0;i<text.size();++i)if(text[i]!=L'\r'||i+1==text.size()||text[i+1]!=L'\n')plain+=text[i];return plain;}
    return text;
}
void clipboardSync(bool captured=false) {
    if(!clipboard)return;auto state=clipboard->snapshot(false);clipboardRevision=state.revision;
    scene.clipboardItems.clear();for(size_t i=0;i<state.entries.size()&&i<4;++i)scene.clipboardItems.push_back({state.entries[i].id,state.entries[i].text});
    if(!state.preferences.show || state.entries.empty())clipboardMode=0;
    else if(captured&&clipboardMode!=2&&!receiving){clipboardPasteTarget=GetForegroundWindow();clipboardMode=1;queue.clear();holdUntil=0;closingNotice=bool(scene.notice);switching=awaitHold=false;}
    if(clipboardMode==2&&std::none_of(state.entries.begin(),state.entries.end(),[&](auto& e){return e.id==clipboardId;}))clipboardMode=state.entries.empty()?0:1;
    scene.clipboardMode=receiving?0:clipboardMode;
    onMusic(rawMusic,true);retarget(.46);inputShape.clear();dirty=true;
}
void clipboardTick() {
    if(!clipboard)return;
    {std::lock_guard lock(clipboardResultMutex);if(clipboardResultReady){clipboardResultReady=false;clipboardPending=false;scene.clipboardStatus=clipboardResult;if(clipboardResult==L"修改已保存"&&!clipboardId.empty()){auto current=clipboard->snapshot();for(auto& e:current.entries)if(e.id==clipboardId){clipboardVersion=e.version;clipboardOriginalDraft=e.text;break;}}dirty=true;}}
    if(clipboardHold>0&&now()-clipboardHold>.5){clipboardHold=0;pressedButton=-1;ReleaseCapture();clipboardMenu();}
    if(scene.clipboardMode!=(receiving?0:clipboardMode)){scene.clipboardMode=receiving?0:clipboardMode;clipboardLayout();dirty=true;}
}
void clipboardLayout() {
    if(!clipboardEdit)return;
    if(clipboardMode!=2||receiving||suspended){ShowWindow(clipboardEditorHost,SW_HIDE);return;}
    RECT rect{};GetClientRect(inputWindow,&rect);double z=scene.contentScale*dpi;int width=(int)(scene.width*dpi),height=(int)(scene.height*dpi);
    int y=(int)(86*z),pad=(int)(14*z);int h=height-y-(int)(28*z);
    if(h<32||width<200){ShowWindow(clipboardEditorHost,SW_HIDE);return;}
    POINT origin{(rect.right-width)/2+pad,y};ClientToScreen(inputWindow,&origin);
    RECT previous{};GetWindowRect(clipboardEditorHost,&previous);int w=std::max(80,width-pad*2);
    if(previous.left!=origin.x||previous.top!=origin.y||previous.right-previous.left!=w||previous.bottom-previous.top!=h){
        SetWindowPos(clipboardEditorHost,HWND_TOPMOST,origin.x,origin.y,w,h,SWP_NOACTIVATE|SWP_NOCOPYBITS);
        int inset=std::max(4,(int)(6*z)),scroll=std::max(12,(int)(14*z));
        SetWindowPos(clipboardEdit,nullptr,inset,inset,std::max(40,w-inset-scroll),std::max(16,h-inset*2),SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOCOPYBITS);
        RedrawWindow(clipboardEditorHost,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);
    }
    int pixels=std::max(10,(int)std::lround(14*z));
    if(pixels!=clipboardFontPixels){
        auto font=CreateFontW(-pixels,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        SendMessageW(clipboardEdit,WM_SETFONT,(WPARAM)font,TRUE);if(clipboardFont)DeleteObject(clipboardFont);clipboardFont=font;clipboardFontPixels=pixels;
    }
    if(!IsWindowVisible(clipboardEditorHost))ShowWindow(clipboardEditorHost,SW_SHOWNOACTIVATE);
    if(clipboardFocusPending&&!animating){clipboardFocusPending=false;SetForegroundWindow(clipboardEditorHost);SetFocus(clipboardEdit);}
}
struct ClipboardScroll {int total=1,visible=1,first=0;RECT track{},thumb{};};
ClipboardScroll clipboardScrollMetrics(){
    ClipboardScroll s;RECT r{},edit{};GetClientRect(clipboardEditorHost,&r);GetClientRect(clipboardEdit,&edit);
    HDC dc=GetDC(clipboardEdit);auto old=SelectObject(dc,clipboardFont);TEXTMETRICW tm{};GetTextMetricsW(dc,&tm);SelectObject(dc,old);ReleaseDC(clipboardEdit,dc);
    s.total=std::max(1,(int)SendMessageW(clipboardEdit,EM_GETLINECOUNT,0,0));s.visible=std::max(1,(int)edit.bottom/std::max(1,(int)tm.tmHeight));s.first=(int)SendMessageW(clipboardEdit,EM_GETFIRSTVISIBLELINE,0,0);
    int pad=std::max(3,(int)(4*dpi)),bar=std::max(4,(int)(5*dpi));s.track={r.right-pad-bar,pad,r.right-pad,r.bottom-pad};
    int extent=std::max(1,(int)(s.track.bottom-s.track.top)),length=std::min(extent,std::max(18,(int)(extent*std::min(1.,double(s.visible)/s.total))));
    int offset=(int)((extent-length)*std::clamp(double(s.first)/std::max(1,s.total-s.visible),0.,1.));s.thumb={s.track.left,s.track.top+offset,s.track.right,s.track.top+offset+length};return s;
}
void clipboardScrollTo(int y){auto s=clipboardScrollMetrics();if(s.total<=s.visible)return;int extent=std::max(1,(int)(s.track.bottom-s.track.top-(s.thumb.bottom-s.thumb.top)));double p=double(y-s.track.top-(s.thumb.bottom-s.thumb.top)/2)/extent;int line=(int)std::lround(std::clamp(p,0.,1.)*(s.total-s.visible));SendMessageW(clipboardEdit,EM_LINESCROLL,0,line-s.first);InvalidateRect(clipboardEditorHost,nullptr,FALSE);}
static LRESULT CALLBACK clipboardHostProc(HWND h,UINT msg,WPARAM wp,LPARAM lp){
    auto a=(App*)GetWindowLongPtrW(h,GWLP_USERDATA);if(msg==WM_NCCREATE){a=(App*)((CREATESTRUCTW*)lp)->lpCreateParams;SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)a);}if(!a)return DefWindowProcW(h,msg,wp,lp);
    if(msg==WM_CTLCOLOREDIT||msg==WM_CTLCOLORSTATIC){SetTextColor((HDC)wp,RGB(237,240,248));SetBkColor((HDC)wp,RGB(19,21,26));return (LRESULT)a->clipboardBrush;}
    if(msg==WM_ERASEBKGND)return 1;
    if(msg==WM_PAINT){PAINTSTRUCT ps;auto dc=BeginPaint(h,&ps);RECT r{};GetClientRect(h,&r);FillRect(dc,&r,a->clipboardBrush);if(a->clipboardEdit){auto s=a->clipboardScrollMetrics();if(s.total>s.visible){auto brush=CreateSolidBrush(RGB(109,118,135));FillRect(dc,&s.thumb,brush);DeleteObject(brush);}}EndPaint(h,&ps);return 0;}
    if(msg==WM_COMMAND){if(HIWORD(wp)==EN_VSCROLL||HIWORD(wp)==EN_CHANGE)InvalidateRect(h,nullptr,FALSE);return 0;}
    if(msg==WM_LBUTTONDOWN){a->clipboardScrollDrag=true;SetCapture(h);a->clipboardScrollTo(GET_Y_LPARAM(lp));return 0;}
    if(msg==WM_MOUSEMOVE&&a->clipboardScrollDrag){a->clipboardScrollTo(GET_Y_LPARAM(lp));return 0;}
    if(msg==WM_LBUTTONUP||msg==WM_CAPTURECHANGED){a->clipboardScrollDrag=false;if(GetCapture()==h)ReleaseCapture();return 0;}
    if(msg==WM_MOUSEWHEEL)return SendMessageW(a->clipboardEdit,msg,wp,lp);
    if(msg==WM_SETFOCUS){SetFocus(a->clipboardEdit);return 0;}
    if(msg==WM_CLOSE){a->clipboardAction(825);return 0;}
    return DefWindowProcW(h,msg,wp,lp);
}
static LRESULT CALLBACK clipboardEditProc(HWND h,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data){auto& a=*(App*)data;
    if(msg==WM_SETTEXT&&lp){auto value=clipboardForEdit((const wchar_t*)lp);return DefSubclassProc(h,msg,wp,(LPARAM)value.c_str());}
    if(msg==WM_PASTE){
        if(!OpenClipboard(h)){a.scene.clipboardStatus=L"剪贴板正忙，请重试粘贴";a.dirty=true;return 0;}
        std::wstring value;auto mem=GetClipboardData(CF_UNICODETEXT);auto bytes=mem?GlobalSize(mem):0;
        if(bytes&&bytes<=(ClipboardTextLimit+1)*sizeof(wchar_t)){auto p=(wchar_t*)GlobalLock(mem);if(p){size_t n=0;while(n<bytes/sizeof(wchar_t)&&p[n])++n;if(n<bytes/sizeof(wchar_t))value.assign(p,n);GlobalUnlock(mem);}}
        CloseClipboard();if(!value.empty()){value=clipboardForEdit(value);SendMessageW(h,EM_REPLACESEL,TRUE,(LPARAM)value.c_str());}return 0;
    }
    if(msg==WM_COPY){DWORD first=0,last=0;SendMessageW(h,EM_GETSEL,(WPARAM)&first,(LPARAM)&last);int n=GetWindowTextLengthW(h);std::wstring s(n+1,L'\0');GetWindowTextW(h,s.data(),n+1);s.resize(n);try{if(last>first)a.clipboard->copyText(s.substr(first,last-first));}catch(const std::exception& e){a.scene.clipboardStatus=wide(e.what());a.dirty=true;}return 0;}
    if(msg==WM_KEYDOWN&&wp==VK_ESCAPE){a.clipboardAction(825);return 0;}
    if(msg==WM_KEYDOWN&&wp==VK_TAB){SetFocus(a.inputWindow);a.scene.keyboard=true;a.scene.focus=820;a.dirty=true;return 0;}
    if(msg==WM_KEYDOWN&&GetKeyState(VK_CONTROL)<0&&wp=='S'){a.clipboardAction(820);return 0;}
    if(msg==WM_CONTEXTMENU){HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,1,L"复制选中文字");AppendMenuW(menu,MF_STRING,2,L"删除选中文字");AppendMenuW(menu,MF_STRING,3,L"翻译选中文字");AppendMenuW(menu,MF_STRING,4,L"粘贴");POINT p;GetCursorPos(&p);int id=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,h,nullptr);DestroyMenu(menu);if(id==1)SendMessageW(h,WM_COPY,0,0);if(id==2)SendMessageW(h,EM_REPLACESEL,TRUE,(LPARAM)L"");if(id==3)a.clipboardAction(823);if(id==4)SendMessageW(h,WM_PASTE,0,0);return 0;}
    auto result=DefSubclassProc(h,msg,wp,lp);if(msg==WM_MOUSEWHEEL||msg==WM_KEYUP||msg==WM_SIZE)InvalidateRect(a.clipboardEditorHost,nullptr,FALSE);return result;
}
void clipboardOpen(const std::wstring& id) {
    using namespace winrt::Windows::Data::Json;
    if(clipboardMode==2&&clipboardText()!=clipboardOriginalDraft){scene.clipboardStatus=L"请先保存修改或取消，再切换记录";dirty=true;return;}
    JsonObject p;p.Insert(L"id",JsonValue::CreateStringValue(id));auto e=clipboard->command("get",p).GetNamedObject(L"entry");clipboardId=id;clipboardVersion=(uint64_t)e.GetNamedNumber(L"version");clipboardDraft=e.GetNamedString(L"text");clipboardOriginalDraft=clipboardDraft;
    if(!clipboardEditorHost){WNDCLASSW wc{};wc.lpfnWndProc=clipboardHostProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"WinIsland.Clipboard.EditorSurface";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);clipboardEditorHost=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST,wc.lpszClassName,L"复制粘贴编辑区域",WS_POPUP|WS_CLIPCHILDREN,0,0,10,10,inputWindow,nullptr,wc.hInstance,this);if(!clipboardEditorHost)throw std::runtime_error("编辑绘制表面创建失败");}
    if(!clipboardEdit){clipboardEdit=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN,0,0,10,10,clipboardEditorHost,(HMENU)840,GetModuleHandleW(nullptr),nullptr);if(!clipboardEdit)throw std::runtime_error("编辑控件创建失败");SendMessageW(clipboardEdit,EM_SETLIMITTEXT,ClipboardTextLimit*2,0);SetWindowSubclass(clipboardEdit,clipboardEditProc,1,(DWORD_PTR)this);}
    clipboardFocusPending=true;
    SetWindowTextW(clipboardEdit,clipboardForEdit(clipboardDraft).c_str());clipboardMode=scene.clipboardMode=2;scene.clipboardStatus=L"修改后需保存；复制草稿不会覆盖历史";onMusic(rawMusic,true);retarget(.5);dirty=true;SetForegroundWindow(inputWindow);SetFocus(inputWindow);
}
void clipboardTask(const std::string& action,const ClipboardJson& params) {
    if(clipboardPending)return;clipboardPending=true;scene.clipboardStatus=L"正在处理…";dirty=true;
    jobs.post([this,action,params]{std::wstring result;try{if(action=="translate"){auto reply=clipboard->command("translate-start",params);while(reply.GetNamedBoolean(L"pending",false)){Sleep(100);reply=clipboard->command("translation-read",params);}auto error=reply.GetNamedString(L"error",L"");if(!error.empty())throw std::runtime_error(utf8(error.c_str()));}else clipboard->command(action,params);result=action=="save"?L"修改已保存":L"操作已完成";}catch(const std::exception& e){result=wide(e.what());}catch(...){result=L"操作失败，历史未确认更改";}std::lock_guard lock(clipboardResultMutex);clipboardResult=result;clipboardResultReady=true;});
}
void clipboardAction(int id) {
    using namespace winrt::Windows::Data::Json;
    if(!clipboard)return;
    try {
        if(id>=800&&id<804){auto i=id-800;if(i<(int)scene.clipboardItems.size())clipboardOpen(scene.clipboardItems[i].first);return;}
        if(id==810){clipboardMode=scene.clipboardMode=0;onMusic(rawMusic,true);retarget(.4);dirty=true;return;}
        if(id==811){clipboard->requestView(L"");openSettings();return;}
        JsonObject p;p.Insert(L"id",JsonValue::CreateStringValue(clipboardId));
        if(id==825){if(clipboardPending){scene.clipboardStatus=L"请等待保存完成，或取消翻译";dirty=true;return;}if(clipboardText()!=clipboardOriginalDraft&&MessageBoxW(inputWindow,L"放弃尚未保存的修改？",L"复制粘贴",MB_YESNO|MB_ICONQUESTION)!=IDYES)return;clipboardMode=scene.clipboardMode=1;ShowWindow(clipboardEditorHost,SW_HIDE);clipboardFocusPending=false;SetForegroundWindow(inputWindow);SetFocus(inputWindow);scene.clipboardStatus.clear();retarget(.4);dirty=true;return;}
        if(id==826){const wchar_t* labels[]={L"自动",L"简体中文",L"繁体中文",L"英文",L"美式英文",L"日文",L"韩文",L"俄语",L"印地语",L"德语"};HMENU m=CreatePopupMenu();for(int i=0;i<10;++i)AppendMenuW(m,MF_STRING|(i==clipboardLanguage?MF_CHECKED:0),i+1,labels[i]);POINT p;GetCursorPos(&p);auto c=TrackPopupMenu(m,TPM_RETURNCMD,p.x,p.y,0,inputWindow,nullptr);DestroyMenu(m);if(c){clipboardLanguage=c-1;scene.clipboardTarget=labels[c-1];dirty=true;}return;}
        auto draft=clipboardText();
        if(id==821||id==822){clipboard->copyText(draft);scene.clipboardStatus=L"已复制草稿，历史未被覆盖";if(id==822){DWORD pid=0;GetWindowThreadProcessId(clipboardPasteTarget,&pid);if(!IsWindow(clipboardPasteTarget)||pid==GetCurrentProcessId()||!SetForegroundWindow(clipboardPasteTarget))throw std::runtime_error("无法恢复目标应用焦点；内容已复制，请在目标位置按 Ctrl+V");INPUT input[4]{};for(auto& x:input)x.type=INPUT_KEYBOARD;input[0].ki.wVk=VK_CONTROL;input[1].ki.wVk='V';input[2].ki.wVk='V';input[2].ki.dwFlags=KEYEVENTF_KEYUP;input[3].ki.wVk=VK_CONTROL;input[3].ki.dwFlags=KEYEVENTF_KEYUP;if(SendInput(4,input,sizeof(INPUT))!=4)throw std::runtime_error("目标权限不允许模拟粘贴；请手动 Ctrl+V");scene.clipboardStatus=L"已发送粘贴快捷键，请在目标应用确认";}dirty=true;return;}
        if(id==820){p.Insert(L"text",JsonValue::CreateStringValue(draft));p.Insert(L"version",JsonValue::CreateNumberValue(double(clipboardVersion)));clipboardTask("save",p);return;}
        if(id==824){if(MessageBoxW(inputWindow,L"只删除该历史记录？系统剪贴板不受影响。",L"复制粘贴",MB_YESNO|MB_ICONQUESTION)==IDYES)clipboardTask("delete",p);return;}
        if(id==823){DWORD first=0,last=0;SendMessageW(clipboardEdit,EM_GETSEL,(WPARAM)&first,(LPARAM)&last);if(last>first){int n=GetWindowTextLengthW(clipboardEdit);std::wstring raw(n+1,L'\0');GetWindowTextW(clipboardEdit,raw.data(),n+1);raw.resize(n);draft=raw.substr(first,last-first);}const wchar_t* codes[]={L"auto",L"zh-Hans",L"zh-Hant",L"en",L"en-US",L"ja",L"ko",L"ru",L"hi",L"de"};p.Insert(L"text",JsonValue::CreateStringValue(draft));p.Insert(L"requestId",JsonValue::CreateStringValue(std::to_wstring(GetTickCount64())));p.Insert(L"target",JsonValue::CreateStringValue(codes[clipboardLanguage]));clipboardTask("translate",p);return;}
    }catch(const std::exception& e){scene.clipboardStatus=wide(e.what());dirty=true;}
}
void clipboardMenu() {
    if(receiving||clipboardMode!=1)return;POINT p;GetCursorPos(&p);POINT client=p;ScreenToClient(inputWindow,&client);int id=inputAt(client.x/dpi,client.y/dpi);if(id<800||id>=804){id=scene.focus;if(id<800||id>=804)return;}auto index=id-800;if(index>=(int)scene.clipboardItems.size())return;auto key=scene.clipboardItems[index].first;
    HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,1,L"复制");AppendMenuW(menu,MF_STRING,2,L"删除");AppendMenuW(menu,MF_STRING,3,L"预览与修改");AppendMenuW(menu,MF_STRING,4,L"翻译");SetTimer(hwnd,1,16,nullptr);int choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,inputWindow,nullptr);KillTimer(hwnd,1);DestroyMenu(menu);
    try{if(choice==1){clipboard->copy(key);scene.clipboardStatus=L"已复制";dirty=true;}if(choice==2){ClipboardJson q;q.Insert(L"id",winrt::Windows::Data::Json::JsonValue::CreateStringValue(key));clipboardTask("delete",q);}if(choice==3||choice==4){clipboardOpen(key);if(choice==4)clipboardAction(823);}}catch(const std::exception& e){scene.clipboardStatus=wide(e.what());dirty=true;}
}
bool clipboardInput(HWND h,UINT msg,WPARAM wp,LPARAM lp) {
    if(!clipboardMode||receiving)return false;
    if(msg==WM_KEYDOWN&&wp==VK_ESCAPE){clipboardAction(clipboardMode==2?825:810);return true;}
    if(msg==WM_KEYDOWN&&(wp==VK_APPS||(wp==VK_F10&&GetKeyState(VK_SHIFT)<0))){clipboardMenu();return true;}
    if(msg==WM_RBUTTONUP||msg==WM_CONTEXTMENU){clipboardMenu();return true;}
    if(msg==WM_LBUTTONDOWN){int id=inputAt(GET_X_LPARAM(lp)/dpi,GET_Y_LPARAM(lp)/dpi);if(id>=800&&id<804)clipboardHold=now();}
    if(msg==WM_LBUTTONUP||msg==WM_CAPTURECHANGED||msg==WM_MOUSELEAVE)clipboardHold=0;
    if(msg==WM_MOUSEMOVE&&clipboardHold){POINT p;GetCursorPos(&p);if(std::abs(p.x-dragStart.x)>5||std::abs(p.y-dragStart.y)>5)clipboardHold=0;}
    if(msg==WM_KEYDOWN&&wp==VK_TAB&&clipboardMode==2&&scene.focus==825){SetFocus(clipboardEdit);return true;}
    return false;
}
