#include "mod_system.h"
#include <iostream>
#include <thread>
static void check(bool b,const char* s){std::cout<<(b?"PASS ":"FAIL ")<<s<<std::endl;if(!b)throw std::runtime_error(s);}
static void wait(wi::ModLoader& l){for(int n=0;n<200;++n){if(!l.busy())return;std::this_thread::sleep_for(std::chrono::milliseconds(50));}throw std::runtime_error("loader timeout");}
int wmain(int argc,wchar_t** argv){
 if(argc!=3)return 2;
 try {
  wi::fs::path dir=argv[2];wi::fs::create_directories(dir/L"mods");
  wi::ModLoader loader(dir/L"mods",false,dir);wait(loader);
  check(loader.install(argv[1]),"install package accepted");wait(loader);
  auto s=loader.snapshot();check(s.mods.size()==1&&s.mods[0].valid,"actual ZIP/manifest validation");
  auto m=s.mods[0];check(m.name=="时间显示"&&m.author=="daxian"&&m.version=="1.0.0"&&m.description=="一个能让 WinIsland 在待机状态显示实时时间的插件。","actual loader metadata");
  auto action=[&](const char* a){
    auto before=loader.snapshot().mods[0].loads;
    check(loader.request(a,"time-display"),a);
    for(int n=0;n<200;++n){
      auto current=loader.snapshot();auto& m=current.mods[0];std::string op=a;
      bool done=op=="disable"?m.status=="已禁用":op=="unload"?m.status=="已卸载":
          m.status=="已启用"&&(op!="reload"||m.loads>before);
      if(done&&!current.busy)return;
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    throw std::runtime_error("operation result timeout");
  };
  for(int i=0;i<3;++i){
    action("enable");s=loader.snapshot();check(s.mods[0].enabled&&s.mods[0].loaded,"DLL loaded and enabled");
    check(s.resources.size()==3,"two events and one timer");
    check(std::none_of(s.resources.begin(),s.resources.end(),[](auto& r){return r.kind==WI_LAYER||r.kind==WI_BUTTON||r.kind==WI_REPLACE;}),"no layer/button/replacement -> no host height contribution");
    loader.emit("music.changed","test music");wait(loader);loader.emit("notice.received","test notice");wait(loader);
    action("reload");check(loader.snapshot().resources.size()==3,"reload does not duplicate resources");
    action("disable");check(loader.snapshot().resources.empty()&&!loader.snapshot().mods[0].enabled,"disable revokes every resource");
    action("unload");s=loader.snapshot();check(s.resources.empty()&&!s.mods[0].loaded&&s.mods[0].loads==s.mods[0].unloads,"unload balances LoadLibrary/FreeLibrary");
  }
  check(wi::fs::exists(dir/L"mods/time-display.wimod"),"unload retains package");
  check(wi::readFile(loader.logPath("time-display")).find("时间显示插件已卸载")!=std::string::npos,"plugin lifecycle logs generated");
  return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
}
