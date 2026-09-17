#pragma once
#include "core.h"
#include <oleacc.h>
#include <servprov.h>
namespace wi {
// Public IAccessible2 ABI used by Chromium/CEF. No injection or private QQ API.
struct IA2Locale { BSTR language, country, variant; };
MIDL_INTERFACE("E89F726E-C4F4-4c19-BB19-B647D7FA8478") BrowserAccessible : IAccessible {
    virtual HRESULT STDMETHODCALLTYPE get_nRelations(long *) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_relation(long, IUnknown **) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_relations(long, IUnknown **, long *) = 0;
    virtual HRESULT STDMETHODCALLTYPE role(long *) = 0;
    virtual HRESULT STDMETHODCALLTYPE scrollTo(int) = 0;
    virtual HRESULT STDMETHODCALLTYPE scrollToPoint(int, long, long) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_groupPosition(long *, long *, long *) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_states(LONGLONG *) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_extendedRole(BSTR *) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_localizedExtendedRole(BSTR *) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_nExtendedStates(long *) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_extendedStates(long, BSTR **, long *) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_localizedExtendedStates(long, BSTR **, long *) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_uniqueID(long *) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_windowHandle(HWND *) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_indexInParent(long *) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_locale(IA2Locale *) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_attributes(BSTR *) = 0;
};
inline ComPtr<BrowserAccessible> browserAccessible(IAccessible *a) {
    ComPtr<BrowserAccessible> b;
    ComPtr<IServiceProvider> services;
    if (a && SUCCEEDED(a->QueryInterface(IID_PPV_ARGS(&services))))
        services->QueryService(__uuidof(IAccessible), __uuidof(BrowserAccessible), (void **)&b);
    return b;
}
inline VARIANT selfChild() { VARIANT v{}; v.vt=VT_I4; v.lVal=CHILDID_SELF; return v; }
inline std::wstring accessibleName(IAccessible *a) {
    BSTR b=nullptr; if(!a || FAILED(a->get_accName(selfChild(),&b))) return {};
    std::wstring s(b ? b : L""); SysFreeString(b); return s;
}
inline std::wstring accessibleAttributes(IAccessible *a) {
    auto b=browserAccessible(a); BSTR text=nullptr;
    if(!b || FAILED(b->get_attributes(&text))) return {};
    std::wstring s(text ? text : L""); SysFreeString(text); return s;
}
inline std::vector<ComPtr<IAccessible>> accessibleChildren(IAccessible *a, long cap=128) {
    std::vector<ComPtr<IAccessible>> out; long count=0;
    if(!a || FAILED(a->get_accChildCount(&count)) || count<=0) return out;
    count=std::min(count,cap);std::vector<VARIANT> values(count);long obtained=0;
    if(FAILED(AccessibleChildren(a,0,count,values.data(),&obtained))) return out;
    for(long i=0;i<obtained;++i) {
        if(values[i].vt==VT_DISPATCH && values[i].pdispVal) {
            ComPtr<IAccessible> child; values[i].pdispVal->QueryInterface(IID_PPV_ARGS(&child));
            if(child) out.push_back(child);
        }
        VariantClear(&values[i]);
    }
    return out;
}
inline std::wstring accessibleAttribute(const std::wstring &attrs, const std::wstring &key) {
    auto token=key+L":";size_t p=attrs.find(token);
    while(p!=attrs.npos && p && attrs[p-1]!=L';')p=attrs.find(token,p+1);
    if(p==attrs.npos)return {};p+=token.size();auto end=attrs.find(L';',p);
    return attrs.substr(p,end==attrs.npos?end:end-p);
}
// Continue bounded discovery across polls instead of restarting at the root and
// permanently starving nodes near the end of a large Chromium document.
inline void scanAccessible(std::vector<ComPtr<IAccessible>> &stack,
    const std::function<bool(IAccessible *,const std::wstring &)> &visit, double seconds=.08) {
    double end=now()+seconds;int limit=180;
    while(!stack.empty()&&limit-->0&&now()<end) {
        auto a=stack.back();stack.pop_back();auto attrs=accessibleAttributes(a.Get());
        if(!visit(a.Get(),attrs))continue;
        auto children=accessibleChildren(a.Get());
        for(auto i=children.rbegin();i!=children.rend();++i)stack.push_back(*i);
        if(stack.size()>3000){stack.clear();break;}
    }
}
inline bool accessibleClass(const std::wstring &attrs, const wchar_t *name) {
    auto cls=L" "+accessibleAttribute(attrs,L"class")+L" ";
    return cls.find(L" "+std::wstring(name)+L" ")!=cls.npos;
}
inline void walkAccessible(IAccessible *root, const std::function<bool(IAccessible *,const std::wstring &)> &visit,
                           int limit=700, double seconds=.2) {
    std::vector<ComPtr<IAccessible>> stack;if(root)stack.emplace_back(root);double end=now()+seconds;
    while(!stack.empty()&&limit-->0&&now()<end) {
        auto a=stack.back();stack.pop_back();auto attrs=accessibleAttributes(a.Get());
        if(!visit(a.Get(),attrs))continue;
        auto children=accessibleChildren(a.Get());
        for(auto i=children.rbegin();i!=children.rend();++i)stack.push_back(*i);
    }
}
inline std::wstring accessibleText(IAccessible *a) {
    std::wstring text;
    std::vector<ComPtr<IAccessible>> stack;if(a)stack.emplace_back(a);
    int budget=96;double end=now()+.08;
    while(!stack.empty()&&budget-->0&&now()<end) {
        auto n=stack.back();stack.pop_back();VARIANT role{};n->get_accRole(selfChild(),&role);
        bool leaf=role.vt==VT_I4 && (role.lVal==ROLE_SYSTEM_STATICTEXT || role.lVal==ROLE_SYSTEM_GRAPHIC);
        VariantClear(&role);
        if(leaf) {auto s=accessibleName(n.Get());if(text.size()+s.size()<1200)text+=s;continue;}
        auto children=accessibleChildren(n.Get());
        for(auto i=children.rbegin();i!=children.rend();++i)stack.push_back(*i);
    }
    return text;
}
inline std::vector<HWND> browserWindows(const std::wstring &exe) {
    struct Scan{std::wstring exe;std::vector<HWND> windows;} scan{exe};
    EnumWindows([](HWND h,LPARAM l)->BOOL {
        auto &s=*(Scan*)l;DWORD pid=0;GetWindowThreadProcessId(h,&pid);
        HANDLE p=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(!p)return TRUE;
        wchar_t path[32768]{};DWORD n=32768;bool ok=QueryFullProcessImageNameW(p,0,path,&n);CloseHandle(p);
        if(!ok || _wcsicmp(fs::path(path).filename().c_str(),s.exe.c_str()))return TRUE;
        EnumChildWindows(h,[](HWND c,LPARAM l)->BOOL {wchar_t cls[128]{};GetClassNameW(c,cls,128);
            if(!wcscmp(cls,L"Chrome_RenderWidgetHostHWND"))((Scan*)l)->windows.push_back(c);return TRUE;},l);
        return TRUE;
    },(LPARAM)&scan);return scan.windows;
}
} // namespace wi

