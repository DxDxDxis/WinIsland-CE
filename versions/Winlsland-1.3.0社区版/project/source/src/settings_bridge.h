#pragma once
#include "core.h"
namespace wi {
constexpr UINT SettingsRequestMessage=WM_APP+44, SettingsOpenMessage=WM_APP+45;
// The pipe thread owns I/O only. App owns settings and handles requests on its window thread.
class SettingsBridge {
public:
    struct Request {
        std::string input,output;
        std::mutex mutex; std::condition_variable ready;
        bool done=false,cancelled=false,started=false;
    };
    explicit SettingsBridge(HWND);
    ~SettingsBridge();
    std::vector<std::shared_ptr<Request>> take();
    static void reply(const std::shared_ptr<Request>&,std::string);
    std::wstring pipeName,token;
private:
    HWND window; HANDLE stop=nullptr;
    std::thread worker; std::mutex mutex;
    std::vector<std::shared_ptr<Request>> pending;
    bool transfer(HANDLE,void*,DWORD,bool);
    void run();
};
}
