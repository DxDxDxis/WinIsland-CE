#pragma once
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.h>
#include <atomic>
#include <chrono>
#include <memory>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <windows.h>
namespace client {
using namespace winrt;
using namespace winrt::Windows::Data::Json;
inline void put(JsonObject const& o,wchar_t const* k,bool v){o.Insert(k,JsonValue::CreateBooleanValue(v));}
inline void put(JsonObject const& o,wchar_t const* k,double v){o.Insert(k,JsonValue::CreateNumberValue(v));}
inline void put(JsonObject const& o,wchar_t const* k,hstring const& v){o.Insert(k,JsonValue::CreateStringValue(v));}
inline void put(JsonObject const& o,wchar_t const* k,wchar_t const* v){put(o,k,hstring(v));}
inline hstring text(JsonObject const& o,wchar_t const* k){return o.GetNamedString(k,L"");}
struct handle { HANDLE h=INVALID_HANDLE_VALUE; ~handle(){if(h!=INVALID_HANDLE_VALUE&&h)CloseHandle(h);} operator HANDLE()const{return h;} };
struct State {std::wstring pipe,token;std::atomic<bool> stopped=false;std::atomic<unsigned> pending=0;};
// Each request owns its connection. The host owns all filesystem/plugin operations.
class Transport {
 std::shared_ptr<State> state;
 static void io(HANDLE pipe,void* data,DWORD bytes,bool write,std::shared_ptr<State> const& s,ULONGLONG until){
  auto* p=static_cast<unsigned char*>(data);
  while(bytes){
   handle event{CreateEventW(nullptr,TRUE,FALSE,nullptr)};if(!event.h)throw std::runtime_error("无法创建 IPC 等待事件");
   OVERLAPPED ov{};ov.hEvent=event;DWORD done=0;
   BOOL ok=write?WriteFile(pipe,p,bytes,&done,&ov):ReadFile(pipe,p,bytes,&done,&ov);
   if(!ok){if(GetLastError()!=ERROR_IO_PENDING)throw std::runtime_error("宿主连接已中断");
    while(WaitForSingleObject(event,25)==WAIT_TIMEOUT){
     if(s->stopped||GetTickCount64()>=until){CancelIoEx(pipe,&ov);GetOverlappedResult(pipe,&ov,&done,TRUE);throw std::runtime_error("IPC 请求已取消或超时；已提交的操作请刷新确认");}
    }
    if(!GetOverlappedResult(pipe,&ov,&done,FALSE))throw std::runtime_error("IPC 读写失败");
   }
   if(!done)throw std::runtime_error("宿主连接已关闭");p+=done;bytes-=done;
  }
 }
public:
 Transport(std::wstring pipe,std::wstring token):state(std::make_shared<State>()){state->pipe=std::move(pipe);state->token=std::move(token);}
 void stop(){state->stopped=true;}
 Windows::Foundation::IAsyncOperation<hstring> call(hstring command,JsonObject params=JsonObject{}) {
  auto s=state;
  static std::set<std::wstring> const allowed={L"settings.read",L"settings.write",L"capabilities.read",L"layout.reset",L"media.read",L"lyrics.import",L"lyrics.clear",L"diagnostics.action",L"mods.list",L"mods.affected",L"mods.import",L"mods.action",L"mods.invoke",L"mods.folder",L"mods.log",L"transfer.call",L"clipboard.call"};
  if(!allowed.contains(std::wstring(command)))throw hresult_invalid_argument(L"不支持的设置命令");
  std::wstring prefix=LR"(\\.\pipe\WinIsland.Settings.)";
  if(s->pipe.rfind(prefix,0)!=0||s->pipe.size()==prefix.size()||s->pipe.find_first_not_of(L"0123456789",prefix.size())!=std::wstring::npos||s->token.empty())throw hresult_invalid_argument(L"请从 WinIsland 主程序打开设置");
  JsonObject request;put(request,L"protocol",1.0);put(request,L"token",hstring(s->token));put(request,L"command",command);request.Insert(L"params",params);
  auto payload=to_string(request.Stringify());if(payload.size()>2*1024*1024)throw hresult_invalid_argument(L"请求过大，请分批操作");
  if(s->pending.fetch_add(1)>=32){s->pending--;throw hresult_error(E_PENDING,L"请求队列已满");}
  struct Count{std::shared_ptr<State> s;~Count(){s->pending--;}} count{s};
  co_await resume_background();
  try{
   if(s->stopped)throw std::runtime_error("设置窗口已关闭");
   auto action=text(params,L"action");auto until=GetTickCount64()+((command==L"transfer.call"&&action==L"drag")||(command==L"clipboard.call"&&action==L"export")?120000:8000);handle pipe;
   do{
    pipe.h=CreateFileW(s->pipe.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);
    if(pipe.h!=INVALID_HANDLE_VALUE)break;
    if(GetLastError()!=ERROR_PIPE_BUSY)throw std::runtime_error("无法连接宿主，请确认主程序仍在运行");
    Sleep(25);
   }while(!s->stopped&&GetTickCount64()<until);
   if(pipe.h==INVALID_HANDLE_VALUE||s->stopped)throw std::runtime_error("宿主连接超时或已取消");
   DWORD size=DWORD(payload.size());io(pipe,&size,4,true,s,until);io(pipe,payload.data(),size,true,s,until);
   io(pipe,&size,4,false,s,until);if(!size||size>4*1024*1024)throw std::runtime_error("宿主响应长度无效");
   std::string reply(size,'\0');io(pipe,reply.data(),size,false,s,until);BYTE ack=1;io(pipe,&ack,1,true,s,until);
   auto result=JsonObject::Parse(to_hstring(reply));if(!result.GetNamedBoolean(L"ok"))throw hresult_error(E_FAIL,text(result,L"code")+L"："+text(result,L"error"));
   co_return result.Stringify();
  }catch(std::exception const& e){throw hresult_error(E_FAIL,to_hstring(e.what()));}
 }
};
struct SettingsClient {Transport& t;auto read(){return t.call(L"settings.read");}auto write(double revision,JsonObject patch){JsonObject p;put(p,L"revision",revision);p.Insert(L"patch",patch);return t.call(L"settings.write",p);}};
struct PluginClient {Transport& t;auto list(){return t.call(L"mods.list");}auto action(hstring op,hstring id,bool cascade=false){JsonObject p;put(p,L"action",op);put(p,L"id",id);put(p,L"cascade",cascade);return t.call(L"mods.action",p);}};
struct ClipboardClient {Transport& t;auto call(hstring action,JsonObject p=JsonObject{}){put(p,L"action",action);return t.call(L"clipboard.call",p);}};
struct TransferClient {Transport& t;auto call(hstring action,JsonObject p=JsonObject{}){put(p,L"action",action);return t.call(L"transfer.call",p);}auto list(){return call(L"list");}};
}
