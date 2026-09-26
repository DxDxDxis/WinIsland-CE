#include "community_runtime.h"
#include <cstring>
namespace wi {
namespace {
thread_local void* callbackOwner=nullptr;
thread_local unsigned callDepth=0;
struct Scope {void* old; Scope(void* owner):old(callbackOwner){callbackOwner=owner;++callDepth;} ~Scope(){callbackOwner=old;--callDepth;}};
int cppInvoke(WiServiceInvoke f,void* c,uint32_t m,const void* in,uint32_t n,void* out,uint32_t cap,uint32_t* written){try{return f(c,m,in,n,out,cap,written);}catch(...){return WI_EXT_FAULT;}}
int safeInvoke(WiServiceInvoke f,void* c,uint32_t m,const void* in,uint32_t n,void* out,uint32_t cap,uint32_t* written){__try{return cppInvoke(f,c,m,in,n,out,cap,written);}__except(EXCEPTION_EXECUTE_HANDLER){return WI_EXT_FAULT;}}
int cppWork(WiJobWork f,void* c,const WiJobContext* j){try{return f(c,j);}catch(...){return WI_EXT_FAULT;}}
int safeWork(WiJobWork f,void* c,const WiJobContext* j){__try{return cppWork(f,c,j);}__except(EXCEPTION_EXECUTE_HANDLER){return WI_EXT_FAULT;}}
int cppDone(WiJobDone f,void* c,WiJob id,int rc,const void* p,uint32_t n){try{if(f)f(c,id,rc,p,n);return 0;}catch(...){return WI_EXT_FAULT;}}
int safeDone(WiJobDone f,void* c,WiJob id,int rc,const void* p,uint32_t n){__try{return cppDone(f,c,id,rc,p,n);}__except(EXCEPTION_EXECUTE_HANDLER){return WI_EXT_FAULT;}}
int cppEvent(WiCommunityListener f,void* c,const WiCommunityEvent* e){try{return f(c,e);}catch(...){return WI_EXT_FAULT;}}
int safeEvent(WiCommunityListener f,void* c,const WiCommunityEvent* e){__try{return cppEvent(f,c,e);}__except(EXCEPTION_EXECUTE_HANDLER){return WI_EXT_FAULT;}}
int cppLife(const WiExternalLifetime* l,bool stop){try{if(stop){l->stop(l->context);return 0;}return l->joined(l->context);}catch(...){return WI_EXT_FAULT;}}
int safeLife(const WiExternalLifetime* l,bool stop){__try{return cppLife(l,stop);}__except(EXCEPTION_EXECUTE_HANDLER){return WI_EXT_FAULT;}}
template<auto F> struct Boundary;
template<class R,class... Args,R(*F)(Args...)> struct Boundary<F>{static R call(Args... args) noexcept {try{return F(args...);}catch(...){return WI_EXT_FAULT;}}};
bool textOk(const char* p,size_t max){return p&&memchr(p,0,max);}
bool nameOk(const char* p){return textOk(p,160)&&strlen(p)>2&&strchr(p,'.')&&std::all_of(p,p+strlen(p),[](unsigned char c){return c<128&&(isalnum(c)||c=='.'||c=='_'||c=='-');});}
}
struct CommunityRuntime::Owner {
 Impl* host; std::string id,error; fs::path data,package; DWORD loader;
 std::atomic<bool> accepting=true; bool stopped=false;
 std::vector<WiExternalLifetime> external;size_t hostLifetimes=0;
};
struct CommunityRuntime::Service {WiServiceInfo info{};WiServiceTable table{};std::shared_ptr<Owner> lifetime;Owner* owner;uint32_t active=0;};
struct CommunityRuntime::Task {
 WiJob id;Owner* owner;WiJobDefinition definition{};std::thread thread;
 std::atomic<bool> cancel=false,done=false;std::atomic<uint32_t> progress=0;
 int rc=0;std::vector<uint8_t> result;
 ~Task(){if(thread.joinable())thread.join();}
};
struct CommunityRuntime::Listener {WiSubscription id;uint64_t afterSequence;Owner* owner;std::string topic;WiCommunityListener callback;void* context;};
struct CommunityRuntime::Event {uint64_t sequence;std::string source,topic;std::vector<uint8_t> data;double expires;};
struct CommunityRuntime::Impl {
 std::mutex mutex;uint64_t next=1,sequence=0;
 std::map<uint64_t,std::shared_ptr<Service>> services;
 struct Ref {Owner* owner;std::shared_ptr<Service> service;};std::map<uint64_t,Ref> refs;
 std::map<uint64_t,std::shared_ptr<Task>> tasks;
 std::vector<Listener> listeners;std::deque<Event> events;
 std::vector<std::shared_ptr<Owner>> owners;
 static bool loader(Owner& o){return GetCurrentThreadId()==o.loader;}
 static bool allowed(Owner& o){return loader(o)||callbackOwner==&o;}
 static int check(Owner& o,bool workerOnly=true){if(workerOnly?!loader(o):!allowed(o))return WI_EXT_THREAD;return o.accepting?0:WI_EXT_STOPPED;}
 void event(const std::string& source,const std::string& topic,const void* p,uint32_t n,bool service=false){
  if(events.size()>=4096)events.pop_front();Event e{++sequence,source,topic,{},service?0:now()+2};
  if(n)e.data.assign((const uint8_t*)p,(const uint8_t*)p+n);events.push_back(std::move(e));
 }
 void changed(Service& s,uint32_t state){s.info.state=state;event(s.owner->id,"service.changed",&s.info,sizeof(s.info),true);}
 static bool matches(const Service& s,const WiServiceQuery& q){return s.info.state==WI_SERVICE_READY&&s.owner->accepting&&q.id==std::string(s.info.id)&&(!q.provider||!*q.provider||q.provider==std::string(s.info.provider))&&s.info.interfaceVersion>=q.minVersion&&s.info.interfaceVersion<=q.maxVersion&&(s.info.capabilities&q.requiredCapabilities)==q.requiredCapabilities;}
 static bool before(const std::shared_ptr<Service>& a,const std::shared_ptr<Service>& b){if(a->info.interfaceVersion!=b->info.interfaceVersion)return a->info.interfaceVersion>b->info.interfaceVersion;int cmp=strcmp(a->info.provider,b->info.provider);return cmp?cmp<0:strcmp(a->info.id,b->info.id)<0;}
 static int registration(void* ctx,const WiServiceDefinition* d,WiService* out){
  auto& o=*(Owner*)ctx;int rc=check(o);if(rc)return rc;
  if(!d||d->size<sizeof(*d)||d->version!=1||!d->table||d->table->size<sizeof(WiServiceTable)||d->table->version!=1)return WI_EXT_VERSION;
  if(!out||!nameOk(d->id)||!d->interfaceVersion||!d->table->invoke||(d->thread!=1&&d->thread!=2))return WI_EXT_INVALID;
  if((d->capabilities&(WI_SERVICE_IMAGE_DECODER|WI_SERVICE_STREAM_DECODER))&&(d->thread!=WI_SERVICE_CONCURRENT||d->interfaceVersion!=1))return WI_EXT_UNSUPPORTED;
  *out=0;auto& h=*o.host;std::lock_guard lock(h.mutex);if(h.services.size()>=512)return WI_EXT_LIMIT;
  for(auto& [id,s]:h.services)if(s->owner==&o&&!strcmp(s->info.id,d->id)&&s->info.interfaceVersion==d->interfaceVersion)return WI_EXT_BUSY;
  auto s=std::make_shared<Service>();s->owner=&o;s->lifetime=*std::find_if(h.owners.begin(),h.owners.end(),[&](auto& p){return p.get()==&o;});s->table=*d->table;s->info={sizeof(WiServiceInfo),1,h.next++};
  strcpy_s(s->info.id,d->id);strcpy_s(s->info.provider,o.id.c_str());s->info.interfaceVersion=d->interfaceVersion;s->info.interfaceSize=sizeof(WiServiceTable);s->info.capabilities=d->capabilities;s->info.thread=d->thread;
  h.services[s->info.handle]=s;*out=s->info.handle;h.changed(*s,WI_SERVICE_REGISTERED);return 0;
 }
 static int state(void* ctx,WiService id,uint32_t state){auto& o=*(Owner*)ctx;int rc=check(o);if(rc)return rc;
  auto& h=*o.host;std::lock_guard lock(h.mutex);auto it=h.services.find(id);if(it==h.services.end()||it->second->owner!=&o)return WI_EXT_NOT_FOUND;
  auto& s=*it->second;if(state!=WI_SERVICE_READY&&state!=WI_SERVICE_STOPPING&&state!=WI_SERVICE_FAILED)return WI_EXT_INVALID;
  if(s.info.state>=WI_SERVICE_STOPPING)return WI_EXT_STOPPED;h.changed(s,state);if(state==WI_SERVICE_FAILED)o.error="社区服务主动报告失败："+std::string(s.info.id);return 0;
 }
 static int unregister(void* ctx,WiService id){auto& o=*(Owner*)ctx;if(!loader(o))return WI_EXT_THREAD;auto& h=*o.host;std::lock_guard lock(h.mutex);
  auto it=h.services.find(id);if(it==h.services.end()||it->second->owner!=&o)return WI_EXT_NOT_FOUND;
  auto s=it->second;if(s->info.state<WI_SERVICE_STOPPING)h.changed(*s,WI_SERVICE_STOPPING);
  if(s->active||std::any_of(h.refs.begin(),h.refs.end(),[&](auto& r){return r.second.service==s;}))return WI_EXT_BUSY;
  h.changed(*s,WI_SERVICE_REMOVED);h.services.erase(it);return 0;
 }
 static int enumerate(void* ctx,WiServiceInfo* out,uint32_t cap,uint32_t* count){auto& o=*(Owner*)ctx;int rc=check(o);if(rc)return rc;if(!count||cap>512||(cap&&!out))return WI_EXT_INVALID;
  auto& h=*o.host;std::lock_guard lock(h.mutex);std::vector<std::shared_ptr<Service>> list;for(auto& [id,s]:h.services)list.push_back(s);std::sort(list.begin(),list.end(),before);*count=(uint32_t)list.size();for(uint32_t i=0;i<std::min(cap,*count);++i)out[i]=list[i]->info;return cap<*count?WI_EXT_LIMIT:0;
 }
 static int acquire(void* ctx,const WiServiceQuery* q,WiServiceRef* out,WiServiceInfo* info){auto& o=*(Owner*)ctx;int rc=check(o);if(rc)return rc;
  if(!q||q->size<sizeof(*q)||q->version!=1||(info&&(info->size<sizeof(*info)||info->version!=1)))return WI_EXT_VERSION;
  if(!out||!nameOk(q->id)||(q->provider&&!textOk(q->provider,81))||!q->minVersion||q->maxVersion<q->minVersion)return WI_EXT_INVALID;
  *out=0;auto& h=*o.host;std::lock_guard lock(h.mutex);if(h.refs.size()>=4096)return WI_EXT_LIMIT;
  std::shared_ptr<Service> best;bool exists=false;for(auto& [id,s]:h.services){if(!strcmp(s->info.id,q->id))exists=true;if(matches(*s,*q)&&(!best||before(s,best)))best=s;}
  if(!best)return exists?WI_EXT_NOT_READY:WI_EXT_NOT_FOUND;*out=h.next++;h.refs[*out]={&o,best};if(info)*info=best->info;return 0;
 }
 static int release(void* ctx,WiServiceRef id){auto& o=*(Owner*)ctx;if(!loader(o))return WI_EXT_THREAD;auto& h=*o.host;std::lock_guard lock(h.mutex);auto it=h.refs.find(id);if(it==h.refs.end()||it->second.owner!=&o)return WI_EXT_NOT_FOUND;h.refs.erase(it);return 0;}
 int call(const std::shared_ptr<Service>& s,uint32_t method,const void* input,uint32_t n,void* output,uint32_t cap,uint32_t* written,bool loaderThread,bool closing=false){
  if(!written||(n&&!input)||(cap&&!output)||n>1024*1024||cap>64*1024*1024||callDepth>=16)return WI_EXT_INVALID;*written=0;
  {std::lock_guard lock(mutex);if(!closing&&(!s->owner->accepting||s->info.state!=WI_SERVICE_READY))return WI_EXT_STOPPED;if(!loaderThread&&s->info.thread!=WI_SERVICE_CONCURRENT)return WI_EXT_THREAD;++s->active;}
  int rc;{Scope scope(s->owner);rc=safeInvoke(s->table.invoke,s->table.context,method,input,n,output,cap,written);}
  {std::lock_guard lock(mutex);--s->active;if(*written>cap){*written=0;rc=WI_EXT_LIMIT;}if(rc==WI_EXT_FAULT){s->owner->error="社区服务回调失败："+std::string(s->info.id);changed(*s,WI_SERVICE_FAILED);}}
  return rc;
 }
 static int invoke(void* ctx,WiServiceRef id,uint32_t m,const void* in,uint32_t n,void* out,uint32_t cap,uint32_t* written){auto& o=*(Owner*)ctx;int rc=check(o,false);if(rc)return rc;auto& h=*o.host;std::shared_ptr<Service> service;
  {std::lock_guard lock(h.mutex);auto it=h.refs.find(id);if(it==h.refs.end()||it->second.owner!=&o)return WI_EXT_NOT_FOUND;service=it->second.service;}
  return h.call(service,m,in,n,out,cap,written,loader(o));
 }
 static int subscribe(void* ctx,const char* topic,WiCommunityListener cb,void* arg,WiSubscription* out){auto& o=*(Owner*)ctx;int rc=check(o);if(rc)return rc;if(!nameOk(topic)||!cb||!out)return WI_EXT_INVALID;auto& h=*o.host;std::lock_guard lock(h.mutex);if(h.listeners.size()>=512)return WI_EXT_LIMIT;*out=h.next++;h.listeners.push_back({*out,h.sequence,&o,topic,cb,arg});return 0;}
 static int unsubscribe(void* ctx,WiSubscription id){auto& o=*(Owner*)ctx;if(!loader(o))return WI_EXT_THREAD;auto& h=*o.host;std::lock_guard lock(h.mutex);return std::erase_if(h.listeners,[&](auto& l){return l.owner==&o&&l.id==id;})?0:WI_EXT_NOT_FOUND;}
 static int publish(void* ctx,const char* topic,const void* data,uint32_t n){auto& o=*(Owner*)ctx;int rc=check(o,false);if(rc)return rc;if(!nameOk(topic)||std::string(topic).rfind(o.id+".",0)!=0||n>65536||(n&&!data))return WI_EXT_INVALID;auto& h=*o.host;std::lock_guard lock(h.mutex);if(h.events.size()>=256)return WI_EXT_LIMIT;h.event(o.id,topic,data,n);return 0;}
 static int cancelled(void* ctx){auto& t=*(Task*)ctx;return t.cancel||!t.owner->accepting;}
 static int result(void* ctx,const void* p,uint32_t n){auto& t=*(Task*)ctx;if(callbackOwner!=t.owner)return WI_EXT_THREAD;if(n>1024*1024||(n&&!p))return WI_EXT_LIMIT;t.result.clear();if(n)t.result.assign((const uint8_t*)p,(const uint8_t*)p+n);return 0;}
 static int progress(void* ctx,uint32_t value){if(value>1000)return WI_EXT_INVALID;((Task*)ctx)->progress=value;return 0;}
 static int startJob(void* ctx,const WiJobDefinition* d,WiJob* out){auto& o=*(Owner*)ctx;int rc=check(o);if(rc)return rc;if(!d||d->size<sizeof(*d)||d->version!=1)return WI_EXT_VERSION;if(!d->work||!out)return WI_EXT_INVALID;
  auto& h=*o.host;std::lock_guard lock(h.mutex);if(h.tasks.size()>=64||std::count_if(h.tasks.begin(),h.tasks.end(),[&](auto& t){return t.second->owner==&o;})>=8)return WI_EXT_LIMIT;
  auto t=std::make_shared<Task>();t->owner=&o;t->id=h.next++;t->definition=*d;h.tasks[t->id]=t;*out=t->id;
  try{t->thread=std::thread([t]{CoInitializeEx(nullptr,COINIT_MULTITHREADED);{Scope scope(t->owner);WiJobContext job{sizeof(job),1,t.get(),cancelled,result,progress};t->rc=safeWork(t->definition.work,t->definition.context,&job);}t->done=true;CoUninitialize();});}catch(...){h.tasks.erase(t->id);*out=0;return WI_EXT_LIMIT;}return 0;
 }
 static int cancelJob(void* ctx,WiJob id){auto& o=*(Owner*)ctx;if(!allowed(o))return WI_EXT_THREAD;auto& h=*o.host;std::lock_guard lock(h.mutex);auto it=h.tasks.find(id);if(it==h.tasks.end()||it->second->owner!=&o)return WI_EXT_NOT_FOUND;it->second->cancel=true;return 0;}
 static int jobInfo(void* ctx,WiJob id,WiJobInfo* out){auto& o=*(Owner*)ctx;if(!loader(o))return WI_EXT_THREAD;if(!out||out->size<sizeof(*out)||out->version!=1)return WI_EXT_VERSION;auto& h=*o.host;std::lock_guard lock(h.mutex);auto it=h.tasks.find(id);if(it==h.tasks.end()||it->second->owner!=&o)return WI_EXT_NOT_FOUND;auto& t=*it->second;*out={sizeof(*out),1,t.done?3u:t.cancel?2u:1u,t.progress,t.done?t.rc:0};return 0;}
 static int manage(void* ctx,const WiExternalLifetime* l){auto& o=*(Owner*)ctx;int rc=check(o);if(rc)return rc;if(!l||l->size<sizeof(*l)||l->version!=1)return WI_EXT_VERSION;if(!l->stop||!l->joined||o.external.size()-o.hostLifetimes>=16)return WI_EXT_INVALID;o.external.push_back(*l);return 0;}
 static int path(void* ctx,uint32_t kind,const char* rel,char* out,uint32_t cap,uint32_t* bytes){auto& o=*(Owner*)ctx;int rc=check(o,false);if(rc)return rc;if(!textOk(rel,4096)||!bytes||(cap&&!out)||(kind!=1&&kind!=2))return WI_EXT_INVALID;
  try{fs::path p=wide(rel);if(p.has_root_path())return WI_EXT_INVALID;for(auto& c:p)if(c==L".."||c.wstring().find(L':')!=std::wstring::npos)return WI_EXT_INVALID;auto base=fs::weakly_canonical(kind==1?o.data:o.package),full=fs::weakly_canonical(base/p);auto a=base.begin(),b=full.begin();for(;a!=base.end();++a,++b)if(b==full.end()||_wcsicmp(a->c_str(),b->c_str()))return WI_EXT_INVALID;auto str=utf8(full.wstring());*bytes=(uint32_t)str.size()+1;if(cap<*bytes)return WI_EXT_LIMIT;memcpy(out,str.c_str(),*bytes);return 0;}catch(...){return WI_EXT_INVALID;}
 }
};
CommunityRuntime::CommunityRuntime():impl(std::make_unique<Impl>()){}
CommunityRuntime::~CommunityRuntime()=default;
std::shared_ptr<CommunityRuntime::Owner> CommunityRuntime::createOwner(const std::string& id,const fs::path& data,const fs::path& package){auto o=std::make_shared<Owner>();o->host=impl.get();o->id=id;o->data=data;o->package=package;o->loader=GetCurrentThreadId();std::lock_guard lock(impl->mutex);impl->owners.push_back(o);return o;}
WiCommunityApi CommunityRuntime::api(Owner& o){return {sizeof(WiCommunityApi),1,&o,Boundary<Impl::registration>::call,Boundary<Impl::state>::call,Boundary<Impl::unregister>::call,Boundary<Impl::enumerate>::call,Boundary<Impl::acquire>::call,Boundary<Impl::release>::call,Boundary<Impl::invoke>::call,Boundary<Impl::subscribe>::call,Boundary<Impl::unsubscribe>::call,Boundary<Impl::publish>::call,Boundary<Impl::startJob>::call,Boundary<Impl::cancelJob>::call,Boundary<Impl::jobInfo>::call,Boundary<Impl::manage>::call,Boundary<Impl::path>::call};}
void CommunityRuntime::beginStop(Owner& o){if(o.stopped)return;o.accepting=false;o.stopped=true;
 {std::lock_guard lock(impl->mutex);for(auto& [id,s]:impl->services)if(s->owner==&o)impl->changed(*s,WI_SERVICE_STOPPING);for(auto& [id,t]:impl->tasks)if(t->owner==&o)t->cancel=true;std::erase_if(impl->refs,[&](auto& p){return p.second.owner==&o;});std::erase_if(impl->listeners,[&](auto& l){return l.owner==&o;});}
 for(auto& life:o.external)if(safeLife(&life,true)<0){std::lock_guard lock(impl->mutex);o.error="第三方线程停止回调失败；DLL 保持加载，等待真实退出";}
}
bool CommunityRuntime::drained(Owner& o){
 {std::lock_guard lock(impl->mutex);for(auto& [id,t]:impl->tasks)if(t->owner==&o)return false;for(auto& [id,s]:impl->services)if(s->owner==&o&&s->active)return false;for(auto& [id,r]:impl->refs)if(r.service->owner==&o&&r.owner!=&o)return false;}
 for(auto& life:o.external)if(safeLife(&life,false)!=1)return false;return true;
}
void CommunityRuntime::attachHostLifetime(Owner& o,const WiExternalLifetime& life){o.external.push_back(life);++o.hostLifetimes;}
void CommunityRuntime::finishStop(Owner& o){std::lock_guard lock(impl->mutex);std::erase_if(impl->services,[&](auto& p){if(p.second->owner!=&o)return false;impl->changed(*p.second,WI_SERVICE_REMOVED);return true;});std::erase_if(impl->owners,[&](auto& p){return p.get()==&o;});o.external.clear();o.hostLifetimes=0;}
void CommunityRuntime::pump(){
 std::vector<std::shared_ptr<Task>> done;{std::lock_guard lock(impl->mutex);for(auto& [id,t]:impl->tasks)if(t->done)done.push_back(t);for(auto& t:done)impl->tasks.erase(t->id);}
 for(auto& t:done){if(t->thread.joinable())t->thread.join();if(t->rc==WI_EXT_FAULT){std::lock_guard lock(impl->mutex);t->owner->error="后台任务异常";}if(t->owner->accepting){int rc=safeDone(t->definition.done,t->definition.context,t->id,t->cancel?WI_EXT_CANCELLED:t->rc,t->result.data(),(uint32_t)t->result.size());if(rc){std::lock_guard lock(impl->mutex);t->owner->error="后台完成回调异常";}}}
 // Bound work per pump; messages published from callbacks run on a later pump.
 size_t count;{std::lock_guard lock(impl->mutex);count=std::min<size_t>(64,impl->events.size());}
 while(count--){Event e;std::vector<Listener> listeners;{std::lock_guard lock(impl->mutex);if(impl->events.empty())break;e=std::move(impl->events.front());impl->events.pop_front();listeners=impl->listeners;}if(e.expires&&e.expires<now())continue;
  for(auto& l:listeners){bool alive;{std::lock_guard lock(impl->mutex);alive=std::any_of(impl->listeners.begin(),impl->listeners.end(),[&](auto& p){return p.id==l.id;});}if(!alive||!l.owner->accepting||l.topic!=e.topic||e.sequence<=l.afterSequence)continue;WiCommunityEvent event{sizeof(event),1,e.sequence,e.source.c_str(),e.topic.c_str(),e.data.data(),(uint32_t)e.data.size()};if(safeEvent(l.callback,l.context,&event)){std::lock_guard lock(impl->mutex);l.owner->error="社区事件回调异常";}}
 }
}
std::vector<std::string> CommunityRuntime::consumers(const std::string& provider){std::set<std::string> result;std::lock_guard lock(impl->mutex);for(auto& [id,r]:impl->refs)if(r.service->owner->id==provider&&r.owner->id!=provider)result.insert(r.owner->id);return {result.begin(),result.end()};}
std::string CommunityRuntime::fault(Owner& o){std::lock_guard lock(impl->mutex);return o.error;}
bool CommunityRuntime::ready(const std::string& id,const std::string& provider,uint32_t low,uint32_t high){WiServiceQuery q{sizeof(q),1,id.c_str(),provider.c_str(),low,high,0};std::lock_guard lock(impl->mutex);for(auto& [key,s]:impl->services)if(Impl::matches(*s,q))return true;return false;}
bool CommunityRuntime::decode(const fs::path& file,uint32_t edge,const std::atomic<bool>& cancelled,MediaFrame& frame,std::string& codec){
 std::vector<std::shared_ptr<Service>> list;{std::lock_guard lock(impl->mutex);for(auto& [id,s]:impl->services)if(s->owner->accepting&&s->info.state==WI_SERVICE_READY&&(s->info.capabilities&WI_SERVICE_IMAGE_DECODER))list.push_back(s);}
 std::sort(list.begin(),list.end(),Impl::before);if(list.empty())return false;
 uint8_t prefix[64]{};std::ifstream in(file,std::ios::binary);in.read((char*)prefix,sizeof(prefix));uint32_t n=(uint32_t)in.gcount();auto path=utf8(file.wstring());
 struct Decode {const std::atomic<bool>* cancel;Owner* owner;MediaFrame* frame;uint32_t edge;bool submitted=false,allowFrame=false;};
 auto isCancelled=[](void* c)->int{auto& d=*(Decode*)c;return *d.cancel||!d.owner->accepting;};
 auto submit=[](void* c,uint32_t w,uint32_t h,const void* p,uint32_t bytes)->int{auto& d=*(Decode*)c;if(*d.cancel||!d.owner->accepting)return WI_EXT_CANCELLED;if(!d.allowFrame||d.submitted||!p||!w||!h||w>d.edge||h>d.edge||uint64_t(w)*h*4!=bytes)return WI_EXT_INVALID;d.frame->width=w;d.frame->height=h;d.frame->bgra.assign((const uint8_t*)p,(const uint8_t*)p+bytes);d.submitted=true;return 0;};
 for(auto& s:list){Decode data{&cancelled,s->owner,&frame,edge};WiImageDecodeRequest req{sizeof(req),1,edge,path.c_str(),prefix,n,&data,isCancelled,submit};uint32_t written=0;
  int rc=impl->call(s,WI_IMAGE_PROBE,&req,sizeof(req),nullptr,0,&written,false);if(rc==WI_EXT_UNSUPPORTED||rc==WI_EXT_STOPPED)continue;if(rc)throw std::runtime_error("Community decoder probe failed: "+std::string(s->info.provider)+" code="+std::to_string(rc));
  data.allowFrame=true;rc=impl->call(s,WI_IMAGE_DECODE,&req,sizeof(req),nullptr,0,&written,false);if(rc||!data.submitted)throw std::runtime_error("Community decoder failed/stopped: "+std::string(s->info.provider)+" code="+std::to_string(rc));codec=std::string(s->info.provider)+"/"+s->info.id;return true;
 }return false;
}
std::shared_ptr<PluginStream> CommunityRuntime::stream(const fs::path& file,uint32_t edge,const std::atomic<bool>& cancel){
 struct Stream final:PluginStream{
  Impl* host;std::shared_ptr<Service> provider;const std::atomic<bool>* cancel;std::string path;uint32_t edge;double lastVideo=-1,lastAudio=-1;
  bool pinned=false,opened=false;
  bool alive()const override{return !*cancel&&provider->owner->accepting;}
  static int cancelled(void* ctx){return !((Stream*)ctx)->alive();}
  WiStreamRequest request(){return {sizeof(WiStreamRequest),1,edge,path.c_str(),info.session,0,this,cancelled,nullptr,nullptr};}
  ~Stream(){if(pinned){if(opened){auto r=request();uint32_t n=0;host->call(provider,WI_STREAM_CLOSE,&r,sizeof(r),nullptr,0,&n,false,true);}std::lock_guard l(host->mutex);--provider->active;}}
  int seek(double pos)override{auto r=request();r.position=pos;uint32_t n=0;int rc=host->call(provider,WI_STREAM_SEEK,&r,sizeof(r),nullptr,0,&n,false);if(!rc)lastVideo=lastAudio=-1;return rc;}
  int read(StreamChunk& chunk)override{
   struct Sink {Stream* stream;StreamChunk* chunk;bool video=false,audio=false;};Sink sink{this,&chunk};auto r=request();r.context=&sink;
   r.cancelled=[](void* c)->int{return !((Sink*)c)->stream->alive();};
   r.video=[](void* c,double pts,uint32_t w,uint32_t h,const void* pixels,uint32_t n)->int{auto& s=*(Sink*)c;auto& self=*s.stream;if(!self.alive())return WI_EXT_CANCELLED;if(s.video||!self.info.hasVideo||!std::isfinite(pts)||pts<self.lastVideo||!pixels||!w||!h||w>self.edge||h>self.edge||uint64_t(w)*h*4!=n)return WI_EXT_INVALID;auto f=std::make_shared<MediaFrame>();f->width=w;f->height=h;f->bgra.assign((const BYTE*)pixels,(const BYTE*)pixels+n);s.chunk->frame=f;s.chunk->videoTime=pts;self.lastVideo=pts;s.video=true;return 0;};
   r.audio=[](void* c,double pts,const int16_t* data,uint32_t frames)->int{auto& s=*(Sink*)c;auto& self=*s.stream;if(!self.alive())return WI_EXT_CANCELLED;if(s.audio||!self.info.hasAudio||!std::isfinite(pts)||pts<self.lastAudio||!data||!frames||frames>self.info.sampleRate/4)return WI_EXT_INVALID;s.chunk->pcm.assign(data,data+size_t(frames)*self.info.channels);s.chunk->audioTime=pts;self.lastAudio=pts;s.audio=true;return 0;};
   uint32_t n=0;int rc=host->call(provider,WI_STREAM_READ,&r,sizeof(r),nullptr,0,&n,false);if(rc==0&&!sink.video&&!sink.audio)return WI_EXT_NOT_READY;return rc;
  }
 };
 std::vector<std::shared_ptr<Service>> list;{std::lock_guard l(impl->mutex);for(auto& [id,s]:impl->services)if(s->owner->accepting&&s->info.state==WI_SERVICE_READY&&(s->info.capabilities&WI_SERVICE_STREAM_DECODER))list.push_back(s);}std::sort(list.begin(),list.end(),Impl::before);
 for(auto& provider:list){auto s=std::make_shared<Stream>();s->host=impl.get();s->provider=provider;s->path=utf8(file.wstring());s->edge=edge;s->cancel=&cancel;auto req=s->request();uint32_t written=0;int rc=impl->call(provider,WI_STREAM_PROBE,&req,sizeof(req),nullptr,0,&written,false);if(rc==WI_EXT_UNSUPPORTED||rc==WI_EXT_STOPPED)continue;if(rc)throw std::runtime_error("stream probe failed "+std::to_string(rc));
  {std::lock_guard l(impl->mutex);if(!provider->owner->accepting||provider->info.state!=WI_SERVICE_READY)continue;++provider->active;s->pinned=true;}
  rc=impl->call(provider,WI_STREAM_OPEN,&req,sizeof(req),&s->info,sizeof(s->info),&written,false);s->opened=s->info.session!=0;
  if(rc||written!=sizeof(s->info)||s->info.size<sizeof(s->info)||s->info.version!=1||!s->opened||(!s->info.hasVideo&&!s->info.hasAudio)||!std::isfinite(s->info.duration)||s->info.duration<0||!std::isfinite(s->info.frameRate)||s->info.frameRate<0||!memchr(s->info.codec,0,sizeof(s->info.codec))||(s->info.hasVideo&&(!s->info.width||!s->info.height||s->info.width>edge||s->info.height>edge))||(s->info.hasAudio&&(s->info.channels<1||s->info.channels>2||s->info.sampleRate<8000||s->info.sampleRate>192000)))throw std::runtime_error("stream open failed or invalid format: "+std::string(provider->info.provider));return s;
 }return {};
}

}
