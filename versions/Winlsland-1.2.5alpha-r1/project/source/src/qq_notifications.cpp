#include "core.h"
#include "browser_access.h"
#include <UIAutomation.h>

namespace wi {
// QQ NT's own messages often never enter wpndatabase.db. Read only new
// incoming accessible message nodes / changed unread previews after a baseline.
struct QqNotices::Impl {
    HWND owner;
    bool fixtures;
    std::atomic_bool stop = false, paused = false;
    std::thread thread;
    std::mutex mutex;
    std::deque<Notice> pending;
    struct Popup {
        DWORD pid = 0;
        uint64_t arrival = 0;
        std::string fingerprint;
        double nextRead = 0;
        bool dirty = true;
    };
    std::map<HWND, Popup> windows;
    struct Chat {
        ComPtr<IAccessible> root, messages, contacts, heading;
        std::vector<ComPtr<IAccessible>> search;
        std::wstring peer;
        uint64_t highWater = 0;
        struct Preview { std::wstring name,body,badge; };
        std::map<long, Preview> previews;
        std::set<std::string> seen;
        bool seeded=false;
        double refresh=0;
        bool reported=false;
        bool dirty=true;
        double readAt=0;
    };
    std::map<HWND,Chat> chats;
    double nextChatScan=0, nextBrowserScan=0;
    ComPtr<IUIAutomation> automation;
    static thread_local Impl *active;
    Impl(HWND h, bool f) : owner(h), fixtures(f), thread([this] { run(); }) {}
    ~Impl() { stop = true; CoCancelCall(GetThreadId(thread.native_handle()),0); thread.join(); }
    bool isQQ(HWND h, bool &synthetic) {
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!process) return false;
        wchar_t name[32768]{};
        DWORD length = 32768;
        bool ok = QueryFullProcessImageNameW(process, 0, name, &length);
        CloseHandle(process);
        auto file = fs::path(name).filename().wstring();
        synthetic = fixtures && !_wcsicmp(file.c_str(), L"WinIsland-MediaFixture.exe");
        return ok && (synthetic || !_wcsicmp(file.c_str(), L"QQ.exe"));
    }
    void observe(HWND h, DWORD event) {
        if (GetAncestor(h, GA_ROOT) != h) return;
        if (event == EVENT_OBJECT_HIDE || event == EVENT_OBJECT_DESTROY) {
            windows.erase(h);
            return;
        }
        if (!IsWindowVisible(h) || IsIconic(h)) return;
        wchar_t cls[128]{};
        GetClassNameW(h, cls, 128);
        if (wcscmp(cls, L"Chrome_WidgetWin_1") && wcscmp(cls, L"TXGuiFoundation") &&
            !(fixtures && !wcscmp(cls, L"WinIsland.QqPopupFixture"))) return;
        // Reject ordinary chat/settings windows before any cross-process COM.
        RECT rect{};
        if (!GetWindowRect(h, &rect)) return;
        UINT dpi = GetDpiForWindow(h);
        double scale = (dpi ? dpi : 96) / 96.;
        double width = (rect.right - rect.left) / scale, height = (rect.bottom - rect.top) / scale;
        if (width < 150 || width > 620 || height < 40 || height > 380) return;
        bool synthetic = false;
        if (!isQQ(h, synthetic)) return;
        if (!(GetWindowLongPtrW(h, GWL_EXSTYLE) & WS_EX_TOPMOST)) return;
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        auto &p = windows[h];
        if (!p.arrival || p.pid != pid) {
            p = {};
            p.pid = pid;
            FILETIME ft; GetSystemTimeAsFileTime(&ft);
            p.arrival = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
        }
        p.dirty = true;
    }
    static void CALLBACK event(HWINEVENTHOOK, DWORD event, HWND h, LONG object, LONG child, DWORD, DWORD) {
        if (!active || !h || active->paused) return;
        HWND root=GetAncestor(h, GA_ROOT);
        for(auto &[window,chat]:active->chats)
            if(GetAncestor(window,GA_ROOT)==root)chat.dirty=true;
        if (object == OBJID_WINDOW && child == CHILDID_SELF && root==h) active->observe(h,event);
        else if (auto i=active->windows.find(root); i!=active->windows.end()) i->second.dirty=true;
    }
    void queueDirect(Notice n) {
        n.application="QQ.exe";n.testSource=false;
        FILETIME ft;GetSystemTimeAsFileTime(&ft);n.arrival=((uint64_t)ft.dwHighDateTime<<32)|ft.dwLowDateTime;
        n.fingerprint=sha256(utf8(n.title+L"\n"+n.body));
        n.correlation=sha256(n.key()+n.fingerprint).substr(0,16);
        traceNotice(n,"discovered");traceNotice(n,"parsed","format=MSAA/IAccessible2;message_body_logged=0");
        std::lock_guard lock(mutex);
        if(paused||stop){traceNotice(n,"filtered","reason=listener_paused");return;}
        bool empty=pending.empty();traceNotice(n,"listener_queued");pending.push_back(std::move(n));
        if(empty)PostMessageW(owner,WM_NOTICE,0,0);
    }
    void scanChats() {
        if(fixtures) return; // Real QQ is never read by synthetic regression runs.
        if(now()>=nextBrowserScan) {
            nextBrowserScan=now()+3;
            auto live=browserWindows(L"QQ.exe");
            std::erase_if(chats,[&](auto &p){return std::find(live.begin(),live.end(),p.first)==live.end();});
            for(auto h:live)chats.try_emplace(h);
        }
        for(auto &[hwnd,c]:chats) {
            if(stop||paused)break;
            if(!c.root) AccessibleObjectFromWindow(hwnd,OBJID_CLIENT,IID_PPV_ARGS(&c.root));
            if(!c.root)continue;
            if(c.search.empty() && now()>=c.refresh) {
                c.refresh=now()+4;c.search.push_back(c.root);
            }
            scanAccessible(c.search,[&](IAccessible *a,const std::wstring &attrs){
                    if(accessibleClass(attrs,L"ml-list")){c.messages=a;return false;}
                    if(accessibleClass(attrs,L"recent-contact-list")){c.contacts=a;return false;}
                    if(accessibleClass(attrs,L"chat-header__contact-name")){c.heading=a;return false;}
                    return !accessibleClass(attrs,L"msg-input");
                });
            if(!c.dirty && now()<c.readAt)continue;
            c.dirty=false;c.readAt=now()+3;
            if(c.messages && c.heading && !c.reported) {
                c.reported=true;monitor.event("QQ 直接消息读取就绪；backend=MSAA/IAccessible2;data=real;history_replay=0");
            }
            auto peer=accessibleName(c.heading.Get());
            if(peer!=c.peer){c.peer=peer;c.highWater=0;c.seeded=false;c.seen.clear();}
            DWORD pid=0;GetWindowThreadProcessId(hwnd,&pid);
            std::vector<std::pair<uint64_t,ComPtr<IAccessible>>> items;
            for(auto &item:accessibleChildren(c.messages.Get())) {
                auto attrs=accessibleAttributes(item.Get());
                if(!accessibleClass(attrs,L"ml-item"))continue;
                auto id=accessibleAttribute(attrs,L"id");
                if(id.empty() || id.find_first_not_of(L"0123456789")!=id.npos)continue;
                try{items.emplace_back(std::stoull(id),item);}catch(...){}
            }
            std::sort(items.begin(),items.end(),[](auto &a,auto &b){return a.first<b.first;});
            bool baseline=!c.seeded;
            if(baseline && !items.empty()) c.highWater=items.back().first;
            for(auto &[id,item]:items) {
                if(id<=c.highWater)continue;
                auto key=sha256(utf8(peer)+std::to_string(id));if(c.seen.contains(key))continue;
                ComPtr<IAccessible> content;
                walkAccessible(item.Get(),[&](IAccessible *a,const std::wstring &attrs){
                    if(accessibleClass(attrs,L"container--others")){content=a;return false;}return true;
                },45,.08);
                if(!content)continue; // Our outgoing messages are not arrivals.
                auto text=accessibleText(content.Get());if(text.empty())continue; // Retry DOM nodes still being populated.
                c.seen.insert(key);
                Notice n;n.id=(long long)id;n.handler=pid;n.origin="qq-message-accessibility";
                n.title=peer.empty()?L"QQ":peer;n.body=text;queueDirect(std::move(n));
            }
            if(c.messages && c.heading && !peer.empty())c.seeded=true; // Empty conversations also establish a baseline.
            for(auto &item:accessibleChildren(c.contacts.Get(),80)) {
                auto attrs=accessibleAttributes(item.Get());if(!accessibleClass(attrs,L"recent-contact-item"))continue;
                auto a2=browserAccessible(item.Get());long id=0;if(!a2||FAILED(a2->get_uniqueID(&id)))continue;
                ComPtr<IAccessible> summary, info;bool unread=false;std::wstring badge;
                walkAccessible(item.Get(),[&](IAccessible *a,const std::wstring &at){
                    if(accessibleClass(at,L"summary-main")){summary=a;return false;}
                    if(accessibleClass(at,L"item__info"))info=a;
                    if(accessibleClass(at,L"q-badge-num")||accessibleClass(at,L"q-badge-dot")){unread=true;badge=accessibleText(a);}
                    return !accessibleClass(at,L"item__avatar");
                },65,.08);
                std::wstring name;
                for(auto &n:accessibleChildren(info.Get(),20)) {
                    VARIANT role{};n->get_accRole(selfChild(),&role);
                    if(role.vt==VT_I4&&role.lVal==ROLE_SYSTEM_STATICTEXT)name=accessibleName(n.Get());
                    VariantClear(&role);
                }
                auto body=accessibleText(summary.Get());auto previous=c.previews.find(id);
                if(previous!=c.previews.end() && previous->second.name==name &&
                   (previous->second.body!=body || _wtoi(badge.c_str())>_wtoi(previous->second.badge.c_str())) &&
                   !body.empty() && unread && !accessibleClass(attrs,L"recent-contact-item--selected")) {
                    Notice n;n.id=(long long)GetTickCount64();n.handler=pid;n.origin="qq-preview-accessibility";
                    n.title=name.empty()?L"QQ":name;n.body=body;queueDirect(std::move(n));
                }
                c.previews[id]={name,body,badge};
            }
            if(c.seen.size()>4096 && !items.empty()){c.highWater=items.back().first;c.seen.clear();}
        }
    }
    void read(HWND h, Popup &p) {
        wchar_t caption[256]{}; GetWindowTextW(h, caption, 256);
        std::wstring title(caption);
        bool semantic = title.find(L"消息通知") != title.npos || title.find(L"新消息") != title.npos ||
                        title.find(L"消息提醒") != title.npos || title == L"QQ通知";
        bool synthetic = false;
        if (!isQQ(h, synthetic)) return;
        std::vector<std::wstring> texts;
        if (automation) {
            ComPtr<IUIAutomationElement> root;
            if (SUCCEEDED(automation->ElementFromHandle(h, &root)) && root) {
                BSTR rootName=nullptr;
                if (SUCCEEDED(root->get_CurrentName(&rootName))) {
                    std::wstring label(rootName ? rootName : L""); SysFreeString(rootName);
                    if (label.find(L"新消息")!=label.npos || label.find(L"消息通知")!=label.npos || label.find(L"消息提醒")!=label.npos) semantic=true;
                }
                ComPtr<IUIAutomationCacheRequest> cache;
                ComPtr<IUIAutomationCondition> condition;
                automation->CreateCacheRequest(&cache);
                VARIANT type{}; type.vt = VT_I4; type.lVal = UIA_TextControlTypeId;
                automation->CreatePropertyCondition(UIA_ControlTypePropertyId, type, &condition);
                if (cache && condition) {
                    cache->AddProperty(UIA_NamePropertyId);
                    cache->AddProperty(UIA_IsOffscreenPropertyId);
                    cache->put_AutomationElementMode(AutomationElementMode_None);
                    ComPtr<IUIAutomationElementArray> list;
                    if (SUCCEEDED(root->FindAllBuildCache(TreeScope_Descendants, condition.Get(), cache.Get(), &list)) && list) {
                        int count = 0; list->get_Length(&count);
                        for (int i = 0; i < std::min(count, 64); ++i) {
                            ComPtr<IUIAutomationElement> e;
                            list->GetElement(i, &e);
                            BOOL off = TRUE;
                            BSTR name = nullptr;
                            if (!e || FAILED(e->get_CachedIsOffscreen(&off)) || off || FAILED(e->get_CachedName(&name))) continue;
                            std::wstring value(name ? name : L""); SysFreeString(name);
                            if (value == L"新消息" || value == L"消息通知" || value == L"消息提醒") semantic = true;
                            if (!value.empty() && value.size() <= 1000 && value != L"QQ" && value != L"关闭" &&
                                value != L"回复" && value != title) texts.push_back(value);
                        }
                    }
                }
            }
        }
        Notice n;
        n.id = (long long)(uintptr_t)h;
        n.handler = p.pid;
        n.arrival = p.arrival;
        n.origin = "qq-native-popup";
        n.application = synthetic ? "QQ-popup-fixture" : "QQ.exe";
        n.testSource = synthetic;
        n.correlation = sha256(n.key()).substr(0, 16);
        if (!semantic) {
            if (p.fingerprint.empty()) traceNotice(n, "filtered", "reason=unconfirmed_popup_semantics");
            p.fingerprint = "unconfirmed";
            return;
        }
        if (texts.empty()) {
            // Positive notification semantics, but Chromium accessibility may
            // hide its body. Report the real arrival without inventing content.
            n.title = L"QQ";
            n.body = L"收到新消息，请在 QQ 查看";
        } else {
            n.title = texts.front();
            for (size_t i = 1; i < std::min<size_t>(texts.size(), 5); ++i) {
                if (!n.body.empty()) n.body += L"\n";
                n.body += texts[i];
            }
        }
        n.fingerprint = sha256(utf8(n.title + L"\n" + n.body));
        if (n.fingerprint == p.fingerprint) return;
        n.correlation = sha256(n.key() + n.fingerprint).substr(0, 16);
        p.fingerprint = n.fingerprint;
        traceNotice(n, "discovered");
        traceNotice(n, "parsed", texts.empty() ? "format=confirmed_popup;body=not_exposed" : "format=UIA_Text");
        std::lock_guard lock(mutex);
        if (paused || stop) { traceNotice(n, "filtered", "reason=listener_paused"); return; }
        bool empty = pending.empty();
        traceNotice(n, "listener_queued");
        pending.push_back(std::move(n));
        if (empty) PostMessageW(owner, WM_NOTICE, 0, 0);
    }
    void run() {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        CoEnableCallCancellation(nullptr);
        // Timeouts apply to cross-process accessibility. Work stays off the UI
        // and toast-database threads even if an external QQ provider is slow.
        ComPtr<IUIAutomation2> timed;
        if (SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation8, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&timed)))) {
            timed->put_ConnectionTimeout(250);
            timed->put_TransactionTimeout(250);
            timed.As(&automation);
        }
        active = this;
        auto hook = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_TEXTEDIT_CONVERSIONTARGETCHANGED, nullptr, event, 0, 0,
                                    WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        double nextScan = 0, nextHealth = 0;
        while (!stop) {
            MSG m;
            while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
            if (paused) { windows.clear();chats.clear(); }
            else {
                if(now()>=nextChatScan){nextChatScan=now()+.7;Measure m(AccessibilityWork);try{scanChats();}catch(...){monitor.event("QQ 直接读取暂不可用；等待辅助功能树恢复");}}
                if (now() >= nextHealth && monitor.active()) {
                    monitor.event(std::string("QQ 弹窗监听；backend=WinEvent/UIA;hook=") + (hook ? "1" : "0") +
                                  ";accessibility=" + (automation ? "1" : "0") + ";direct_roots=" + std::to_string(chats.size()) + ";history_replay=0");
                    nextHealth = now() + 30;
                }
                if (now() >= nextScan) {
                    nextScan = now() + 3;
                    EnumWindows([](HWND h, LPARAM ctx) -> BOOL { ((Impl *)ctx)->observe(h, EVENT_OBJECT_SHOW); return TRUE; }, (LPARAM)this);
                }
                for (auto it = windows.begin(); it != windows.end();) {
                    if (!IsWindow(it->first) || !IsWindowVisible(it->first)) { it = windows.erase(it); continue; }
                    auto &p = it->second;
                    if (p.dirty && now() >= p.nextRead) {
                        p.dirty = false; p.nextRead = now() + .2;
                        try { Measure work(NoticeWork); read(it->first, p); }
                        catch (...) { monitor.event("QQ 弹窗解析失败；保持 QQ 原弹窗"); }
                    }
                    ++it;
                }
            }
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT);
        }
        if (hook) UnhookWinEvent(hook);
        active = nullptr;
        automation.Reset(); timed.Reset();
        chats.clear();
        CoDisableCallCancellation(nullptr);
        CoUninitialize();
    }
};
thread_local QqNotices::Impl *QqNotices::Impl::active = nullptr;
QqNotices::QqNotices(HWND h, bool fixtures) : impl(std::make_unique<Impl>(h, fixtures)) {}
QqNotices::~QqNotices() = default;
void QqNotices::pause(bool p) {
    std::lock_guard lock(impl->mutex); impl->paused = p; impl->pending.clear();
}
std::deque<Notice> QqNotices::take() {
    std::lock_guard lock(impl->mutex); std::deque<Notice> r; r.swap(impl->pending); return r;
}
} // namespace wi

