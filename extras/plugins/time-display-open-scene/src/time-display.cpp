#define NOMINMAX
#include "../sdk/mod_api.h"
#include <windows.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
using Microsoft::WRL::ComPtr;
namespace {
constexpr const wchar_t* font=L"Microsoft YaHei UI";
constexpr const char* modes="统一颜色\n分别设置\n自定义循环\n彩虹循环";
constexpr const char* fieldKeys[]={"time-color-year","time-color-month","time-color-day","time-color-hour","time-color-minute"};
constexpr const char* fieldLabels[]={"年颜色（#RRGGBB）","月颜色（#RRGGBB）","日颜色（#RRGGBB）","时颜色（#RRGGBB）","分颜色（#RRGGBB）"};
struct Color {double r=184/255.0,g=194/255.0,b=209/255.0;};
std::string trim(const std::string& s){auto a=s.find_first_not_of(" \r\n\t");return a==s.npos?"":s.substr(a,s.find_last_not_of(" \r\n\t")-a+1);}
bool parseColor(const std::string& input,Color& out){
    auto s=trim(input);if(s.size()!=7||s[0]!='#')return false;
    unsigned n=0;for(size_t i=1;i<s.size();++i){char c=s[i];int x=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;if(x<0)return false;n=n*16+x;}
    out={double((n>>16)&255)/255,double((n>>8)&255)/255,double(n&255)/255};return true;
}
bool parsePalette(const std::string& s,std::vector<Color>& out){
    std::vector<Color> next;size_t start=0;
    do{auto end=s.find(',',start);Color c;if(!parseColor(s.substr(start,end==s.npos?end:end-start),c)||next.size()>=16)return false;next.push_back(c);if(end==s.npos)break;start=end+1;}while(true);
    if(next.size()<2)return false;out=std::move(next);return true;
}
Color mix(Color a,Color b,double t){return {a.r+(b.r-a.r)*t,a.g+(b.g-a.g)*t,a.b+(b.b-a.b)*t};}
Color rainbow(double phase){constexpr Color stops[]={{1,0,0},{1,1,0},{0,1,0},{0,1,1},{0,0,1},{1,0,1},{1,0,0}};double h=(phase-std::floor(phase))*6;int i=(int)h;return mix(stops[i],stops[i+1],h-i);}
void require(int r){if(r!=WI_OK)throw std::runtime_error("宿主接口失败，返回码 "+std::to_string(r));}
void requireHR(HRESULT hr){if(FAILED(hr))throw std::runtime_error("DirectWrite 文字测量失败");}
WiPropertyValue value(double a=0,double b=0,double c=0,double d=0){WiPropertyValue v{};v.size=sizeof(v);v.version=1;v.number[0]=a;v.number[1]=b;v.number[2]=c;v.number[3]=d;return v;}
WiPropertyValue textValue(const std::string& s){auto v=value();strcpy_s(v.text,s.c_str());return v;}
}
struct ClockMod {
    WinIslandHostApi host{};IWinIslandMod api{};WiSceneApi scene{};WiSettingsApi settings{};
    ComPtr<IDWriteFactory> write;
    WiElement group=0;std::array<WiElement,9> parts{};std::vector<uint64_t> resources;
    std::map<std::pair<WiElement,uint32_t>,WiPropertyValue> cache;
    bool enabled=false,show=true,applying=false,dirty=false;
    int mode=0;double period=6;Color unified;std::array<Color,5> colors{};
    std::vector<Color> palette{{1,0.3,0.3},{0.3,1,0.5},{0.3,0.6,1}};
    ULONGLONG lastSettings=0,started=0;
    std::string lastLayout,lastError,lastConfigError;float lastWidth=0,lastHeight=0;
    void log(const std::string& s){if(host.log)host.log(host.context,s.c_str());}
    void set(WiElement id,uint32_t property,const WiPropertyValue& v){
        auto key=std::make_pair(id,property);auto it=cache.find(key);
        if(it!=cache.end()&&!memcmp(&it->second,&v,sizeof(v)))return;
        require(scene.set(scene.context,id,property,&v,0));cache[key]=v;dirty=true;
    }
    void define(uint32_t type,const char* key,const char* label,const char* def,const char* choices=nullptr,double low=0,double high=1){
        WiSettingDefinition d{sizeof(d),1,type,0,key,label,def,choices,low,high};uint64_t id=0;require(settings.define(settings.context,&d,&id));if(!id)throw std::runtime_error("设置注册失败");resources.push_back(id);
    }
    std::string read(const char* key){char b[4097]{};require(settings.read(settings.context,key,b,sizeof(b)));return trim(b);}
    void readSettings(){
        std::string errors;auto v=read("time-display-enabled");
        if(v=="1"||!_stricmp(v.c_str(),"true"))show=true;
        else if(v=="0"||!_stricmp(v.c_str(),"false"))show=false;
        else{show=false;errors+="显示时间值无效；";}
        auto m=read("time-color-mode");const char* names[]={"统一颜色","分别设置","自定义循环","彩虹循环"};
        bool known=false;for(int i=0;i<4;++i)if(m==names[i]){mode=i;known=true;break;}if(!known)errors+="颜色模式无效；";
        if(!parseColor(read("time-color-all"),unified))errors+="统一颜色无效；";
        for(size_t i=0;i<colors.size();++i)if(!parseColor(read(fieldKeys[i]),colors[i]))errors+=std::string(fieldLabels[i])+"无效；";
        if(!parsePalette(read("time-cycle-colors"),palette))errors+="循环颜色需为 2–16 个 #RRGGBB，用英文逗号分隔；";
        auto seconds=read("time-cycle-period");char* end=nullptr;double n=strtod(seconds.c_str(),&end);
        if(end!=seconds.c_str()&&!*end&&std::isfinite(n)&&n>=1&&n<=120)period=n;else errors+="循环时长需为 1–120 秒；";
        if(errors!=lastConfigError){if(!errors.empty())log(errors+"颜色参数保留上一次有效值。");lastConfigError=errors;}
    }
    void layout(const SYSTEMTIME& t,float width,float height){
        char stamp[32];sprintf_s(stamp,"%u.%u.%u %02u:%02u",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute);
        if(lastLayout==stamp&&width==lastWidth&&height==lastHeight)return;
        std::array<std::string,9> strings={std::to_string(t.wYear),".",std::to_string(t.wMonth),".",std::to_string(t.wDay)," ","",":",""};
        char b[8];sprintf_s(b,"%02u",t.wHour);strings[6]=b;sprintf_s(b,"%02u",t.wMinute);strings[8]=b;
        std::array<float,9> widths{};float total=0,line=0,fontSize=15;
        auto measure=[&](){
            ComPtr<IDWriteTextFormat> f;requireHR(write->CreateTextFormat(font,nullptr,DWRITE_FONT_WEIGHT_BOLD,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,fontSize,L"zh-CN",&f));
            requireHR(f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP));total=line=0;
            for(size_t i=0;i<strings.size();++i){std::wstring w(strings[i].begin(),strings[i].end());ComPtr<IDWriteTextLayout> l;requireHR(write->CreateTextLayout(w.c_str(),(UINT32)w.size(),f.Get(),4096,256,&l));DWRITE_TEXT_METRICS m{};requireHR(l->GetMetrics(&m));widths[i]=m.widthIncludingTrailingWhitespace;total+=widths[i];line=std::max(line,m.height);}
        };
        measure();float fit=std::min({1.f,std::max(1.f,width-16)/std::max(1.f,total),std::max(1.f,height-4)/std::max(1.f,line)});
        if(fit<1){fontSize*=fit;measure();}
        // Parent anchor follows actual rendered island dimensions between snapshots.
        set(group,WI_RECT,value(0,0,total,line+2));set(group,WI_ANCHOR,value(.5,.5));
        float x=0;for(size_t i=0;i<parts.size();++i){
            // CENTER is 2; 1 is TRAILING. Guard against wrap due to float rounding.
            set(parts[i],WI_ALIGN,value(DWRITE_TEXT_ALIGNMENT_CENTER));
            set(parts[i],WI_RECT,value(x-1,0,widths[i]+2,line+2));set(parts[i],WI_FONT_SIZE,value(fontSize));
            set(parts[i],WI_TEXT_VALUE,textValue(strings[i]));x+=widths[i];
        }
        lastLayout=stamp;lastWidth=width;lastHeight=height;
    }
    void apply(){
        if(!enabled||applying)return;applying=true;
        try{
            ULONGLONG now=GetTickCount64();if(!lastSettings||now-lastSettings>=1000){readSettings();lastSettings=now;}
            WiSceneSnapshot s{sizeof(s),1};require(scene.snapshot(scene.context,&s));
            bool visible=show&&std::isfinite(s.width)&&std::isfinite(s.height)&&s.width>0&&s.height>0&&!(s.flags&(WI_MUSIC|WI_PLAYING|WI_LYRIC|WI_NOTICE));
            set(group,WI_VISIBLE,value(visible?1:0));
            if(visible){
                SYSTEMTIME t{};GetLocalTime(&t);layout(t,s.width,s.height);
                double phase=(s.flags&WI_REDUCED_MOTION)?0:double(now-started)/(period*1000);Color common=unified;
                if(mode==2){double p=(phase-std::floor(phase))*palette.size();size_t i=(size_t)p;common=mix(palette[i],palette[(i+1)%palette.size()],p-i);}
                if(mode==3)common=rainbow(phase);
                constexpr int fields[]={0,0,1,1,2,2,3,3,4};
                for(size_t i=0;i<parts.size();++i){auto c=mode==1?colors[fields[i]]:common;set(parts[i],WI_TEXT_COLOR,value(c.r,c.g,c.b,1));}
            }
            if(dirty){require(scene.commit(scene.context));dirty=false;}lastError.clear();
        }catch(const std::exception& e){cache.clear();lastLayout.clear();if(lastError!=e.what()){lastError=e.what();log(lastError);}}
        applying=false;
    }
    int load(){
        if(!host.queryInterface)return 1;
        require(host.queryInterface(host.context,WI_SCENE_INTERFACE,WI_SCENE_ABI,&scene,sizeof(scene)));
        require(host.queryInterface(host.context,"winisland.settings",1,&settings,sizeof(settings)));
        if(scene.size<sizeof(scene)||scene.version!=1||!scene.snapshot||!scene.find||!scene.create||!scene.set||!scene.remove||!scene.commit||settings.size<sizeof(settings)||settings.version!=1||!settings.define||!settings.read)return 1;
        requireHR(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),(IUnknown**)write.GetAddressOf()));return 0;
    }
    int enable(){
        if(enabled)return 0;
        define(WI_SETTING_SWITCH,"time-display-enabled","显示时间","1");
        define(WI_SETTING_CHOICE,"time-color-mode","颜色模式","统一颜色",modes);
        define(WI_SETTING_TEXT,"time-color-all","统一颜色（#RRGGBB）","#B8C2D1");
        for(size_t i=0;i<colors.size();++i)define(WI_SETTING_TEXT,fieldKeys[i],fieldLabels[i],"#B8C2D1");
        define(WI_SETTING_TEXT,"time-cycle-colors","循环颜色（英文逗号分隔）","#FF4D4D,#4DFF80,#4D99FF");
        define(WI_SETTING_NUMBER,"time-cycle-period","循环一轮时长（秒）","6",nullptr,1,120);
        readSettings();lastSettings=started=GetTickCount64();
        WiElement root=0;require(scene.find(scene.context,"island",&root));require(scene.create(scene.context,WI_CONTAINER,"time-display.group",root,&group));
        set(group,WI_VISIBLE,value(0));set(group,WI_Z_ORDER,value(100));set(group,WI_OPACITY,value(.9));set(group,WI_RECEIVE_INPUT,value(0));set(group,WI_BLOCK_INPUT,value(0));
        for(size_t i=0;i<parts.size();++i){auto key="time-display.part-"+std::to_string(i);require(scene.create(scene.context,WI_TEXT,key.c_str(),group,&parts[i]));set(parts[i],WI_FONT_WEIGHT,value(700));set(parts[i],WI_FONT_FAMILY,textValue("Microsoft YaHei UI"));set(parts[i],WI_RECEIVE_INPUT,value(0));set(parts[i],WI_BLOCK_INPUT,value(0));}
        WinIslandResource ev{sizeof(ev),1,WI_EVENT,"scene.changed","场景状态","",updateCb,this,0};auto event=host.add(host.context,&ev);if(!event)throw std::runtime_error("场景事件注册失败");resources.push_back(event);
        WinIslandResource tm{sizeof(tm),1,WI_TIMER,"clock","时间与颜色更新","",updateCb,this,100};auto timer=host.add(host.context,&tm);if(!timer)throw std::runtime_error("定时器注册失败");resources.push_back(timer);
        enabled=true;apply();if(!lastError.empty())throw std::runtime_error(lastError);log("时间显示 1.1.0 已启用：居中、分段配色与颜色循环");return 0;
    }
    int disable(){
        enabled=false;for(auto h:resources)host.remove(host.context,h);resources.clear();
        bool hadNodes=group!=0;for(auto& p:parts){if(p&&scene.remove)scene.remove(scene.context,p);p=0;}if(group&&scene.remove)scene.remove(scene.context,group);group=0;
        if(hadNodes&&scene.commit)scene.commit(scene.context);cache.clear();lastLayout.clear();lastSettings=0;dirty=false;return 0;
    }
    static int loadCb(void* c,const char*){auto& s=*(ClockMod*)c;try{return s.load();}catch(const std::exception& e){s.log(e.what());return 1;}}
    static int enableCb(void* c,const char*){auto& s=*(ClockMod*)c;try{return s.enable();}catch(const std::exception& e){s.log(e.what());s.disable();return 1;}}
    static int updateCb(void* c,const char*){((ClockMod*)c)->apply();return 0;}
    static int disableCb(void* c,const char*){return ((ClockMod*)c)->disable();}
    static void destroy(void* c){delete (ClockMod*)c;}
};
extern "C" WINISLAND_MOD_API uint32_t WinIsland_ModAbi(){return WINISLAND_MOD_ABI;}
extern "C" WINISLAND_MOD_API IWinIslandMod* WinIsland_CreateMod(const WinIslandHostApi* h){
    if(!h||h->version!=WINISLAND_MOD_ABI||h->size<sizeof(*h)||!h->add||!h->remove)return nullptr;
    try{auto p=new ClockMod;p->host=*h;p->api={sizeof(IWinIslandMod),WINISLAND_MOD_ABI,p,ClockMod::loadCb,ClockMod::enableCb,ClockMod::disableCb,ClockMod::disableCb,ClockMod::destroy};return &p->api;}catch(...){return nullptr;}
}
