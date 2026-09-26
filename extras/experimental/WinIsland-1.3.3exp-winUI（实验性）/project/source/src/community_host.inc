// Loader-side orchestration. The registry owns handles/call gates, never UI objects.
void validatePrivateLibraries(Record& r){
    std::map<std::wstring,std::string> claims;
    auto base=mediaRoot(r);
    for(auto& item:fs::recursive_directory_iterator(base))if(item.is_regular_file()){
        auto ext=item.path().extension().wstring();CharLowerBuffW(ext.data(),(DWORD)ext.size());if(ext!=L".dll")continue;
        auto bytes=readFile(item.path(),256*1024*1024);if(bytes.size()<64)throw std::runtime_error("依赖 DLL 不是有效 PE");
        uint32_t offset=0;memcpy(&offset,bytes.data()+60,4);if(offset>bytes.size()-6||bytes.substr(offset,4)!=std::string("PE\0\0",4))throw std::runtime_error("依赖 DLL 的 PE 标头无效");
        uint16_t machine=0;memcpy(&machine,bytes.data()+offset+4,2);if(machine!=0x8664)throw std::runtime_error("插件及其 DLL 依赖必须为 x64："+utf8(item.path().filename().wstring()));
        if(fs::equivalent(item.path(),r.entry))continue; // entry modules are loaded by full path; private imports are the shared namespace
        auto name=item.path().filename().wstring();CharLowerBuffW(name.data(),(DWORD)name.size());auto hash=sha256(bytes);
        // Entry DLL filenames are local module identities too. No SetDllDirectory/AddDllDirectory.
        if(claims.contains(name)&&claims[name]!=hash)throw std::runtime_error("包内同名 DLL 内容冲突："+utf8(name));claims[name]=hash;
        for(auto& [id,other]:records)if(other.get()!=&r&&other->dll&&other->libraryClaims.contains(name)&&other->libraryClaims[name]!=hash)throw std::runtime_error("DLL 名称冲突："+utf8(name)+"；已由 "+id+" 使用不同版本，请给私有依赖使用独立名称或进程隔离");
        HMODULE loaded=GetModuleHandleW(name.c_str());if(loaded){wchar_t path[32768]{};if(GetModuleFileNameW(loaded,path,32768)&&fileSha256(path)!=hash)throw std::runtime_error("进程已加载同名但不同内容的 DLL："+utf8(name));}
    }
    r.libraryClaims=std::move(claims);
}
bool serviceDependencies(Record& r,std::set<std::string>& visiting,bool restoring){
    r.info.serviceProviders.clear();if(!r.package)return false;
    for(auto& dep:r.package->manifest.services){
        if(dep.optional)continue; // Optional discovery never silently enables another mod.
        Record* best=nullptr;uint32_t version=0;
        for(auto& [id,p]:records){if(!p->info.valid||!p->package||(!dep.provider.empty()&&id!=dep.provider))continue;
            for(auto& offer:p->package->manifest.provides)if(offer.id==dep.id&&offer.version>=dep.minVersion&&offer.version<=dep.maxVersion&&(offer.version>version||(offer.version==version&&(!best||id<best->info.id)))){version=offer.version;best=p.get();}
        }
        if(!best||(restoring&&best->desired!="enabled")){r.info.error="必需服务缺失、版本不符或提供者未启用："+dep.id;return false;}
        if(!enable(*best,visiting,restoring)){r.info.error="必需服务提供者无法启动："+best->info.id+"；"+best->info.error;return false;}
        if(!community.ready(dep.id,best->info.id,dep.minVersion,dep.maxVersion)){r.info.error="提供者未就绪："+dep.id;return false;}
        r.info.serviceProviders.push_back(best->info.id);
    }return true;
}
bool ownServicesReady(Record& r){
    if(!r.package)return true;
    auto all=[&]{for(auto& s:r.package->manifest.provides)if(!community.ready(s.id,r.info.id,s.version,s.version))return false;return true;};
    double deadline=now()+5;
    while(!all()){
        if(now()>=deadline||stopping||!community.fault(*r.communityOwner).empty())return false;
        openRuntime.pump();community.pump();Sleep(5);
    }return true;
}
void finishDisable(Record& r){
    revoke(r);
    if(guardedTable(r.plugin)){int rc=guarded(r.plugin->onDisable,r.plugin->context,"");if(rc){r.info.error="onDisable 异常";log(&r,r.info.error);}}
    if(r.communityOwner){community.finishStop(*r.communityOwner);}
    destroy(r);if(r.openOwner){openRuntime.retire(*r.openOwner);r.openOwner.reset();}r.communityOwner.reset();r.info.enabled=false;r.info.stopping=false;r.info.pendingRestart=false;r.alpha=0;
    state(r,r.unloadRequested?"已卸载":"已禁用");
}
void pollCommunity(){
    openRuntime.pump();community.pump();
    std::vector<std::string> failures;
    for(auto& [id,r]:records)if(r->communityOwner&&r->accepting&&!community.fault(*r->communityOwner).empty())failures.push_back(id);
    for(auto& id:failures){auto reason=community.fault(*records.at(id)->communityOwner);auto order=closeOrder(id);for(auto& dep:order){auto& r=*records.at(dep);r.info.error=reason;quarantine(r);}}
    for(auto& [id,r]:records)if(r->info.stopping&&!r->info.pendingRestart&&now()-r->stoppingAt>5){r->info.pendingRestart=true;state(*r,"正在等待合作退出，需重启才能确保关闭（DLL 仍加载）");}
    for(auto& [id,r]:records)if(r->info.stopping&&(!r->communityOwner||community.drained(*r->communityOwner)))finishDisable(*r);
}
