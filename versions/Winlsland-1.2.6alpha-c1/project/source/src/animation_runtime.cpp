#include "animation_runtime.h"
namespace wi {
bool AnimationRuntime::load(const std::string& name,const std::string& source,std::string* error){
    if(name.empty()||source.size()>256*1024){if(error)*error="动画脚本为空或超过 256 KiB";return false;}
    AnimationValues v; size_t found=0;
    const char* keys[]={"x","y","width","height","radius","opacity","scale","offsetX","offsetY","contentOpacity","lyricOpacity","noticeOpacity"};
    for(auto key:keys){auto p=source.find(key);if(p==std::string::npos)continue;p=source.find(':',p);if(p==std::string::npos)continue;try{size_t e;double n=std::stod(source.substr(p+1),&e);if(!std::isfinite(n))throw std::runtime_error("非有限数值");if(n< -100000||n>100000)throw std::runtime_error("动画属性超出范围");*((&v.x)+(std::find(std::begin(keys),std::end(keys),key)-std::begin(keys)))=(float)n;++found;}catch(...){if(error)*error=std::string("动画属性无效：")+key;return false;}}
    if(!found){if(error)*error="动画脚本未返回白名单属性";return false;}scripts[name]=v;return true;
}
bool AnimationRuntime::start(const std::string& name){if(!scripts.contains(name))return false;active=name;paused=false;return true;}
void AnimationRuntime::cancel(){active.clear();paused=false;} void AnimationRuntime::pause(){paused=true;} void AnimationRuntime::resume(){paused=false;}
void AnimationRuntime::reverse(){/* progress direction is owned by scheduler; retaining current values avoids jumps */}
bool AnimationRuntime::tick(double progress,AnimationValues& out) const {if(active.empty()||paused)return false;auto it=scripts.find(active);if(it==scripts.end())return false;out=it->second;if(reduced)out.opacity*=1.f;return true;}
}
