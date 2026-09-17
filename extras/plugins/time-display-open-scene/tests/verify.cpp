#include "../sdk/mod_api.h"
#define NOMINMAX
#include <windows.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>
using Microsoft::WRL::ComPtr;
struct Node { uint32_t type; WiElement parent; std::string key; std::map<uint32_t,WiPropertyValue> p; };
struct Host {
    std::map<WiElement,Node> nodes;
    std::map<uint64_t,WinIslandResource> resources;
    std::map<std::string,std::string> settings;
    uint64_t next=10; int commits=0,failSet=0;
    WiSceneSnapshot snapshot{sizeof(WiSceneSnapshot),1,WI_IDLE,96,1,0,0,0,257,38};
    WinIslandHostApi api{};
    Host() {
        api.size=sizeof(api); api.version=WINISLAND_MOD_ABI; api.context=this;
        api.log=[](void*,const char* s){printf("LOG %s\n",s);};
        api.add=[](void* c,const WinIslandResource* r)->uint64_t {auto& h=*(Host*)c; auto id=h.next++;h.resources[id]=*r;return id;};
        api.remove=[](void* c,uint64_t id){((Host*)c)->resources.erase(id);return 0;};
        api.queryInterface=[](void* c,const char* name,uint32_t,void* out,uint32_t)->int {
            if(!strcmp(name,WI_SCENE_INTERFACE)) {
                WiSceneApi s{};s.size=sizeof(s);s.version=1;s.context=c;
                s.snapshot=[](void* c,WiSceneSnapshot* out){*out=((Host*)c)->snapshot;return 0;};
                s.find=[](void*,const char* key,WiElement* out)->int{*out=1;return strcmp(key,"island")?WI_NOT_FOUND:WI_OK;};
                s.create=[](void* c,uint32_t type,const char* key,WiElement parent,WiElement* out){auto& h=*(Host*)c;*out=h.next++;h.nodes[*out]={type,parent,key,{}};return 0;};
                s.set=[](void* c,WiElement id,uint32_t p,const WiPropertyValue* v,int32_t){auto& h=*(Host*)c;if(h.failSet>0&&!--h.failSet)return (int)WI_LIMIT;if(!h.nodes.count(id))return (int)WI_NOT_FOUND;h.nodes[id].p[p]=*v;return 0;};
                s.remove=[](void* c,WiElement id){((Host*)c)->nodes.erase(id);return 0;};
                s.commit=[](void* c){++((Host*)c)->commits;return 0;};
                s.reset=[](void* c){((Host*)c)->nodes.clear();return 0;};
                *(WiSceneApi*)out=s; return 0;
            }
            if(!strcmp(name,"winisland.settings")) {
                WiSettingsApi s{};s.size=sizeof(s);s.version=1;s.context=c;
                s.define=[](void* c,const WiSettingDefinition* d,uint64_t* out){auto& h=*(Host*)c;h.settings.try_emplace(d->key,d->defaultValue);*out=h.next++;h.resources[*out]={};return 0;};
                s.read=[](void* c,const char* key,char* out,uint32_t bytes){auto& m=((Host*)c)->settings;if(!m.count(key))return (int)WI_NOT_FOUND;strncpy_s(out,bytes,m[key].c_str(),_TRUNCATE);return 0;};
                s.write=[](void* c,const char* key,const char* value){((Host*)c)->settings[key]=value;return 0;};
                *(WiSettingsApi*)out=s;return 0;
            }
            return WI_UNSUPPORTED;
        };
    }
    void tick() {auto copy=resources;for(auto& [id,r]:copy)if(r.kind==WI_TIMER&&r.callback)r.callback(r.context,"");}
    void poll() {Sleep(1050);tick();}
    Node& named(const char* key){for(auto& [id,n]:nodes)if(n.key==key)return n;throw "missing node";}
};
static void check(bool yes,const char* message){printf("%s %s\n",yes?"PASS":"FAIL",message);if(!yes)ExitProcess(2);}
static double number(Node& n,uint32_t p,int i=0,double fallback=0){return n.p.count(p)?n.p[p].number[i]:fallback;}
struct Box {double x,y,w,h;bool visible;};
static Box box(Host& h,WiElement id) {
    if(id==1)return {0,0,h.snapshot.width,h.snapshot.height,true};
    auto& n=h.nodes.at(id);auto b=box(h,n.parent);double w=number(n,WI_RECT,2),ht=number(n,WI_RECT,3);
    return {b.x+number(n,WI_RECT)+(b.w-w)*number(n,WI_ANCHOR),b.y+number(n,WI_RECT,1)+(b.h-ht)*number(n,WI_ANCHOR,1),w,ht,b.visible&&number(n,WI_VISIBLE,0,1)!=0};
}
static void centered(Host& h) {
    ComPtr<IDWriteFactory> factory;DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),(IUnknown**)factory.GetAddressOf());
    double lo=1e9,hi=-1e9,top=1e9,bottom=-1e9;
    for(auto& [id,n]:h.nodes)if(n.type==WI_TEXT){auto b=box(h,id);if(!b.visible)continue;auto text=n.p[WI_TEXT_VALUE].text;std::wstring t(text,text+strlen(text));
        ComPtr<IDWriteTextFormat> f;auto hr=factory->CreateTextFormat(L"Microsoft YaHei UI",nullptr,(DWRITE_FONT_WEIGHT)(int)number(n,WI_FONT_WEIGHT,0,400),DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,(float)number(n,WI_FONT_SIZE,0,14),L"zh-CN",&f);if(FAILED(hr))check(false,"DirectWrite format");
        f->SetTextAlignment((DWRITE_TEXT_ALIGNMENT)(int)number(n,WI_ALIGN));f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        ComPtr<IDWriteTextLayout> l;factory->CreateTextLayout(t.c_str(),(UINT32)t.size(),f.Get(),(float)b.w,(float)b.h,&l);DWRITE_TEXT_METRICS m{};l->GetMetrics(&m);
        lo=std::min(lo,b.x+m.left);hi=std::max(hi,b.x+m.left+m.widthIncludingTrailingWhitespace);top=std::min(top,b.y+m.top);bottom=std::max(bottom,b.y+m.top+m.height);
    }
    printf("Text center=(%.3f,%.3f); island center=(%.3f,%.3f)\n",(lo+hi)/2,(top+bottom)/2,h.snapshot.width/2,h.snapshot.height/2);
    check(std::abs((lo+hi)/2-h.snapshot.width/2)<0.15&&std::abs((top+bottom)/2-h.snapshot.height/2)<0.15,"actual DirectWrite text bounds centered");
    check(lo>=0&&hi<=h.snapshot.width&&top>=0&&bottom<=h.snapshot.height,"text fits island");
}
static bool color(Node& n,double r,double g,double b){auto v=n.p.at(WI_TEXT_COLOR);return std::abs(v.number[0]-r)<.0001&&std::abs(v.number[1]-g)<.0001&&std::abs(v.number[2]-b)<.0001;}
int wmain(int argc,wchar_t** argv) {
    setvbuf(stdout,nullptr,_IONBF,0);
    check(argc>=2,"DLL path supplied");HMODULE dll=LoadLibraryW(argv[1]);check(dll!=nullptr,"x64 DLL loaded");
    auto create=(WinIslandCreateMod)GetProcAddress(dll,"WinIsland_CreateMod");auto abi=(WinIslandModAbi)GetProcAddress(dll,"WinIsland_ModAbi");check(create&&abi&&abi()==WINISLAND_MOD_ABI,"ABI exports");
    Host h;auto p=create(&h.api);check(p&&p->onLoad(p->context,"")==0&&p->onEnable(p->context,"")==0,"DLL enabled");centered(h);
    check(h.settings.size()==10&&h.nodes.size()==10&&h.resources.size()==12,"five fields, ten settings, one timer and one event");
    for(auto& [key,v]:h.settings)check(key.find("second")==key.npos,"no seconds setting");
    for(auto dpi:{96u,120u,144u,192u})for(float width:{160.f,257.f,420.f}){h.snapshot.dpi=dpi;h.snapshot.width=width;h.snapshot.height=48;h.tick();centered(h);}
    // Parent anchor must track actual dimensions even without another plugin callback.
    h.snapshot.width=300;h.snapshot.height=52;centered(h);
    auto& group=h.named("time-display.group");
    for(uint32_t flags:{0u,32u,64u,96u}){h.snapshot.flags=flags;h.tick();check(number(group,WI_VISIBLE)==1,"settings and expansion remain visible");}
    for(uint32_t flags:{2u,4u,8u,16u,20u}){h.snapshot.flags=flags;h.tick();check(number(group,WI_VISIBLE)==0,"music or notice hides clock");}
    h.snapshot.flags=WI_IDLE;h.tick();
    h.settings["time-color-all"]="#1234Ab";h.poll();
    for(auto& [id,n]:h.nodes)if(n.type==WI_TEXT)check(color(n,18/255.,52/255.,171/255.),"custom uniform color applied live");
    int committed=h.commits;for(int i=0;i<20;++i)h.tick();check(h.commits==committed,"unchanged static display does not commit");
    h.settings["time-color-mode"]="分别设置";
    const char* keys[]={"year","month","day","hour","minute"};const char* values[]={"#FF0000","#00FF00","#0000FF","#FFFF00","#00FFFF"};
    for(int i=0;i<5;++i)h.settings[std::string("time-color-")+keys[i]]=values[i];h.poll();
    check(color(h.named("time-display.part-0"),1,0,0)&&color(h.named("time-display.part-2"),0,1,0)&&color(h.named("time-display.part-4"),0,0,1)&&color(h.named("time-display.part-6"),1,1,0)&&color(h.named("time-display.part-8"),0,1,1),"five independent field colors");
    centered(h);
    h.settings["time-color-year"]="oops";h.poll();check(color(h.named("time-display.part-0"),1,0,0),"invalid color keeps last valid color");h.settings["time-color-year"]="#FF0000";
    h.settings["time-color-mode"]="自定义循环";h.settings["time-cycle-colors"]="#FF0000,#0000FF";h.settings["time-cycle-period"]="2";h.poll();
    auto c=h.named("time-display.part-0").p[WI_TEXT_COLOR];Sleep(180);h.tick();auto d=h.named("time-display.part-0").p[WI_TEXT_COLOR];
    check(c.number[1]==0&&d.number[1]==0&&std::abs(c.number[0]-d.number[0])>.01,"custom palette interpolates only selected colors");
    h.settings["time-color-mode"]="彩虹循环";h.poll();c=h.named("time-display.part-0").p[WI_TEXT_COLOR];Sleep(180);h.tick();d=h.named("time-display.part-0").p[WI_TEXT_COLOR];
    check(memcmp(c.number,d.number,sizeof(c.number))!=0,"rainbow advances");
    h.snapshot.flags=WI_NOTICE;h.tick();committed=h.commits;Sleep(180);h.tick();check(h.commits==committed,"hidden cycle does not publish colors");
    h.snapshot.flags=WI_IDLE|WI_REDUCED_MOTION;h.tick();committed=h.commits;Sleep(180);h.tick();check(h.commits==committed,"reduced motion stops color animation");
    h.settings["time-display-enabled"]="false";h.poll();check(number(group,WI_VISIBLE)==0,"legacy false setting hides immediately after polling");
    p->onDisable(p->context,"");check(h.nodes.empty()&&h.resources.empty(),"disable revokes every owned node and resource");
    check(p->onEnable(p->context,"")==0&&number(h.named("time-display.group"),WI_VISIBLE)==0,"re-enable preserves settings");
    p->onDisable(p->context,"");h.failSet=15;check(p->onEnable(p->context,"")!=0&&h.nodes.empty()&&h.resources.empty(),"partial initialization failure rolls back");
    h.failSet=0;h.settings["time-display-enabled"]="1";h.snapshot.flags=WI_IDLE;
    check(p->onEnable(p->context,"")==0,"enable recovers after failed initialization");
    for(int i=0;i<3;++i){p->onDisable(p->context,"");check(p->onEnable(p->context,"")==0&&h.nodes.size()==10&&h.resources.size()==12,"repeat enable without duplicates");}
    p->onDisable(p->context,"");p->onUnload(p->context,"");p->destroy(p->context);FreeLibrary(dll);
    return 0;
}
