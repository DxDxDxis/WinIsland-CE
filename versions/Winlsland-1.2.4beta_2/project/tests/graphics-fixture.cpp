#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl.h>
#include <fstream>
#include <filesystem>
#include <chrono>
using Microsoft::WRL::ComPtr;
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR args,int) {
    std::filesystem::path folder=args;std::filesystem::create_directories(folder);
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=instance;wc.lpszClassName=L"WinIsland.DxgiFixture";RegisterClassW(&wc);
    HWND window=CreateWindowW(wc.lpszClassName,L"WinIsland DXGI 验证窗口",WS_OVERLAPPEDWINDOW|WS_VISIBLE,80,520,320,200,nullptr,nullptr,instance,nullptr);
    DXGI_SWAP_CHAIN_DESC desc{};desc.BufferCount=2;desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.OutputWindow=window;desc.SampleDesc.Count=1;desc.Windowed=TRUE;desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;ComPtr<IDXGISwapChain> swap;
    if(FAILED(D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&desc,&swap,&device,nullptr,&context)))return 2;
    ComPtr<ID3D11Texture2D> back;swap->GetBuffer(0,IID_PPV_ARGS(&back));ComPtr<ID3D11RenderTargetView> target;device->CreateRenderTargetView(back.Get(),nullptr,&target);
    std::ofstream(folder/L"ready.txt")<<GetCurrentProcessId()<<"\n"<<(uintptr_t)window;
    auto begin=std::chrono::steady_clock::now(),last=begin;int count=0;bool stopped=false;
    while(std::chrono::steady_clock::now()-begin<std::chrono::seconds(120)&&!std::filesystem::exists(folder/L"exit.request")){
        MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
        if(!IsWindow(window))break;
        stopped=std::filesystem::exists(folder/L"pause.request");
        if(!stopped){float color[]={.08f,.10f,.15f,1};context->ClearRenderTargetView(target.Get(),color);if(swap->Present(1,0)==S_OK)++count;}else Sleep(16);
        auto now=std::chrono::steady_clock::now();double elapsed=std::chrono::duration<double>(now-last).count();
        if(elapsed>=1){std::ofstream(folder/L"frames.txt")<<(stopped?0:count/elapsed);last=now;count=0;}
    }
    if(IsWindow(window))DestroyWindow(window);return 0;
}
