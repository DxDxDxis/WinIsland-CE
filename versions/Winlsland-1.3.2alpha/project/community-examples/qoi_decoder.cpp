#define NOMINMAX
#include <windows.h>
#include "../sdk/mod_api.h"
#include "../sdk/community_api.h"
#include <filesystem>
#include <fstream>
#include <vector>
#include <memory>
#define QOI_NO_STDIO
#define QOI_IMPLEMENTATION
#include "third_party/qoi.h"
struct Decoder {
 IWinIslandMod lifecycle{};const WinIslandHostApi* host;WiCommunityApi api{};WiService service=0;
 static int invoke(void*,uint32_t method,const void* input,uint32_t n,void*,uint32_t,uint32_t* written){
  *written=0;if(n!=sizeof(WiImageDecodeRequest)||!input)return WI_EXT_INVALID;auto& r=*(const WiImageDecodeRequest*)input;
  if(r.size<sizeof(r)||r.version!=1)return WI_EXT_VERSION;
  if(r.prefixBytes<14||memcmp(r.prefix,"qoif",4))return WI_EXT_UNSUPPORTED;
  if(method==WI_IMAGE_PROBE)return 0;if(method!=WI_IMAGE_DECODE)return WI_EXT_UNSUPPORTED;
  auto be=[](const uint8_t* p){return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];};uint32_t w=be(r.prefix+4),h=be(r.prefix+8);
  if(!w||!h||uint64_t(w)*h>16*1024*1024)return WI_EXT_LIMIT;
  std::ifstream in(std::filesystem::u8path(r.path),std::ios::binary|std::ios::ate);if(!in)return WI_EXT_INVALID;auto size=in.tellg();if(size<14||size>128*1024*1024)return WI_EXT_LIMIT;
  std::vector<uint8_t> encoded((size_t)size);in.seekg(0);in.read((char*)encoded.data(),size);if(!in)return WI_EXT_INVALID;
  if(r.cancelled(r.context))return WI_EXT_CANCELLED;qoi_desc desc{};std::unique_ptr<unsigned char,decltype(&free)> rgba((unsigned char*)qoi_decode(encoded.data(),(int)encoded.size(),&desc,4),free);if(!rgba)return WI_EXT_INVALID;
  // The third-party decode is bounded; cancellation is checked before/after and every output row.
  uint32_t dw=w,dh=h;if(w>r.maxEdge||h>r.maxEdge){double scale=double(r.maxEdge)/(std::max)(w,h);dw=(std::max)(1u,uint32_t(w*scale));dh=(std::max)(1u,uint32_t(h*scale));}
  std::vector<uint8_t> bgra(size_t(dw)*dh*4);
  for(uint32_t y=0;y<dh;++y){if(r.cancelled(r.context))return WI_EXT_CANCELLED;for(uint32_t x=0;x<dw;++x){auto src=rgba.get()+(size_t(y)*h/dh*w+size_t(x)*w/dw)*4;auto dst=bgra.data()+(size_t(y)*dw+x)*4;dst[0]=uint8_t(unsigned(src[2])*src[3]/255);dst[1]=uint8_t(unsigned(src[1])*src[3]/255);dst[2]=uint8_t(unsigned(src[0])*src[3]/255);dst[3]=src[3];}}
  return r.frame(r.context,dw,dh,bgra.data(),(uint32_t)bgra.size());
 }
 static int enable(void* ctx,const char*){auto& d=*(Decoder*)ctx;if(d.host->queryInterface(d.host->context,WI_COMMUNITY_INTERFACE,1,&d.api,sizeof(d.api)))return -1;WiServiceTable table{sizeof(table),1,&d,invoke};WiServiceDefinition spec{sizeof(spec),1,"org.winisland.example.qoi",1,WI_SERVICE_IMAGE_DECODER,WI_SERVICE_CONCURRENT,&table};int rc=d.api.registerService(d.api.context,&spec,&d.service);return rc?rc:d.api.setServiceState(d.api.context,d.service,WI_SERVICE_READY);}
 static void destroy(void* c){delete (Decoder*)c;}
};
extern "C" __declspec(dllexport) uint32_t WinIsland_ModAbi(){return WINISLAND_MOD_ABI;}
extern "C" __declspec(dllexport) IWinIslandMod* WinIsland_CreateMod(const WinIslandHostApi* h){auto d=new Decoder;d->host=h;d->lifecycle={sizeof(IWinIslandMod),WINISLAND_MOD_ABI,d,nullptr,Decoder::enable,nullptr,nullptr,Decoder::destroy};return &d->lifecycle;}
