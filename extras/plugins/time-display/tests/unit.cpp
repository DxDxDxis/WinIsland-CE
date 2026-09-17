#include "../src/time-display.cpp"
#include <map>
#include <string>
#include <cstdlib>

static void check(bool ok, const char* label) {
    printf("%s %s\n",ok?"PASS":"FAIL",label); if (!ok) exit(1);
}
struct Host {
    std::map<uint64_t,WinIslandResource> items;
    uint64_t next=0;
    int failAt=0, calls=0;
    static void log(void*,const char*) {}
    static uint64_t add(void* ctx,const WinIslandResource* r) {
        auto& s=*(Host*)ctx; ++s.calls;
        if(s.failAt==s.calls) return 0;
        check(r->kind==WI_TIMER||r->kind==WI_EVENT,"only nonvisual resources");
        auto id=++s.next; s.items[id]=*r; return id;
    }
    static int remove(void* ctx,uint64_t h) {return (int)((Host*)ctx)->items.erase(h);}
    WinIslandHostApi table() {WinIslandHostApi h{};h.size=sizeof(h);h.version=WINISLAND_MOD_ABI;h.context=this;h.log=log;h.add=add;h.remove=remove;return h;}
};
int main() {
    SYSTEMTIME t{};t.wYear=2026;t.wMonth=9;t.wDay=21;t.wHour=9;t.wMinute=5;
    char text[32]{};check(formatTime(t,text)&&!strcmp(text,"2026.9.21 09:05"),"natural date and zero padded time");
    for(auto value:{SYSTEMTIME{2026,12,0,31,23,59,0,0},SYSTEMTIME{2027,1,0,1,0,0,0,0},SYSTEMTIME{2028,2,0,29,12,0,0,0},SYSTEMTIME{2028,3,0,1,0,0,0,0}})
        check(formatTime(value,text),"valid year/month/day boundary");
    t.wMonth=0;check(!formatTime(t,text)&&text[0]==0,"invalid time cleared");
    check(!WinIsland_CreateMod(nullptr),"no host rejected");
    Host h;auto api=h.table();api.version=1;check(!WinIsland_CreateMod(&api),"wrong ABI rejected");api=h.table();
    for(int cycle=0;cycle<3;++cycle) {
        auto* p=WinIsland_CreateMod(&api);check(p&&p->onLoad(p->context,"")==0,"load");
        check(p->onEnable(p->context,"")==0&&h.items.size()==3,"enable three resources");
        p->onEnable(p->context,"");check(h.items.size()==3,"repeated enable idempotent");
        for(auto& [id,r]:h.items){if(r.kind==WI_TIMER)check(r.intervalMs==30000,"30s timer");r.callback(r.context,"");}
        auto& mod=*(ClockMod*)p->context;check(strlen(mod.localTime)>=14,"actual local time formatted");
        p->onDisable(p->context,"");check(h.items.empty(),"disable removes all resources");
        p->onEnable(p->context,"");h.items.clear(); // real loader revokes before onDisable
        p->onDisable(p->context,"");p->onUnload(p->context,"");p->destroy(p->context);
        check(h.items.empty(),"host pre-revocation and unload safe");
    }
    for(int fail=1;fail<=3;++fail){h.calls=0;h.failAt=fail;auto* p=WinIsland_CreateMod(&api);check(p->onEnable(p->context,"")!=0&&h.items.empty(),"registration failure rolls back");p->onUnload(p->context,"");p->destroy(p->context);}
}
