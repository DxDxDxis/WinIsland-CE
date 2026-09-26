#define NOMINMAX
#include <windows.h>
#include "../sdk/mod_api.h"
#include "../sdk/open_api.h"
#include <fstream>
#include <filesystem>
#include <map>
#include <mutex>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstring>
struct Mod{
 IWinIslandMod life{};const WinIslandHostApi* host;WiCommunityApi community{};WiMediaApi media{};WiSceneApi scene{};WiService service=0;WiMedia playing=0;std::mutex mutex;uint64_t next=1;std::map<uint64_t,uint32_t> streams;
 static int control(void* c,const char* v){auto& m=*(Mod*)c;if(!strcmp(v,"pause"))return m.media.pause(m.media.context,m.playing);if(!strcmp(v,"play"))return m.media.play(m.media.context,m.playing);if(!strcmp(v,"seek"))return m.media.seek(m.media.context,m.playing,1.);if(!strcmp(v,"rate"))return m.media.setRate(m.media.context,m.playing,1.5);if(!strcmp(v,"mute"))return m.media.setMuted(m.media.context,m.playing,1);if(!strcmp(v,"unmute"))return m.media.setMuted(m.media.context,m.playing,0);return WI_EXT_INVALID;}
 static int invoke(void* ctx,uint32_t method,const void* input,uint32_t n,void* out,uint32_t cap,uint32_t* written){auto& m=*(Mod*)ctx;*written=0;if(n!=sizeof(WiStreamRequest))return WI_EXT_INVALID;auto& r=*(const WiStreamRequest*)input;if(r.size<sizeof(r)||r.version!=1)return WI_EXT_VERSION;
  if(method==WI_STREAM_PROBE){std::ifstream in(std::filesystem::u8path(r.path),std::ios::binary);char magic[4]{};in.read(magic,4);return !memcmp(magic,"WIAV",4)?0:WI_EXT_UNSUPPORTED;}
  std::lock_guard lock(m.mutex);
  if(method==WI_STREAM_OPEN){if(cap<sizeof(WiStreamInfo))return WI_EXT_LIMIT;WiStreamInfo info{sizeof(info),1,64,32,48000,2,2.,30.,m.next++,1,1};strcpy_s(info.codec,"Community WIAV frame+PCM fixture v1");m.streams[info.session]=0;memcpy(out,&info,sizeof(info));*written=sizeof(info);return 0;}
  auto it=m.streams.find(r.session);if(it==m.streams.end())return WI_EXT_NOT_FOUND;
  if(method==WI_STREAM_CLOSE){m.streams.erase(it);return 0;}
  if(r.cancelled(r.context))return WI_EXT_CANCELLED;
  if(method==WI_STREAM_SEEK){it->second=(uint32_t)std::clamp(r.position*30.,0.,60.);return 0;}
  if(method!=WI_STREAM_READ)return WI_EXT_UNSUPPORTED;auto index=it->second++;if(index>=60)return 1;
  std::vector<uint8_t> pixels(64*32*4);for(size_t p=0;p<pixels.size();p+=4){pixels[p]=uint8_t(index*4);pixels[p+1]=180;pixels[p+2]=50;pixels[p+3]=255;}
  int rc=r.video(r.context,index/30.,64,32,pixels.data(),(uint32_t)pixels.size());if(rc)return rc;
  std::vector<int16_t> pcm(1600*2);for(int i=0;i<1600;++i){auto value=(int16_t)(std::sin((index*1600+i)*6.28318530718*440/48000)*2000);pcm[i*2]=pcm[i*2+1]=value;}return r.audio(r.context,index/30.,pcm.data(),1600);
 }
 static int enable(void* ctx,const char*){auto& m=*(Mod*)ctx;
#ifdef STREAM_VIEW
  if(m.host->queryInterface(m.host->context,WI_MEDIA_INTERFACE,1,&m.media,sizeof(m.media))||m.host->queryInterface(m.host->context,WI_SCENE_INTERFACE,1,&m.scene,sizeof(m.scene)))return -1;WiElement node=0;m.scene.create(m.scene.context,WI_IMAGE,"open.stream",1,&node);WiPropertyValue p{sizeof(p),1};p.number[0]=8;p.number[1]=8;p.number[2]=256;p.number[3]=128;m.scene.set(m.scene.context,node,WI_RECT,&p,20);p.number[0]=0;p.number[1]=0;p.number[2]=272;p.number[3]=144;m.scene.set(m.scene.context,1,WI_RECT,&p,20);p={sizeof(p),1};p.number[0]=WI_PLUGIN_MANAGED;m.scene.set(m.scene.context,1,WI_SIZE_MODE,&p,20);WiMedia media=0;int rc=m.media.load(m.media.context,"assets/clip.wiav",&media);if(rc)return rc;m.playing=media;WinIslandResource control{sizeof(control),1,WI_BUTTON,"stream-control","播放控制（SDK 示例）","",Mod::control,&m};if(!m.host->add(m.host->context,&control))return -3;m.media.bind(m.media.context,media,node);m.scene.commit(m.scene.context);m.media.setMuted(m.media.context,media,0);return m.media.play(m.media.context,media);
#else
  if(m.host->queryInterface(m.host->context,WI_COMMUNITY_INTERFACE,1,&m.community,sizeof(m.community)))return -1;WiServiceTable table{sizeof(table),1,&m,invoke};WiServiceDefinition def{sizeof(def),1,"org.winisland.example.wiav",1,WI_SERVICE_STREAM_DECODER,WI_SERVICE_CONCURRENT,&table};int rc=m.community.registerService(m.community.context,&def,&m.service);return rc?rc:m.community.setServiceState(m.community.context,m.service,WI_SERVICE_READY);
#endif
 }
 static void destroy(void* c){delete (Mod*)c;}
};
extern "C" __declspec(dllexport) uint32_t WinIsland_ModAbi(){return WINISLAND_MOD_ABI;}
extern "C" __declspec(dllexport) IWinIslandMod* WinIsland_CreateMod(const WinIslandHostApi* h){auto m=new Mod;m->host=h;m->life={sizeof(IWinIslandMod),WINISLAND_MOD_ABI,m,nullptr,Mod::enable,nullptr,nullptr,Mod::destroy};return &m->life;}



