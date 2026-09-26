// ModLoader worker bridge: never opens a file or decodes on a plugin callback.
template<class F> static int mediaCall(void* ctx,F fn){
    if(!ctx)return WI_MEDIA_INVALID;auto& r=*(Record*)ctx;
    if(GetCurrentThreadId()!=r.host->workerId)return WI_MEDIA_WRONG_THREAD;
    if(!r.accepting)return WI_MEDIA_OWNER_STOPPED;
    try{return fn(r,*r.host);}catch(const std::exception& e){r.host->log(&r,e.what());return WI_MEDIA_INVALID;}catch(...){return WI_MEDIA_INVALID;}
}
static int mediaCaps(void* ctx,WiMediaCapabilities* out){return mediaCall(ctx,[&](Record&,Impl&)->int{
    if(!out||out->size<sizeof(*out)||out->version!=WI_MEDIA_ABI)return WI_MEDIA_VERSION;
    *out={sizeof(*out),WI_MEDIA_ABI,WI_MEDIA_PNG|WI_MEDIA_JPEG|WI_MEDIA_BMP,WI_MEDIA_GIF,WI_MEDIA_H264,0,1,0};
    strcpy_s(out->formats,"WIC PNG/JPEG/BMP; GIF full frames/disposal/timing; Media Foundation MP4/H.264 software RGB32; optional decoded audio via bounded PCM waveOut; default mute; Windows Media Feature Pack required");return WI_MEDIA_OK;
});}
static bool mediaRelative(const char* path,fs::path& out){
    if(!path||!memchr(path,0,4096)||!*path)return false;out=wide(path);if(out.has_root_path())return false;
    for(auto& component:out){auto s=component.wstring();if(s==L".."||s.find(L':')!=s.npos)return false;}return true;
}
static fs::path mediaRoot(Record& r){auto p=r.entry;for(auto& component:fs::path(wide(r.package->manifest.entry))){(void)component;p=p.parent_path();}return p;}
static int mediaLoadFrom(void* ctx,uint32_t source,const char* path,uint32_t edge,WiMedia* out){return mediaCall(ctx,[&](Record& r,Impl& h)->int{
    fs::path relative;if(!out||!r.package||source>WI_MEDIA_PLUGIN_DATA||edge<32||edge>4096||!mediaRelative(path,relative))return WI_MEDIA_INVALID;*out=0;
    auto id=h.nextMedia++;int rc=h.mediaEngine.load(id,source==WI_MEDIA_PACKAGE?mediaRoot(r):r.folder,relative,edge);if(rc)return rc;h.mediaAssets.emplace(id,MediaAsset{id,r.info.id});*out=id;return WI_MEDIA_OK;
});}
static int mediaChooseLocal(void* ctx,uint32_t edge,WiMedia* out){return mediaCall(ctx,[&](Record& r,Impl& h)->int{
    if(!out)return WI_MEDIA_INVALID;*out=0;auto id=h.nextMedia++;int rc=h.mediaEngine.chooseLocalFile(id,edge);if(rc)return rc;h.mediaAssets.emplace(id,MediaAsset{id,r.info.id});*out=id;return WI_MEDIA_OK;
});}
static int mediaLoad(void* ctx,const char* path,WiMedia* out){return mediaCall(ctx,[&](Record& r,Impl& h)->int{
    fs::path relative;if(!out||!r.package||!mediaRelative(path,relative))return WI_MEDIA_INVALID;*out=0;
    auto id=h.nextMedia++;int rc=h.mediaEngine.load(id,mediaRoot(r),relative);if(rc)return rc;
    h.mediaAssets.emplace(id,MediaAsset{id,r.info.id});*out=id;return WI_MEDIA_OK;
});}
static int mediaInfo(void* ctx,WiMedia id,WiMediaInfo* out){return mediaCall(ctx,[&](Record& r,Impl& h)->int{
    if(!out||out->size<offsetof(WiMediaInfo,originalWidth)||out->version!=WI_MEDIA_ABI)return WI_MEDIA_VERSION;
    auto it=h.mediaAssets.find(id);if(it==h.mediaAssets.end()||it->second.owner!=r.info.id)return WI_MEDIA_NOT_FOUND;
    MediaSnapshot snap;if(!h.mediaEngine.snapshot(id,snap))return WI_MEDIA_NOT_FOUND;memcpy(out,&snap.info,std::min<size_t>(out->size,sizeof(snap.info)));return WI_MEDIA_OK;
});}
static int mediaBind(void* ctx,WiMedia id,WiElement element){return mediaCall(ctx,[&](Record& r,Impl& h)->int{
    auto asset=h.mediaAssets.find(id);if(asset==h.mediaAssets.end()||asset->second.owner!=r.info.id)return WI_MEDIA_NOT_FOUND;
    auto node=std::find_if(h.scenes.pending.created.begin(),h.scenes.pending.created.end(),[&](auto& n){return n.id==element&&n.owner==r.info.id;});if(node==h.scenes.pending.created.end())return WI_MEDIA_NOT_FOUND;
    // A node has one media writer. A resource can bind many nodes of its owner.
    for(auto& [key,other]:h.mediaAssets)other.bound.erase(element);asset->second.bound.insert(element);asset->second.revision=0;
    MediaSnapshot snap;if(h.mediaEngine.snapshot(id,snap))node->mediaFrame=snap.frame;node->pixels.clear();return WI_MEDIA_OK;
});}
static int mediaUnbind(void* ctx,WiMedia id,WiElement element){return mediaCall(ctx,[&](Record& r,Impl& h)->int{
    auto a=h.mediaAssets.find(id);if(a==h.mediaAssets.end()||a->second.owner!=r.info.id)return WI_MEDIA_NOT_FOUND;if(!a->second.bound.erase(element))return WI_MEDIA_NOT_FOUND;
    for(auto& n:h.scenes.pending.created)if(n.id==element&&n.owner==r.info.id)n.mediaFrame.reset();return WI_MEDIA_OK;
});}
static int mediaControl(void* ctx,WiMedia id,uint32_t op,double value=0){return mediaCall(ctx,[&](Record& r,Impl& h)->int{
    auto a=h.mediaAssets.find(id);if(a==h.mediaAssets.end()||a->second.owner!=r.info.id)return WI_MEDIA_NOT_FOUND;return h.mediaEngine.command(id,op,value);
});}
static int mediaPlay(void* c,WiMedia m){return mediaControl(c,m,MediaPlay);}
static int mediaPause(void* c,WiMedia m){return mediaControl(c,m,MediaPause);}
static int mediaStop(void* c,WiMedia m){return mediaControl(c,m,MediaStop);}
static int mediaSeek(void* c,WiMedia m,double v){return mediaControl(c,m,MediaSeek,v);}
static int mediaLoop(void* c,WiMedia m,uint32_t v){return v<=1?mediaControl(c,m,MediaLoop,v):WI_MEDIA_INVALID;}
static int mediaRate(void* c,WiMedia m,double v){return mediaControl(c,m,MediaRate,v);}
static int mediaMuted(void* c,WiMedia m,uint32_t v){return v<=1?mediaControl(c,m,MediaMuted,v):WI_MEDIA_INVALID;}
static int mediaVolume(void* c,WiMedia m,double v){return mediaControl(c,m,MediaVolume,v);}
static int mediaHiddenPolicy(void* c,WiMedia m,uint32_t v){return mediaControl(c,m,MediaHiddenPolicy,v);}
static int mediaReplace(void* ctx,WiMedia id,const char* path){return mediaCall(ctx,[&](Record& r,Impl& h)->int{
    auto a=h.mediaAssets.find(id);if(a==h.mediaAssets.end()||a->second.owner!=r.info.id)return WI_MEDIA_NOT_FOUND;fs::path relative;if(!mediaRelative(path,relative))return WI_MEDIA_INVALID;
    // Last good frame remains visible until the new generation produces a frame.
    return h.mediaEngine.replace(id,mediaRoot(r),relative);
});}
static int mediaRelease(void* ctx,WiMedia id){return mediaCall(ctx,[&](Record& r,Impl& h)->int{
    auto a=h.mediaAssets.find(id);if(a==h.mediaAssets.end()||a->second.owner!=r.info.id)return WI_MEDIA_NOT_FOUND;
    for(auto& n:h.scenes.pending.created)if(a->second.bound.contains(n.id))n.mediaFrame.reset();
    for(auto& n:h.scenes.committed.created)if(a->second.bound.contains(n.id))n.mediaFrame.reset();
    h.mediaEngine.release(id);h.mediaAssets.erase(a);h.publish();return WI_MEDIA_OK;
});}
void pollMedia(){
    struct Event {std::string owner,topic,payload;WiMedia id;uint64_t generation;};std::vector<Event> events;bool changed=false;
    for(auto& [id,a]:mediaAssets){auto r=records.find(a.owner);if(r==records.end()||!r->second->accepting)continue;
        MediaSnapshot s;if(!mediaEngine.snapshot(id,s))continue;
        bool visible=false;for(auto& n:scenes.committed.created)if(a.bound.contains(n.id)){
            auto resolved=scenes.committed.resolve(n);if(resolved.n(WI_VISIBLE,0,1)&&resolved.n(WI_OPACITY,0,1)>0){bool parentsVisible=true;auto parent=resolved.parent();std::set<WiElement> seen;
                while(parent&&seen.insert(parent).second&&scenes.exists(parent)){auto p=scenes.get(parent);if(!p.n(WI_VISIBLE,0,1)||p.n(WI_OPACITY,0,1)<=0){parentsVisible=false;break;}parent=p.parent();}visible|=parentsVisible;
            }
        }
        if(a.hidden==visible){a.hidden=!visible;mediaEngine.command(id,MediaHidden,a.hidden);}
        if(a.revision!=s.revision){a.revision=s.revision;
            if(s.frame){for(auto& n:scenes.pending.created)if(a.bound.contains(n.id))n.mediaFrame=s.frame;
                // Update only retained frame slots. Never publish another uncommitted property edit.
                for(auto& n:scenes.committed.created)if(a.bound.contains(n.id)&&n.mediaFrame!=s.frame){n.mediaFrame=s.frame;changed=true;}}
        }
        if(a.eventSequence!=s.eventSequence){a.eventSequence=s.eventSequence;changed=true;
            std::ostringstream json;json<<"{\"resource\":"<<id<<",\"generation\":"<<s.generation<<",\"state\":"<<s.info.state<<",\"position\":"<<s.info.position<<"}";
            events.push_back({a.owner,"media."+s.event,json.str(),id,s.generation});
            if(s.event!="loop")log(r->second.get(),"media resource="+std::to_string(id)+" generation="+std::to_string(s.generation)+" "+s.event+(s.info.error[0]?" "+std::string(s.info.error):""));
        }
    }
    if(changed)publish();
    for(auto& e:events){auto a=mediaAssets.find(e.id);if(a==mediaAssets.end())continue;MediaSnapshot current;if(!mediaEngine.snapshot(e.id,current)||current.generation!=e.generation)continue;
        auto listeners=resources;for(auto& res:listeners)if(res.view.owner==e.owner&&res.view.kind==WI_EVENT&&(res.view.key==e.topic||res.view.key=="media.changed")){
            auto r=records.find(e.owner);if(r==records.end()||!r->second->accepting)break;
            if(std::none_of(resources.begin(),resources.end(),[&](auto& live){return live.view.handle==res.view.handle;}))continue;
            if(guarded(res.callback,res.context,e.payload.c_str())!=0){r->second->info.error="媒体事件回调失败";disable(*r->second,false);break;}
        }
    }
}
