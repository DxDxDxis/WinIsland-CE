#pragma once
#include "core.h"
#include "mod_api.h"
#include "mod_package.h"
#include "scene_store.h"
namespace wi {
struct ModResourceView {
    uint64_t handle=0; uint32_t kind=0;
    std::string owner,key,label,value; float alpha=1;
    uint32_t settingType=0;double minimum=0,maximum=0;std::vector<std::string> choices;
};
struct ModInfo {
    std::string id,name="未命名模组",description="---",author="作者并未填写",version="版本号未填写";
    std::string status="未加载",error;
    std::vector<std::string> dependencies;
    std::map<std::string,std::string> dependencyVersions;
    std::string packageFile,packageHash,signature;
    bool loaded=false,enabled=false,valid=false;
    uint64_t loads=0,unloads=0;
};
struct ModSnapshot {
    uint64_t operation=0, completedOperation=0;
    std::string operationError;
    ScenePlan scene;
    uint64_t revision=0;
    bool busy=false;
    std::string message;
    std::vector<ModInfo> mods;
    std::vector<ModResourceView> resources;
    double replacement(const std::string& key,double fallback) const;
    std::string text(const std::string& key,std::string fallback) const;
};
class ModLoader {
    struct Impl; std::unique_ptr<Impl> impl;
public:
    explicit ModLoader(fs::path root, bool autoStart=true, fs::path dataRoot={});
    ~ModLoader();
    ModSnapshot snapshot() const;
    bool snapshotSince(uint64_t revision, ModSnapshot& out) const;
    // Reject simultaneous operations; UI remains responsive during worker calls.
    bool request(std::string action, std::string id={}, bool cascade=false);
    bool invoke(uint64_t handle, std::string value={});
    bool emit(std::string topic,std::string payload={});
    std::string replacementText(const std::string& key,std::string fallback={}) const;
    bool install(fs::path folder);
    std::vector<std::string> affected(const std::string& id) const;
    fs::path directory() const;
    fs::path logPath(const std::string& id) const;
    bool busy() const;
    void updateScene(WiSceneSnapshot, std::vector<SceneNode>, std::vector<SceneNode> visible={});
    bool sceneInput(WiInputEvent);
    std::vector<int> takeSceneActions();
};
void openModManager(HWND parent, ModLoader& loader);
void closeModManager();
bool modManagerMessage(MSG& message);
int modSystemTest(const fs::path& output);
int modUiTest(ModLoader&,const fs::path& output);
}

