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
    check(s.resources.size()==4,"setting, two events and one timer");
    auto item=std::find_if(s.resources.begin(),s.resources.end(),[](auto& r){return r.kind==WI_SETTING;});
    check(item!=s.resources.end()&&item->key=="time-display-enabled"&&item->label=="显示时间"&&item->value=="true","actual setting registration and persisted true");
    check(std::none_of(s.resources.begin(),s.resources.end(),[](auto& r){return r.kind==WI_LAYER||r.kind==WI_BUTTON||r.kind==WI_REPLACE;}),"no layer/button/replacement -> no host height contribution");
    loader.emit("music.changed","test music");wait(loader);loader.emit("notice.received","test notice");wait(loader);
    auto toggle=[&](const char* value,size_t count){
      auto current=loader.snapshot();auto setting=std::find_if(current.resources.begin(),current.resources.end(),[](auto& r){return r.kind==WI_SETTING;});
      check(setting!=current.resources.end()&&loader.invoke(setting->handle,value),"invoke actual host setting callback");
      bool done=false;
      for(int n=0;n<200;++n){auto v=loader.snapshot();auto r=std::find_if(v.resources.begin(),v.resources.end(),[](auto& r){return r.kind==WI_SETTING;});
        if(!v.busy&&v.resources.size()==count&&r!=v.resources.end()&&r->value==value){done=true;break;}
        std::this_thread::sleep_for(std::chrono::milliseconds(50));}
      check(done,"setting applied without duplicate resources");
    };
    toggle("false",3);
    check(wi::readFile(dir/L"mods/time-display/settings.xml").find("false")!=std::string::npos,"false written by actual host settings persistence");
    wi::fs::copy_file(dir/L"mods/time-display/settings.xml",dir/L"off-settings.xml",wi::fs::copy_options::overwrite_existing);
    action("reload");check(loader.snapshot().resources.size()==3,"reload restores off and does not recreate timer");
    toggle("true",4);toggle("not-a-boolean",3);
    action("reload");check(loader.snapshot().resources.size()==3,"invalid persisted value safely off after reload");
    toggle("true",4);
    action("reload");check(loader.snapshot().resources.size()==4,"reload restores on with single timer");
    action("disable");check(loader.snapshot().resources.empty()&&!loader.snapshot().mods[0].enabled,"disable revokes every resource");
    action("unload");s=loader.snapshot();check(s.resources.empty()&&!s.mods[0].loaded&&s.mods[0].loads==s.mods[0].unloads,"unload balances LoadLibrary/FreeLibrary");
  }
  check(wi::fs::exists(dir/L"mods/time-display.wimod"),"unload retains package");
  check(wi::readFile(loader.logPath("time-display")).find("时间显示插件已卸载")!=std::string::npos,"plugin lifecycle logs generated");
  return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
}
