#include "embedded_components.h"
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>

namespace wi {
namespace {
constexpr WORD BundleResource = 100;
constexpr char Magic[] = "WICOMP1";
fs::path selected;

std::string jsonString(const std::string& s, const char* key) {
    std::string needle = std::string("\"") + key + "\"";
    auto p = s.find(needle); if (p == s.npos) return {};
    p = s.find(':', p + needle.size()); if (p == s.npos) return {};
    p = s.find('"', p + 1); if (p == s.npos) return {};
    auto e = s.find('"', p + 1); if (e == s.npos) return {};
    return s.substr(p + 1, e - p - 1);
}
std::string componentId() { return BuildId; }
fs::path locatorPath() { return dataDir() / L"installer" / L"install-location.json"; }
fs::path statePath(const fs::path& root) { return root / L"install-state.json"; }
bool writeBytes(const fs::path& p, const unsigned char* data, size_t n) {
    std::error_code ec; fs::create_directories(p.parent_path(), ec); if (ec) return false;
    std::ofstream f(p, std::ios::binary); if (!f) return false;
    f.write((const char*)data, (std::streamsize)n); return !!f;
}
bool resourceBytes(std::vector<unsigned char>& out) {
    HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(BundleResource), RT_RCDATA);
    if (!r) return false; HGLOBAL h = LoadResource(nullptr, r); if (!h) return false;
    auto n = SizeofResource(nullptr, r); auto p = (const unsigned char*)LockResource(h);
    if (!p || n < 12) return false; out.assign(p, p + n); return true;
}
uint32_t u32(const unsigned char*& p) { uint32_t v=0; memcpy(&v,p,4); p+=4; return v; }
uint64_t u64(const unsigned char*& p) { uint64_t v=0; memcpy(&v,p,8); p+=8; return v; }
fs::path chooseRoot() {
    auto writable = [](const fs::path& p) {
        std::error_code ec; fs::create_directories(p, ec); if (ec) return false;
        auto probe=p/L".write-test"; std::ofstream f(probe); bool ok=!!f; f.close(); fs::remove(probe,ec); return ok;
    };
    for (wchar_t drive : {L'D',L'E',L'F',L'G',L'H',L'I',L'J'}) {
        wchar_t root[4]{drive,L':',L'\\',0}; UINT type=GetDriveTypeW(root); if (type!=DRIVE_FIXED) continue;
        ULARGE_INTEGER avail{}, total{}, free{}; if (!GetDiskFreeSpaceExW(root,&avail,&total,&free) || avail.QuadPart < 64ull*1024*1024) continue;
        fs::path p=fs::path(root)/L"WinIsland"; if (writable(p)) return p;
    }
    return dataDir();
}
fs::path chooseManualRoot(HWND owner) {
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)))) return {};
    DWORD options=0; dialog->GetOptions(&options); dialog->SetOptions(options|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST);
    dialog->SetTitle(L"选择 WinIsland 安装文件夹");
    if (FAILED(dialog->Show(owner))) return {};
    ComPtr<IShellItem> item; PWSTR path=nullptr;
    if (FAILED(dialog->GetResult(&item)) || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))) return {};
    fs::path result(path); CoTaskMemFree(path); return result;
}
bool unpack(const fs::path& root, const std::vector<unsigned char>& b, bool persist=true) {
    if (memcmp(b.data(), Magic, sizeof(Magic)-1)!=0) return false;
    const unsigned char* p=b.data()+sizeof(Magic)-1; const unsigned char* end=b.data()+b.size();
    if (p+4>end) return false; uint32_t count=u32(p); fs::path version=root/L"components"/L"lyric-provider"/wide(componentId());
    fs::path stage=root/L".component-staging"/wide(componentId()); std::error_code ec; fs::remove_all(stage,ec); fs::create_directories(stage,ec);
    if (ec) return false;
    for(uint32_t i=0;i<count;++i){ if(p+4>end)return false; auto plen=u32(p); if(p+plen+8>end)return false; std::string rel((const char*)p,plen);p+=plen;auto n=u64(p);if(n>uint64_t(end-p))return false;
        fs::path relative=fs::path(wide(rel));
        if(relative.is_absolute()) return false;
        for(const auto& part:relative) if(part==L".."||part==L".") return false;
        fs::path out=stage/relative;
        if(!writeBytes(out,p,(size_t)n))return false;p+=n;
    }
    fs::remove_all(version,ec); fs::create_directories(version.parent_path(),ec); if(ec)return false;
    fs::rename(stage,version,ec); if(ec){fs::remove_all(version,ec);fs::rename(stage,version,ec);} if(ec)return false;
    if(persist){
        writeAtomic(statePath(root), std::string("{\"format\":1,\"componentId\":\"")+componentId()+"\",\"managed\":\"components/lyric-provider/"+componentId()+"\"}");
        writeAtomic(locatorPath(), std::string("{\"format\":1,\"instance\":\"winisland\",\"root\":\"")+utf8(root.wstring())+"\",\"componentId\":\""+componentId()+"\"}");
    }
    selected=version; return true;
}
}
fs::path lyricProviderDirectory(){return selected.empty()?exePath().parent_path()/L"lyric-provider":selected;}
bool prepareEmbeddedComponents(bool diagnostic) {
    if(!selected.empty()) return true;
    std::vector<unsigned char> bundle; if(!resourceBytes(bundle)) return true; // developer builds may ship side-by-side files
    if(diagnostic) return false;
    auto loc=readFile(locatorPath()); fs::path root;
    if(!loc.empty()){auto v=jsonString(loc,"root");if(!v.empty())root=fs::path(wide(v));}
    if(root.empty()) root=chooseRoot();
    auto version=root/L"components"/L"lyric-provider"/wide(componentId());
    if(fs::exists(version/L"WinIsland-LyricHelper.exe")&&fs::exists(version/L"Qt6Core.dll")){selected=version;return true;}
    int choice=MessageBoxW(nullptr,(L"WinIsland 首次需要释放歌词组件。\n默认安装位置："+root.wstring()+L"\n选择“是”使用此位置，选择“否”自定义文件夹，选择“取消”跳过本次安装。").c_str(),L"WinIsland 组件安装",MB_YESNOCANCEL|MB_ICONINFORMATION);
    if(choice==IDCANCEL) return false;
    if(choice==IDNO){auto manual=chooseManualRoot(nullptr);if(manual.empty())return false;root=manual;}
    bool ok=unpack(root,bundle);
    MessageBoxW(nullptr,ok?L"组件安装完成，已固定当前组件路径。":L"组件安装失败，未更改现有组件。",L"WinIsland 组件安装",MB_OK|(ok?MB_ICONINFORMATION:MB_ICONERROR));
    return ok;
}
int embeddedComponentsTest(const fs::path& output){
    std::vector<unsigned char> bundle; if(!resourceBytes(bundle)) return 2;
    return unpack(output,bundle,false)?0:1;
}
}
