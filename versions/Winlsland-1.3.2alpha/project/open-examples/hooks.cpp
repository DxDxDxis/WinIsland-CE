#include "../sdk/mod_api.h"
#include "../sdk/open_api.h"
#include <cstring>
struct Hooks{
 IWinIslandMod life{};const WinIslandHostApi* host;WiOpenApi open{};
 static int hook(void*,WiHostPacket* p){if(p->topic==WI_HOST_MUSIC){strcpy_s(p->title,"社区替换：播放器信息");return WI_HOOK_REPLACE;}if(p->topic==WI_HOST_NOTICE&&strstr(p->title,"block-by-plugin"))return WI_HOOK_CONSUME;if(p->topic==WI_HOST_ACTION&&p->number[0]==1){p->number[0]=3;return WI_HOOK_REPLACE;}return WI_HOOK_PASS;}
 static int enable(void* c,const char*){auto& m=*(Hooks*)c;if(m.host->queryInterface(m.host->context,WI_OPEN_INTERFACE,1,&m.open,sizeof(m.open)))return -1;for(uint32_t topic=1;topic<=3;++topic){WiHostHookDefinition d{sizeof(d),1,topic,10,&m,hook};WiHook h=0;int rc=m.open.registerHook(m.open.context,&d,&h);if(rc)return rc;}WiHostPacket packet{sizeof(packet),1,WI_HOST_NOTICE};strcpy_s(packet.title,"社区数据来源");strcpy_s(packet.detail,"此通知由模组通过正式宿主入口发布");return m.open.submitHost(m.open.context,&packet);}
 static void destroy(void* c){delete (Hooks*)c;}
};
extern "C" __declspec(dllexport) uint32_t WinIsland_ModAbi(){return WINISLAND_MOD_ABI;}
extern "C" __declspec(dllexport) IWinIslandMod* WinIsland_CreateMod(const WinIslandHostApi* h){auto m=new Hooks;m->host=h;m->life={sizeof(IWinIslandMod),WINISLAND_MOD_ABI,m,nullptr,Hooks::enable,nullptr,nullptr,Hooks::destroy};return &m->life;}
