#include "../sdk/mod_api.h"
#include "../sdk/community_api.h"
#include <thread>
#include <atomic>
#include <chrono>
struct External {
 IWinIslandMod life{};const WinIslandHostApi* host;WiCommunityApi api{};std::thread worker;std::atomic<bool> stopping=false,exited=false;
 static void stop(void* c){((External*)c)->stopping=true;}
 static int joined(void* c){auto& s=*(External*)c;if(!s.exited)return 0;if(s.worker.joinable())s.worker.join();return 1;}
 static int enable(void* c,const char*){auto& s=*(External*)c;if(s.host->queryInterface(s.host->context,WI_COMMUNITY_INTERFACE,1,&s.api,sizeof(s.api)))return -1;WiExternalLifetime l{sizeof(l),1,&s,stop,joined};int rc=s.api.manageLifetime(s.api.context,&l);if(rc)return rc;s.worker=std::thread([&s]{while(!s.stopping)std::this_thread::sleep_for(std::chrono::milliseconds(10));std::this_thread::sleep_for(std::chrono::milliseconds(6200));s.exited=true;});return 0;}
 static void destroy(void* c){auto s=(External*)c;if(s->worker.joinable())s->worker.join();delete s;}
};
extern "C" __declspec(dllexport) uint32_t WinIsland_ModAbi(){return WINISLAND_MOD_ABI;}
extern "C" __declspec(dllexport) IWinIslandMod* WinIsland_CreateMod(const WinIslandHostApi* h){auto p=new External;p->host=h;p->life={sizeof(IWinIslandMod),WINISLAND_MOD_ABI,p,nullptr,External::enable,nullptr,nullptr,External::destroy};return &p->life;}
