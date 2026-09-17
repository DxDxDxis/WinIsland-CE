#include "animation_runtime.h"
#include "../third_party/quickjs-ng/quickjs.h"
#include <chrono>
namespace wi {
namespace {
static const char kGsap[] =
#include "../animation/gsap_source.inc"
;
static const char kBootstrap[] = R"JS(globalThis.window=globalThis;globalThis.self=globalThis;globalThis.setTimeout=function(){};globalThis.clearTimeout=function(){};globalThis.requestAnimationFrame=function(){};globalThis.cancelAnimationFrame=function(){};globalThis.performance={now:function(){return 0;}};)JS";
static const char* keys[] = {"x","y","width","height","radius","opacity","scale","offsetX","offsetY","contentOpacity","lyricOpacity","noticeOpacity"};
static double clockSeconds(){return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
}
struct AnimationRuntime::Impl {
    JSRuntime* runtime=nullptr; JSContext* context=nullptr; JSValue function=JS_UNDEFINED, object=JS_UNDEFINED, tween=JS_UNDEFINED;
    std::string activeName,error; bool paused=false,reversed=false,reduced=false; uint32_t performanceMode=0; double duration=.24,deadline=0;
    static int interrupt(JSRuntime*,void* opaque){auto* p=(Impl*)opaque;return p->deadline>0&&clockSeconds()>p->deadline;}
    Impl(){runtime=JS_NewRuntime();if(runtime){JS_SetMemoryLimit(runtime,16*1024*1024);JS_SetMaxStackSize(runtime,512*1024);JS_SetInterruptHandler(runtime,&interrupt,this);context=JS_NewContext(runtime);}}
    ~Impl(){if(context){JSValue global=JS_GetGlobalObject(context);JS_SetPropertyStr(context,global,"__wiAnimation",JS_UNDEFINED);JS_SetPropertyStr(context,global,"__wiObject",JS_UNDEFINED);JS_SetPropertyStr(context,global,"__wiTween",JS_UNDEFINED);JS_FreeValue(context,global);JS_FreeContext(context);}if(runtime)JS_FreeRuntime(runtime);}
    bool eval(const char* source,size_t size,const char* name){if(!context)return false;deadline=clockSeconds()+.05;JSValue v=JS_Eval(context,source,size,name,JS_EVAL_TYPE_GLOBAL);deadline=0;if(JS_IsException(v)){JSValue e=JS_GetException(context);const char* s=JS_ToCString(context,e);error=s?s:"JavaScript exception";if(s)JS_FreeCString(context,s);JS_FreeValue(context,e);JS_FreeValue(context,v);return false;}JS_FreeValue(context,v);return true;}
    JSValue number(double n){return JS_NewFloat64(context,n);}
    JSValue objectFrom(const AnimationValues& v){JSValue o=JS_NewObject(context);for(int i=0;i<12;++i){float value=*((&v.x)+i);JS_SetPropertyStr(context,o,keys[i],number(value));}return o;}
    bool property(JSValue o,const char* key,float& out){
        JSValue v=JS_GetPropertyStr(context,o,key);
        // Animation scripts may animate a subset of the host properties. An
        // omitted field keeps the caller's default/current value; a present
        // field still must be finite and bounded.
        if(JS_IsUndefined(v)){JS_FreeValue(context,v);return true;}
        double n=0;bool ok=!JS_IsException(v)&&JS_ToFloat64(context,&n,v)==0&&std::isfinite(n)&&n>=-100000&&n<=100000;
        JS_FreeValue(context,v);if(ok)out=(float)n;return ok;
    }
};
AnimationRuntime::AnimationRuntime():impl(std::make_unique<Impl>()){}
AnimationRuntime::~AnimationRuntime()=default;
bool AnimationRuntime::load(const std::string& name,const std::string& source,std::string* error){if(name.empty()||source.size()>256*1024||!impl->context){if(error)*error="脚本为空、过大或引擎不可用";return false;}impl->error.clear();if(!impl->eval(kBootstrap,sizeof(kBootstrap)-1,"bootstrap.js")||!impl->eval(kGsap,sizeof(kGsap)-1,"gsap.min.js")||!impl->eval(source.data(),source.size(),"mod-animation.js")){if(error)*error=impl->error;return false;}JSValue global=JS_GetGlobalObject(impl->context);JSValue fn=JS_GetPropertyStr(impl->context,global,"winislandAnimation");JS_FreeValue(impl->context,global);if(!JS_IsFunction(impl->context,fn)){JS_FreeValue(impl->context,fn);if(error)*error="脚本必须定义 winislandAnimation(state,from,to,duration)";return false;}JS_FreeValue(impl->context,impl->function);impl->function=fn;impl->activeName=name;return true;}
bool AnimationRuntime::start(const std::string& name,const AnimationValues& from,const AnimationValues& to,double duration){if(name!=impl->activeName||JS_IsUndefined(impl->function))return false;impl->duration=std::clamp(duration,.01,5.0);impl->paused=false;impl->reversed=false;JSValue state=JS_NewString(impl->context,name.c_str());JSValue a[4]={state,impl->objectFrom(from),impl->objectFrom(to),JS_NewFloat64(impl->context,impl->duration)};impl->deadline=clockSeconds()+.05;JSValue result=JS_Call(impl->context,impl->function,JS_UNDEFINED,4,a);impl->deadline=0;for(auto& v:a)JS_FreeValue(impl->context,v);if(JS_IsException(result)){JS_FreeValue(impl->context,result);cancel();return false;}JSValue obj=JS_GetPropertyStr(impl->context,result,"object"),tween=JS_GetPropertyStr(impl->context,result,"tween");JS_FreeValue(impl->context,result);if(!JS_IsObject(obj)||!JS_IsObject(tween)){JS_FreeValue(impl->context,obj);JS_FreeValue(impl->context,tween);cancel();return false;}JS_FreeValue(impl->context,impl->object);JS_FreeValue(impl->context,impl->tween);impl->object=obj;impl->tween=tween;return true;}
void AnimationRuntime::cancel(){if(!impl->context)return;JS_FreeValue(impl->context,impl->object);JS_FreeValue(impl->context,impl->tween);impl->object=JS_UNDEFINED;impl->tween=JS_UNDEFINED;impl->paused=false;}
void AnimationRuntime::pause(){impl->paused=true;} void AnimationRuntime::resume(){impl->paused=false;} void AnimationRuntime::reverse(){impl->reversed=!impl->reversed;}
void AnimationRuntime::setReducedMotion(bool value){impl->reduced=value;} void AnimationRuntime::setPerformanceMode(uint32_t mode){impl->performanceMode=mode;}
bool AnimationRuntime::tick(double progress,AnimationValues& out){if(impl->paused||JS_IsUndefined(impl->object)||JS_IsUndefined(impl->tween)||!impl->context)return false;progress=std::clamp(progress,0.,1.);if(impl->reversed)progress=1-progress;if(impl->reduced)progress=progress>=1?1:0;double t=progress*impl->duration;JSValue arg=JS_NewFloat64(impl->context,t);JSAtom atom=JS_NewAtom(impl->context,"time");JSValue r=JS_Invoke(impl->context,impl->tween,atom,1,&arg);JS_FreeAtom(impl->context,atom);JS_FreeValue(impl->context,arg);if(JS_IsException(r)){JS_FreeValue(impl->context,r);cancel();return false;}JS_FreeValue(impl->context,r);for(int i=0;i<12;++i)if(!impl->property(impl->object,keys[i],*((&out.x)+i))){cancel();return false;}return true;}
bool AnimationRuntime::active() const{return impl&&impl->context&&!JS_IsUndefined(impl->object)&&!impl->paused;}
bool AnimationRuntime::selfTest(const fs::path& output){AnimationRuntime r;std::string error;std::string script=R"JS(function winislandAnimation(state,from,to,duration){var o={};for(var k in from)o[k]=from[k];var t=gsap.to(o,{duration:duration,paused:true,ease:'power2.out',x:to.x,opacity:to.opacity});return {object:o,tween:t};})JS";bool ok=r.load("self-test",script,&error);AnimationValues a,b,o;a.x=0;b.x=100;a.opacity=0;b.opacity=1;ok=ok&&r.start("self-test",a,b,.2);r.tick(.25,o);double first=o.x;r.tick(.75,o);double second=o.x;ok=ok&&second>first&&second<101;r.reverse();r.tick(.75,o);double reverse=o.x;ok=ok&&reverse<second;r.pause();double paused=reverse;r.tick(.5,o);ok=ok&&o.x==paused;r.resume();r.cancel();ok=ok&&!r.active();std::ofstream f(output);f<<"quickjs_gsap="<<(ok?"PASS":"FAIL")<<"\nfirst="<<first<<" second="<<second<<" reverse="<<reverse<<" paused="<<paused<<"\n"<<(error.empty()?"":error);return ok;}
}
