// Real public SDK consumer: PNG, GIF composition, MP4/H.264, controls, pointer capture.
#include "../src/mod_api.h"
#include <cstring>
#include <cstdio>
#include <cstddef>
#include <algorithm>
#include <initializer_list>
#include <thread>
static const WinIslandHostApi* host;static WiSceneApi scene;static WiMediaApi media;
static WiMedia png,gif,video;static WiElement statusText,progress,dragImage;
static bool paused=false,looped=true,dragging=false;static double dragX=0,dragY=0;static char previousStatus[240]{};
static WiPropertyValue value(double a,double b=0,double c=0,double d=0){WiPropertyValue v{};v.size=sizeof(v);v.version=1;v.number[0]=a;v.number[1]=b;v.number[2]=c;v.number[3]=d;return v;}
static int set(WiElement id,uint32_t p,WiPropertyValue v){return scene.set(scene.context,id,p,&v,10);}
static void text(WiElement id,const char* s){auto v=value(0);strncpy_s(v.text,s,_TRUNCATE);set(id,WI_TEXT_VALUE,v);}
static WiElement node(uint32_t type,const char* key,WiElement parent,double x,double y,double w,double h){WiElement id=0;if(scene.create(scene.context,type,key,parent,&id))return 0;set(id,WI_RECT,value(x,y,w,h));set(id,WI_VISIBLE,value(1));set(id,WI_TEXT_COLOR,value(.88,.94,1,1));set(id,WI_FONT_SIZE,value(14));return id;}
static int tick(void*,const char*){WiMediaInfo i{sizeof(i),WI_MEDIA_ABI};if(media.info(media.context,video,&i))return -1;
    char message[240];sprintf_s(message,"H.264  %.2f / %.2f s  frame %llu  state %u",i.position,i.duration,(unsigned long long)i.frameSequence,i.state);if(!strcmp(message,previousStatus))return 0;strcpy_s(previousStatus,message);text(statusText,message);set(progress,WI_VALUE,value(i.duration?i.position/i.duration:0));return scene.commit(scene.context);}
static int onMedia(void*,const char*){for(auto id:{gif,video}){WiMediaInfo i{sizeof(i),WI_MEDIA_ABI};if(media.info(media.context,id,&i))return -1;if(i.state==WI_MEDIA_READY){if(media.setLoop(media.context,id,looped)||media.play(media.context,id))return -2;}}return 0;}
static int clicked(void* context,const WiInputEvent* e){if(e->kind!=WI_POINTER_CLICK)return 0;int command=(int)(uintptr_t)context;int rc=0;
    if(command==1){paused=!paused;for(auto id:{gif,video})rc|=paused?media.pause(media.context,id):media.play(media.context,id);}
    if(command==2)rc=media.seek(media.context,video,1.5);
    if(command==3){for(auto id:{gif,video})rc|=media.stop(media.context,id);paused=true;}
    if(command==4){looped=!looped;for(auto id:{gif,video})rc|=media.setLoop(media.context,id,looped);}
    if(command==5){rc=media.replaceSource(media.context,gif,"assets/motion.gif");paused=false;}
    if(command==6){WiMedia chosen=0;rc=media.chooseLocalFile(media.context,1920,&chosen);if(!rc){media.release(media.context,png);png=chosen;rc=media.bind(media.context,png,dragImage);scene.commit(scene.context);}}
    host->log(host->context,rc?"media control FAILED":"media control accepted");return rc?-1:1;}
static int slider(void*,const WiInputEvent* e){if(e->kind==WI_DRAG_START||e->kind==WI_DRAG_UPDATE||e->kind==WI_POINTER_CLICK){WiMediaInfo i{sizeof(i),WI_MEDIA_ABI};media.info(media.context,video,&i);media.seek(media.context,video,std::clamp(e->x/600.,0.,1.)*i.duration);return 1;}
    if(e->kind==WI_KEY_DOWN&&(e->key==37||e->key==39)){WiMediaInfo i{sizeof(i),WI_MEDIA_ABI};media.info(media.context,video,&i);media.seek(media.context,video,std::clamp(i.position+(e->key==37?-.1:.1),0.,i.duration));return 1;}return 0;}
static int drag(void*,const WiInputEvent* e){if(e->kind==WI_DRAG_START){dragging=true;dragX=e->x;dragY=e->y;}
    if(e->kind==WI_DRAG_END||e->kind==WI_DRAG_CANCEL){dragging=false;host->log(host->context,"drag finished/cancelled");}
    if(e->kind==WI_DRAG_UPDATE&&dragging){WiPropertyValue r{sizeof(r),1};if(scene.read(scene.context,dragImage,WI_RECT,&r))return -1;r.number[0]+=e->x-dragX;r.number[1]+=e->y-dragY;set(dragImage,WI_RECT,r);scene.commit(scene.context);}return 1;}
static int load(void*,const char*){
    if(!host||host->size<offsetof(WinIslandHostApi,queryInterface)+sizeof(host->queryInterface))return -1;
    if(host->queryInterface(host->context,WI_SCENE_INTERFACE,1,&scene,sizeof(scene))||host->queryInterface(host->context,WI_MEDIA_INTERFACE,1,&media,sizeof(media)))return -2;
    // Exercise optional-prefix compatibility and explicit failure contracts through the real DLL boundary.
    WiMediaApi prefix{};if(host->queryInterface(host->context,WI_MEDIA_INTERFACE,1,&prefix,(uint32_t)offsetof(WiMediaApi,replaceSource))||!prefix.release)return -20;
    if(!host->queryInterface(host->context,"winisland.media.missing",1,&prefix,sizeof(prefix)))return -21;
    int threadResult=0;std::thread wrongThread([&]{WiMediaCapabilities c{sizeof(c),1};threadResult=media.capabilities(media.context,&c);});wrongThread.join();if(threadResult!=WI_MEDIA_WRONG_THREAD)return -22;
    host->log(host->context,"ABI checks passed: old media table prefix, missing extension, wrong thread");
    WiElement root=0;if(scene.find(scene.context,"island",&root))return -3;
    set(root,WI_SIZE_MODE,value(WI_PLUGIN_MANAGED));set(root,WI_RECT,value(0,0,640,352));set(root,WI_RADIUS,value(24));set(root,WI_BACKGROUND,value(.03,.06,.1,1));
    auto title=node(WI_TEXT,"media.title",root,20,10,600,28);text(title,"WinIsland r1 · PNG / GIF / H.264");set(title,WI_FONT_SIZE,value(21));
    dragImage=node(WI_IMAGE,"media.drag",root,20,50,140,53);scene.listen(scene.context,dragImage,drag,nullptr);set(dragImage,WI_RECEIVE_INPUT,value(1));set(dragImage,WI_BLOCK_INPUT,value(1));
    auto gifNode=node(WI_IMAGE,"media.gif",root,20,108,240,120);auto videoNode=node(WI_IMAGE,"media.video",root,280,50,340,192);
    if(media.load(media.context,"assets/skin.png",&png)||media.load(media.context,"assets/motion.gif",&gif)||media.load(media.context,"assets/motion.mp4",&video))return -4;
    alignas(WiMediaInfo) unsigned char smaller[offsetof(WiMediaInfo,originalWidth)+8];memset(smaller,0xA5,sizeof(smaller));auto oldInfo=(WiMediaInfo*)smaller;oldInfo->size=(uint32_t)offsetof(WiMediaInfo,originalWidth);oldInfo->version=1;
    if(media.info(media.context,png,oldInfo))return -23;for(size_t i=offsetof(WiMediaInfo,originalWidth);i<sizeof(smaller);++i)if(smaller[i]!=0xA5)return -24;
    host->log(host->context,"ABI checks passed: old media info prefix keeps caller guard bytes");
    if(media.bind(media.context,png,dragImage)||media.bind(media.context,gif,gifNode)||media.bind(media.context,video,videoNode))return -5;
    const char* labels[]={"Play / Pause","Seek 1.5s","Stop","Loop","Reload GIF","Local file"};
    for(int i=0;i<6;++i){char key[64];sprintf_s(key,"media.button.%d",i);auto b=node(WI_SCENE_BUTTON,key,root,20+i*101,288,95,36);text(b,labels[i]);set(b,WI_FONT_SIZE,value(12));set(b,WI_RADIUS,value(10));set(b,WI_BACKGROUND,value(.1,.2,.3,1));set(b,WI_ALIGN,value(2));set(b,WI_RECEIVE_INPUT,value(1));set(b,WI_BLOCK_INPUT,value(1));scene.listen(scene.context,b,clicked,(void*)(uintptr_t)(i+1));}
    progress=node(WI_SLIDER,"media.progress",root,20,248,600,20);set(progress,WI_RECEIVE_INPUT,value(1));set(progress,WI_BLOCK_INPUT,value(1));scene.listen(scene.context,progress,slider,nullptr);
    statusText=node(WI_TEXT,"media.status",root,20,326,600,20);text(statusText,"Loading media on host decoder worker...");set(statusText,WI_FONT_SIZE,value(12));
    WinIslandResource event{sizeof(event),1,WI_EVENT,"media.changed","","",onMedia,nullptr,0};if(!host->add(host->context,&event))return -6;
    WinIslandResource timer{sizeof(timer),1,WI_TIMER,"media-status","","",tick,nullptr,100};if(!host->add(host->context,&timer))return -7;
    WiMedia bad=0;if(media.load(media.context,"../escape.png",&bad)!=WI_MEDIA_INVALID)return -8;
    host->log(host->context,"media fixture: async package assets, scene binding, controls and drag");return scene.commit(scene.context);}
static int ok(void*,const char*){return 0;}static void destroy(void*){}
extern "C" WINISLAND_MOD_API uint32_t WinIsland_ModAbi(){return WINISLAND_MOD_ABI;}
extern "C" WINISLAND_MOD_API IWinIslandMod* WinIsland_CreateMod(const WinIslandHostApi* h){host=h;static IWinIslandMod api{sizeof(api),WINISLAND_MOD_ABI,nullptr,load,ok,ok,ok,destroy};return &api;}
