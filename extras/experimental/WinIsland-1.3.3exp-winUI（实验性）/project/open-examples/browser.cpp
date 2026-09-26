#define NOMINMAX
#include <windows.h>
#include <wrl.h>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include "../sdk/mod_api.h"
#include "../sdk/open_api.h"
#include "third_party/webview2/build/native/include/WebView2.h"
using Microsoft::WRL::ComPtr;using Microsoft::WRL::Callback;
struct Browser{
 IWinIslandMod life{};const WinIslandHostApi* host;WiOpenApi open{};WiCommunityApi community{};WiSceneApi scene{};WiView view=0;WiElement node=0;
 std::wstring data;std::atomic<bool> closing=false,destroyed=false;std::atomic<int> pending=0;std::atomic<uint64_t> fence=0;HWND parent=nullptr;ComPtr<ICoreWebView2Environment> environment;ComPtr<ICoreWebView2Controller> controller;ComPtr<ICoreWebView2> web;EventRegistrationToken messageToken{};
 void log(const char* name,const std::string& value){std::ofstream(std::filesystem::path(data)/name,std::ios::binary)<<value;}
 void barrier(){uint64_t ticket=0;open.viewBarrier(open.context,&ticket);fence=ticket;}
 static void stop(void* c){((Browser*)c)->closing=true;}static int joined(void* c){auto& b=*(Browser*)c;return b.destroyed&&b.pending==0&&(!b.fence||b.open.viewBarrierReady(b.open.context,b.fence)==0);}
 static int event(void* c,WiViewEvent* e){auto& b=*(Browser*)c;
  if(e->kind==WI_VIEW_CREATE){b.parent=(HWND)e->parent;++b.pending;auto result=CreateCoreWebView2EnvironmentWithOptions(nullptr,(b.data+L"\\web-profile").c_str(),nullptr,Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>([&b](HRESULT hr,ICoreWebView2Environment* env)->HRESULT{
    if(SUCCEEDED(hr)&&env&&!b.closing){b.environment=env;++b.pending;HRESULT start=env->CreateCoreWebView2Controller(b.parent,Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>([&b](HRESULT status,ICoreWebView2Controller* ctl)->HRESULT{
      if(SUCCEEDED(status)&&ctl&&!b.closing){b.controller=ctl;ctl->get_CoreWebView2(&b.web);RECT rect;GetClientRect(b.parent,&rect);ctl->put_Bounds(rect);ctl->put_IsVisible(TRUE);ComPtr<ICoreWebView2Controller2> c2;if(SUCCEEDED(ctl->QueryInterface(IID_PPV_ARGS(&c2))))c2->put_DefaultBackgroundColor({255,0,0,0});
        b.web->add_WebMessageReceived(Callback<ICoreWebView2WebMessageReceivedEventHandler>([&b](ICoreWebView2*,ICoreWebView2WebMessageReceivedEventArgs* args)->HRESULT{LPWSTR text=nullptr;if(SUCCEEDED(args->TryGetWebMessageAsString(&text))){int n=WideCharToMultiByte(CP_UTF8,0,text,-1,nullptr,0,nullptr,nullptr);std::string s(n,0);WideCharToMultiByte(CP_UTF8,0,text,-1,s.data(),n,nullptr,nullptr);b.log("browser-message.txt",s);CoTaskMemFree(text);}return S_OK;}).Get(),&b.messageToken);
        b.web->NavigateToString(L"<!doctype html><meta charset='utf-8'><style>html{background:#000;color:#eee;font:16px 'Microsoft YaHei';}input,button{background:#222;color:white;border:1px solid #555;border-radius:8px;padding:8px}body{margin:16px}</style><h3>社区网页视图</h3><input id='text' placeholder='支持键盘与系统输入法'><button onclick='window.chrome.webview.postMessage(document.querySelector(\"input\").value)'>发送</button><p id='state'>实际 WebView2，宿主托管尺寸和退出</p><script>window.chrome.webview.postMessage('browser-ready');</script>");b.log("browser-created.txt","WebView2 controller created");
      }else{if(ctl)ctl->Close();b.log("browser-error.txt",std::to_string(status));}b.barrier();--b.pending;return S_OK;
    }).Get());if(FAILED(start)){b.barrier();--b.pending;b.log("browser-error.txt",std::to_string(start));}}
    else b.log("browser-error.txt",std::to_string(hr));b.barrier();--b.pending;return S_OK;
   }).Get());if(FAILED(result)){b.barrier();--b.pending;b.log("browser-error.txt",std::to_string(result));return WI_EXT_FAULT;}return 0;
  }
  if(e->kind==WI_VIEW_LAYOUT&&b.controller){RECT rect{0,0,e->width,e->height};b.controller->put_Bounds(rect);b.controller->put_IsVisible(TRUE);b.controller->NotifyParentWindowPositionChanged();}
  if(e->kind==WI_VIEW_HIDDEN&&b.controller)b.controller->put_IsVisible(FALSE);
  if(e->kind==WI_VIEW_FOCUS&&e->flags&&b.controller)b.controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
  if(e->kind==WI_VIEW_FILES&&e->text)b.log("browser-files.txt",e->text);
  if(e->kind==WI_VIEW_DESTROY){b.closing=true;if(b.web)b.web->remove_WebMessageReceived(b.messageToken);if(b.controller)b.controller->Close();b.web.Reset();b.controller.Reset();b.environment.Reset();b.barrier();b.destroyed=true;b.log("browser-closed.txt","controller closed before DLL unload");}
  return 0;
 }
 static int enable(void* c,const char*){auto& b=*(Browser*)c;if(b.host->queryInterface(b.host->context,WI_OPEN_INTERFACE,1,&b.open,sizeof(b.open))||b.host->queryInterface(b.host->context,WI_COMMUNITY_INTERFACE,1,&b.community,sizeof(b.community))||b.host->queryInterface(b.host->context,WI_SCENE_INTERFACE,1,&b.scene,sizeof(b.scene)))return -1;char path[32768]{};uint32_t n=0;if(b.community.path(b.community.context,WI_PATH_DATA,"",path,sizeof(path),&n))return -2;b.data=std::filesystem::u8path(path).wstring();WiExternalLifetime lifetime{sizeof(lifetime),1,&b,stop,joined};b.community.manageLifetime(b.community.context,&lifetime);
  b.scene.create(b.scene.context,WI_CONTAINER,"open.browser",1,&b.node);WiPropertyValue p{sizeof(p),1};p.number[0]=0;p.number[1]=0;p.number[2]=560;p.number[3]=230;b.scene.set(b.scene.context,b.node,WI_RECT,&p,20);b.scene.set(b.scene.context,1,WI_RECT,&p,20);p={sizeof(p),1};p.number[0]=WI_PLUGIN_MANAGED;b.scene.set(b.scene.context,1,WI_SIZE_MODE,&p,20);b.scene.commit(b.scene.context);WiViewDefinition view{sizeof(view),1,WI_VIEW_NATIVE,1,b.node,&b,event};int rc=b.open.createView(b.open.context,&view,&b.view);if(rc)b.destroyed=true;return rc;
 }
 static void destroy(void* c){delete (Browser*)c;}
};
extern "C" __declspec(dllexport) uint32_t WinIsland_ModAbi(){return WINISLAND_MOD_ABI;}
extern "C" __declspec(dllexport) IWinIslandMod* WinIsland_CreateMod(const WinIslandHostApi* h){auto b=new Browser;b->host=h;b->life={sizeof(IWinIslandMod),WINISLAND_MOD_ABI,b,nullptr,Browser::enable,nullptr,nullptr,Browser::destroy};return &b->life;}
