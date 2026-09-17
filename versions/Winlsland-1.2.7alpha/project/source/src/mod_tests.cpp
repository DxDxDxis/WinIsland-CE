#include "mod_system.h"
namespace wi {
namespace {
uint32_t crc(const std::string& s){uint32_t v=~0u;for(unsigned char c:s){v^=c;for(int n=0;n<8;++n)v=(v>>1)^(0xedb88320u&-(int)(v&1));}return ~v;}
// Test-only STORE writer. Production package reading uses vendored miniz; real
// compressed .wimod examples are produced by System.IO.Compression at build time.
std::string zip(const std::vector<std::pair<std::string,std::string>>& files){
    std::string local,central;auto word=[](std::string& s,uint32_t v,int n){for(int i=0;i<n;++i)s.push_back(char(v>>(8*i)));};
    for(auto& [name,data]:files){auto offset=local.size();uint32_t hash=crc(data);
        word(local,0x04034b50,4);word(local,20,2);word(local,0x800,2);word(local,0,2);word(local,0,4);word(local,hash,4);word(local,(uint32_t)data.size(),4);word(local,(uint32_t)data.size(),4);word(local,(uint32_t)name.size(),2);word(local,0,2);local+=name;local+=data;
        word(central,0x02014b50,4);word(central,20,2);word(central,20,2);word(central,0x800,2);word(central,0,2);word(central,0,4);word(central,hash,4);word(central,(uint32_t)data.size(),4);word(central,(uint32_t)data.size(),4);word(central,(uint32_t)name.size(),2);word(central,0,2);word(central,0,2);word(central,0,2);word(central,0,2);word(central,0,4);word(central,(uint32_t)offset,4);central+=name;
    }
    auto offset=local.size();local+=central;word(local,0x06054b50,4);word(local,0,4);word(local,(uint32_t)files.size(),2);word(local,(uint32_t)files.size(),2);word(local,(uint32_t)central.size(),4);word(local,(uint32_t)offset,4);word(local,0,2);return local;
}
}
int modSystemTest(const fs::path& output){
    fs::create_directories(output);std::ostringstream report;int passed=0,failed=0;
    auto check=[&](bool yes,const char* name){report<<(yes?"PASS ":"FAIL ")<<name<<'\n';yes?++passed:++failed;};
    auto base=fs::absolute(output)/wide("run-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));auto root=base/L"packages",data=base/L"data";
    auto samples=exePath().parent_path()/L"examples";if(!fs::exists(samples))samples=exePath().parent_path().parent_path()/L"examples";
    auto manifest=[](std::string id,std::string fields){return "{\"id\":\""+id+"\",\"apiVersion\":1,\"entry\":\"bin/example.dll\""+fields+"}";};
    auto fixture=[&](std::string id,std::string extra="",std::string source="example-button"){
        auto package=root/(wide(id)+L".wimod");auto dll=readFile(samples/wide(source)/L"example.dll");
        writeAtomic(package,zip({{"mod.json",manifest(id,extra)},{"bin/example.dll",dll}}));writeAtomic(data/L"mods"/wide(id)/L"host-state.txt","disabled");return package;
    };
    auto wait=[&](ModLoader& loader){double end=now()+15;while(loader.busy()&&now()<end)Sleep(10);check(!loader.busy(),"worker completes within deadline");};
    auto run=[&](ModLoader& loader,const char* action,const char* id="",bool cascade=false){check(loader.request(action,id,cascade),"operation accepted");wait(loader);};
    auto info=[](const ModSnapshot& s,const char* id){for(auto& m:s.mods)if(m.id==id||m.packageFile==std::string(id)+".wimod")return m;return ModInfo{};};
    try{
        auto fixtureDll=readFile(exePath().parent_path().parent_path()/L"test-fixtures/scene-fixture/bin/example.dll");
        check(!fixtureDll.empty(),"scene C ABI fixture DLL exists");
        if(!fixtureDll.empty()){
            auto sr=base/L"scene-packages",sd=base/L"scene-data";
            for(auto id:{"scene-a","scene-b"}){writeAtomic(sr/(wide(id)+L".wimod"),zip({{"mod.json",manifest(id,"")},{"bin/example.dll",fixtureDll}}));writeAtomic(sd/L"mods"/wide(id)/L"host-state.txt","disabled");}
            ModLoader host(sr,false,sd);run(host,"scan");run(host,"enable","scene-a");auto snap=host.snapshot();
            check(info(snap,"scene-a").enabled&&snap.scene.created.size()==1,"new interface queried by actual DLL, text committed");
            auto rootNode=sceneNode(1,"island",WI_CONTAINER,0,0,180,30);
            check(snap.scene.resolve(rootNode).n(WI_RECT,2)==340,"explicit plugin size reaches published render plan");
            run(host,"enable","scene-b");snap=host.snapshot();check(snap.scene.created.size()==2,"two scene owners coexist");
            run(host,"disable","scene-a");snap=host.snapshot();check(snap.scene.created.size()==1&&snap.scene.created[0].owner=="scene-b"&&snap.scene.resolve(rootNode).n(WI_RECT,2)==340,"disable A removes only A; B geometry and size survive");
            WiSceneSnapshot st{sizeof(st),1};st.flags=WI_MUSIC|WI_PLAYING;st.width=340;st.height=64;host.updateScene(st,{rootNode});host.emit("scene.changed");Sleep(180);snap=host.snapshot();
            check(!snap.scene.created.empty()&&snap.scene.resolve(snap.scene.created[0]).n(WI_VISIBLE)==0,"scene music state callback hides fixture element");
            st.flags=WI_IDLE;host.updateScene(st,{rootNode});host.emit("scene.changed");Sleep(180);snap=host.snapshot();
            check(!snap.scene.created.empty()&&snap.scene.resolve(snap.scene.created[0]).n(WI_VISIBLE)==1,"scene idle callback restores element after content ends");
            WiInputEvent event{sizeof(event),1};event.kind=WI_POINTER_CLICK;event.target=snap.scene.created.empty()?0:snap.scene.created[0].id;host.sceneInput(event);Sleep(180);snap=host.snapshot();
            check(!snap.scene.created.empty()&&snap.scene.resolve(snap.scene.created[0]).text(WI_TEXT_VALUE)=="Scene input received","input callback updates same retained element");
            run(host,"unload","scene-b");host.sceneInput(event);Sleep(100);snap=host.snapshot();check(snap.scene.created.empty()&&snap.scene.edits.empty()&&snap.scene.resolve(rootNode).n(WI_RECT,2)==180,"unload revokes elements input size and stale input target");
        }
        {ModLoader empty(root,false,data);run(empty,"scan");check(empty.snapshot().mods.empty(),"empty package directory produces empty list");}
        ModPackage compressed(samples/L"example-button.wimod");compressed.validate();check(compressed.manifest.id=="example-button","actual DEFLATE .wimod manifest and CRC validated");auto extracted=compressed.extract(data/L"test-cache");check(fs::exists(extracted)&&sha256(readFile(extracted))==sha256(readFile(samples/L"example-button/example.dll")),"ZIP entry extracted exactly before DLL loading");
        check(modVersionMatches("1.2.7alpha",">=1.2.5alpha-r1 <1.3.0"),"host prerelease range matches");check(!modVersionMatches("1.0.0",">=2.0.0"),"dependency version mismatch rejected");
        fixture("a",R"(,"name":"基础模组","version":"1.0.0")");fixture("blank",R"(,"name":" \u3000","description":null,"author":4,"version":"\t")");
        fixture("b",R"(,"name":"第二模组","description":"独立替换链","version":"1.0.0")","example-overlay");
        fixture("dep",R"(,"name":"依赖模组","dependencies":[{"id":"a","version":">=1.0.0"}])");
        fixture("missing-dep",R"(,"dependencies":[{"id":"absent","version":">=1.0.0"}])");
        fixture("version-dep",R"(,"dependencies":[{"id":"a","version":">=2.0.0"}])");
        fixture("cycle-one",R"(,"dependencies":["cycle-two"])");fixture("cycle-two",R"(,"dependencies":["cycle-one"])");
        auto dll=readFile(samples/L"example-button/example.dll");
        writeAtomic(root/L"bad-json.wimod",zip({{"mod.json","{broken"},{"bin/example.dll",dll}}));
        writeAtomic(root/L"bad-api.wimod",zip({{"mod.json",R"({"id":"bad-api","apiVersion":9,"entry":"bin/example.dll"})"},{"bin/example.dll",dll}}));
        writeAtomic(root/L"missing-dll.wimod",zip({{"mod.json",manifest("missing-dll","")}}));
        writeAtomic(root/L"missing-json.wimod",zip({{"bin/example.dll",dll}}));
        writeAtomic(root/L"traversal.wimod",zip({{"mod.json",manifest("traversal","")},{"bin/example.dll",dll},{"../escape.txt","no"}}));
        writeAtomic(root/L"duplicate-path.wimod",zip({{"mod.json",manifest("duplicate-path","")},{"bin/example.dll",dll},{"BIN/EXAMPLE.DLL",dll}}));
        writeAtomic(root/L"signature.wimod",zip({{"mod.json",manifest("signature","")},{"bin/example.dll",dll},{"signature.json","{}"},{"signature.p7s","invalid"}}));
        fixture("duplicate-id");fs::copy_file(root/L"duplicate-id.wimod",root/L"duplicate-id-copy.wimod");
        fixture("fault","","fault-fixture");
        std::map<std::string,std::string> hashes;for(auto name:{"a","b","dep"})hashes[name]=sha256(readFile(root/(wide(name)+L".wimod")));
        {
            ModLoader loader(root,false,data);run(loader,"scan");auto s=loader.snapshot();auto blank=info(s,"blank");
            check(blank.name=="未命名模组"&&blank.description=="---"&&blank.author=="作者并未填写"&&blank.version=="版本号未填写","metadata default text for missing wrong-type and whitespace fields");
            for(auto id:{"missing-dep","version-dep","bad-json","bad-api","missing-dll","missing-json","traversal","duplicate-path","signature","duplicate-id","duplicate-id-copy"})check(info(s,id).status=="加载失败"&&!info(s,id).error.empty(),id);
            check(!fs::exists(base/L"escape.txt"),"ZIP traversal did not create files outside cache");
            run(loader,"enable","cycle-one");check(info(loader.snapshot(),"cycle-one").status=="加载失败","dependency cycle rejected");
            check(loader.request("enable","a"),"enable queued");check(!loader.request("reload","a"),"conflicting operation rejected");wait(loader);s=loader.snapshot();check(info(s,"a").enabled&&info(s,"a").loads==1,"DLL loaded from package cache and enabled");
            check(s.resources.size()>=8,"setting button layer replacement timer and event resources registered");
            check(std::any_of(s.resources.begin(),s.resources.end(),[](auto& r){return r.owner=="a"&&r.kind==WI_ANIMATION;}),"plugin JavaScript animation registered through the real host ABI");
            auto setting=std::find_if(s.resources.begin(),s.resources.end(),[](auto& r){return r.owner=="a"&&r.kind==WI_SETTING;});
            if(setting!=s.resources.end()){check(loader.invoke(setting->handle,"persisted value"),"setting callback queued");wait(loader);}
            run(loader,"enable","b");s=loader.snapshot();check(info(s,"a").enabled&&info(s,"b").enabled,"two independent packages enabled");check(s.replacement("animation-duration",1)==1.2,"later replacement takes precedence");
            run(loader,"disable","b");s=loader.snapshot();check(info(s,"a").enabled&&!info(s,"b").loaded,"single disable preserves unrelated module");check(s.replacement("animation-duration",1)==.8,"replacement chain restores A");
            run(loader,"disable","a");s=loader.snapshot();check(s.resources.empty()&&s.replacement("animation-duration",1)==1,"last owner restores host default");
            run(loader,"enable","a");s=loader.snapshot();check(std::any_of(s.resources.begin(),s.resources.end(),[](auto& r){return r.kind==WI_SETTING&&r.value=="persisted value";}),"configuration survives cache DLL unload");
            auto button=std::find_if(s.resources.begin(),s.resources.end(),[](auto& r){return r.kind==WI_BUTTON;});if(button!=s.resources.end()){check(loader.invoke(button->handle),"real package DLL button invocation");wait(loader);}
            s=loader.snapshot();check(std::any_of(s.resources.begin(),s.resources.end(),[](auto& r){return r.kind==WI_LAYER&&r.label=="按钮已响应";}),"button callback changes Scene layer");
            uint64_t loads=info(s,"a").loads,unloads=info(s,"a").unloads;run(loader,"reload","a");s=loader.snapshot();check(info(s,"a").loads==loads+1&&info(s,"a").unloads==unloads+1,"reload really unloads DLL revalidates package and loads again");
            run(loader,"enable","dep");check(loader.affected("a").size()==2,"versioned dependency impact includes dependent");
            run(loader,"disable","a");s=loader.snapshot();check(info(s,"a").enabled&&info(s,"dep").enabled,"current-only refuses broken dependency");
            run(loader,"disable","a",true);s=loader.snapshot();check(!info(s,"a").loaded&&!info(s,"dep").loaded,"cascade closes dependent before dependency");
            run(loader,"enable","fault");s=loader.snapshot();check(info(s,"fault").status=="加载失败"&&!info(s,"fault").loaded,"guarded exception does not crash host");
            for(int i=0;i<6;++i){run(loader,"enable","a");run(loader,"disable","a");}check(loader.snapshot().resources.empty(),"repeated enable/disable leaves no resources");
            run(loader,"enable","dep");run(loader,"enable","b");check(modUiTest(loader,output)==0,"native .wimod manager controls DPI and confirmation tests");
            run(loader,"enable","a");run(loader,"disable","",true);s=loader.snapshot();check(s.resources.empty()&&std::none_of(s.mods.begin(),s.mods.end(),[](auto& m){return m.loaded||m.enabled;}),"disable all clears running resources");
            run(loader,"enable","a");run(loader,"unload","",true);s=loader.snapshot();check(s.resources.empty()&&std::none_of(s.mods.begin(),s.mods.end(),[](auto& m){return m.loaded||m.enabled;}),"unload all clears running DLLs");check(s.message=="全部插件已卸载","batch completion status");
            for(auto& [name,hash]:hashes)check(fs::exists(data/L"mods"/wide(name)/L"mod.log")&&sha256(readFile(root/(wide(name)+L".wimod")))==hash,"unload preserves .wimod bytes and plugin logs");
            check(fs::exists(data/L"mods/a/settings.xml")&&fs::exists(data/L"mod-cache/a"),"unload preserves configuration and extracted cache");
            run(loader,"enable","a");
        }
        {
            ModLoader restarted(root,true,data);wait(restarted);auto s=restarted.snapshot();check(info(s,"a").enabled&&!info(s,"b").enabled,"startup restores enabled choices");
            auto input=base/L"installed.wimod";writeAtomic(input,zip({{"mod.json",manifest("installed",R"(,"name":"安装验证")")},{"bin/example.dll",dll}}));
            check(restarted.install(input),"add .wimod queued");wait(restarted);check(info(restarted.snapshot(),"installed").valid&&!info(restarted.snapshot(),"installed").enabled,"add installs single package and defaults disabled");
            check(restarted.install(input),"duplicate import queued");wait(restarted);check(restarted.snapshot().message.find("重复")!=std::string::npos,"duplicate ID import rejected without overwrite");
        }
        check(readFile(data/L"mods/a/mod.log").find("示例 onUnload")!=std::string::npos,"per-module lifecycle log written");
    }catch(const std::exception& e){check(false,e.what());}catch(...){check(false,"unexpected exception");}
    report<<"Passed="<<passed<<" Failed="<<failed<<"\nIsolated package root="<<utf8(root.wstring())<<'\n';writeAtomic(output/L"mod-tests.txt",report.str());return failed?1:0;
}
}


