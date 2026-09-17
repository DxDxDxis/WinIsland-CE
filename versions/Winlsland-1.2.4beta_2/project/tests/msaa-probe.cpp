#include "../src/browser_access.h"
#include <regex>
using namespace wi;
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int) {
    int argc;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);if(argc<2)return 1;
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);std::ostringstream out;int total=0;
    struct Target{HWND h;std::wstring app;};std::vector<Target> targets;
    EnumWindows([](HWND h,LPARAM l)->BOOL {
        DWORD pid=0;GetWindowThreadProcessId(h,&pid);HANDLE p=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(!p)return TRUE;
        wchar_t path[32768]{};DWORD n=32768;QueryFullProcessImageNameW(p,0,path,&n);CloseHandle(p);
        auto file=fs::path(path).filename().wstring();if(_wcsicmp(file.c_str(),L"QQ.exe")&&_wcsicmp(file.c_str(),L"cloudmusic.exe"))return TRUE;
        auto &v=*(std::vector<Target>*)l;
        struct Children{std::vector<Target>*v;std::wstring file;} state{&v,file};
        EnumChildWindows(h,[](HWND c,LPARAM l)->BOOL{auto&s=*(Children*)l;wchar_t cls[100]{};GetClassNameW(c,cls,100);if(!wcscmp(cls,L"Chrome_RenderWidgetHostHWND"))s.v->push_back({c,s.file});return TRUE;},(LPARAM)&state);return TRUE;
    },(LPARAM)&targets);
    for(auto &target:targets){ComPtr<IAccessible> root;
        if(FAILED(AccessibleObjectFromWindow(target.h,OBJID_CLIENT,IID_PPV_ARGS(&root))))continue;
        out<<"APP="<<utf8(target.app)<<" hwnd="<<(uintptr_t)target.h<<"\n";
        std::vector<std::pair<ComPtr<IAccessible>,int>> stack{{root,0}};int nodes=0;
        while(!stack.empty()&&nodes++<4000){auto [a,depth]=stack.back();stack.pop_back();auto attrs=accessibleAttributes(a.Get());auto name=accessibleName(a.Get());
            VARIANT role{};a->get_accRole(selfChild(),&role);long id=0;auto b=browserAccessible(a.Get());if(b)b->get_uniqueID(&id);
            out<<"depth="<<depth<<" role="<<role.lVal<<" id="<<id<<" name_length="<<name.size()<<" attrs="<<utf8(attrs);
            if(target.app==L"cloudmusic.exe" && (accessibleClass(attrs,L"slider-vinyl") || accessibleClass(attrs,L"current") || accessibleClass(attrs,L"title") || accessibleClass(attrs,L"artist"))) {
                BSTR value=nullptr;a->get_accValue(selfChild(),&value);
                out<<" MUSIC_NAME="<<utf8(name)<<" MUSIC_VALUE="<<utf8(value?value:L"")<<" MUSIC_TEXT="<<utf8(accessibleText(a.Get()));SysFreeString(value);
            }
            if(target.app==L"cloudmusic.exe" && std::regex_match(utf8(name),std::regex("[0-9]{1,3}:[0-9]{2}([ /]+[0-9]{1,3}:[0-9]{2})?")))out<<" TIME="<<utf8(name);
            out<<"\n";VariantClear(&role);
            if(depth<30)for(auto &c:accessibleChildren(a.Get(),100))stack.push_back({c,depth+1});
        }out<<"NODES="<<nodes<<"\n";total+=nodes;
    }
    writeAtomic(argv[1],out.str());LocalFree(argv);CoUninitialize();return total?0:2;
}
