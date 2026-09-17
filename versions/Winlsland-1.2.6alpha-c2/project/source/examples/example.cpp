#include "../src/mod_api.h"
#include <windows.h>
#include <cstring>
// Built with its own static CRT. Allocation and destruction stay inside this DLL.
struct Example {
    WinIslandHostApi host;
    IWinIslandMod api{};
    uint64_t layer=0;
    static int loaded(void* ctx,const char*) {auto& s=*(Example*)ctx;s.host.log(s.host.context,"示例 onLoad");return 0;}
    static int setting(void* ctx,const char*) {auto& s=*(Example*)ctx;s.host.log(s.host.context,"示例设置已保存");return 0;}
    static int click(void* ctx,const char*) {
        auto& s=*(Example*)ctx;
        s.host.log(s.host.context,"示例按钮已执行");
        s.host.remove(s.host.context,s.layer);
        s.layer=s.add(WI_LAYER,"greeting","按钮已响应","来自实际 DLL 回调");return 0;
    }
    static int timer(void*,const char*) {return 0;}
    static int event(void* ctx,const char* value) {
        auto& s=*(Example*)ctx;
        s.host.log(s.host.context,value?value:"事件已收到");
        return 0;
    }
    uint64_t add(uint32_t kind,const char* key,const char* label,const char* value,WinIslandCallback callback=nullptr,uint32_t interval=0){
        WinIslandResource r{sizeof(r),1,kind,key,label,value,callback,this,interval};return host.add(host.context,&r);
    }
    static int enable(void* ctx,const char*) {
        auto& s=*(Example*)ctx;
        if(!s.add(WI_SETTING,"greeting-text","问候文字","你好，WinIsland",setting))return 1;
        if(!s.add(WI_BUTTON,"hello","示例按钮","",click))return 1;
        char value[512]{};s.host.getSetting(s.host.context,"greeting-text",value,sizeof(value));
        s.layer=s.add(WI_LAYER,"greeting","示例插件",value);
#ifdef SECOND_EXAMPLE
        s.add(WI_REPLACE,"animation-duration","动画时长倍率","1.2");
        s.add(WI_REPLACE,"island-tint","背景颜色","1579032");
#else
        s.add(WI_REPLACE,"animation-duration","动画时长倍率","0.8");
#endif
        s.add(WI_TIMER,"heartbeat","托管定时器","",timer,1000);
        s.add(WI_EVENT,"music.changed","音乐事件观察","",event);
        s.add(WI_EVENT,"notice.received","通知事件观察","",event);
        s.add(WI_HOOK,"lyrics.request","歌词请求观察","",event);
        if(s.host.registerAnimation){
            static const char animation[]="function winislandAnimation(state,from,to,duration){var o={};for(var k in from)o[k]=from[k];var t=gsap.to(o,{x:to.x,duration:duration,paused:true,ease:'power2.out'});return {object:o,tween:t};}";
            WinIslandAnimationDefinition def{sizeof(def),1,"example-category",animation,.24,0};
            s.host.registerAnimation(s.host.context,&def);
        }
        return s.layer?0:1;
    }
    static int disable(void* ctx,const char*) {auto& s=*(Example*)ctx;s.host.log(s.host.context,"示例 onDisable");return 0;}
    static int unload(void* ctx,const char*) {auto& s=*(Example*)ctx;s.host.log(s.host.context,"示例 onUnload");return 0;}
    static void destroy(void* ctx){delete (Example*)ctx;}
};
extern "C" WINISLAND_MOD_API uint32_t WinIsland_ModAbi(){return WINISLAND_MOD_ABI;}
extern "C" WINISLAND_MOD_API IWinIslandMod* WinIsland_CreateMod(const WinIslandHostApi* host){
    if(!host||host->version!=WINISLAND_MOD_ABI||host->size<sizeof(*host))return nullptr;
    auto p=new Example;p->host=*host;p->api={sizeof(IWinIslandMod),WINISLAND_MOD_ABI,p,Example::loaded,Example::enable,Example::disable,Example::unload,Example::destroy};return &p->api;
}
