#pragma once
#include "core.h"
#include "scene_api.h"
namespace wi {
struct SceneNode {
    WiElement id=0; std::string owner,key; uint32_t type=WI_CONTAINER;
    std::map<uint32_t,WiPropertyValue> values;
    std::vector<WiDrawCommand> draw;
    std::vector<uint8_t> pixels; uint32_t pixelWidth=0,pixelHeight=0;
    double n(uint32_t p,int component=0,double fallback=0) const;
    std::string text(uint32_t p) const;
    WiElement parent() const;
    WiElementInfo info() const;
};
struct SceneEdit { std::string owner; WiElement target; uint32_t property; WiPropertyValue value; int32_t priority; uint64_t sequence; WiPropertyValue previous{}; };
struct ScenePlan {
    std::vector<SceneNode> created;
    std::vector<SceneEdit> edits;
    SceneNode resolve(SceneNode node) const;
};
WiPropertyValue sceneNumber(double a,double b=0,double c=0,double d=0);
WiPropertyValue sceneText(const std::string&);
SceneNode sceneNode(WiElement id,const std::string& key,uint32_t type,float x,float y,float w,float h);
// Worker-owned mutations; UI receives copies through ModSnapshot. No DLL pointers here.
class SceneStore {
    uint64_t next=1000000,sequence=0;
public:
    ScenePlan pending,committed;
    std::vector<SceneNode> base,displayed;
    WiSceneSnapshot state{sizeof(WiSceneSnapshot),WI_SCENE_ABI};
    SceneNode* baseNode(WiElement);
    bool exists(WiElement) const;
    SceneNode get(WiElement) const;
    int create(const std::string&,uint32_t,const char*,WiElement,WiElement*);
    int set(const std::string&,WiElement,uint32_t,const WiPropertyValue*,int32_t);
    int clear(const std::string&,WiElement,uint32_t);
    int remove(const std::string&,WiElement);
    void revoke(const std::string&);
    void commit(const std::string& owner={});
};
int sceneStoreTest(const fs::path&);
}

