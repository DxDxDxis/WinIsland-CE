#pragma once
#include "core.h"
namespace wi {
struct AnimationValues { float x=0,y=0,width=0,height=0,radius=0,opacity=1,scale=1,offsetX=0,offsetY=0,contentOpacity=1,lyricOpacity=1,noticeOpacity=1; };
class AnimationRuntime {
    std::map<std::string,AnimationValues> scripts; std::string active; bool paused=false,reduced=false;
public:
    bool load(const std::string& name,const std::string& source,std::string* error=nullptr);
    bool start(const std::string& name); void cancel(); void pause(); void resume(); void reverse();
    void setReducedMotion(bool value){reduced=value;} bool tick(double progress,AnimationValues& out) const;
    bool empty() const{return scripts.empty();}
};
}
