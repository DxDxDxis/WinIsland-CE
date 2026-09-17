#pragma once
#include "core.h"
namespace wi {
struct ModManifest {
    std::string id,name="未命名模组",description="---",author="作者并未填写",version="版本号未填写",entry,gameVersion;
    std::vector<std::string> dependencies;
    std::map<std::string,std::string> dependencyVersions;
};
bool modSafeId(const std::string&);
std::string modTrim(const std::string&);
void parseModManifest(const std::string&,ModManifest&);
bool modVersionMatches(const std::string& version,const std::string& range);
class ModPackage {
    struct Impl;std::unique_ptr<Impl> impl;
public:
    explicit ModPackage(const fs::path&);
    ~ModPackage();
    ModManifest manifest;
    std::string hash,signature="未签名";
    fs::path path;
    void validate();
    fs::path extract(const fs::path& cacheRoot);
    const std::string& bytes()const;
};
}

