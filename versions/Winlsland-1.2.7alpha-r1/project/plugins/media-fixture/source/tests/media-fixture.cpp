#include "../src/mod_api.h"
#include <cstring>
static const WinIslandHostApi* host; static WiSceneApi scene; static WiMediaApi media; static WiElement image; static WiMedia asset;
static WiPropertyValue v(double a,double b=0,double c=0,double d=0){WiPropertyValue x{};x.size=sizeof(x);x.version=1;x.number[0]=a;x.number[1]=b;x.number[2]=c;x.number[3]=d;return x;}
static int load(void*,const char*){
 if(!host||host->queryInterface(host->context,WI_SCENE_INTERFACE,1,&scene,sizeof(scene)))return -1;
 if(!host->queryInterface(host->context,WI_MEDIA_INTERFACE,WI_MEDIA_ABI,&media,sizeof(media)))return -2;
 WiElement root=0;if(scene.find(scene.context,"island",&root))return -3;
 if(scene.create(scene.context,WI_IMAGE,"media.fixture.image",root,&image))return -4;
 WiMediaCapabilities caps{}; caps.size=sizeof(caps); caps.version=WI_MEDIA_ABI;if(media.capabilities(media.context,&caps))return -5;
 if(media.load(media.context,"skin.bmp",&asset))return -6;
 WiMediaInfo info{sizeof(info),WI_MEDIA_ABI};if(media.info(media.context,asset,&info))return -7;
 auto rect=v(12,6,(double)info.width,(double)info.height); if(scene.set(scene.context,image,WI_RECT,&rect,0))return -8;
 auto visible=v(1); if(scene.set(scene.context,image,WI_VISIBLE,&visible,0))return -9;
 if(media.bind(media.context,asset,image))return -10;
 host->log(host->context,"media fixture decoded package-local skin.bmp and bound image element");
 return scene.commit(scene.context);
}
static int ok(void*,const char*){return 0;} static void destroy(void*){}
extern "C" WINISLAND_MOD_API uint32_t WinIsland_ModAbi(){return WINISLAND_MOD_ABI;}
extern "C" WINISLAND_MOD_API IWinIslandMod* WinIsland_CreateMod(const WinIslandHostApi* h){host=h;static IWinIslandMod api{sizeof(api),WINISLAND_MOD_ABI,nullptr,load,ok,ok,ok,destroy};return &api;}

