#include "../sdk/mod_api.h"
#include "../sdk/open_api.h"
#include <string>
struct Worker {
 IWinIslandMod life{};const WinIslandHostApi* host;WiOpenApi open{};WiProcess process=0;
 static int poll(void* c,const char*){auto& w=*(Worker*)c;char data[4096]{};uint32_t n=0;int rc=w.open.receiveProcess(w.open.context,w.process,data,sizeof(data)-1,&n);if(!rc){data[n]=0;w.host->log(w.host->context,data);}return 0;}
 static int crash(void* c,const char*){auto& w=*(Worker*)c;return w.open.sendProcess(w.open.context,w.process,"crash",5);}
 static int enable(void* c,const char*){auto& w=*(Worker*)c;if(w.host->queryInterface(w.host->context,WI_OPEN_INTERFACE,1,&w.open,sizeof(w.open)))return -1;WiProcessDefinition d{sizeof(d),1,"bin/helper.exe","",1000};int rc=w.open.startProcess(w.open.context,&d,&w.process);if(rc)return rc;w.open.sendProcess(w.open.context,w.process,"hello from managed helper",25);WinIslandResource timer{sizeof(timer),1,WI_TIMER,"helper-poll","","",poll,&w,100};if(!w.host->add(w.host->context,&timer))return -2;WinIslandResource button{sizeof(button),1,WI_BUTTON,"helper-crash","测试辅助进程异常退出（不影响宿主）","",crash,&w};return w.host->add(w.host->context,&button)?0:-3;}
 static void destroy(void* c){delete (Worker*)c;}
};
extern "C" __declspec(dllexport) uint32_t WinIsland_ModAbi(){return WINISLAND_MOD_ABI;}
extern "C" __declspec(dllexport) IWinIslandMod* WinIsland_CreateMod(const WinIslandHostApi* h){auto p=new Worker;p->host=h;p->life={sizeof(IWinIslandMod),WINISLAND_MOD_ABI,p,nullptr,Worker::enable,nullptr,nullptr,Worker::destroy};return &p->life;}

