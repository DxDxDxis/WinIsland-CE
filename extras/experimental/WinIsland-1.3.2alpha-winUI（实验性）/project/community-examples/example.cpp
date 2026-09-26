#define NOMINMAX
#include <windows.h>
#include "../sdk/mod_api.h"
#include "../sdk/community_api.h"
#include <cstring>
#include <string>
#include <fstream>
#include <filesystem>
#include <vector>
#ifndef ROLE
#define ROLE 1
#endif
#ifndef SERVICE_VERSION
#define SERVICE_VERSION 1
#endif
struct Mod {
 IWinIslandMod lifecycle{};const WinIslandHostApi* host;WiCommunityApi ext{};WiSceneApi scene{};WiMediaApi media{};
 WiService service=0;WiServiceRef ref=0;WiElement label=0;WiJob job=0;std::string data;
 static int call(void*,uint32_t method,const void*,uint32_t,void* out,uint32_t cap,uint32_t* bytes){
  if(method!=1)return WI_EXT_UNSUPPORTED;const char* result=SERVICE_VERSION==1?"社区基础服务 v1：42":"社区基础服务 v2：84";auto n=(uint32_t)strlen(result)+1;if(cap<n)return WI_EXT_LIMIT;memcpy(out,result,n);*bytes=n;return 0;
 }
 void show(const char* message){
  if(!label)scene.create(scene.context,WI_TEXT,"community.result",1,&label);
  WiPropertyValue p{sizeof(p),1};p.number[0]=10;p.number[1]=6;p.number[2]=300;p.number[3]=30;scene.set(scene.context,label,WI_RECT,&p,10);
  p={sizeof(p),1};strcpy_s(p.text,message);scene.set(scene.context,label,WI_TEXT_VALUE,&p,10);
  p={sizeof(p),1};p.number[0]=1;p.number[1]=1;p.number[2]=1;p.number[3]=1;scene.set(scene.context,label,WI_TEXT_COLOR,&p,10);
  p={sizeof(p),1};p.number[0]=16;scene.set(scene.context,label,WI_FONT_SIZE,&p,10);
  p={sizeof(p),1};p.number[0]=0;p.number[1]=0;p.number[2]=360;p.number[3]=54;scene.set(scene.context,1,WI_RECT,&p,10);p={sizeof(p),1};p.number[0]=WI_PLUGIN_MANAGED;scene.set(scene.context,1,WI_SIZE_MODE,&p,10);scene.commit(scene.context);
 }
 static int work(void* ctx,const WiJobContext* job){auto& m=*(Mod*)ctx;
  // Files opened only on this background thread; plugin folder obtained from host.
  std::ofstream(std::filesystem::u8path(m.data)/L"task-started.txt")<<"started";
  for(uint32_t i=0;i<500;++i){if(job->cancelled(job->context)){std::ofstream(std::filesystem::u8path(m.data)/L"task-cancelled.txt")<<"exited cooperatively";return WI_EXT_CANCELLED;}job->progress(job->context,i*2);Sleep(10);}
  const char result[]="后台任务完成";return job->result(job->context,result,sizeof(result));
 }
 static void done(void* ctx,WiJob,int rc,const void* data,uint32_t n){auto& m=*(Mod*)ctx;m.host->log(m.host->context,"community task completion on loader");if(!rc&&n)m.show((const char*)data);}
 static int enable(void* ctx,const char*){auto& m=*(Mod*)ctx;
  if(m.host->queryInterface(m.host->context,WI_COMMUNITY_INTERFACE,1,&m.ext,sizeof(m.ext)))return -1;
  if(m.host->queryInterface(m.host->context,WI_SCENE_INTERFACE,1,&m.scene,sizeof(m.scene)))return -2;
  if constexpr(ROLE==1){WiServiceTable table{sizeof(table),1,&m,call};WiServiceDefinition spec{sizeof(spec),1,"org.winisland.example.math",SERVICE_VERSION,0,WI_SERVICE_CONCURRENT,&table};int rc=m.ext.registerService(m.ext.context,&spec,&m.service);if(rc)return rc;return m.ext.setServiceState(m.ext.context,m.service,WI_SERVICE_READY);}
  if constexpr(ROLE==2||ROLE==5){WiServiceQuery q{sizeof(q),1,"org.winisland.example.math",nullptr,1,1,0};WiServiceInfo info{sizeof(info),1};int rc=m.ext.acquire(m.ext.context,&q,&m.ref,&info);if(rc){if constexpr(ROLE==5){m.show("可选服务未就绪：降级显示");return 0;}return rc;}char buffer[256]{};uint32_t bytes=0;rc=m.ext.invoke(m.ext.context,m.ref,1,nullptr,0,buffer,sizeof(buffer),&bytes);if(!rc)m.show(buffer);return rc;}
  if constexpr(ROLE==3){char path[32768]{};uint32_t n=0;int rc=m.ext.path(m.ext.context,WI_PATH_DATA,"",path,sizeof(path),&n);if(rc)return rc;m.data=path;m.show("可取消后台任务运行中");WiJobDefinition def{sizeof(def),1,&m,work,done};return m.ext.startJob(m.ext.context,&def,&m.job);}
  if constexpr(ROLE==4){if(m.host->queryInterface(m.host->context,WI_MEDIA_INTERFACE,1,&m.media,sizeof(m.media)))return -3;WiElement node=0;int rc=m.scene.create(m.scene.context,WI_IMAGE,"community.qoi.image",1,&node);if(rc)return rc;WiPropertyValue p{sizeof(p),1};p.number[0]=8;p.number[1]=8;p.number[2]=128;p.number[3]=64;m.scene.set(m.scene.context,node,WI_RECT,&p,10);p.number[0]=0;p.number[1]=0;p.number[2]=144;p.number[3]=80;m.scene.set(m.scene.context,1,WI_RECT,&p,10);WiMedia media=0;rc=m.media.load(m.media.context,"assets/example.qoi",&media);if(rc)return rc;m.media.bind(m.media.context,media,node);p={sizeof(p),1};p.number[0]=WI_PLUGIN_MANAGED;m.scene.set(m.scene.context,1,WI_SIZE_MODE,&p,10);return m.scene.commit(m.scene.context);}
  return 0;
 }
 static int disable(void* ctx,const char*){auto& m=*(Mod*)ctx;m.host->log(m.host->context,"onDisable: all managed work has exited");return 0;}
 static void destroy(void* ctx){delete (Mod*)ctx;}
};
extern "C" __declspec(dllexport) uint32_t WinIsland_ModAbi(){return WINISLAND_MOD_ABI;}
extern "C" __declspec(dllexport) IWinIslandMod* WinIsland_CreateMod(const WinIslandHostApi* host){auto m=new Mod;m->host=host;m->lifecycle={sizeof(IWinIslandMod),WINISLAND_MOD_ABI,m,nullptr,Mod::enable,Mod::disable,nullptr,Mod::destroy};return &m->lifecycle;}
