// Host API integration fixture, deliberately unrelated to the user's time-display mod.
#include "../src/mod_api.h"
#include <stddef.h>
#include <string.h>
#include <stdio.h>
static const WinIslandHostApi* host;
static WiSceneApi scene;
static WiElement textId;
static int changes=0;
static WiPropertyValue number(double a,double b=0,double c=0,double d=0){WiPropertyValue v{};v.size=sizeof(v);v.version=1;v.number[0]=a;v.number[1]=b;v.number[2]=c;v.number[3]=d;return v;}
static int paint(void*,const WiDrawContext* context){
    if(context->size<sizeof(*context)||context->version!=1)return -1;
    WiDrawCommand d{};d.size=sizeof(d);d.version=1;d.kind=WI_DRAW_RECT;d.x=0;d.y=32;d.width=120;d.height=2;d.radius=1;d.color[0]=.3f;d.color[1]=.9f;d.color[2]=.5f;d.color[3]=1;
    int rc=scene.draw(scene.context,textId,&d,1);if(rc)return rc;return scene.commit(scene.context);
}
static int input(void*,const WiInputEvent* e){if(e->kind==WI_POINTER_CLICK){auto v=number(0);strcpy_s(v.text,"Scene input received");scene.set(scene.context,textId,WI_TEXT_VALUE,&v,0);scene.commit(scene.context);host->log(host->context,"scene fixture input received");}return 1;}
static int state(void*,const char*){WiSceneSnapshot s{sizeof(s),1};if(scene.snapshot(scene.context,&s)!=WI_OK)return -1;auto v=number((s.flags&(WI_MUSIC|WI_NOTICE|WI_LYRIC))?0:1);scene.set(scene.context,textId,WI_VISIBLE,&v,0);return scene.commit(scene.context);}
static int timer(void*,const char*){WiSceneSnapshot s{sizeof(s),1};scene.snapshot(scene.context,&s);++changes;if(changes==1)host->log(host->context,"scene fixture timer active");return 0;}
static int load(void*,const char*){
    if(host->size<offsetof(WinIslandHostApi,queryInterface)+sizeof(host->queryInterface))return -1;
    if(host->queryInterface(host->context,WI_SCENE_INTERFACE,1,&scene,sizeof(scene)))return -2;
    WiElement root=0;if(scene.find(scene.context,"island",&root))return -3;
    if(scene.create(scene.context,WI_TEXT,"fixture.label",root,&textId))return -4;
    auto set=[&](WiElement id,uint32_t p,WiPropertyValue v){return scene.set(scene.context,id,p,&v,0);};
    auto str=number(0);strcpy_s(str.text,"Open scene / UTF-8 文本");set(textId,WI_TEXT_VALUE,str);
    set(textId,WI_RECT,number(18,8,286,36));set(textId,WI_FONT_SIZE,number(19));set(textId,WI_TEXT_COLOR,number(.5,.85,1,1));
    set(textId,WI_RECEIVE_INPUT,number(1));set(textId,WI_BLOCK_INPUT,number(1));scene.listen(scene.context,textId,input,nullptr);
    set(root,WI_SIZE_MODE,number(WI_PLUGIN_MANAGED));set(root,WI_RECT,number(0,0,340,64));set(root,WI_RADIUS,number(18));
    WinIslandResource event{sizeof(event),1,WI_EVENT,"scene.changed","","",state,nullptr,0};host->add(host->context,&event);
    WinIslandResource t{sizeof(t),1,WI_TIMER,"fixture-timer","","",timer,nullptr,100};host->add(host->context,&t);
    if(scene.onDraw(scene.context,textId,paint,nullptr)!=WI_OK)return -5;
    WiSettingsApi settings{};if(host->queryInterface(host->context,"winisland.settings",1,&settings,sizeof(settings)))return -6;
    WiSettingDefinition definition{sizeof(definition),1,WI_SETTING_SWITCH,0,"fixture-switch","Fixture switch","1",nullptr,0,1};uint64_t setting=0;
    if(settings.define(settings.context,&definition,&setting))return -7;
    if(settings.write(settings.context,"fixture-switch","bad")!=WI_INVALID)return -8;
    if(settings.write(settings.context,"fixture-switch","0"))return -9;
    char stored[32]{};if(settings.read(settings.context,"fixture-switch",stored,sizeof(stored))||strcmp(stored,"0"))return -10;
    host->log(host->context,"scene fixture loaded via queried C ABI; typed setting validated and persisted");return scene.commit(scene.context);
}
static int ok(void*,const char*){return 0;}
static void destroy(void*){}
extern "C" WINISLAND_MOD_API uint32_t WinIsland_ModAbi(){return WINISLAND_MOD_ABI;}
extern "C" WINISLAND_MOD_API IWinIslandMod* WinIsland_CreateMod(const WinIslandHostApi* h){host=h;static IWinIslandMod api{sizeof(api),WINISLAND_MOD_ABI,nullptr,load,ok,ok,ok,destroy};return &api;}

