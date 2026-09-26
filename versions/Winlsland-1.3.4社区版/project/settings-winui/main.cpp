#define NOMINMAX
#include <windows.h>
#undef GetCurrentTime
#include <shellapi.h>
#include <string>
#include <vector>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.XamlTypeInfo.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Windows.Graphics.h>
#include <microsoft.ui.xaml.window.h>
#include <UndockedRegFreeWinRT-AutoInitializer.cpp>
using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
static std::wstring g_pipe,g_token;
#include "app.h"
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){
 int argc=0;LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);for(int i=1;i<argc;i++){std::wstring a=argv[i];if(a.rfind(L"--pipe=",0)==0)g_pipe=a.substr(7);else if(a.rfind(L"--token=",0)==0)g_token=a.substr(8);}if(argv)LocalFree(argv);
 try{init_apartment(apartment_type::single_threaded);Application::Start([](auto&&){make<SettingsApp>();});return 0;}
 catch(hresult_error const& e){MessageBoxW(nullptr,e.message().c_str(),L"WinIsland WinUI 启动失败",MB_ICONERROR);return int(e.code());}
}
