#include "settings_bridge.h"
#include <sddl.h>
namespace wi {
SettingsBridge::SettingsBridge(HWND w):window(w) {
    GUID id{}; if(FAILED(CoCreateGuid(&id)))throw std::runtime_error("IPC token creation failed");
    wchar_t value[40]; StringFromGUID2(id,value,40); token=value;
    pipeName=L"\\\\.\\pipe\\WinIsland.Settings."+std::to_wstring(GetCurrentProcessId());
    stop=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(!stop)throw std::runtime_error("IPC stop event failed");
    worker=std::thread([this]{run();});
}
SettingsBridge::~SettingsBridge(){SetEvent(stop);if(worker.joinable())worker.join();CloseHandle(stop);}
bool SettingsBridge::transfer(HANDLE pipe,void* buffer,DWORD size,bool write){
    auto* p=static_cast<char*>(buffer);
    while(size){
        OVERLAPPED ov{};ov.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);DWORD n=0;
        BOOL ok=write?WriteFile(pipe,p,size,&n,&ov):ReadFile(pipe,p,size,&n,&ov);
        if(!ok&&GetLastError()==ERROR_IO_PENDING){HANDLE events[]{stop,ov.hEvent};
            if(WaitForMultipleObjects(2,events,FALSE,5000)==WAIT_OBJECT_0+1)ok=GetOverlappedResult(pipe,&ov,&n,FALSE);
            else {CancelIoEx(pipe,&ov);GetOverlappedResult(pipe,&ov,&n,TRUE);ok=FALSE;}}
        CloseHandle(ov.hEvent);if(!ok||!n)return false;p+=n;size-=n;
    }return true;
}
std::vector<std::shared_ptr<SettingsBridge::Request>> SettingsBridge::take(){std::lock_guard lock(mutex);auto r=std::move(pending);pending.clear();return r;}
void SettingsBridge::reply(const std::shared_ptr<Request>& r,std::string output){std::lock_guard lock(r->mutex);r->output=std::move(output);r->done=true;r->ready.notify_all();}
void SettingsBridge::run(){
    // The current user is the only permitted client; remote clients are rejected too.
    HANDLE processToken=nullptr;PSECURITY_DESCRIPTOR sd=nullptr;
    if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&processToken))return;
    DWORD bytes=0;GetTokenInformation(processToken,TokenUser,nullptr,0,&bytes);std::vector<BYTE> data(bytes);
    if(!GetTokenInformation(processToken,TokenUser,data.data(),bytes,&bytes)){CloseHandle(processToken);return;}
    LPWSTR sid=nullptr;ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid,&sid);CloseHandle(processToken);
    if(!sid)return;
    auto acl=L"D:P(A;;GA;;;"+std::wstring(sid)+L")";LocalFree(sid);
    if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(acl.c_str(),SDDL_REVISION_1,&sd,nullptr))return;
    SECURITY_ATTRIBUTES sa{sizeof(sa),sd,FALSE};
    HANDLE pipe=CreateNamedPipeW(pipeName.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,65536,65536,0,&sa);
    if(pipe==INVALID_HANDLE_VALUE){LocalFree(sd);return;}
    while(WaitForSingleObject(stop,0)!=WAIT_OBJECT_0){
        OVERLAPPED ov{};ov.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        bool connected=ConnectNamedPipe(pipe,&ov)!=FALSE;auto err=GetLastError();
        if(!connected&&err==ERROR_PIPE_CONNECTED)connected=true;
        if(!connected&&err==ERROR_IO_PENDING){HANDLE events[]{stop,ov.hEvent};
            connected=WaitForMultipleObjects(2,events,FALSE,INFINITE)==WAIT_OBJECT_0+1;
            if(!connected){CancelIoEx(pipe,&ov);DWORD n;GetOverlappedResult(pipe,&ov,&n,TRUE);}}
        CloseHandle(ov.hEvent);
        uint32_t length=0;
        if(connected&&transfer(pipe,&length,4,false)&&length>0&&length<=65536){
            auto request=std::make_shared<Request>();request->input.resize(length);
            if(transfer(pipe,request->input.data(),length,false)){
                {std::lock_guard lock(mutex);pending.push_back(request);}
                PostMessageW(window,SettingsRequestMessage,0,0);
                std::unique_lock lock(request->mutex);
                auto until=std::chrono::steady_clock::now()+std::chrono::seconds(60);
                while(!request->done&&WaitForSingleObject(stop,0)!=WAIT_OBJECT_0&&std::chrono::steady_clock::now()<until)
                    request->ready.wait_for(lock,std::chrono::milliseconds(100));
                if(!request->done){request->cancelled=true;request->output=R"({"ok":false,"code":"TIMEOUT","error":"核心请求超时，请重新读取实际状态"})";}
                auto response=request->output;lock.unlock();length=(uint32_t)response.size();
                if(length<=4*1024*1024&&transfer(pipe,&length,4,true)&&transfer(pipe,response.data(),length,true)){
                    char ack=0;transfer(pipe,&ack,1,false); // client confirms the response was consumed before disconnect
                }
            }
        }
        DisconnectNamedPipe(pipe);
    }
    CloseHandle(pipe);
    LocalFree(sd);
}
}
