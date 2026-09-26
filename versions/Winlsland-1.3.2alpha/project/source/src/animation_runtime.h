#pragma once
#include "core.h"
namespace wi {
struct AnimationValues { float x=0,y=0,width=0,height=0,radius=0,opacity=1,scale=1,offsetX=0,offsetY=0,contentOpacity=1,lyricOpacity=1,noticeOpacity=1; };
class AnimationRuntime {
    struct Impl; std::unique_ptr<Impl> impl;
public:
    AnimationRuntime(); ~AnimationRuntime();
    AnimationRuntime(const AnimationRuntime&)=delete; AnimationRuntime& operator=(const AnimationRuntime&)=delete;
    bool load(const std::string& name,const std::string& source,std::string* error=nullptr);
    bool start(const std::string& name,const AnimationValues& from,const AnimationValues& to,double duration=0.24);
    void cancel(); void pause(); void resume(); void reverse();
    void setReducedMotion(bool value); void setPerformanceMode(uint32_t mode);
    bool tick(double progress,AnimationValues& out); bool active() const;
    static bool selfTest(const fs::path& output);
};
}

