#pragma once
#include "core.h"
#include "open_api.h"
#include "scene_store.h"
namespace wi {
class OpenRuntime {
public:
 struct Owner;struct Impl;
 OpenRuntime();~OpenRuntime();
 std::shared_ptr<Owner> owner(std::string,const fs::path&,const fs::path&);
 WiOpenApi api(Owner&);WiExternalLifetime lifetime(Owner&);
 void retire(Owner&);void pump();uint64_t revision()const;
 void layout(HWND,float,float,const std::vector<SceneNode>&);
 WiHostPacket route(WiHostPacket);bool enqueue(WiHostPacket);
 std::vector<WiHostPacket> take();
 static int test(const fs::path&);
private:std::unique_ptr<Impl> impl;
};
}
