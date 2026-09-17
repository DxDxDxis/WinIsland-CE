#pragma once
#include "core.h"
#include <d2d1_1.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_3.h>
#include <wincodec.h>
namespace wi {
inline constexpr float MusicScale = .855f;
struct Box {
    float x = 0, y = 0, w = 0, h = 0;
    bool hit(float px, float py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};
struct Scene {
    double width = 180.4, height = 29.92, radius = 14.96, musicH = 0, musicAlpha = 0, noticeAlpha = 0,
           noticeTextWidth = 132, noticeBodyHeight = 0;
    bool expanded = false, keyboard = false, busy = false;
    int focus = 0, hover = -1;
    std::shared_ptr<Music> music;
    std::shared_ptr<Notice> notice;
    std::wstring lyric, feedback;
    std::array<float, MusicBarCount> bars{};
    Box barArea;
    float songTextRight = 0, barGap = 0;
    bool showFps = false, showPing = false, holdTriggered = false;
    int fps = -1, ping = -1;
    double infoAlpha = 0, infoH = 0, press = 0, fpsAlpha = 0, pingAlpha = 0;
    std::vector<std::pair<int, Box>> buttons;
};
LRESULT accessibleObject(HWND, WPARAM, Scene &, float);
std::wstring controlName(const Scene &, int);
class Renderer {
    HWND hwnd;
    float scale = 1;
    UINT width = 0, height = 0;
    bool software = false;
    ComPtr<ID3D11Device> d3d;
    ComPtr<ID2D1Factory1> factory;
    ComPtr<ID2D1Device> d2d;
    ComPtr<ID2D1DeviceContext> context;
    ComPtr<ID2D1DCRenderTarget> dcTarget;
    ComPtr<ID2D1RenderTarget> target;
    ComPtr<IDXGISwapChain1> swap;
    ComPtr<IDCompositionDevice> composition;
    ComPtr<IDCompositionTarget> compositionTarget;
    ComPtr<IDCompositionVisual> visual;
    ComPtr<ID2D1Bitmap1> surface;
    ComPtr<IDWriteFactory> write;
    ComPtr<IWICImagingFactory> wic;
    ComPtr<ID2D1SolidColorBrush> brush;
    ComPtr<ID2D1Bitmap> cover;
    ComPtr<ID2D1BitmapBrush> coverBrush;
    std::shared_ptr<std::vector<uint8_t>> coverData;
    std::map<std::wstring, ComPtr<IDWriteTextLayout>> texts;
    HDC memory = nullptr;
    HBITMAP bitmap = nullptr, old = nullptr;
    void releaseSurface();
    void init(bool);
    ComPtr<IDWriteTextLayout> layout(const std::wstring &, float, float, float, bool, bool);
    void text(const std::wstring &, float, float, float, float, float, D2D1_COLOR_F, bool = false,
              bool = false);
    void rounded(Box, float, D2D1_COLOR_F);

  public:
    explicit Renderer(HWND, bool = false);
    ~Renderer();
    void resize(UINT, UINT, float);
    double textHeight(const std::wstring &, double, double, bool = false);
    double textWidth(const std::wstring &, double, bool = false);
    bool draw(Scene &);
    void capture(const fs::path &);
    bool fallback() const {
        return software;
    }
    bool valid() const {
        return target != nullptr;
    }
};
} // namespace wi
