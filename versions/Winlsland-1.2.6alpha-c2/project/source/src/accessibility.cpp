#include "render.h"
#include <oleacc.h>

namespace wi {
std::wstring controlName(const Scene &scene, int id) {
    if(id>=101&&id<=104)return controlName(scene,id-100);
    for(auto& n:scene.visibleNodes)if(n.id==(uint64_t)id)return wide(n.text(WI_TEXT_VALUE).empty()?n.key:n.text(WI_TEXT_VALUE));
    if(id>=50000)for(auto& r:scene.modResources)if(r.handle==(uint64_t)id)return wide(r.label);
    auto m = scene.music;
    const wchar_t *names[] = {scene.expanded ? L"收起音乐" : L"展开音乐", L"上一首",
                              m && m->playing ? L"暂停" : L"播放", L"下一首",
                              m && m->shuffle       ? L"随机播放，切换模式"
                              : m && m->repeat == 1 ? L"单曲循环，切换模式"
                              : m && m->repeat == 2 ? L"列表循环，切换模式"
                                                    : L"顺序播放，切换模式"};
    return id >= 0 && id <= 4 ? names[id] : L"";
}
// MSAA simple children are bridged by Windows UI Automation. Real names, bounds,
// state and actions stay available without creating opaque child HWND surfaces.
class Accessible final : public IAccessible {
    std::atomic_ulong refs{1};
    HWND window;
    Scene &scene;
    float scale;
    int id(VARIANT child) const {
        if (!IsWindow(window) || child.vt != VT_I4 || child.lVal < 0 ||
            child.lVal > (long)scene.buttons.size())
            return -1;
        return child.lVal;
    }
    HRESULT string(BSTR *out, const std::wstring &value) {
        if (!out)
            return E_POINTER;
        *out = SysAllocString(value.c_str());
        return *out ? S_OK : E_OUTOFMEMORY;
    }

  public:
    Accessible(HWND h, Scene &s, float dpi) : window(h), scene(s), scale(dpi) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (iid == IID_IUnknown || iid == IID_IDispatch || iid == IID_IAccessible) {
            *out = static_cast<IAccessible *>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return ++refs;
    }
    ULONG STDMETHODCALLTYPE Release() override {
        auto n = --refs;
        if (!n)
            delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT *n) override {
        if (!n)
            return E_POINTER;
        *n = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT, LCID, ITypeInfo **p) override {
        if (p)
            *p = nullptr;
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID, LPOLESTR *, UINT, LCID, DISPID *) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE Invoke(DISPID, REFIID, LCID, WORD, DISPPARAMS *, VARIANT *, EXCEPINFO *,
                                     UINT *) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE get_accParent(IDispatch **p) override {
        if (!p)
            return E_POINTER;
        *p = nullptr;
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE get_accChildCount(long *n) override {
        if (!n)
            return E_POINTER;
        *n = IsWindow(window) ? (long)scene.buttons.size() : 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_accChild(VARIANT, IDispatch **p) override {
        if (!p)
            return E_POINTER;
        *p = nullptr;
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE get_accName(VARIANT child, BSTR *out) override {
        int n = id(child);
        if (n < 0)
            return E_INVALIDARG;
        if (!n)
            return string(out, L"WinIsland 音乐与通知");
        return string(out, controlName(scene, scene.buttons[n - 1].first));
    }
    HRESULT STDMETHODCALLTYPE get_accValue(VARIANT child, BSTR *out) override {
        if (id(child) < 0)
            return E_INVALIDARG;
        int childIndex=id(child);if(childIndex>0)for(auto& node:scene.visibleNodes)if(node.id==(uint64_t)scene.buttons[childIndex-1].first)return string(out,wide(node.text(WI_TEXT_VALUE)));
        std::wstring value;
        if (scene.music)
            value = scene.music->title + L"，" + scene.music->artist + L"，" + scene.lyric;
        if (scene.notice)
            value += L"。" + scene.notice->title + L"，" + scene.notice->body;
        return string(out, value);
    }
    HRESULT STDMETHODCALLTYPE get_accDescription(VARIANT c, BSTR *p) override {
        return get_accValue(c, p);
    }
    HRESULT STDMETHODCALLTYPE get_accRole(VARIANT child, VARIANT *out) override {
        if (!out)
            return E_POINTER;
        int n = id(child);
        if (n < 0)
            return E_INVALIDARG;
        VariantInit(out);
        out->vt = VT_I4;
        out->lVal = n ? ROLE_SYSTEM_PUSHBUTTON : ROLE_SYSTEM_GROUPING;
        if(n)for(auto& node:scene.visibleNodes)if(node.id==(uint64_t)scene.buttons[n-1].first){auto type=(uint32_t)node.n(WI_ELEMENT_TYPE,0,node.type);if(type==WI_INPUT||type==WI_TEXT)out->lVal=ROLE_SYSTEM_TEXT;else if(type==WI_SLIDER)out->lVal=ROLE_SYSTEM_SLIDER;else if(type==WI_PROGRESS)out->lVal=ROLE_SYSTEM_PROGRESSBAR;else if(type==WI_CONTAINER)out->lVal=ROLE_SYSTEM_GROUPING;break;}
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_accState(VARIANT child, VARIANT *out) override {
        if (!out)
            return E_POINTER;
        int n = id(child);
        if (n < 0)
            return E_INVALIDARG;
        VariantInit(out);
        out->vt = VT_I4;
        out->lVal = n ? STATE_SYSTEM_FOCUSABLE : 0;
        if (n > 1 && scene.busy)
            out->lVal |= STATE_SYSTEM_UNAVAILABLE;
        if (n && GetFocus() == window && scene.focus == scene.buttons[n - 1].first)
            out->lVal |= STATE_SYSTEM_FOCUSED;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_accHelp(VARIANT, BSTR *p) override {
        if (!p)
            return E_POINTER;
        *p = nullptr;
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE get_accHelpTopic(BSTR *p, VARIANT, long *n) override {
        if (p)
            *p = nullptr;
        if (n)
            *n = 0;
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE get_accKeyboardShortcut(VARIANT, BSTR *p) override {
        return string(p, L"Tab，Enter 或空格");
    }
    HRESULT STDMETHODCALLTYPE get_accFocus(VARIANT *out) override {
        if (!out)
            return E_POINTER;
        VariantInit(out);
        if (GetFocus() == window) {
            out->vt = VT_I4;
            for (size_t i = 0; i < scene.buttons.size(); i++)
                if (scene.buttons[i].first == scene.focus)
                    out->lVal = (long)i + 1;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_accSelection(VARIANT *p) override {
        if (!p)
            return E_POINTER;
        VariantInit(p);
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE get_accDefaultAction(VARIANT c, BSTR *p) override {
        return id(c) > 0 ? string(p, L"执行") : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE accSelect(long flags, VARIANT c) override {
        if (id(c) <= 0)
            return E_INVALIDARG;
        if (flags & SELFLAG_TAKEFOCUS) {
            PostMessageW(window, WM_APP + 7, scene.buttons[c.lVal - 1].first, 0);
            return S_OK;
        }
        return E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE accLocation(long *x, long *y, long *w, long *h, VARIANT c) override {
        int n = id(c);
        if (n < 0)
            return E_INVALIDARG;
        if (!x || !y || !w || !h)
            return E_POINTER;
        RECT r;
        GetWindowRect(window, &r);
        Box b{0, 0, (float)(r.right - r.left) / scale, (float)scene.height};
        if (n)
            b = scene.buttons[n - 1].second;
        *x = r.left + (long)(b.x * scale);
        *y = r.top + (long)(b.y * scale);
        *w = (long)(b.w * scale);
        *h = (long)(b.h * scale);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE accNavigate(long nav, VARIANT c, VARIANT *out) override {
        if (!out)
            return E_POINTER;
        VariantInit(out);
        int n = id(c);
        if (n < 0)
            return E_INVALIDARG;
        std::vector<long> children;
        for (size_t i = 0; i < scene.buttons.size(); i++)
            children.push_back((long)i + 1);
        long result = 0;
        if (!children.empty()) {
            if (nav == NAVDIR_FIRSTCHILD && !n)
                result = children.front();
            else if (nav == NAVDIR_LASTCHILD && !n)
                result = children.back();
            else {
                auto i = std::find(children.begin(), children.end(), n);
                if (i != children.end()) {
                    if (nav == NAVDIR_NEXT && i + 1 != children.end())
                        result = *(i + 1);
                    if (nav == NAVDIR_PREVIOUS && i != children.begin())
                        result = *(i - 1);
                }
            }
        }
        if (!result)
            return S_FALSE;
        out->vt = VT_I4;
        out->lVal = result;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE accHitTest(long x, long y, VARIANT *out) override {
        if (!out)
            return E_POINTER;
        VariantInit(out);
        if (!IsWindow(window))
            return S_FALSE;
        POINT p{x, y};
        ScreenToClient(window, &p);
        for (auto i = scene.buttons.rbegin(); i != scene.buttons.rend(); ++i)
            if (i->second.hit(p.x / scale, p.y / scale)) {
                out->vt = VT_I4;
                out->lVal = (long)(i.base() - scene.buttons.begin());
                return S_OK;
            }
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE accDoDefaultAction(VARIANT c) override {
        int n = id(c);
        if (n <= 0)
            return E_INVALIDARG;
        if (n > 1 && scene.busy)
            return E_ACCESSDENIED;
        PostMessageW(window, WM_APP + 6, scene.buttons[n - 1].first, 0);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_accName(VARIANT, BSTR) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE put_accValue(VARIANT, BSTR) override {
        return E_NOTIMPL;
    }
};
LRESULT accessibleObject(HWND h, WPARAM wp, Scene &scene, float dpi) {
    auto *p = new Accessible(h, scene, dpi);
    auto result = LresultFromObject(IID_IAccessible, wp, p);
    p->Release();
    return result;
}
} // namespace wi

