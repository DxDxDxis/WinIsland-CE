#include "transfer.h"
#include "render.h"
#include "transfer_outline.h"
#include "transfer_paint.h"
#include "transfer_motion.h"
#include <ole2.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellscalingapi.h>
#include <dwmapi.h>
namespace wi {
using namespace winrt::Windows::Data::Json;
namespace {
std::atomic_bool outgoingDrag=false;
bool returnedToHost=false;
CLIPFORMAT sourceFormat(){static auto f=(CLIPFORMAT)RegisterClipboardFormatW(L"WinIsland.FileTransfer.Source.v1");return f;}
const GUID& sourceIdentity(){static GUID id=[] {GUID v{};CoCreateGuid(&v);return v;}();return id;}
bool ownSource(IDataObject* object){FORMATETC format{sourceFormat(),nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};STGMEDIUM medium{};
    if(!object||FAILED(object->GetData(&format,&medium)))return false;
    bool same=false;if(GlobalSize(medium.hGlobal)>=sizeof(GUID)){auto id=(GUID*)GlobalLock(medium.hGlobal);if(id){same=IsEqualGUID(*id,sourceIdentity());GlobalUnlock(medium.hGlobal);}}ReleaseStgMedium(&medium);return same;}
std::vector<fs::path> dropped(IDataObject* object){std::vector<fs::path> files;FORMATETC format{CF_HDROP,nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};STGMEDIUM medium{};
    if(!object||FAILED(object->GetData(&format,&medium)))return files;
    auto drop=(HDROP)medium.hGlobal;auto count=DragQueryFileW(drop,0xffffffff,nullptr,0);if(count<=2000)for(UINT i=0;i<count;++i){std::wstring name(DragQueryFileW(drop,i,nullptr,0)+1,L'\0');DragQueryFileW(drop,i,name.data(),(UINT)name.size());name.pop_back();files.emplace_back(name);}ReleaseStgMedium(&medium);return files;}
struct DropTarget:IDropTarget {
    ULONG refs=1;std::function<bool()> enabled;std::function<void(bool)> hover;std::function<void(std::vector<fs::path>)> receive;bool valid=false,internal=false;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** p)override{if(id==IID_IUnknown||id==IID_IDropTarget){*p=this;AddRef();return S_OK;}*p=nullptr;return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}ULONG STDMETHODCALLTYPE Release()override{auto r=--refs;if(!r)delete this;return r;}
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* o,DWORD,POINTL,DWORD* effect)override{internal=ownSource(o);FORMATETC f{CF_HDROP,nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};valid=!internal&&enabled()&&SUCCEEDED(o->QueryGetData(&f));*effect=valid?DROPEFFECT_COPY:DROPEFFECT_NONE;if(!internal)hover(valid);return S_OK;}
    HRESULT STDMETHODCALLTYPE DragOver(DWORD,POINTL,DWORD* effect)override{*effect=!internal&&valid&&enabled()?DROPEFFECT_COPY:DROPEFFECT_NONE;return S_OK;}
    HRESULT STDMETHODCALLTYPE DragLeave()override{if(!internal&&valid)hover(false);valid=internal=false;return S_OK;}
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* o,DWORD,POINTL,DWORD* effect)override{*effect=DROPEFFECT_NONE;if(internal||ownSource(o)){returnedToHost=true;valid=internal=false;return S_OK;}if(valid&&enabled()){auto files=dropped(o);if(!files.empty()){receive(std::move(files));*effect=DROPEFFECT_COPY;}}else hover(false);valid=false;return S_OK;}

};
struct FileObject:IDataObject {
    ULONG refs=1;fs::path file;bool internal;explicit FileObject(fs::path p,bool own=true):file(std::move(p)),internal(own){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** p)override{if(id==IID_IUnknown||id==IID_IDataObject){*p=this;AddRef();return S_OK;}*p=nullptr;return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}ULONG STDMETHODCALLTYPE Release()override{auto r=--refs;if(!r)delete this;return r;}
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* f)override{return f&&(f->cfFormat==CF_HDROP||(internal&&f->cfFormat==sourceFormat()))&&(f->tymed&TYMED_HGLOBAL)&&f->dwAspect==DVASPECT_CONTENT?S_OK:DV_E_FORMATETC;}
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* f,STGMEDIUM* m)override{if(FAILED(QueryGetData(f)))return DV_E_FORMATETC;if(f->cfFormat==sourceFormat()){auto h=GlobalAlloc(GHND,sizeof(GUID));if(!h)return E_OUTOFMEMORY;auto v=GlobalLock(h);memcpy(v,&sourceIdentity(),sizeof(GUID));GlobalUnlock(h);m->tymed=TYMED_HGLOBAL;m->hGlobal=h;m->pUnkForRelease=nullptr;return S_OK;}auto name=file.wstring();auto bytes=sizeof(DROPFILES)+(name.size()+2)*sizeof(wchar_t);auto h=GlobalAlloc(GHND,bytes);if(!h)return E_OUTOFMEMORY;auto d=(DROPFILES*)GlobalLock(h);d->pFiles=sizeof(DROPFILES);d->fWide=TRUE;memcpy((BYTE*)d+sizeof(DROPFILES),name.c_str(),(name.size()+1)*sizeof(wchar_t));GlobalUnlock(h);m->tymed=TYMED_HGLOBAL;m->hGlobal=h;m->pUnkForRelease=nullptr;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*,STGMEDIUM*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*,FORMATETC* o)override{if(o)o->ptd=nullptr;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*,STGMEDIUM*,BOOL)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD direction,IEnumFORMATETC** e)override{FORMATETC f[]={{CF_HDROP,nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL},{sourceFormat(),nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL}};return direction==DATADIR_GET?SHCreateStdEnumFmtEtc(internal?2:1,f,e):E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*,DWORD,IAdviseSink*,DWORD*)override{return OLE_E_ADVISENOTSUPPORTED;}
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD)override{return OLE_E_ADVISENOTSUPPORTED;}
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**)override{return OLE_E_ADVISENOTSUPPORTED;}
};
struct DragSource:IDropSource {
    ULONG refs=1;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** p)override{if(id==IID_IUnknown||id==IID_IDropSource){*p=this;AddRef();return S_OK;}*p=nullptr;return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}ULONG STDMETHODCALLTYPE Release()override{auto r=--refs;if(!r)delete this;return r;}
    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape,DWORD keys)override{return escape?DRAGDROP_S_CANCEL:!(keys&MK_LBUTTON)?DRAGDROP_S_DROP:S_OK;}
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD)override{return DRAGDROP_S_USEDEFAULTCURSORS;}
};
enum class DragResult { Cancelled, Returned, Copied, Rejected };
DragResult dragFile(const fs::path& p){if(outgoingDrag.exchange(true))return DragResult::Rejected;
    struct Session{~Session(){outgoingDrag=false;returnedToHost=false;}} session;returnedToHost=false;
    auto object=new FileObject(p);auto source=new DragSource;DWORD effect=0;auto hr=DoDragDrop(object,source,DROPEFFECT_COPY,&effect);source->Release();object->Release();
    return returnedToHost?DragResult::Returned:hr==DRAGDROP_S_CANCEL?DragResult::Cancelled:effect==DROPEFFECT_COPY?DragResult::Copied:DragResult::Rejected;
}
bool inside(RECT r,POINT p){return PtInRect(&r,p)!=FALSE;}
}
void transferDragFile(const fs::path& file){dragFile(file);}
bool transferDragActive(){return outgoingDrag.load();}

struct TransferWidget::Impl {
    enum class View { Compact, Panel, More, Floating };
    enum class Phase { Idle, Transition, Moving, Resizing, FileDrag };
    struct Shape {std::array<double,3> attachment{};double compact=1,cornerDip=23.1*MusicScale;bool operator==(const Shape&)const=default;};
    struct ReturnState { bool valid=false;View view=View::Panel;std::wstring dock;POINT anchor{};RECT bounds{},button{};int scroll=0; } source;
    struct Layout {RECT title{},search{},list{},footer{};std::vector<std::pair<std::wstring,RECT>> cards;} layout;
    struct PaintCache:TransferPaint {
        struct Shell {HRGN region=nullptr;RECT bounds{};Shape shape;double scale=0,shoulder=0;ComPtr<ID2D1PathGeometry> geometry;};std::array<Shell,2> shells;
        ~PaintCache(){for(auto& s:shells)if(s.region)DeleteObject(s.region);}
    } paintCache;
    Scene accessibility;TransferStore& store;HWND hwnd=nullptr,island=nullptr;DropTarget *islandDrop=nullptr,*widgetDrop=nullptr;
    std::function<void(int,const std::wstring&)> receiver;std::function<void()> openPage;Jobs imports;
    TransferSnapshot snapshot;std::vector<std::wstring> batch;std::wstring query,message,receiverText,dock=L"top",candidateDock=L"top",pressedId;
    std::vector<size_t> filtered;std::map<std::wstring,std::pair<size_t,int>> index;POINT anchor{},down{};RECT visual{},from{},target{},windowRect{},work{},islandRect{},downRect{};
    View view=View::Compact;Phase phase=Phase::Idle;double idleW=250,idleH=40,scale=1,moreW=460,moreH=420,start=0,duration=.52,nextSync=0,hoverAt=0,downAt=0,receivedAt=0;
    Shape shape,shapeFrom,shapeTarget;double shoulderDip=4,idleDeadline=0;
    bool incomingDrag=false,composing=false,hoverSuppressed=false;
    bool reduced=false,animating=false,morph=false,morphOpening=false,returning=false,hidden=true,dismissing=false,importing=false,pressed=false,titlePressed=false,menu=false,applying=false;
    int scroll=0,focus=800,hoverControl=0,pressedControl=0;unsigned paints=0;double paintSeconds=0;
    bool expanded()const{return view!=View::Compact;}
    bool side()const{return dock==L"left"||dock==L"right";}
    bool resizable()const{return view==View::More||view==View::Floating;}
    int px(double n)const{return (int)std::lround(n*scale);}
    Impl(TransferStore& s,HWND h,std::function<void(int,const std::wstring&)> r,std::function<void()> open):store(s),island(h),receiver(std::move(r)),openPage(std::move(open)){
        WNDCLASSEXW wc{sizeof(wc)};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.lpszClassName=L"WinIsland.FileTransfer";RegisterClassExW(&wc);
        hwnd=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_LAYERED,wc.lpszClassName,L"文件中转",WS_POPUP|WS_CLIPCHILDREN,0,0,160,42,nullptr,nullptr,wc.hInstance,this);
        DWMNCRENDERINGPOLICY nc=DWMNCRP_DISABLED;DwmSetWindowAttribute(hwnd,DWMWA_NCRENDERING_POLICY,&nc,sizeof(nc));
        COLORREF border=RGB(0,0,0);DwmSetWindowAttribute(hwnd,34,&border,sizeof(border));
        auto p=store.snapshot().preferences;dock=p.GetNamedString(L"dock",L"top").c_str();if(dock!=L"top"&&dock!=L"left"&&dock!=L"right"&&dock!=L"float")dock=L"top";
        anchor={(LONG)p.GetNamedNumber(L"x",0),(LONG)p.GetNamedNumber(L"y",0)};moreW=std::clamp(p.GetNamedNumber(L"moreWidth",460),360.,1400.);moreH=std::clamp(p.GetNamedNumber(L"moreHeight",420),300.,1100.);
        if(dock==L"float")view=p.GetNamedBoolean(L"compact",false)?View::Compact:View::Floating;
        visual={anchor.x,anchor.y,anchor.x+160,anchor.y+42};monitor(visual);
        islandDrop=new DropTarget;islandDrop->enabled=[this]{return store.enabled()&&!importing&&batch.empty()&&!outgoingDrag;};
        islandDrop->hover=[this](bool active){incomingDrag=active;resetIdle();if(batch.empty()&&!importing){receiver(active?1:0,active?L"中转文件":L"");if(active)SetTimer(island,1,16,nullptr);else KillTimer(island,1);}};
        islandDrop->receive=[this](auto paths){receive(paths);};RegisterDragDrop(island,islandDrop);
        widgetDrop=new DropTarget;widgetDrop->enabled=[this]{return store.enabled()&&!importing&&!outgoingDrag;};widgetDrop->hover=[this](bool active){incomingDrag=active;resetIdle();};widgetDrop->receive=[this](auto paths){receive(paths);};RegisterDragDrop(hwnd,widgetDrop);
    }
    ~Impl(){imports.finish();KillTimer(hwnd,1);KillTimer(hwnd,2);KillTimer(island,1);RevokeDragDrop(island);RevokeDragDrop(hwnd);islandDrop->Release();widgetDrop->Release();DestroyWindow(hwnd);}
    bool idleEligible()const{return view==View::Panel&&dock!=L"float"&&!hidden&&!animating&&!morph&&!dismissing;}
    bool idleBusy()const{return pressed||menu||composing||incomingDrag||outgoingDrag||importing||!batch.empty()||phase!=Phase::Idle||store.viewInteractionActive();}
    void cancelIdle(){idleDeadline=0;KillTimer(hwnd,2);}
    void resetIdle(){cancelIdle();if(idleEligible()){if(!idleBusy())idleDeadline=now()+10;SetTimer(hwnd,2,250,nullptr);}}
    void idleTick(){if(!idleEligible()||!store.enabled()){cancelIdle();return;}if(idleBusy()){idleDeadline=0;return;}if(!idleDeadline){idleDeadline=now()+10;return;}
        if(now()>=idleDeadline){cancelIdle();hoverSuppressed=true;hoverAt=0;toggle();}}
    void fail(const std::exception& e){message=wide(e.what());InvalidateRect(hwnd,nullptr,FALSE);}
    void preferences(){try{JsonObject p,v;v.Insert(L"dock",JsonValue::CreateStringValue(dock));v.Insert(L"x",JsonValue::CreateNumberValue(anchor.x));v.Insert(L"y",JsonValue::CreateNumberValue(anchor.y));v.Insert(L"compact",JsonValue::CreateBooleanValue(view==View::Compact));v.Insert(L"moreWidth",JsonValue::CreateNumberValue(moreW));v.Insert(L"moreHeight",JsonValue::CreateNumberValue(moreH));p.Insert(L"preferences",v);store.command("preferences",p);}catch(const std::exception& e){fail(e);}}
    void monitor(RECT rect,const POINT* pointer=nullptr){auto m=pointer?MonitorFromPoint(*pointer,MONITOR_DEFAULTTONEAREST):MonitorFromRect(&rect,MONITOR_DEFAULTTONEAREST);MONITORINFO mi{sizeof(mi)};GetMonitorInfoW(m,&mi);work=mi.rcWork;UINT x=96,y=96;if(SUCCEEDED(GetDpiForMonitor(m,MDT_EFFECTIVE_DPI,&x,&y)))scale=x/96.;else scale=GetDpiForWindow(hwnd)/96.;}
    RECT bounded(RECT r)const{auto w=std::clamp(r.right-r.left,1L,work.right-work.left),h=std::clamp(r.bottom-r.top,1L,work.bottom-work.top);r.left=std::clamp(r.left,work.left,work.right-w);r.top=std::clamp(r.top,work.top,work.bottom-h);r.right=r.left+w;r.bottom=r.top+h;return r;}
    RECT geometry(View v)const{
        int w=px(std::max(124.,idleW/scale/3)),h=px(42);
        if(v==View::Compact&&side()){w=px(46);h=px(148);}
        if(v==View::Panel){w=std::max(px(320),(int)(idleW*(side()?.5:1.)));h=std::min(px((double)filtered.size()*82+132),(int)((work.bottom-work.top)*.65));h=std::max(px(240),h);}
        if(v==View::More||v==View::Floating){w=px(moreW);h=px(moreH);}
        RECT r{anchor.x,anchor.y,anchor.x+w,anchor.y+h};
        if(dock==L"top"){r.top=work.top;r.bottom=r.top+h;}if(dock==L"left"){r.left=work.left;r.right=r.left+w;}if(dock==L"right"){r.right=work.right;r.left=r.right-w;}
        if(v==View::More&&source.valid){r.left=source.bounds.right-w;r.right=r.left+w;r.top=source.bounds.top;r.bottom=r.top+h;if(dock==L"left"){r.left=work.left;r.right=r.left+w;}}
        return bounded(r);
    }
    static std::wstring snap(RECT r,RECT bounds,double dpi,const std::wstring& previous){int enter=(int)std::lround(44*dpi),leave=(int)std::lround(72*dpi);
        std::array<std::pair<const wchar_t*,LONG>,3> edges{{{L"left",std::max(0L,r.left-bounds.left)},{L"right",std::max(0L,bounds.right-r.right)},{L"top",std::max(0L,r.top-bounds.top)}}};
        std::wstring result=L"float";LONG best=LONG_MAX;
        for(auto [edge,distance]:edges)if(distance<(previous==edge?leave:enter)&&(distance<best||(distance==best&&previous==edge))){result=edge;best=distance;}
        return result;
    }
    Layout arrangement(RECT rect,View v,int offsetScroll)const{
        Layout l;l.title={rect.left+px(12),rect.top,rect.right-px(12),rect.top+px(42)};
        if(v==View::Compact){l.title=rect;return l;}
        l.search={rect.left+px(12),rect.top+px(44),rect.right-px(12),rect.top+px(76)};
        l.footer={rect.left+px(12),rect.bottom-px(46),rect.right-px(12),rect.bottom-px(10)};
        l.list={rect.left+px(10),rect.top+px(84),rect.right-px(10),l.footer.top-px(8)};
        if(rect.right-rect.left<px(240)||rect.bottom-rect.top<px(190))return l;
        int first=std::max(0,offsetScroll/82),last=std::min((int)filtered.size(),first+std::max(0,(int)((l.list.bottom-l.list.top)/scale)/82)+3);
        for(int row=first;row<last;++row){int y=l.list.top+px(row*82-offsetScroll);RECT card{l.list.left,y,l.list.right,y+px(76)};RECT hit{};if(IntersectRect(&hit,&card,&l.list))l.cards.push_back({snapshot.entries[filtered[row]].id,card});}return l;
    }
    void style(){auto ex=GetWindowLongPtrW(hwnd,GWL_EXSTYLE);SetWindowLongPtrW(hwnd,GWL_EXSTYLE,(ex&~(WS_EX_APPWINDOW|WS_EX_TOOLWINDOW))|(resizable()?WS_EX_APPWINDOW:WS_EX_TOOLWINDOW));auto st=GetWindowLongPtrW(hwnd,GWL_STYLE);auto next=resizable()?st|WS_THICKFRAME:st&~WS_THICKFRAME;if(st!=next){SetWindowLongPtrW(hwnd,GWL_STYLE,next);SetWindowPos(hwnd,nullptr,0,0,0,0,SWP_FRAMECHANGED|SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);}}
    Shape wantedShape(RECT rect,View v)const{Shape s;s.compact=v==View::Compact?1:0;
        if(morph&&source.valid&&EqualRect(&rect,&source.button))s.cornerDip=7;
        if(dock==L"top"&&rect.top==work.top)s.attachment[0]=1;
        if(dock==L"left"&&rect.left==work.left)s.attachment[1]=1;
        if(dock==L"right"&&rect.right==work.right)s.attachment[2]=1;
        return s;
    }
    HRGN contourRegion(RECT rect,Shape s,int slot=0){auto& cache=paintCache.shells[slot];if(cache.region&&EqualRect(&rect,&cache.bounds)&&cache.shape==s&&cache.scale==scale&&cache.shoulder==shoulderDip)return cache.region;
        auto w=rect.right-rect.left,h=rect.bottom-rect.top;
        double radius=mix(std::min<double>(px(s.cornerDip),std::min(w,h)/2.),std::min(w,h)/2.,s.compact);
        auto contour=transferContour(w,h,radius,shoulderDip*scale,s.attachment);
        auto point=[&](OutlinePoint p){return POINT{rect.left+(LONG)std::lround(p.x),rect.top+(LONG)std::lround(p.y)};};
        auto dc=paintCache.pathDc;BeginPath(dc);auto start=point(contour.back().end);MoveToEx(dc,start.x,start.y,nullptr);
        for(auto& c:contour){auto p=point(c.start);LineTo(dc,p.x,p.y);POINT curve[]={point(c.c1),point(c.c2),point(c.end)};PolyBezierTo(dc,curve,3);}CloseFigure(dc);EndPath(dc);if(cache.region)DeleteObject(cache.region);cache={PathToRegion(dc),rect,s,scale,shoulderDip};
        if(!paintCache.factory)TransferPaint::checked(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,paintCache.factory.GetAddressOf()));
        TransferPaint::checked(paintCache.factory->CreatePathGeometry(&cache.geometry));ComPtr<ID2D1GeometrySink> sink;TransferPaint::checked(cache.geometry->Open(&sink));
        auto fp=[&](OutlinePoint p){return D2D1::Point2F((float)(rect.left+p.x),(float)(rect.top+p.y));};sink->BeginFigure(fp(contour.back().end),D2D1_FIGURE_BEGIN_FILLED);for(auto& c:contour){sink->AddLine(fp(c.start));sink->AddBezier(D2D1::BezierSegment(fp(c.c1),fp(c.c2),fp(c.end)));}sink->EndFigure(D2D1_FIGURE_END_CLOSED);TransferPaint::checked(sink->Close());return cache.region;
    }
    void apply(){applying=true;windowRect=visual;if(morph&&source.valid)UnionRect(&windowRect,&source.bounds,&visual);
        auto content=visual;OffsetRect(&content,-windowRect.left,-windowRect.top);layout=arrangement(content,morph?View::More:view,scroll);applying=false;
        // ULW commits pixels, bounds and alpha together. Never clip AA pixels via SetWindowRgn.
        InvalidateRect(hwnd,nullptr,FALSE);SendMessageW(hwnd,WM_PAINT,0,0);
    }
    void animateTo(RECT rect){cancelIdle();from=visual;target=bounded(rect);shapeFrom=shape;shapeTarget=wantedShape(target,morph?View::More:view);start=now();duration=transferMotionSeconds(reduced?.01:.52);animating=!EqualRect(&from,&target)||shapeFrom!=shapeTarget;phase=animating?Phase::Transition:Phase::Idle;style();if(animating)SetTimer(hwnd,1,16,nullptr);else finish();}
    void finish(){visual=target;shape=shapeTarget;animating=false;phase=Phase::Idle;
        if(morph&&!morphOpening){view=source.view;dock=source.dock;anchor=source.anchor;scroll=source.scroll;visual=target=bounded(source.bounds);shape=shapeTarget=wantedShape(visual,view);source.valid=false;clampScroll();}morph=false;
        if(dismissing){dismissing=false;hidden=true;ShowWindow(hwnd,SW_HIDE);}else{style();apply();}if(!hoverAt)KillTimer(hwnd,1);resetIdle();
    }
    void go(){monitor(visual);animateTo(geometry(view));}
    void toggle(){cancelIdle();hoverAt=0;if(view==View::More||morph){footerAction();return;}source.valid=false;view=expanded()?View::Compact:(dock==L"float"?View::Floating:View::Panel);go();preferences();}
    void footerAction(){cancelIdle();hoverAt=0;
        if(morph){morphOpening=!morphOpening;view=morphOpening?View::More:source.view;animateTo(morphOpening?geometry(View::More):source.button);return;}
        if(view==View::Floating){view=View::Compact;go();preferences();return;}
        if(view==View::More&&source.valid){morph=true;morphOpening=false;view=source.view;animateTo(source.button);return;}
        if(view!=View::Panel)return;
        source={true,view,dock,anchor,geometry(view),{},scroll};auto bounds=source.bounds;auto l=arrangement(bounds,view,scroll);source.button=l.footer;
        morph=true;morphOpening=true;view=View::More;visual=source.button;shape=wantedShape(visual,View::More);animateTo(geometry(view));apply();
    }
    void filter(){filtered.clear();index.clear();auto term=query;std::transform(term.begin(),term.end(),term.begin(),towlower);for(size_t i=0;i<snapshot.entries.size();++i){auto name=snapshot.entries[i].name;std::transform(name.begin(),name.end(),name.begin(),towlower);int row=-1;if(term.empty()||name.find(term)!=name.npos){row=(int)filtered.size();filtered.push_back(i);}index[snapshot.entries[i].id]={i,row};}clampScroll();}
    void clampScroll(){auto height=(visual.bottom-visual.top)/scale-142;scroll=std::clamp(scroll,0,std::max(0,(int)(filtered.size()*82-height)));}
    void receive(const std::vector<fs::path>& paths){if(importing||outgoingDrag)return;incomingDrag=false;importing=true;resetIdle();receiver(2,L"正在读取文件…");SetTimer(island,1,16,nullptr);imports.post([this,paths]{try{auto result=new std::vector<std::wstring>(store.import(paths));PostMessageW(hwnd,WM_APP+91,0,(LPARAM)result);}catch(const std::exception& e){PostMessageW(hwnd,WM_APP+92,0,(LPARAM)new std::wstring(wide(e.what())));}});}
    void choose(int kind){try{if(kind==2){for(auto& id:batch){JsonObject p;p.Insert(L"id",JsonValue::CreateStringValue(id));store.command("cancel",p);}batch.clear();receiver(0,L"");KillTimer(island,1);return;}store.choose(batch,kind?L"saved":L"reference");receivedAt=now();}catch(const std::exception& e){receiver(2,wide(e.what()));}}
    void sync(const RECT& r,double w,double h,bool reduce,double arc=4){islandRect=r;idleW=w;idleH=h;reduced=reduce;bool shapeChanged=shoulderDip!=arc;shoulderDip=arc;if(shapeChanged&&!hidden)apply();if(now()<nextSync)return;nextSync=now()+.15;
        bool changed=store.refreshSnapshot(snapshot);
        if(!snapshot.enabled){cancelIdle();if(!hidden&&!dismissing){dismissing=true;EnableWindow(hwnd,FALSE);morph=false;source.valid=false;animateTo(bounded(islandRect));}batch.clear();receiver(0,L"");KillTimer(island,1);return;}
        if(dismissing){dismissing=false;EnableWindow(hwnd,TRUE);go();}
        if(!batch.empty()){std::wstring text;bool pending=false,busy=false;size_t lines=0;for(auto& id:batch)for(auto& e:snapshot.entries)if(e.id==id){++lines;text+=e.name+L"\n";pending|=e.state==L"pending";busy|=e.state==L"queued"||e.state==L"copying";}
            if(lines>1)text+=L"共 "+std::to_wstring(lines)+L" 个文件，可滚轮查看名称\n";text+=pending?L"待选择：仅记录位置 / 复制到数据目录":busy?L"正在复制，可在文件中转列表取消或查看进度":L"处理结果请查看中转列表";
            if(text!=receiverText){receiverText=text;receiver(2,text);}if(!pending&&(!receivedAt||now()-receivedAt>.7)){batch.clear();receiverText.clear();receiver(0,L"");KillTimer(island,1);}}
        if(snapshot.entries.empty()){cancelIdle();ShowWindow(hwnd,SW_HIDE);KillTimer(hwnd,1);hidden=true;return;}
        if(changed)filter();
        if(hidden){if(anchor.x==0&&anchor.y==0){monitor(islandRect);anchor={islandRect.right+12,work.top};if(anchor.x+px(124)>work.right)anchor.x=islandRect.left-px(136);}auto rect=geometry(view);visual=islandRect;hidden=false;EnableWindow(hwnd,TRUE);animateTo(rect);apply();ShowWindow(hwnd,SW_SHOWNOACTIVATE);}
        else if(changed){auto content=visual;OffsetRect(&content,-windowRect.left,-windowRect.top);layout=arrangement(content,morph?View::More:view,scroll);InvalidateRect(hwnd,nullptr,FALSE);}
    }
    const TransferEntry* entry(const std::wstring& id)const{auto it=index.find(id);return it==index.end()?nullptr:&snapshot.entries[it->second.first];}
    std::wstring hit(POINT p)const{if(!inside(layout.list,p))return L"";for(auto& c:layout.cards)if(inside(c.second,p))return c.first;return L"";}
    int control(POINT p)const{if(inside(layout.title,p))return 800;if(expanded()||morph){if(inside(layout.footer,p))return 801;if(inside(layout.search,p))return 802;}return 0;}
    void details(const std::wstring& id=L""){JsonObject p;p.Insert(L"active",JsonValue::CreateBooleanValue(true));store.command("interaction",p);resetIdle();store.requestView(id);openPage();}
    void context(const std::wstring& id){if(id.empty())return;menu=true;hoverAt=0;resetIdle();auto popup=CreatePopupMenu();AppendMenuW(popup,MF_STRING,1,L"从中转站移除（保留所有文件）");AppendMenuW(popup,MF_STRING,2,L"打开原位置");auto e=entry(id);if(e&&e->mode==L"saved")AppendMenuW(popup,MF_STRING,3,L"打开保存位置");AppendMenuW(popup,MF_STRING,4,L"文件详情与操作");POINT p;GetCursorPos(&p);SetForegroundWindow(hwnd);int cmd=TrackPopupMenu(popup,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,hwnd,nullptr);DestroyMenu(popup);menu=false;
        try{JsonObject params;params.Insert(L"id",JsonValue::CreateStringValue(id));if(cmd==1)store.command("remove",params);if(cmd==2||cmd==3){params.Insert(L"original",JsonValue::CreateBooleanValue(cmd==2));store.command("location",params);}if(cmd==4)details(id);}catch(const std::exception& error){fail(error);}resetIdle();}
    void draw(HDC dc,RECT rect,View v,int offsetScroll,bool interactive){
        contourRegion(rect,interactive?shape:wantedShape(source.bounds,source.view),interactive?0:1);auto geometry=paintCache.shells[interactive?0:1].geometry;
        auto rt=paintCache.target.Get();paintCache.color(paintCache.black);rt->FillGeometry(geometry.Get(),paintCache.brush.Get());
        auto layer=D2D1::LayerParameters();layer.geometricMask=geometry.Get();rt->PushLayer(layer,nullptr);
        auto round=[&](RECT box,COLORREF brush,bool focused=false){paintCache.round(box,brush,focused,scale);};
        auto text=[&](const std::wstring& value,RECT r,COLORREF color,UINT flags,bool compactFont=false){paintCache.text(value,r,color,flags,compactFont);};
        auto node=[&](int id,const std::wstring& label,RECT r){if(!interactive)return;accessibility.buttons.push_back({id,{(float)r.left,(float)r.top,(float)(r.right-r.left),(float)(r.bottom-r.top)}});auto n=sceneNode(id,"transfer",WI_SCENE_BUTTON,0,0,0,0);n.values[WI_TEXT_VALUE]=sceneText(utf8(label));accessibility.visibleNodes.push_back(n);};
        auto l=arrangement(rect,v,offsetScroll);rt->PushAxisAlignedClip(TransferPaint::box(rect),D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        if(v==View::Compact){RECT title=rect;if(rect.right-rect.left<px(96)&&rect.bottom-rect.top>rect.right-rect.left){title.top+=(title.bottom-title.top-px(52))/2;text(L"文件\n中转",title,RGB(246,246,248),DT_CENTER|DT_WORDBREAK);}else text(L"文件中转",title,RGB(246,246,248),DT_CENTER|DT_VCENTER|DT_SINGLELINE);node(800,L"文件中转，展开，长按拖动",rect);rt->PopAxisAlignedClip();rt->PopLayer();return;}
        text(v==View::Floating?L"文件中转 · 浮动":v==View::More?L"文件中转 · 更多":L"文件中转 · 收起",l.title,RGB(246,246,248),DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);node(800,L"文件中转标题，长按拖动，点击紧凑",l.title);
        if(rect.right-rect.left<px(260)||rect.bottom-rect.top<px(200)){rt->PopAxisAlignedClip();rt->PopLayer();return;}
        round(l.search,paintCache.card,interactive&&focus==802);auto sr=l.search;InflateRect(&sr,-px(9),0);text(query.empty()?L"搜索文件名 · 直接输入":query,sr,RGB(169,179,196),DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS,true);node(802,L"搜索文件名："+query,l.search);
        rt->PushAxisAlignedClip(TransferPaint::box(l.list),D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        for(auto& [id,card]:l.cards){auto e=entry(id);if(!e)continue;round(card,paintCache.card);RECT name{card.left+px(10),card.top+px(7),card.right-px(10),card.top+px(48)};text(e->name,name,RGB(244,245,247),DT_WORDBREAK|DT_END_ELLIPSIS);
            RECT meta{card.left+px(10),card.top+px(51),card.right-px(10),card.bottom-px(3)};auto status=e->state==L"ready"?(e->mode==L"saved"?L"已保存":L"仅中转"):e->state==L"pending"?L"待选择":e->state==L"copying"?L"复制中":L"需处理";text(e->extension+L" · "+std::to_wstring((uint64_t)e->size)+L" B · "+status,meta,RGB(169,179,196),DT_SINGLELINE|DT_END_ELLIPSIS,true);
            RECT visible{};IntersectRect(&visible,&card,&l.list);node(900+index.at(id).second,e->name+L"，文件详情与操作",visible);}
        rt->PopAxisAlignedClip();
        auto label=v==View::Floating?L"紧凑":v==View::More?(morph&&!morphOpening?L"展开更多文件":L"返回原面板"):L"显示更多文件";
        round(l.footer,interactive&&pressedControl==801?paintCache.pressed:interactive&&hoverControl==801?paintCache.hover:paintCache.button,interactive&&focus==801);
        auto ft=l.footer;InflateRect(&ft,-px(10),0);text(std::wstring(label)+(message.empty()?L"":L" · "+message),ft,RGB(198,215,244),DT_SINGLELINE|DT_VCENTER|DT_CENTER|DT_END_ELLIPSIS,true);node(801,label,l.footer);rt->PopAxisAlignedClip();rt->PopLayer();
    }
    void renderFrame(){RECT rc{0,0,std::max(1L,windowRect.right-windowRect.left),std::max(1L,windowRect.bottom-windowRect.top)};paintCache.ensure(nullptr,rc.right,rc.bottom,scale);
        auto rt=paintCache.target.Get();rt->BeginDraw();rt->Clear(D2D1::ColorF(0,0,0,0));
        accessibility.buttons.clear();accessibility.visibleNodes.clear();accessibility.accessibleTitle=L"文件中转";accessibility.width=rc.right;accessibility.height=rc.bottom;
        if(morph&&source.valid){auto base=source.bounds;OffsetRect(&base,-windowRect.left,-windowRect.top);draw(paintCache.dc,base,source.view,source.scroll,false);}
        auto area=visual;OffsetRect(&area,-windowRect.left,-windowRect.top);draw(paintCache.dc,area,morph?View::More:view,scroll,true);
        auto result=rt->EndDraw();if(result==D2DERR_RECREATE_TARGET){paintCache.brush.Reset();paintCache.target.Reset();InvalidateRect(hwnd,nullptr,FALSE);return;}TransferPaint::checked(result);
    }
    void paint(){auto stamp=now();PAINTSTRUCT ps;BeginPaint(hwnd,&ps);
        try{renderFrame();POINT origin{windowRect.left,windowRect.top},zero{};SIZE size{std::max(1L,windowRect.right-windowRect.left),std::max(1L,windowRect.bottom-windowRect.top)};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};applying=true;
            if(!UpdateLayeredWindow(hwnd,nullptr,&origin,&size,paintCache.dc,&zero,0,&blend,ULW_ALPHA))message=L"透明表面更新失败："+std::to_wstring(GetLastError());applying=false;
        }catch(const std::exception& e){message=wide(e.what());applying=false;}EndPaint(hwnd,&ps);++paints;paintSeconds+=now()-stamp;
    }
    void endMove(){cancelIdle();dock=candidateDock;anchor={visual.left,visual.top};source.valid=false;morph=false;
        view=dock==L"float"?View::Floating:((side()&&snapshot.preferences.GetNamedBoolean(L"sideAutoExpand",false))?View::Panel:View::Compact);go();preferences();}
    void setDock(const std::wstring& value){cancelIdle();source.valid=false;morph=false;dock=candidateDock=value;anchor={visual.left,visual.top};view=dock==L"float"?View::Floating:View::Compact;go();preferences();}
    void cancelPointer(){hoverAt=0;bool moving=phase==Phase::Moving;pressed=titlePressed=false;pressedId.clear();pressedControl=0;if(GetCapture()==hwnd)ReleaseCapture();if(moving)endMove();}
    void resizeChanged(){if(applying||phase!=Phase::Resizing)return;GetWindowRect(hwnd,&visual);target=visual;windowRect=visual;monitor(visual);moreW=(visual.right-visual.left)/scale;moreH=(visual.bottom-visual.top)/scale;if(view==View::Floating)anchor={visual.left,visual.top};clampScroll();apply();}
    static LRESULT CALLBACK proc(HWND h,UINT msg,WPARAM wp,LPARAM lp){auto self=(Impl*)GetWindowLongPtrW(h,GWLP_USERDATA);if(msg==WM_NCCREATE){self=(Impl*)((CREATESTRUCTW*)lp)->lpCreateParams;SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)self);}if(!self)return DefWindowProcW(h,msg,wp,lp);auto& a=*self;
        switch(msg){
        case WM_NCCALCSIZE:case WM_NCPAINT:return 0;
        case WM_NCACTIVATE:InvalidateRect(h,nullptr,FALSE);return TRUE;
        case WM_NCHITTEST:{POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(h,&p);auto area=a.visual;OffsetRect(&area,-a.windowRect.left,-a.windowRect.top);if(!PtInRegion(a.contourRegion(area,a.shape),p.x,p.y))return HTTRANSPARENT;if(a.resizable()&&!a.morph){RECT r;GetClientRect(h,&r);int edge=a.px(7);bool l=p.x<edge,rgt=p.x>=r.right-edge,t=p.y<edge,b=p.y>=r.bottom-edge;if(t)return l?HTTOPLEFT:rgt?HTTOPRIGHT:HTTOP;if(b)return l?HTBOTTOMLEFT:rgt?HTBOTTOMRIGHT:HTBOTTOM;if(l)return HTLEFT;if(rgt)return HTRIGHT;}return HTCLIENT;}
        case WM_GETMINMAXINFO:{auto m=(MINMAXINFO*)lp;m->ptMinTrackSize={std::min<LONG>(a.px(360),a.work.right-a.work.left),std::min<LONG>(a.px(300),a.work.bottom-a.work.top)};m->ptMaxTrackSize={a.work.right-a.work.left,a.work.bottom-a.work.top};return 0;}
        case WM_ENTERSIZEMOVE:a.cancelIdle();a.phase=Phase::Resizing;a.animating=false;a.morph=false;a.hoverAt=0;KillTimer(h,1);return 0;
        case WM_SIZE:case WM_MOVE:a.resizeChanged();return 0;
        case WM_EXITSIZEMOVE:a.resizeChanged();a.phase=Phase::Idle;a.preferences();a.resetIdle();return 0;
        case WM_GETOBJECT:if((LONG)lp==OBJID_CLIENT)return accessibleObject(h,wp,a.accessibility,1);break;
        case WM_APP+6:a.resetIdle();if(wp==800)a.toggle();else if(wp==801)a.footerAction();else if(wp==802){SetFocus(h);a.focus=802;}else if(wp>=900&&wp-900<a.filtered.size())a.details(a.snapshot.entries[a.filtered[wp-900]].id);return 0;
        case WM_APP+7:a.resetIdle();SetFocus(h);a.focus=(int)wp;InvalidateRect(h,nullptr,FALSE);return 0;
        case WM_ERASEBKGND:return 1;case WM_PAINT:a.paint();return 0;
        case WM_APP+91:{std::unique_ptr<std::vector<std::wstring>> ids((std::vector<std::wstring>*)lp);a.batch=*ids;a.importing=false;a.receivedAt=now();a.nextSync=0;return 0;}
        case WM_APP+92:{std::unique_ptr<std::wstring> error((std::wstring*)lp);a.importing=false;a.receiver(2,*error);a.message=*error;KillTimer(a.island,1);return 0;}
        case WM_TIMER:{if(wp==2){a.idleTick();return 0;}auto time=now();if(a.animating){double t=std::clamp((time-a.start)/a.duration,0.,1.);double p=t*t*(3-2*t);auto f=(LONG*)&a.from,to=(LONG*)&a.target,v=(LONG*)&a.visual;for(int i=0;i<4;++i)v[i]=(LONG)std::lround(mix(f[i],to[i],p));for(int i=0;i<3;++i)a.shape.attachment[i]=mix(a.shapeFrom.attachment[i],a.shapeTarget.attachment[i],p);a.shape.compact=mix(a.shapeFrom.compact,a.shapeTarget.compact,p);a.shape.cornerDip=mix(a.shapeFrom.cornerDip,a.shapeTarget.cornerDip,p);a.apply();if(t>=1)a.finish();}
            if(a.hoverAt&&time-a.hoverAt>=3&&a.view==View::Compact&&!a.pressed&&!a.menu&&a.phase!=Phase::FileDrag){a.hoverAt=0;a.toggle();}if(!a.animating&&!a.hoverAt)KillTimer(h,1);return 0;}
        case WM_MOUSEMOVE:{POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,h,0};TrackMouseEvent(&track);int control=a.control(p);if(control!=a.hoverControl){a.hoverControl=control;InvalidateRect(h,nullptr,FALSE);}if(a.view==View::Compact&&a.dock==L"top"&&!a.pressed&&!a.hoverAt&&!a.hoverSuppressed){a.hoverAt=now();SetTimer(h,1,16,nullptr);}
            if(a.pressed){POINT screen=p;ClientToScreen(h,&screen);int dx=screen.x-a.down.x,dy=screen.y-a.down.y;bool threshold=std::abs(dx)+std::abs(dy)>a.px(8);
                if(!a.pressedId.empty()&&threshold&&now()-a.downAt>.35){auto id=a.pressedId;a.pressed=false;a.pressedControl=0;a.pressedId.clear();ReleaseCapture();a.phase=Phase::FileDrag;a.hoverAt=0;a.resetIdle();try{dragFile(a.store.resolve(id));}catch(const std::exception& e){a.fail(e);}a.phase=Phase::Idle;a.resetIdle();InvalidateRect(h,nullptr,FALSE);}
                else if(a.titlePressed&&threshold&&now()-a.downAt>.25){a.cancelIdle();a.phase=Phase::Moving;a.animating=a.morph=false;a.source.valid=false;a.hoverAt=0;a.visual={a.downRect.left+dx,a.downRect.top+dy,a.downRect.right+dx,a.downRect.bottom+dy};a.monitor(a.visual,&screen);a.visual=a.bounded(a.visual);a.candidateDock=snap(a.visual,a.work,a.scale,a.candidateDock);a.apply();}}
            return 0;}
        case WM_MOUSELEAVE:a.hoverSuppressed=false;a.hoverAt=0;a.hoverControl=0;InvalidateRect(h,nullptr,FALSE);return 0;
        case WM_LBUTTONDOWN:{a.cancelIdle();a.hoverSuppressed=false;SetFocus(h);a.pressed=true;a.downAt=now();a.hoverAt=0;a.down={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};a.pressedControl=a.control(a.down);a.focus=a.pressedControl;a.titlePressed=a.pressedControl==800;a.pressedId=a.hit(a.down);if(a.titlePressed)a.pressedId.clear();ClientToScreen(h,&a.down);a.downRect=a.visual;a.candidateDock=a.dock;SetCapture(h);InvalidateRect(h,nullptr,FALSE);return 0;}
        case WM_LBUTTONUP:{if(!a.pressed)return 0;POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};bool moved=a.phase==Phase::Moving;int control=a.pressedControl;auto id=a.pressedId;a.pressed=a.titlePressed=false;a.pressedId.clear();a.pressedControl=0;a.phase=Phase::Idle;ReleaseCapture();if(moved)a.endMove();else if(control&&control==a.control(p)){if(control==800)a.toggle();else if(control==801)a.footerAction();}else if(!id.empty()&&id==a.hit(p))a.details(id);a.resetIdle();InvalidateRect(h,nullptr,FALSE);return 0;}
        case WM_CAPTURECHANGED:case WM_CANCELMODE:case WM_KILLFOCUS:if(a.pressed){a.pressed=false;a.cancelPointer();}a.hoverAt=0;a.composing=false;a.resetIdle();return 0;
        case WM_MOUSEWHEEL:a.resetIdle();a.scroll-=GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA*60;a.clampScroll();a.apply();return 0;
        case WM_RBUTTONUP:a.resetIdle();if(a.control({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)})==800){a.menu=true;a.hoverAt=0;a.resetIdle();auto m=CreatePopupMenu();AppendMenuW(m,MF_STRING,1,L"停靠顶部");AppendMenuW(m,MF_STRING,2,L"停靠左侧");AppendMenuW(m,MF_STRING,3,L"停靠右侧");AppendMenuW(m,MF_STRING,4,L"浮动窗口");AppendMenuW(m,MF_STRING,5,L"打开文件中转主界面");POINT p;GetCursorPos(&p);int selected=TrackPopupMenu(m,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,h,nullptr);DestroyMenu(m);a.menu=false;if(selected>=1&&selected<=4){const wchar_t* docks[]={L"top",L"left",L"right",L"float"};a.setDock(docks[selected-1]);}if(selected==5)a.details();}else a.context(a.hit({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)}));a.resetIdle();return 0;
        case WM_IME_STARTCOMPOSITION:a.composing=true;a.resetIdle();break;
        case WM_IME_ENDCOMPOSITION:a.composing=false;a.resetIdle();break;
        case WM_CHAR:a.resetIdle();if(a.expanded()){if(wp==8){if(!a.query.empty())a.query.pop_back();}else if(wp>=32)a.query+=(wchar_t)wp;a.filter();a.apply();}return 0;
        case WM_KEYDOWN:a.resetIdle();if(wp==VK_ESCAPE){if(a.view==View::More||a.morph)a.footerAction();else if(a.expanded())a.toggle();}else if(wp==VK_TAB){a.focus=a.focus==800?802:a.focus==802?801:800;InvalidateRect(h,nullptr,FALSE);}else if(wp==VK_RETURN||wp==VK_SPACE){if(a.focus==801)a.footerAction();else if(a.focus>=900&&(size_t)(a.focus-900)<a.filtered.size())a.details(a.snapshot.entries[a.filtered[a.focus-900]].id);else if(a.focus==800)a.toggle();}else if(wp==VK_UP||wp==VK_DOWN){a.focus=900+std::clamp(a.focus-900+(wp==VK_DOWN?1:-1),0,std::max(0,(int)a.filtered.size()-1));a.scroll=std::max(0,(a.focus-900)*82-82);a.apply();}return 0;
        case WM_DPICHANGED:case WM_DISPLAYCHANGE:if(a.phase==Phase::Moving)return 0;a.monitor(a.visual);if(a.source.valid){a.source.bounds=a.geometry(a.source.view);a.source.button=a.arrangement(a.source.bounds,a.source.view,a.source.scroll).footer;}a.target=a.morph?(a.morphOpening?a.geometry(View::More):a.source.button):a.geometry(a.view);a.animateTo(a.target);return 0;
        case WM_CLOSE:if(a.view==View::More||a.morph)a.footerAction();else if(a.expanded())a.toggle();return 0;
        }return DefWindowProcW(h,msg,wp,lp);
    }
};
TransferWidget::TransferWidget(TransferStore& s,HWND island,std::function<void(int,const std::wstring&)> receiver,std::function<void()> open):impl(std::make_unique<Impl>(s,island,std::move(receiver),std::move(open))){}
TransferWidget::~TransferWidget()=default;
void TransferWidget::sync(const RECT& r,double w,double h,bool reduced,double shoulder){impl->sync(r,w,h,reduced,shoulder);}
void TransferWidget::receive(const std::vector<fs::path>& paths){impl->receive(paths);}
void TransferWidget::receiverAction(int action){impl->choose(action);}
void TransferWidget::diagnosticDrop(const fs::path& path,int stage){if(stage==0){impl->islandDrop->DragLeave();return;}FileObject object(path,false);DWORD effect=DROPEFFECT_COPY;impl->islandDrop->DragEnter(&object,0,{},&effect);if(stage==2)impl->islandDrop->Drop(&object,0,{},&effect);}

int transferWidgetTest(const fs::path& output){
    fs::create_directories(output);std::ostringstream report;int failures=0,count=0;
    auto check=[&](bool ok,const char* name){report<<(ok?"PASS ":"FAIL ")<<name<<'\n';++count;if(!ok)++failures;};
    try {
    auto parent=CreateWindowExW(0,L"STATIC",L"Transfer test",WS_POPUP,0,0,250,40,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    {
        using View=TransferWidget::Impl::View;using Phase=TransferWidget::Impl::Phase;
        int receiving=0,opens=0;TransferStore store(output/L"data");TransferWidget widget(store,parent,[&](int value,const std::wstring&){receiving=value;},[&]{++opens;});auto& a=*widget.impl;
        auto settle=[&]{a.start=now()-2;SendMessageW(a.hwnd,WM_TIMER,1,0);};
        auto capture=[&](const wchar_t* name){ // Offscreen drawing evidence, not a desktop screenshot.
            a.renderFrame();int w=a.windowRect.right-a.windowRect.left,h=a.windowRect.bottom-a.windowRect.top;BITMAPINFOHEADER info{sizeof(BITMAPINFOHEADER),w,-h,1,32,BI_RGB};BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(info);header.bfSize=header.bfOffBits+w*h*4;std::string bytes(header.bfSize,'\0');memcpy(bytes.data(),&header,sizeof(header));memcpy(bytes.data()+sizeof(header),&info,sizeof(info));for(int y=0;y<h;++y)memcpy(bytes.data()+header.bfOffBits+y*w*4,a.paintCache.pixels+y*a.paintCache.width,w*4);writeAtomic(output/name,bytes);
        };
        auto fixture=output/L"fixture.txt";writeAtomic(fixture,"fixture");auto fixtureIds=store.import({fixture});auto mixedIds=store.import({fs::path(fixture.generic_wstring())});check(fixtureIds==mixedIds&&store.snapshot().entries.size()==1,"mixed separators normalize before extended path validation and deduplicate");JsonObject initial;initial.Insert(L"enabled",JsonValue::CreateBooleanValue(true));store.command("enable",initial);store.refreshSnapshot(a.snapshot);a.filter();
        for(bool right:{false,true}){
            a.hidden=false;a.dock=L"float";a.view=View::Floating;a.anchor={a.work.left+300,a.work.top+200};a.go();settle();
            POINT grip{a.px(150),a.px(20)};SendMessageW(a.hwnd,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(grip.x,grip.y));a.downAt=now()-1;
            POINT moved{right?a.work.right-5:a.work.left+5,a.work.top+220};ScreenToClient(a.hwnd,&moved);SendMessageW(a.hwnd,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(moved.x,moved.y));
            POINT released{right?a.work.right-5:a.work.left+5,a.work.top+220};ScreenToClient(a.hwnd,&released);SendMessageW(a.hwnd,WM_LBUTTONUP,0,MAKELPARAM(released.x,released.y));settle();
            report<<"DragSide="<<(right?"right":"left")<<" Result="<<utf8(a.dock)<<" Visual="<<a.visual.left<<","<<a.visual.top<<","<<a.visual.right<<","<<a.visual.bottom<<'\n';
            check(a.dock==(right?L"right":L"left")&&a.view==View::Compact,"expanded drag beyond side edge reliably docks with auto-open off");
        }
        check((GetWindowLongPtrW(a.hwnd,GWL_EXSTYLE)&WS_EX_LAYERED)!=0,"visual shell uses per-pixel alpha rather than binary window region");
        a.hidden=false;a.anchor={400,200};a.dock=L"float";a.view=View::Floating;a.go();settle();
        a.pressed=a.titlePressed=true;a.pressedControl=800;a.phase=Phase::Moving;a.candidateDock=L"float";SetCapture(a.hwnd);
        check(GetCapture()==a.hwnd,"native capture established");SendMessageW(a.hwnd,WM_LBUTTONUP,0,MAKELPARAM(55,20));settle();
        check(a.dock==L"float"&&a.view==View::Floating&&!a.pressed,"drag release must not become title click");
        check(store.snapshot().preferences.GetNamedString(L"dock",L"")==L"float","drag release persists final dock");
        a.pressed=true;SetCapture(a.hwnd);SendMessageW(a.hwnd,WM_KILLFOCUS,0,0);check(!a.pressed&&GetCapture()!=a.hwnd,"focus loss releases pointer capture");
        a.view=View::Compact;a.dock=L"top";a.go();settle();a.hoverAt=now()-3.1;SendMessageW(a.hwnd,WM_TIMER,1,0);check(a.view==View::Panel,"three second hover expands");settle();
        a.toggle();settle();a.hoverAt=now()-3.1;SendMessageW(a.hwnd,WM_MOUSELEAVE,0,0);SendMessageW(a.hwnd,WM_TIMER,1,0);check(a.view==View::Compact,"leaving cancels hover deadline");
        a.pressed=true;a.hoverAt=now()-3.1;SendMessageW(a.hwnd,WM_TIMER,1,0);check(a.view==View::Compact,"press suppresses hover expansion");a.pressed=false;a.hoverAt=0;
        a.setDock(L"left");settle();check(a.visual.bottom-a.visual.top>a.visual.right-a.visual.left,"side compact island is vertical");auto left=a.visual;a.toggle();settle();check(a.visual.left==left.left&&a.visual.right>left.right,"left expands inward right");a.toggle();settle();check(EqualRect(&left,&a.visual),"left compact restores exact anchor");
        a.setDock(L"right");settle();auto right=a.visual;a.toggle();settle();check(a.visual.right==right.right&&a.visual.left<right.left,"right expands inward left");a.toggle();settle();check(EqualRect(&right,&a.visual),"right compact restores exact anchor");
        for(auto dock:{L"top",L"left",L"right"}){a.setDock(dock);settle();RECT local{0,0,a.visual.right-a.visual.left,a.visual.bottom-a.visual.top};auto shell=a.contourRegion(local,a.shape);
            bool side=a.side();check(side?PtInRegion(shell,dock==std::wstring(L"left")?0:local.right-1,local.bottom/2):PtInRegion(shell,local.right/2,0),"shared island contour connects exactly to docking edge");
            capture((std::wstring(L"contour-")+dock+L".bmp").c_str());
        }
        for(double dpi:{1.,1.25,1.5,2.}){auto main=islandOutline(4*dpi,140*dpi,42*dpi,21*dpi,-4*dpi);auto transfer=transferContour(148*dpi,42*dpi,21*dpi,4*dpi,{1,0,0});bool same=true;for(size_t i=0;i<4;++i)for(double t:{0.,.25,.5,.75,1.}){auto p=outlineAt(main[i],t),q=outlineAt(transfer[i],t);same&=std::abs(p.x-q.x)<1e-8&&std::abs(p.y-q.y)<1e-8;}check(same,"top contour uses exact main-island cubics at matching DPI");}
        a.setDock(L"left");a.start=now()-.2;SendMessageW(a.hwnd,WM_TIMER,1,0);auto halfway=a.shape;a.setDock(L"right");check(a.shapeFrom==halfway,"orientation reversal preserves current contour progress");settle();
        check(!store.snapshot().preferences.GetNamedBoolean(L"sideAutoExpand",false),"side auto expansion defaults off");
        a.candidateDock=L"left";a.endMove();settle();check(a.view==View::Compact,"side attachment does not open by default");
        JsonObject autoExpand;autoExpand.Insert(L"enabled",JsonValue::CreateBooleanValue(true));store.command("side-auto",autoExpand);store.refreshSnapshot(a.snapshot);
        a.candidateDock=L"right";a.endMove();settle();check(a.view==View::Panel,"enabled side attachment opens once");auto start=a.start;for(int i=0;i<5;++i){a.nextSync=0;a.sync(a.islandRect,250,40,false);}check(a.start==start,"idle sync never retriggers side expansion");
        auto prefs=store.snapshot().preferences;JsonObject saved,values;values.Insert(L"x",JsonValue::CreateNumberValue(450));saved.Insert(L"preferences",values);store.command("preferences",saved);check(store.snapshot().preferences.GetNamedBoolean(L"sideAutoExpand",false),"geometry save does not overwrite user preference");
        for(double dpi:{1.,1.25,1.5,2.}){RECT work{0,0,(LONG)(1920*dpi),(LONG)(1080*dpi)};RECT r{(LONG)(40*dpi),200,(LONG)(240*dpi),500};check(TransferWidget::Impl::snap(r,work,dpi,L"float")==L"left","44 DIP edge entry uses component bounds");r.left=(LONG)(60*dpi);r.right=r.left+200;check(TransferWidget::Impl::snap(r,work,dpi,L"left")==L"left"&&TransferWidget::Impl::snap(r,work,dpi,L"float")==L"float","72 DIP exit hysteresis is distinct");}
        a.query=L"";a.scroll=0;
        for(auto dock:{L"top",L"left",L"right"}){a.dock=dock;a.anchor={500,220};a.view=View::Panel;a.go();settle();auto before=a.visual;for(int i=0;i<20;++i){SendMessageW(a.hwnd,WM_APP+6,801,0);settle();SendMessageW(a.hwnd,WM_APP+6,801,0);settle();}check(EqualRect(&before,&a.visual),"twenty more-return cycles preserve source bounds");}
        a.footerAction();check(a.morph&&EqualRect(&a.from,&a.source.button),"more starts at complete footer button rectangle");a.start=now()-.15;SendMessageW(a.hwnd,WM_TIMER,1,0);auto current=a.visual;a.footerAction();check(EqualRect(&current,&a.from),"reverse starts at current visual rectangle");SendMessageW(a.hwnd,WM_DISPLAYCHANGE,0,0);check(EqualRect(&a.target,&a.source.button),"display change while closing preserves button return target");settle();check(!a.morph&&a.view==View::Panel,"reverse returns to real source panel");
        a.footerAction();settle();check((GetWindowLongPtrW(a.hwnd,GWL_STYLE)&WS_THICKFRAME)!=0,"more window permits native edge resize");
        MINMAXINFO limits{};SendMessageW(a.hwnd,WM_GETMINMAXINFO,0,(LPARAM)&limits);check(limits.ptMinTrackSize.x==a.px(360)&&limits.ptMinTrackSize.y==a.px(300),"minimum size covers search cards and footer");
        SendMessageW(a.hwnd,WM_ENTERSIZEMOVE,0,0);SetWindowPos(a.hwnd,nullptr,a.visual.left,a.visual.top,a.px(380),a.px(320),SWP_NOACTIVATE|SWP_NOZORDER);SendMessageW(a.hwnd,WM_EXITSIZEMOVE,0,0);
        check(std::abs(a.moreW-380)<2&&std::abs(a.moreH-320)<2&&a.layout.footer.bottom<=a.visual.bottom-a.visual.top,"native resize updates layout and stored dimensions");auto source=a.source.bounds;a.footerAction();settle();check(EqualRect(&source,&a.visual),"resized more returns without anchor drift");
        a.visual={400,200,900,650};a.candidateDock=L"float";a.endMove();settle();check(a.view==View::Floating,"middle drag synchronizes floating state");auto floating=a.visual;a.footerAction();settle();check(a.view==View::Compact&&a.dock==L"float"&&a.visual.left==floating.left&&a.visual.top==floating.top,"floating compact stays at current position");a.toggle();settle();check(a.view==View::Floating,"floating compact reopens full view");
        SendMessageW(a.hwnd,WM_ENTERSIZEMOVE,0,0);SetWindowPos(a.hwnd,nullptr,a.visual.left,a.visual.top,a.px(380),a.px(340),SWP_NOACTIVATE|SWP_NOZORDER);SendMessageW(a.hwnd,WM_EXITSIZEMOVE,0,0);
        auto button=a.layout.footer;POINT point{(button.left+button.right)/2,(button.top+button.bottom)/2};SendMessageW(a.hwnd,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(point.x,point.y));SendMessageW(a.hwnd,WM_LBUTTONUP,0,MAKELPARAM(point.x,point.y));settle();RECT actual{};GetWindowRect(a.hwnd,&actual);
        check(a.view==View::Compact&&a.dock==L"float"&&a.anchor.x==floating.left&&a.anchor.y==floating.top,"resized floating footer click compacts without docking change");
        check(EqualRect(&actual,&a.visual),"actual HWND bounds match final visual and hit layout");a.toggle();settle();
        auto file=output/L"中转 Unicode.txt";writeAtomic(file,"OLE data object");FileObject own(file),external(file,false);auto paths=dropped(&own);check(paths.size()==1&&paths[0]==file,"CF_HDROP preserves real Unicode source");
        JsonObject enable;enable.Insert(L"enabled",JsonValue::CreateBooleanValue(true));store.command("enable",enable);auto before=store.snapshot();
        DWORD effect=DROPEFFECT_COPY;a.islandDrop->DragEnter(&own,0,{},&effect);check(effect==DROPEFFECT_NONE&&receiving==0,"own source never enters external receiver");a.islandDrop->Drop(&own,0,{},&effect);check(effect==DROPEFFECT_NONE&&receiving==0&&store.snapshot().revision==before.revision,"own return changes no entries preferences or receiver state");a.islandDrop->DragLeave();
        a.widgetDrop->DragEnter(&own,0,{},&effect);a.widgetDrop->Drop(&own,0,{},&effect);check(effect==DROPEFFECT_NONE&&store.snapshot().revision==before.revision,"own return to widget does not import");
        a.islandDrop->DragEnter(&external,0,{},&effect);check(effect==DROPEFFECT_COPY&&receiving==1,"normal external source still enters receiver");a.islandDrop->DragLeave();check(receiving==0,"external leave restores receiver");
        outgoingDrag=true;auto ids=store.import({file});outgoingDrag=false;check(ids.empty()&&store.snapshot().revision==before.revision,"renderer view also rejects imports during outgoing session");
        a.go();settle();SendMessageW(a.hwnd,WM_PAINT,0,0);auto resources=a.paintCache.rebuilds;auto stamp=now();for(int i=0;i<120;++i)SendMessageW(a.hwnd,WM_PAINT,0,0);double elapsed=now()-stamp;
        check(a.paintCache.rebuilds==resources,"120 paints reuse fonts bitmap DC brushes and pens");report<<"Paint120Ms="<<elapsed*1000<<" ResourceRebuilds="<<resources<<'\n';
        TransferSnapshot unchanged=store.snapshot();check(!store.refreshSnapshot(unchanged),"unchanged revision skips snapshot list copying");
        // Expiration is driven by real WM_TIMER and steady-clock deadlines, not frame count.
        a.batch.clear();a.importing=a.incomingDrag=a.menu=a.pressed=false;a.dock=L"top";a.view=View::Compact;a.go();settle();auto compact=a.visual;a.toggle();
        check(a.idleDeadline==0,"idle countdown does not run during opening animation");settle();
        check(a.idleDeadline-now()>9.9&&a.idleDeadline-now()<=10,"panel gets ten seconds after opening completes");
        a.idleDeadline=now()+.1;SendMessageW(a.hwnd,WM_MOUSEWHEEL,MAKEWPARAM(0,WHEEL_DELTA),0);check(a.idleDeadline-now()>9.9,"scroll renews full idle deadline");
        double deadline=a.idleDeadline;SendMessageW(a.hwnd,WM_MOUSEMOVE,0,MAKELPARAM(40,50));check(a.idleDeadline==deadline,"stationary pointer does not renew panel deadline");
        for(auto busy:{&a.menu,&a.composing,&a.pressed,&a.incomingDrag,&a.importing}){*busy=true;a.idleDeadline=now()-1;SendMessageW(a.hwnd,WM_TIMER,2,0);check(a.view==View::Panel&&a.idleDeadline==0,"active interaction suspends automatic collapse");*busy=false;SendMessageW(a.hwnd,WM_TIMER,2,0);check(a.idleDeadline-now()>9.9,"interaction completion starts a fresh ten seconds");}
        JsonObject lease;lease.Insert(L"active",JsonValue::CreateBooleanValue(true));store.command("interaction",lease);a.idleDeadline=now()-1;a.idleTick();check(a.view==View::Panel&&!a.idleDeadline,"associated settings or file dialog lease pauses idle collapse");lease.SetNamedValue(L"active",JsonValue::CreateBooleanValue(false));store.command("interaction",lease);a.idleTick();check(a.idleDeadline-now()>9.9,"related view completion restarts full deadline");
        auto revision=store.snapshot().revision;a.idleDeadline=now()-1;SendMessageW(a.hwnd,WM_TIMER,2,0);check(a.view==View::Compact&&a.hoverSuppressed,"expired dock panel collapses and suppresses immediate hover reopening");check(std::abs(a.duration-.52/1.2)<1e-9,"native collapse runs at 120 percent baseline speed");settle();check(EqualRect(&compact,&a.visual)&&store.snapshot().entries.size()==before.entries.size(),"automatic collapse preserves dock anchor and file records");
        SendMessageW(a.hwnd,WM_MOUSEMOVE,0,MAKELPARAM(40,20));check(!a.hoverAt,"unchanged pointer cannot reopen after automatic collapse");SendMessageW(a.hwnd,WM_MOUSELEAVE,0,0);SendMessageW(a.hwnd,WM_MOUSEMOVE,0,MAKELPARAM(40,20));check(a.hoverAt>0,"new pointer entry can start unchanged three second hover");a.hoverAt=0;
        a.toggle();settle();a.footerAction();settle();check(!a.idleDeadline&&a.view==View::More,"more view is excluded from auto collapse");a.footerAction();settle();a.setDock(L"float");settle();check(!a.idleDeadline&&a.view==View::Floating,"floating view is excluded from auto collapse");
        auto originalScale=a.scale;auto originalWork=a.work;
        for(double dpi:{1.,1.25,1.5,2.}){
            RECT monitor{-2560,-900,0,1440};for(int width:{360,460,900}){RECT left{monitor.left-150,monitor.top+200,monitor.left-150+(LONG)(width*dpi),monitor.top+600};RECT right{monitor.right-(LONG)(width*dpi)+150,monitor.top+200,monitor.right+150,monitor.top+600};check(TransferWidget::Impl::snap(left,monitor,dpi,L"float")==L"left"&&TransferWidget::Impl::snap(right,monitor,dpi,L"float")==L"right","negative monitor coordinates and resized window overshoot dock by actual bounds");}
            for(auto dock:{L"top",L"left",L"right",L"float"})for(auto view:{View::Compact,View::Panel,View::Floating,View::More}){
                a.scale=dpi;a.work={0,0,4000,3000};a.dock=dock;a.view=view;a.source.valid=false;a.morph=false;a.anchor={200,180};a.visual=a.geometry(view);a.windowRect=a.visual;a.shape=a.wantedShape(a.visual,view);a.renderFrame();
                unsigned fractional=0,opaque=0;bool premultiplied=true;int w=a.visual.right-a.visual.left,h=a.visual.bottom-a.visual.top;
                for(int y=0;y<h;++y)for(int x=0;x<w;++x){auto pixel=a.paintCache.pixels[y*a.paintCache.width+x];auto alpha=pixel>>24;fractional+=alpha>0&&alpha<255;opaque+=alpha==255;premultiplied&=(pixel&255)<=alpha&&((pixel>>8)&255)<=alpha&&((pixel>>16)&255)<=alpha;}
                check(fractional>8&&opaque>100&&premultiplied,"DPI state has antialiased premultiplied edge and opaque crisp interior");
                if(dpi==1.5&&view==View::Compact)capture((std::wstring(L"aa-")+dock+L"-150.bmp").c_str());
            }
            a.shape.attachment={.3,.5,0};a.shape.compact=.4;a.visual={0,0,(LONG)(245*dpi),(LONG)(188*dpi)};a.windowRect=a.visual;a.renderFrame();unsigned fractions=0;for(int y=0;y<a.visual.bottom;++y)for(int x=0;x<a.visual.right;++x){auto alpha=a.paintCache.pixels[y*a.paintCache.width+x]>>24;fractions+=alpha>0&&alpha<255;}check(fractions>8,"intermediate animation contour retains antialiasing");
        }
        a.scale=originalScale;a.work=originalWork;a.dock=L"top";a.view=View::Panel;a.go();settle();
        // Real elapsed-time and message-loop validation (no shortened production timer).
        auto waitStart=now();a.resetIdle();MSG msg{};while(now()-waitStart<10.9&&a.view==View::Panel){while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}Sleep(5);}
        report<<"IdleCollapseSeconds="<<now()-waitStart<<'\n';check(a.view==View::Compact&&now()-waitStart>=9.95&&now()-waitStart<10.6,"real idle timer collapses dock panel after ten seconds");
    }
    DestroyWindow(parent);
    } catch(const std::exception& e) {check(false,"native widget diagnostic exception");report<<e.what()<<'\n';}
    catch(...) {check(false,"native widget diagnostic exception (non-standard)");}
    report<<"Passed="<<count-failures<<" Failed="<<failures<<'\n';writeAtomic(output/L"widget-tests.txt",report.str());return failures?1:0;
}
}
