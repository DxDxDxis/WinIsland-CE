#include "scene_store.h"
#include <cstring>
namespace wi {
WiPropertyValue sceneNumber(double a,double b,double c,double d){WiPropertyValue v{};v.size=sizeof(v);v.version=1;v.number[0]=a;v.number[1]=b;v.number[2]=c;v.number[3]=d;return v;}
WiPropertyValue sceneText(const std::string& text){auto v=sceneNumber(0);auto n=std::min(text.size(),sizeof(v.text)-1);memcpy(v.text,text.data(),n);return v;}
double SceneNode::n(uint32_t p,int c,double fallback)const{auto it=values.find(p);return it==values.end()?fallback:it->second.number[c];}
std::string SceneNode::text(uint32_t p)const{auto it=values.find(p);return it==values.end()?"":it->second.text;}
WiElement SceneNode::parent()const{auto it=values.find(WI_PARENT);return it==values.end()?0:it->second.handle;}
WiElementInfo SceneNode::info()const{WiElementInfo v{};v.size=sizeof(v);v.version=1;v.type=(uint32_t)n(WI_ELEMENT_TYPE,0,type);v.visible=n(WI_VISIBLE,0,1)!=0;v.id=id;v.parent=parent();strncpy_s(v.owner,owner.c_str(),_TRUNCATE);strncpy_s(v.key,key.c_str(),_TRUNCATE);v.x=(float)n(WI_RECT);v.y=(float)n(WI_RECT,1);v.width=(float)n(WI_RECT,2);v.height=(float)n(WI_RECT,3);v.opacity=(float)n(WI_OPACITY,0,1);v.z=(int)n(WI_Z_ORDER);return v;}
SceneNode sceneNode(WiElement id,const std::string& key,uint32_t type,float x,float y,float w,float h){SceneNode n;n.id=id;n.key=key;n.type=type;n.values[WI_RECT]=sceneNumber(x,y,w,h);n.values[WI_VISIBLE]=sceneNumber(1);n.values[WI_OPACITY]=sceneNumber(1);n.values[WI_FONT_SIZE]=sceneNumber(14);n.values[WI_TEXT_COLOR]=sceneNumber(1,1,1,1);return n;}
SceneNode ScenePlan::resolve(SceneNode node)const{
    std::map<uint32_t,const SceneEdit*> winners;
    for(auto& e:edits)if(e.target==node.id){auto& best=winners[e.property];if(!best||std::pair{e.priority,e.sequence}>std::pair{best->priority,best->sequence})best=&e;}
    for(auto& [p,e]:winners)node.values[p]=e->value;
    return node;
}
bool SceneStore::exists(WiElement id)const{return std::any_of(pending.created.begin(),pending.created.end(),[&](auto& n){return n.id==id;})||std::any_of(base.begin(),base.end(),[&](auto& n){return n.id==id;});}
SceneNode* SceneStore::baseNode(WiElement id){for(auto& n:base)if(n.id==id)return &n;return nullptr;}
SceneNode SceneStore::get(WiElement id)const{for(auto& n:pending.created)if(n.id==id)return pending.resolve(n);for(auto& n:base)if(n.id==id)return pending.resolve(n);return {};}
int SceneStore::create(const std::string& owner,uint32_t type,const char* key,WiElement parent,WiElement* out){
    if(!out||!key||!memchr(key,0,128)||!*key||type<WI_CONTAINER||type>WI_CUSTOM_DRAW)return WI_INVALID;
    if(parent&&!exists(parent))return WI_NOT_FOUND;
    if(pending.created.size()>=1024||next>=0x7fffffff)return WI_LIMIT;
    for(auto& n:pending.created)if(n.owner==owner&&n.key==key)return WI_CONFLICT;
    auto n=sceneNode(next++,key,type,0,0,100,24);n.owner=owner;n.values[WI_PARENT]=sceneNumber(0);n.values[WI_PARENT].handle=parent;
    pending.created.push_back(n);*out=n.id;return WI_OK;
}
int SceneStore::set(const std::string& owner,WiElement target,uint32_t p,const WiPropertyValue* v,int32_t priority){
    if(!exists(target))return WI_NOT_FOUND;
    if(!v||v->size<sizeof(*v)||v->version!=1)return WI_VERSION;
    if(p<WI_RECT||p>WI_MEDIA_CROP)return WI_UNSUPPORTED;
    if(p==WI_MEDIA_FIT&&(v->number[0]<0||v->number[0]>2))return WI_INVALID;
    if(p==WI_MEDIA_CROP&&(v->number[0]<0||v->number[1]<0||v->number[2]<=0||v->number[3]<=0||v->number[0]+v->number[2]>1||v->number[1]+v->number[3]>1))return WI_INVALID;
    if(target==1&&(p==WI_SCALE||p==WI_ROTATION||p==WI_SHADOW))return WI_UNSUPPORTED;
    if((p==WI_POSITION||p==WI_EXPANDED)&&target!=1)return WI_UNSUPPORTED;
    if(p==WI_SHADOW&&v->number[2]!=0)return WI_UNSUPPORTED; // soft blur is not implemented; offset solid shadow only

    for(auto d:v->number)if(!std::isfinite(d)||std::abs(d)>1000000)return WI_INVALID;
    if(!memchr(v->text,0,sizeof(v->text)))return WI_INVALID;
    if((p==WI_OPACITY||p==WI_VALUE)&&(v->number[0]<0||v->number[0]>1))return WI_INVALID;
    if((p==WI_BACKGROUND||p==WI_BORDER_COLOR||p==WI_TEXT_COLOR)&&std::any_of(std::begin(v->number),std::end(v->number),[](double d){return d<0||d>1;}))return WI_INVALID;
    if(p==WI_RECT&&target==1&&(v->number[0]!=0||v->number[1]!=0))return WI_INVALID;
    if(p==WI_RECT&&(v->number[2]<0||v->number[3]<0||v->number[2]>16384||v->number[3]>16384))return WI_INVALID;
    if(p==WI_SCALE&&(v->number[0]<=0||v->number[1]<=0||v->number[0]>32||v->number[1]>32))return WI_INVALID;
    if(p==WI_FONT_SIZE&&(v->number[0]<1||v->number[0]>512))return WI_INVALID;
    if(p==WI_ELEMENT_TYPE&&(v->number[0]<WI_CONTAINER||v->number[0]>WI_CUSTOM_DRAW))return WI_INVALID;
    if(p==WI_SIZE_MODE&&(target!=1||v->number[0]<0||v->number[0]>3))return WI_INVALID;
    if(p==WI_PARENT){
        if(v->handle&&!exists(v->handle))return WI_NOT_FOUND;
        std::set<WiElement> visited{target};auto id=v->handle;
        while(id){if(!visited.insert(id).second)return WI_CONFLICT;id=get(id).parent();}
    }
    if((p==WI_VISIBLE||p==WI_RECEIVE_INPUT||p==WI_BLOCK_INPUT||p==WI_CLIP||p==WI_LAYOUT||p==WI_EXPANDED||p==WI_ENABLED)&&(v->number[0]!=0&&v->number[0]!=1))return WI_INVALID;
    if(p==WI_FONT_WEIGHT&&(v->number[0]<1||v->number[0]>999))return WI_INVALID;
    if(p==WI_RADIUS&&v->number[0]<0)return WI_INVALID;
    if(p==WI_BORDER_WIDTH&&(v->number[0]<0||v->number[0]>512))return WI_INVALID;
    if((p==WI_MIN_SIZE||p==WI_MAX_SIZE)&&(v->number[0]<0||v->number[1]<0))return WI_INVALID;
    if(p==WI_ALIGN&&(v->number[0]<0||v->number[0]>2))return WI_INVALID;
    if(pending.edits.size()>=32768)return WI_LIMIT;
    auto before=get(target);auto old=before.values.contains(p)?before.values[p]:sceneNumber(0);
    clear(owner,target,p);pending.edits.push_back({owner,target,p,*v,priority,++sequence,old});return WI_OK;
}
int SceneStore::clear(const std::string& owner,WiElement id,uint32_t p){if(!exists(id))return WI_NOT_FOUND;std::erase_if(pending.edits,[&](auto& e){return e.owner==owner&&e.target==id&&e.property==p;});return WI_OK;}
int SceneStore::remove(const std::string& owner,WiElement id){
    if(!exists(id))return WI_NOT_FOUND;
    auto it=std::find_if(pending.created.begin(),pending.created.end(),[&](auto& n){return n.id==id&&n.owner==owner;});
    if(it==pending.created.end()){auto v=sceneNumber(0);return set(owner,id,WI_VISIBLE,&v,0);}
    pending.created.erase(it);std::erase_if(pending.edits,[&](auto& e){return e.target==id;});return WI_OK;
}
void SceneStore::revoke(const std::string& owner){std::set<WiElement> removed;for(auto& n:pending.created)if(n.owner==owner)removed.insert(n.id);std::erase_if(pending.created,[&](auto& n){return n.owner==owner;});std::erase_if(pending.edits,[&](auto& e){return e.owner==owner||removed.contains(e.target);});commit(owner);}
void SceneStore::commit(const std::string& owner){
    if(owner.empty()){committed=pending;return;}
    std::set<WiElement> removed;for(auto& n:committed.created)if(n.owner==owner&&std::none_of(pending.created.begin(),pending.created.end(),[&](auto& p){return p.id==n.id;}))removed.insert(n.id);
    std::erase_if(committed.created,[&](auto& n){return n.owner==owner;});std::erase_if(committed.edits,[&](auto& e){return e.owner==owner||removed.contains(e.target);});
    for(auto& n:pending.created)if(n.owner==owner)committed.created.push_back(n);
    for(auto& e:pending.edits)if(e.owner==owner)committed.edits.push_back(e);
}
int sceneStoreTest(const fs::path& path){SceneStore s;std::ostringstream log;int failed=0;auto check=[&](bool b,const char* name){log<<(b?"PASS ":"FAIL ")<<name<<'\n';if(!b)++failed;};s.base.push_back(sceneNode(1,"island",WI_CONTAINER,0,0,180,30));
    WiElement a=0,b=0;check(s.create("a",WI_TEXT,"label",1,&a)==0,"create generic text");check(s.create("b",WI_SHAPE,"shape",1,&b)==0,"second owner");auto va=sceneNumber(240,0,0,0),vb=sceneNumber(320);check(s.set("a",1,WI_RADIUS,&va,0)==0&&s.set("b",1,WI_RADIUS,&vb,0)==0&&s.get(1).n(WI_RADIUS)==320,"last commit layer wins");s.revoke("a");check(s.get(1).n(WI_RADIUS)==320&&s.exists(b)&&!s.exists(a),"remove middle owner preserves newer edit and foreign node");s.revoke("b");check(s.get(1).n(WI_RADIUS)==0&&!s.exists(b),"restore live host baseline");check(s.set("a",a,WI_RADIUS,&va,0)==WI_NOT_FOUND,"stale handle fails");WiElement c; s.create("a",WI_CONTAINER,"parent",1,&c);auto pv=sceneNumber(0);pv.handle=c;check(s.set("a",1,WI_PARENT,&pv,0)==WI_CONFLICT,"parent cycle rejected");va.number[0]=NAN;check(s.set("a",1,WI_OPACITY,&va,0)==WI_INVALID,"NaN rejected");check(s.committed.created.empty(),"uncommitted scene invisible to renderer");s.commit();check(s.committed.created.size()==1,"commit publishes complete copied scene");writeAtomic(path,log.str());return failed?1:0;}
}

