// Members of ModLoader::Impl. All plugin calls remain behind its worker gate.
template<class F> static int sceneCall(void* ctx,F fn){
    if(!ctx)return WI_INVALID;auto& r=*(Record*)ctx;
    if(GetCurrentThreadId()!=r.host->workerId)return WI_THREAD;
    if(!r.accepting)return WI_STOPPED;
    try{return fn(r,*r.host);}catch(...){r.host->log(&r,"Scene API exception");return WI_INVALID;}
}
static int sceneSnapshot(void* ctx,WiSceneSnapshot* out){return sceneCall(ctx,[&](Record&,Impl& h)->int{
    if(!out||out->size<sizeof(*out)||out->version!=1)return WI_VERSION;
    *out=h.scenes.state;return WI_OK;
});}
static int sceneEnumerate(void* ctx,uint64_t generation,WiElement parent,uint32_t type,WiElementInfo* out,uint32_t cap,uint32_t* count){return sceneCall(ctx,[&](Record&,Impl& h)->int{
    if(!count||cap>4096||(cap&&!out))return WI_INVALID;
    if(generation&&generation!=h.scenes.state.generation)return WI_CONFLICT;
    if(parent&&!h.scenes.exists(parent))return WI_NOT_FOUND;
    std::vector<SceneNode> nodes=h.scenes.base;nodes.insert(nodes.end(),h.scenes.pending.created.begin(),h.scenes.pending.created.end());
    uint32_t n=0;for(auto node:nodes){node=h.scenes.pending.resolve(node);auto info=node.info();if((!parent||info.parent==parent)&&(!type||info.type==type)){if(n<cap)out[n]=info;++n;}}
    *count=n;return cap<n?WI_LIMIT:WI_OK;
});}
static int sceneVisible(void* ctx,uint64_t generation,WiElementInfo* out,uint32_t cap,uint32_t* count){return sceneCall(ctx,[&](Record&,Impl& h)->int{
    if(!count||cap>4096||(cap&&!out))return WI_INVALID;
    if(generation&&generation!=h.scenes.state.generation)return WI_CONFLICT;
    *count=(uint32_t)h.scenes.displayed.size();for(uint32_t i=0;i<std::min(cap,*count);++i)out[i]=h.scenes.displayed[i].info();
    return cap<*count?WI_LIMIT:WI_OK;
});}
static int sceneFind(void* ctx,const char* key,WiElement* out){return sceneCall(ctx,[&](Record& r,Impl& h)->int{
    if(!key||!out||!memchr(key,0,128))return WI_INVALID;
    for(auto& n:h.scenes.pending.created)if(n.owner==r.info.id&&n.key==key){*out=n.id;return WI_OK;}
    for(auto& n:h.scenes.base)if(n.key==key){*out=n.id;return WI_OK;}
    WiElement match=0;for(auto& n:h.scenes.pending.created)if(n.key==key){if(match)return WI_CONFLICT;match=n.id;}
    *out=match;return match?WI_OK:WI_NOT_FOUND;
});}
static int sceneRead(void* ctx,WiElement id,uint32_t prop,WiPropertyValue* out){return sceneCall(ctx,[&](Record&,Impl& h)->int{
    if(!out||out->size<sizeof(*out)||out->version!=1)return WI_VERSION;
    if(!h.scenes.exists(id))return WI_NOT_FOUND;auto n=h.scenes.get(id);auto it=n.values.find(prop);
    if(it==n.values.end())return WI_NOT_FOUND;*out=it->second;return WI_OK;
});}
static int sceneCreate(void* ctx,uint32_t type,const char* key,WiElement parent,WiElement* out){return sceneCall(ctx,[&](Record& r,Impl& h)->int{return h.scenes.create(r.info.id,type,key,parent,out);});}
static int sceneClone(void* ctx,WiElement source,const char* key,WiElement* out){return sceneCall(ctx,[&](Record& r,Impl& h)->int{
    if(!h.scenes.exists(source))return WI_NOT_FOUND;auto node=h.scenes.get(source);int rc=h.scenes.create(r.info.id,node.type,key,node.parent(),out);
    if(rc==WI_OK){node.id=*out;node.owner=r.info.id;node.key=key;h.scenes.pending.created.back()=std::move(node);}return rc;
});}
static int sceneSet(void* ctx,WiElement id,uint32_t prop,const WiPropertyValue* value,int32_t priority){return sceneCall(ctx,[&](Record& r,Impl& h)->int{return h.scenes.set(r.info.id,id,prop,value,priority);});}
static int sceneClear(void* ctx,WiElement id,uint32_t prop){return sceneCall(ctx,[&](Record& r,Impl& h)->int{return h.scenes.clear(r.info.id,id,prop);});}
static int sceneRemove(void* ctx,WiElement id){return sceneCall(ctx,[&](Record& r,Impl& h)->int{int rc=h.scenes.remove(r.info.id,id);if(!h.scenes.exists(id))std::erase_if(h.bindings,[&](auto& b){return b.element==id;});return rc;});}
static int sceneListen(void* ctx,WiElement id,WiInputCallback cb,void* arg){return sceneCall(ctx,[&](Record& r,Impl& h)->int{
    if(!h.scenes.exists(id))return WI_NOT_FOUND;
    std::erase_if(h.bindings,[&](auto& b){return b.element==id&&b.owner==r.info.id;});
    if(cb)h.bindings.push_back({r.info.id,id,cb,arg});return WI_OK;
});}
static int sceneDraw(void* ctx,WiElement id,const WiDrawCommand* data,uint32_t count){return sceneCall(ctx,[&](Record& r,Impl& h)->int{
    if(count>4096||(count&&!data))return WI_LIMIT;
    auto it=std::find_if(h.scenes.pending.created.begin(),h.scenes.pending.created.end(),[&](auto& n){return n.id==id&&n.owner==r.info.id;});
    if(it==h.scenes.pending.created.end())return WI_NOT_FOUND;
    bool pathOpen=false;
    for(uint32_t i=0;i<count;++i){auto& d=data[i];if(d.size<sizeof(d)||d.version!=1)return WI_VERSION;
        if(d.kind<WI_DRAW_RECT||d.kind>WI_PATH_END)return WI_UNSUPPORTED;
        for(float f:{d.x,d.y,d.width,d.height,d.x2,d.y2,d.radius,d.stroke,d.fontSize})if(!std::isfinite(f)||std::abs(f)>16384)return WI_INVALID;
        for(float f:d.color)if(!std::isfinite(f)||f<0||f>1)return WI_INVALID;
        if(!memchr(d.text,0,sizeof(d.text)))return WI_INVALID;
        if(d.kind==WI_PATH_BEGIN){if(pathOpen)return WI_INVALID;pathOpen=true;}
        else if(d.kind>=WI_PATH_LINE){if(!pathOpen)return WI_INVALID;if(d.kind==WI_PATH_END)pathOpen=false;}
        else if(pathOpen)return WI_INVALID;
    }
    if(pathOpen)return WI_INVALID;
    it->draw.clear();if(count)it->draw.assign(data,data+count);return WI_OK;
});}
static int sceneBitmap(void* ctx,WiElement id,uint32_t w,uint32_t height,const void* bgra,uint32_t bytes){return sceneCall(ctx,[&](Record& r,Impl& h)->int{
    if(!bgra||!w||!height||w>2048||height>2048||uint64_t(w)*height*4!=bytes)return WI_INVALID;
    auto it=std::find_if(h.scenes.pending.created.begin(),h.scenes.pending.created.end(),[&](auto& n){return n.id==id&&n.owner==r.info.id;});
    if(it==h.scenes.pending.created.end())return WI_NOT_FOUND;
    size_t total=bytes;for(auto& n:h.scenes.pending.created)if(n.id!=id)total+=n.pixels.size();if(total>64*1024*1024)return WI_LIMIT;
    it->pixels.assign((const uint8_t*)bgra,(const uint8_t*)bgra+bytes);it->pixelWidth=w;it->pixelHeight=height;return WI_OK;
});}
static int sceneCommit(void* ctx){return sceneCall(ctx,[](Record& r,Impl& h)->int{h.scenes.commit(r.info.id);h.publish();return WI_OK;});}
static int sceneReset(void* ctx){return sceneCall(ctx,[](Record& r,Impl& h)->int{h.scenes.revoke(r.info.id);std::erase_if(h.drawBindings,[&](auto& b){return b.owner==r.info.id;});std::erase_if(h.bindings,[&](auto& b){return b.owner==r.info.id;});h.publish();return WI_OK;});}
static bool validSetting(const ModResourceView& v,const std::string& value){
    if(value.size()>4096)return false;
    if(v.settingType==WI_SETTING_SWITCH)return value=="0"||value=="1";
    if(v.settingType==WI_SETTING_CHOICE)return std::find(v.choices.begin(),v.choices.end(),value)!=v.choices.end();
    if(v.settingType==WI_SETTING_NUMBER){try{size_t end;double n=std::stod(value,&end);return end==value.size()&&std::isfinite(n)&&n>=v.minimum&&n<=v.maximum;}catch(...){return false;}}
    return true;
}
static int settingDefine(void* ctx,const WiSettingDefinition* d,uint64_t* out){return sceneCall(ctx,[&](Record& r,Impl& h)->int{
    if(!d||d->size<sizeof(*d)||d->version!=1)return WI_VERSION;
    if(!out||!d->key||!d->defaultValue||d->type<WI_SETTING_TEXT||d->type>WI_SETTING_CHOICE||!std::isfinite(d->minimum)||!std::isfinite(d->maximum)||d->minimum>d->maximum)return WI_INVALID;
    ModResourceView v;v.settingType=d->type;v.minimum=d->minimum;v.maximum=d->maximum;
    if(d->choices){if(strlen(d->choices)>8192)return WI_LIMIT;std::istringstream in(d->choices);std::string line;while(std::getline(in,line))if(!line.empty())v.choices.push_back(line);}
    if(!validSetting(v,d->defaultValue))return WI_INVALID;
    WinIslandResource resource{sizeof(resource),1,WI_SETTING,d->key,d->label,d->defaultValue,nullptr,nullptr,0};
    auto id=add(ctx,&resource);if(!id)return WI_CONFLICT;
    for(auto& res:h.resources)if(res.view.handle==id){res.view.settingType=v.settingType;res.view.minimum=v.minimum;res.view.maximum=v.maximum;res.view.choices=v.choices;}
    *out=id;h.publish();return WI_OK;
});}
static int settingRead(void* ctx,const char* key,char* out,uint32_t bytes){return sceneCall(ctx,[&](Record&,Impl&)->int{return getSetting(ctx,key,out,bytes)?WI_OK:WI_NOT_FOUND;});}
static int settingWrite(void* ctx,const char* key,const char* value){return sceneCall(ctx,[&](Record& r,Impl& h)->int{
    if(!key||!value)return WI_INVALID;
    for(auto& res:h.resources)if(res.view.owner==r.info.id&&res.view.kind==WI_SETTING&&res.view.key==key){
        if(!validSetting(res.view,value))return WI_INVALID;
        r.settings[wide(key)]=wide(value);h.saveValues(r);res.view.value=value;h.publish();return WI_OK;
    }return WI_NOT_FOUND;
});}
static int sceneRequestDraw(void* ctx,WiElement id){return sceneCall(ctx,[&](Record&,Impl& h)->int{
    if(!h.scenes.exists(id))return WI_NOT_FOUND;
    std::lock_guard l(h.mutex);for(auto& w:h.queue)if(w.action=="scene-draw"&&w.handle==id)return WI_OK;
    if(h.queue.size()>=256)return WI_LIMIT;Work w{"scene-draw"};w.handle=id;h.queue.push_back(w);h.cv.notify_all();return WI_OK;
});}
#include "media_host.inc"
static int sceneOnDraw(void* ctx,WiElement id,WiDrawCallback callback,void* arg){return sceneCall(ctx,[&](Record& r,Impl& h)->int{
    if(!h.scenes.exists(id))return WI_NOT_FOUND;
    std::erase_if(h.drawBindings,[&](auto& b){return b.owner==r.info.id&&b.element==id;});
    if(callback){h.drawBindings.push_back({r.info.id,id,callback,arg});return sceneRequestDraw(ctx,id);}return WI_OK;
});}
static int cppDraw(WiDrawCallback fn,void* ctx,const WiDrawContext* frame){try{return fn(ctx,frame);}catch(...){return -1001;}}
static int guardedDraw(WiDrawCallback fn,void* ctx,const WiDrawContext* frame){__try{return cppDraw(fn,ctx,frame);}__except(EXCEPTION_EXECUTE_HANDLER){return -1002;}}
void runSceneDraws(WiElement target){auto callbacks=drawBindings;for(auto& b:callbacks){
    if(target&&target!=b.element)continue;
    if(std::none_of(drawBindings.begin(),drawBindings.end(),[&](auto& live){return live.element==b.element&&live.owner==b.owner&&live.callback==b.callback;}))continue;auto r=records.find(b.owner);if(r==records.end()||!r->second->accepting||!scenes.exists(b.element))continue;
    WiDrawContext frame{};frame.size=sizeof(frame);frame.version=1;frame.scene=scenes.state;frame.scene.time=now();frame.element=scenes.get(b.element).info();frame.lastInput=lastSceneInput;
    frame.clip[2]=frame.element.width;frame.clip[3]=frame.element.height;frame.background[3]=1;for(auto& c:frame.foreground)c=1;
    double start=now();int rc=guardedDraw(b.callback,b.context,&frame);if(now()-start>.05)log(r->second.get(),"draw callback exceeded 50ms (observed after return)");
    if(rc<0){r->second->info.error="绘图回调异常";auto order=closeOrder(r->first);for(auto& id:order)disable(*records.at(id),false);}
}}
static int queryInterface(void* ctx,const char* name,uint32_t version,void* out,uint32_t bytes){
    if(name&&!strcmp(name,"winisland.settings")){
        if(version!=1||bytes<sizeof(WiSettingsApi)||!out)return WI_VERSION;
        WiSettingsApi api{sizeof(api),1,ctx,&settingDefine,&settingRead,&settingWrite};memcpy(out,&api,sizeof(api));return WI_OK;
    }
    if(name&&!strcmp(name,WI_MEDIA_INTERFACE)){
        if(version!=WI_MEDIA_ABI||bytes<offsetof(WiMediaApi,replaceSource)||!out)return WI_MEDIA_VERSION;
        WiMediaApi api{sizeof(api),WI_MEDIA_ABI,ctx,&mediaCaps,&mediaLoad,&mediaInfo,&mediaBind,&mediaPlay,&mediaPause,&mediaStop,&mediaSeek,&mediaLoop,&mediaRate,&mediaMuted,&mediaRelease,&mediaReplace,&mediaUnbind,&mediaVolume,&mediaHiddenPolicy,&mediaLoadFrom,&mediaChooseLocal};memcpy(out,&api,std::min<size_t>(bytes,sizeof(api)));return WI_MEDIA_OK;
    }
    if(!name||strcmp(name,WI_SCENE_INTERFACE))return WI_UNSUPPORTED;
    if(version!=WI_SCENE_ABI||bytes<sizeof(WiSceneApi)||!out)return WI_VERSION;
    WiSceneApi api{sizeof(WiSceneApi),WI_SCENE_ABI,ctx,&sceneSnapshot,&sceneEnumerate,&sceneFind,&sceneRead,&sceneCreate,&sceneClone,&sceneSet,&sceneClear,&sceneRemove,&sceneListen,&sceneDraw,&sceneBitmap,&sceneCommit,&sceneVisible,&sceneOnDraw,&sceneRequestDraw,&sceneReset};
    memcpy(out,&api,sizeof(api));return WI_OK;
}
static int cppInput(WiInputCallback fn,void* ctx,const WiInputEvent* event){try{return fn(ctx,event);}catch(...){return -1001;}}
static int guardedInput(WiInputCallback fn,void* ctx,const WiInputEvent* event){__try{return cppInput(fn,ctx,event);}__except(EXCEPTION_EXECUTE_HANDLER){return -1002;}}
void dispatchSceneInput(const WiInputEvent& input){
    if(!scenes.exists(input.target))return;
    auto copy=bindings;bool handled=false;
    for(auto it=copy.rbegin();it!=copy.rend();++it)if(it->element==input.target){
        if(std::none_of(bindings.begin(),bindings.end(),[&](auto& live){return live.owner==it->owner&&live.element==it->element&&live.callback==it->callback;}))continue;
        auto r=records.find(it->owner);if(r==records.end()||!r->second->accepting)continue;
        double start=now();int rc=guardedInput(it->callback,it->context,&input);
        if(now()-start>.05)log(r->second.get(),"input callback exceeded 50ms (observed after return)");
        if(rc<0){handled=true;r->second->info.error="输入回调异常";auto order=closeOrder(r->first);for(auto& id:order)disable(*records.at(id),false);break;}
        if(rc>0){handled=true;break;}
    }
    if(!handled&&input.kind==WI_POINTER_CLICK&&input.target>=101&&input.target<=104){std::lock_guard lock(mutex);sceneActions.push_back((int)input.target-100);}
    else if(!handled&&input.kind==WI_POINTER_CLICK&&input.target>=50000&&input.target<1000000){std::lock_guard lock(mutex);sceneActions.push_back((int)input.target);}
}




