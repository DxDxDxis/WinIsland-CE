#pragma once
#include "core.h"
#include "community_api.h"
#include "media_runtime.h"
namespace wi {
class CommunityRuntime {
public:
 struct Owner;
 struct Service; struct Task; struct Listener; struct Event;
 CommunityRuntime(); ~CommunityRuntime();
 std::shared_ptr<Owner> createOwner(const std::string&,const fs::path&,const fs::path&);
 WiCommunityApi api(Owner&);
 void beginStop(Owner&); bool drained(Owner&); void finishStop(Owner&);void attachHostLifetime(Owner&,const WiExternalLifetime&);
 void pump();
 std::vector<std::string> consumers(const std::string&);
 std::string fault(Owner&);
 bool ready(const std::string&,const std::string&,uint32_t,uint32_t);
 bool decode(const fs::path&,uint32_t,const std::atomic<bool>&,MediaFrame&,std::string&);
 std::shared_ptr<PluginStream> stream(const fs::path&,uint32_t,const std::atomic<bool>&);
private:
 struct Impl; std::unique_ptr<Impl> impl;
};
}
