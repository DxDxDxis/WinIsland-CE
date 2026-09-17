#include "../third_party/quickjs-ng/quickjs.h"
#include <cstdio>
#include <cstring>
static const char gsap[] =
#include "gsap_source.inc"
;
int main(){setvbuf(stdout,nullptr,_IONBF,0);JSRuntime* r=JS_NewRuntime();JS_SetMemoryLimit(r,16*1024*1024);JSContext* c=JS_NewContext(r);auto eval=[&](const char* s){JSValue v=JS_Eval(c,s,(int)strlen(s),"probe.js",JS_EVAL_TYPE_GLOBAL);if(JS_IsException(v)){JSValue e=JS_GetException(c);const char* x=JS_ToCString(c,e);printf("ERR %s\n",x?x:"");JS_FreeCString(c,x);JS_FreeValue(c,e);return false;}JS_FreeValue(c,v);return true;};eval("globalThis.window=globalThis;globalThis.setTimeout=function(){};globalThis.clearTimeout=function(){};globalThis.requestAnimationFrame=function(){};globalThis.cancelAnimationFrame=function(){};");eval(gsap);eval("var o={x:0}; var t=gsap.to(o,{x:100,duration:1,ease:'power2.out',paused:true});");for(int i=0;i<=4;i++){char b[128];sprintf_s(b,"t.time(%f); globalThis.v=o.x;",i*.25);eval(b);JSValue v=JS_GetPropertyStr(c,JS_GetGlobalObject(c),"v");double d=0;JS_ToFloat64(c,&d,v);printf("%f\n",d);JS_FreeValue(c,v);}JSValue g=JS_GetGlobalObject(c);JS_FreeValue(c,g);JS_RunGC(r);JS_FreeContext(c);for(int i=0;i<8;++i)JS_RunGC(r);JS_FreeRuntime(r);}




