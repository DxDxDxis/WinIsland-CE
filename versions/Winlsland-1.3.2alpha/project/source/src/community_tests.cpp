#include "mod_system.h"
#include "community_runtime.h"
namespace wi {
namespace {
struct TestService {uint32_t value=42;std::atomic<bool> entered=false,leave=false;bool block=false;};
int testService(void* context,uint32_t method,const void*,uint32_t,void* out,uint32_t cap,uint32_t* n){auto& s=*(TestService*)context;if(method==99)throw std::runtime_error("test failure");if(method!=1)return WI_EXT_UNSUPPORTED;s.entered=true;while(s.block&&!s.leave)Sleep(2);if(cap<4)return WI_EXT_LIMIT;memcpy(out,&s.value,4);*n=4;return 0;}
struct JobTest {WiCommunityApi api;WiServiceRef ref;std::atomic<bool> returned=false;bool callback=false;};
int testJob(void* p,const WiJobContext*){auto& j=*(JobTest*)p;uint32_t n=0,value=0;int rc=j.api.invoke(j.api.context,j.ref,1,nullptr,0,&value,4,&n);j.returned=true;return rc;}
void testDone(void* p,WiJob,int,const void*,uint32_t){((JobTest*)p)->callback=true;}
int testEvent(void* p,const WiCommunityEvent* e){if(!strcmp(e->topic,"service.changed"))((std::vector<uint32_t>*)p)->push_back(((const WiServiceInfo*)e->data)->state);return 0;}
struct Life {bool stopped=false,joined=false;};
void stopLife(void* p){((Life*)p)->stopped=true;}int joinedLife(void* p){return ((Life*)p)->joined;}
}
int communitySystemTest(const fs::path& examples,const fs::path& output){
 fs::create_directories(output);setDataRoot(fs::absolute(output)/L"isolated-runtime");std::ostringstream report;int passed=0,failed=0;auto check=[&](bool ok,const char* name){report<<(ok?"PASS ":"FAIL ")<<name<<'\n';ok?++passed:++failed;};
 auto base=fs::absolute(output)/wide("run-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
 try{
  CommunityRuntime runtime;auto p=runtime.createOwner("a-provider",base,base),p2=runtime.createOwner("b-provider",base,base),c=runtime.createOwner("consumer",base,base),c2=runtime.createOwner("consumer2",base,base);
  auto pa=runtime.api(*p),pb=runtime.api(*p2),ca=runtime.api(*c),cb=runtime.api(*c2);TestService service,second;second.value=84;
  WiServiceTable table{sizeof(table),1,&service,testService},table2{sizeof(table2),1,&second,testService};WiServiceDefinition d{sizeof(d),1,"org.test.service",1,0,WI_SERVICE_CONCURRENT,&table};WiService sid=0,sid2=0;std::vector<uint32_t> notifications;WiSubscription sub=0;
  check(ca.subscribe(ca.context,"service.changed",testEvent,&notifications,&sub)==0,"service state subscription");
  check(pa.registerService(pa.context,&d,&sid)==0,"service registration binds provider owner");WiServiceRef r=0,r2=0;WiServiceInfo info{sizeof(info),1};WiServiceQuery q{sizeof(q),1,"org.test.service",nullptr,1,2,0};
  check(ca.acquire(ca.context,&q,&r,&info)==WI_EXT_NOT_READY,"registered is not ready; consumers cannot acquire");
  check(pa.setServiceState(pa.context,sid,WI_SERVICE_READY)==0,"explicit ready transition");d.table=&table2;d.interfaceVersion=2;check(pb.registerService(pb.context,&d,&sid2)==0&&pb.setServiceState(pb.context,sid2,WI_SERVICE_READY)==0,"second provider and interface version");
  check(ca.acquire(ca.context,&q,&r,&info)==0&&!strcmp(info.provider,"b-provider")&&info.interfaceVersion==2,"deterministic highest compatible interface version");
  uint32_t value=0,n=0;check(ca.invoke(ca.context,r,1,nullptr,0,&value,4,&n)==0&&value==84&&n==4,"broker executes actual provider table");
  check(ca.invoke(ca.context,r,777,nullptr,0,&value,4,&n)==WI_EXT_UNSUPPORTED,"unknown method is diagnostic");check(cb.invoke(cb.context,r,1,nullptr,0,&value,4,&n)==WI_EXT_NOT_FOUND,"reference cannot be used by another owner");
  q.provider="a-provider";check(cb.acquire(cb.context,&q,&r2,&info)==0&&!strcmp(info.provider,"a-provider"),"explicit provider pin");q.minVersion=q.maxVersion=3;WiServiceRef bad=0;check(ca.acquire(ca.context,&q,&bad,&info)==WI_EXT_NOT_READY,"version mismatch cannot acquire");
  check(ca.release(ca.context,r)==0&&ca.invoke(ca.context,r,1,nullptr,0,&value,4,&n)==WI_EXT_NOT_FOUND,"released handle becomes stale");
  check(cb.invoke(cb.context,r2,1,nullptr,0,&value,4,&n)==0&&value==42,"closing one reference preserves unrelated consumer");
  runtime.pump();check(notifications.size()>=4&&notifications[0]==WI_SERVICE_REGISTERED&&notifications[1]==WI_SERVICE_READY,"appeared and ready events preserve sequence");
  WiServiceInfo list[4]{};uint32_t count=0;check(ca.enumerate(ca.context,list,4,&count)==0&&count==2,"enumeration exposes provider version state thread and capabilities");
  check(ca.publish(ca.context,"other.fake",nullptr,0)==WI_EXT_INVALID,"event source namespace is owner bound");char path[1024];check(ca.path(ca.context,WI_PATH_DATA,"../escape",path,sizeof(path),&n)==WI_EXT_INVALID,"path traversal rejected");
  q.provider="b-provider";q.minVersion=q.maxVersion=2;check(ca.acquire(ca.context,&q,&r,&info)==0,"acquire fault fixture");check(ca.invoke(ca.context,r,99,nullptr,0,&value,4,&n)==WI_EXT_FAULT&&!runtime.fault(*p2).empty(),"provider exception marks service failed and diagnoses owner");ca.release(ca.context,r);
  int wrongThread=0;std::thread foreign([&]{WiService ignored=0;wrongThread=pa.registerService(pa.context,&d,&ignored);});foreign.join();check(wrongThread==WI_EXT_THREAD,"unregistered foreign thread rejected");
  service.entered=false;service.block=true;JobTest job{cb,r2};WiJobDefinition jd{sizeof(jd),1,&job,testJob,testDone};WiJob jid=0;check(cb.startJob(cb.context,&jd,&jid)==0,"managed background job starts");double deadline=now()+2;while(!service.entered&&now()<deadline)Sleep(2); // reset below ensures genuinely in-flight
  Sleep(30);runtime.beginStop(*p);check(!runtime.drained(*p),"provider remains pinned while reference/call active");runtime.beginStop(*c2);check(!runtime.drained(*c2),"cancel request is not thread termination");service.leave=true;
  deadline=now()+3;while((!runtime.drained(*c2)||!runtime.drained(*p))&&now()<deadline){runtime.pump();Sleep(2);}check(job.returned&&!job.callback&&runtime.drained(*c2)&&runtime.drained(*p),"in-flight call finishes; cancelled owner receives no late completion");runtime.finishStop(*c2);runtime.finishStop(*p);
  check(cb.invoke(cb.context,r2,1,nullptr,0,&value,4,&n)==WI_EXT_STOPPED,"old owner cannot call after stop");
  auto reload=runtime.createOwner("a-provider",base,base);auto ra=runtime.api(*reload);d.table=&table;d.interfaceVersion=1;WiService newId=0;check(ra.registerService(ra.context,&d,&newId)==0&&newId!=sid,"reload never reuses old service generation handle");ra.setServiceState(ra.context,newId,WI_SERVICE_READY);
  q.provider="a-provider";q.minVersion=q.maxVersion=1;check(ca.acquire(ca.context,&q,&r,&info)==0,"new instance acquired explicitly");check(ra.unregisterService(ra.context,newId)==WI_EXT_BUSY,"unregister cannot unload a referenced service");check(ca.invoke(ca.context,r,1,nullptr,0,&value,4,&n)==WI_EXT_STOPPED,"stopping service rejects new calls");ca.release(ca.context,r);check(ra.unregisterService(ra.context,newId)==0,"unregister completes after release");
  Life life;WiExternalLifetime lifetime{sizeof(lifetime),1,&life,stopLife,joinedLife};check(ra.manageLifetime(ra.context,&lifetime)==0,"third-party lifetime registered");runtime.beginStop(*reload);check(life.stopped&&!runtime.drained(*reload),"external cancellation alone cannot unload DLL");life.joined=true;check(runtime.drained(*reload),"external joined acknowledgment permits cleanup");runtime.finishStop(*reload);
  runtime.beginStop(*c);runtime.beginStop(*p2);runtime.pump();runtime.finishStop(*c);runtime.finishStop(*p2);
 }catch(const std::exception& e){check(false,e.what());}
 try{
  auto root=base/L"packages",data=base/L"data";
  {
   auto badRoot=base/L"failure-packages",badData=base/L"failure-data";fs::create_directories(badRoot);
   for(auto name:{"community-timeout","community-cycle-a","community-cycle-b"}){fs::copy_file(examples.parent_path()/L"test-packages"/(wide(name)+L".wimod"),badRoot/(wide(name)+L".wimod"));writeAtomic(badData/L"mods"/wide(name)/L"host-state.txt","disabled");}
   ModLoader failedHost(badRoot,false,badData);auto runBad=[&](const char* op,const char* id=""){failedHost.request(op,id);double end=now()+9;while(failedHost.busy()&&now()<end)Sleep(10);return failedHost.snapshot();};runBad("scan");auto bad=runBad("enable","community-cycle-a");check(!bad.operationError.empty()&&std::none_of(bad.mods.begin(),bad.mods.end(),[](auto& m){return m.loaded;}),"service dependency cycle rejected before executing either DLL");bad=runBad("enable","community-timeout");check(!bad.operationError.empty()&&std::none_of(bad.mods.begin(),bad.mods.end(),[](auto& m){return m.loaded;}),"advertised service initialization timeout drains registered service and DLL");
  }
  fs::create_directories(root);
  auto add=[&](const char* id){fs::copy_file(examples/(wide(id)+L".wimod"),root/(wide(id)+L".wimod"));writeAtomic(data/L"mods"/wide(id)/L"host-state.txt","disabled");};
  for(auto id:{"community-consumer","community-optional","community-task","community-qoi-view"})add(id);
  auto info=[](const ModSnapshot& s,const std::string& id){for(auto& m:s.mods)if(m.id==id)return m;return ModInfo{};};
  {
   ModLoader host(root,false,data);ModSnapshot snap;
   auto wait=[&](auto predicate,int seconds=8){double end=now()+seconds;while(now()<end){snap=host.snapshot();if(predicate(snap))return true;Sleep(10);}return false;};
   auto run=[&](const char* op,const char* id="",bool cascade=false){check(host.request(op,id,cascade),"real loader accepts operation");check(wait([](auto& s){return !s.busy;}),"real loader operation returns");};
   run("scan");check(std::all_of(snap.mods.begin(),snap.mods.end(),[](auto& m){return !m.loaded;}),"scanning metadata does not execute DLLs");
   run("enable","community-consumer");check(!info(snap,"community-consumer").loaded&&!info(snap,"community-consumer").error.empty(),"required service missing blocks consumer before DLL execution");
   run("enable","community-optional");check(info(snap,"community-optional").enabled&&!snap.scene.created.empty(),"optional service missing uses visible degradation");run("disable","community-optional");
   add("community-base-v2");run("scan");run("enable","community-consumer");check(!info(snap,"community-consumer").enabled,"installed incompatible provider version does not satisfy dependency");
   add("community-base");run("scan");run("enable","community-consumer");check(info(snap,"community-base").enabled&&info(snap,"community-consumer").enabled,"required provider starts before consumer");
   check(std::any_of(snap.scene.created.begin(),snap.scene.created.end(),[&](auto& node){return snap.scene.resolve(node).text(WI_TEXT_VALUE).find("42")!=std::string::npos;}),"actual service result reaches retained host display plan");
   run("enable","community-optional");run("disable","community-base");check(info(snap,"community-base").enabled&&!snap.operationError.empty(),"provider disable blocked while real consumers exist");
   run("disable","community-consumer");check(info(snap,"community-base").enabled&&info(snap,"community-optional").enabled,"one consumer stop preserves provider and other consumer");
   run("disable","community-base",true);check(wait([&](auto& s){return !info(s,"community-base").loaded&&!info(s,"community-optional").loaded;}),"cascade stops consumers before unloading provider");
   run("enable","community-qoi-view");check(wait([](auto& s){return !s.media.empty()&&s.media[0].info.state==WI_MEDIA_ERROR;}),"QOI has no host-native decoder before provider enabled");
   add("community-qoi-decoder");run("scan");run("enable","community-qoi-decoder");run("reload","community-qoi-view");
   check(wait([](auto& s){for(auto& n:s.scene.created)if(n.mediaFrame&&n.mediaFrame->width==8&&n.mediaFrame->height==8)return true;return false;}),"QOI plugin feeds existing media load/bind and actual host display frame");
   bool pixels=false;for(auto& n:snap.scene.created)if(n.mediaFrame){auto& b=n.mediaFrame->bgra;pixels=b.size()==256&&b[0]==96&&b[1]==192&&b[2]==32&&b[3]==255;writeAtomic(base/L"qoi-decoded.bgra",std::string((char*)b.data(),b.size()));}check(pixels,"third-party QOI output has expected copied PBGRA pixels");
   run("disable","community-qoi-decoder");check(info(snap,"community-qoi-view").enabled,"decoded immutable image remains valid after decoder unload");run("reload","community-qoi-view");check(wait([](auto& s){return !s.media.empty()&&s.media[0].info.state==WI_MEDIA_ERROR;}),"new decode after provider disable fails explicitly");run("disable","community-qoi-view");
   run("enable","community-task");check(wait([&](auto&){return fs::exists(data/L"mods/community-task/task-started.txt");}),"real DLL background task entered");run("disable","community-task");check(wait([&](auto& s){return !info(s,"community-task").loaded;}),"disable during job waits then unloads DLL");check(fs::exists(data/L"mods/community-task/task-cancelled.txt"),"real task observed cancellation and exited");check(readFile(data/L"mods/community-task/mod.log").find("completion on loader")==std::string::npos,"no post-disable task completion enters unloaded DLL");
   for(int i=0;i<3;++i){run("enable","community-consumer");run("disable","community-base",true);}check(wait([&](auto& s){return !info(s,"community-base").loaded;}),"repeated provider consumer lifecycle drains resources");
   add("community-restart");run("scan");run("enable","community-restart");run("disable","community-restart");check(info(snap,"community-restart").pendingRestart&&info(snap,"community-restart").loaded&&info(snap,"community-restart").enabled,"restart-required disable truthfully retains running state");
   run("enable","community-base");check(readFile(data/L"mods/community-base/host-state.txt")=="enabled","enable selection persisted immediately");
  }
  check(readFile(data/L"mods/community-base/host-state.txt")=="enabled","normal process cleanup preserves enabled preference");
  {ModLoader restored(root,true,data);double end=now()+8;while(restored.busy()&&now()<end)Sleep(10);auto s=restored.snapshot();check(info(s,"community-base").enabled&&!info(s,"community-task").enabled&&!info(s,"community-restart").enabled,"restart restores enabled/disabled and restart-required choices");}
 }catch(const std::exception& e){check(false,e.what());}
 report<<"Passed="<<passed<<" Failed="<<failed<<'\n';writeAtomic(output/L"community-tests.txt",report.str());return failed?1:0;
}
}
