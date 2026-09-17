#include "render.h"
namespace wi {
static void hr(HRESULT h) {
    if (FAILED(h))
        throw std::runtime_error("render HRESULT " + std::to_string(h));
}
Renderer::Renderer(HWND h, bool soft) : hwnd(h) {
    hr(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                           (IUnknown **)write.GetAddressOf()));
    hr(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)));
    try {
        init(soft);
    } catch (...) {
        monitor.event("硬件渲染不可用或资源重建，切换 Direct2D 软件渲染");
        releaseSurface();
        init(true);
    }
}
void Renderer::init(bool soft) {
    software = soft;
    D2D1_FACTORY_OPTIONS options{};
    if (!factory)
        hr(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, options, factory.GetAddressOf()));
    if (soft) {
        // A previous SetLayeredWindowAttributes call must be reset before ULW.
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & ~WS_EX_LAYERED);
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE,
                          (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & ~WS_EX_NOREDIRECTIONBITMAP) |
                              WS_EX_LAYERED);
        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        hr(factory->CreateDCRenderTarget(&props, &dcTarget));
        dcTarget.As(&target);
    } else {
        // Layered + transparent is the documented cross-process input bypass.
        // Keep rendering through DirectComposition; this does not copy pixels to GDI.
        SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        hr(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION,
                             &d3d, nullptr, nullptr));
        ComPtr<IDXGIDevice> device;
        hr(d3d.As(&device));
        hr(factory->CreateDevice(device.Get(), &d2d));
        hr(d2d->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &context));
        context.As(&target);
        ComPtr<IDXGIAdapter> adapter;
        device->GetAdapter(&adapter);
        ComPtr<IDXGIFactory2> dxgi;
        hr(adapter->GetParent(IID_PPV_ARGS(&dxgi)));
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = 656;
        desc.Height = 432;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        hr(dxgi->CreateSwapChainForComposition(d3d.Get(), &desc, nullptr, &swap));
        hr(DCompositionCreateDevice(device.Get(), IID_PPV_ARGS(&composition)));
        hr(composition->CreateTargetForHwnd(hwnd, TRUE, &compositionTarget));
        hr(composition->CreateVisual(&visual));
        hr(visual->SetContent(swap.Get()));
        hr(compositionTarget->SetRoot(visual.Get()));
        hr(composition->Commit());
    }
    hr(target->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &brush));
}
void Renderer::releaseSurface() {
    coverBrush.Reset();
    cover.Reset();
    coverData.reset();
    brush.Reset();
    target.Reset();
    dcTarget.Reset();
    if (context)
        context->SetTarget(nullptr);
    surface.Reset();
    context.Reset();
    visual.Reset();
    compositionTarget.Reset();
    composition.Reset();
    swap.Reset();
    d2d.Reset();
    d3d.Reset();
    if (memory) {
        SelectObject(memory, old);
        DeleteObject(bitmap);
        DeleteDC(memory);
        memory = nullptr;
    }
    width = height = 0;
}
Renderer::~Renderer() {
    releaseSurface();
}
void Renderer::resize(UINT w, UINT h, float dpi) {
    scale = dpi;
    if (w == width && h == height) {
        target->SetDpi(96 * scale, 96 * scale);
        return;
    }
    width = std::max(1u, w);
    height = std::max(1u, h);
    if (software) {
        if (memory) {
            SelectObject(memory, old);
            DeleteObject(bitmap);
            DeleteDC(memory);
        }
        memory = CreateCompatibleDC(nullptr);
        BITMAPINFO info{};
        info.bmiHeader = {sizeof(BITMAPINFOHEADER), (LONG)width, -(LONG)height, 1, 32, BI_RGB};
        void *bits = nullptr;
        bitmap = CreateDIBSection(memory, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        old = (HBITMAP)SelectObject(memory, bitmap);
        RECT rc{0, 0, (LONG)width, (LONG)height};
        hr(dcTarget->BindDC(memory, &rc));
    } else {
        context->SetTarget(nullptr);
        surface.Reset();
        hr(swap->ResizeBuffers(2, width, height, DXGI_FORMAT_B8G8R8A8_UNORM, 0));
        ComPtr<IDXGISurface> dxgi;
        hr(swap->GetBuffer(0, IID_PPV_ARGS(&dxgi)));
        auto props = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96 * scale,
            96 * scale);
        hr(context->CreateBitmapFromDxgiSurface(dxgi.Get(), &props, &surface));
        context->SetTarget(surface.Get());
    }
    target->SetDpi(96 * scale, 96 * scale);
}
ComPtr<IDWriteTextLayout> Renderer::layout(const std::wstring &s, float size, float w, float h, bool bold,
                                           bool wrap) {
    std::wstring key = s + L"\x1f" + std::to_wstring(size) + L":" + std::to_wstring((int)w) + L":" +
                       std::to_wstring((int)h) + (bold ? L"b" : L"n") + (wrap ? L"w" : L"l");
    if (auto i = texts.find(key); i != texts.end())
        return i->second;
    if (texts.size() > 256)
        texts.clear();
    ComPtr<IDWriteTextFormat> f;
    write->CreateTextFormat(L"Segoe UI", nullptr,
                            bold ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
                            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"zh-CN", &f);
    ComPtr<IDWriteTextLayout> l;
    hr(write->CreateTextLayout(s.c_str(), (UINT)s.size(), f.Get(), std::max(1.f, w), std::max(1.f, h), &l));
    l->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    ComPtr<IDWriteInlineObject> ellipsis;
    write->CreateEllipsisTrimmingSign(f.Get(), &ellipsis);
    DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    l->SetTrimming(&trim, ellipsis.Get());
    texts[key] = l;
    return l;
}
double Renderer::textHeight(const std::wstring &s, double size, double w, bool bold) {
    DWRITE_TEXT_METRICS m{};
    layout(s, (float)size, (float)w, 10000, bold, true)->GetMetrics(&m);
    return m.height;
}
double Renderer::textWidth(const std::wstring &s, double size, bool bold) {
    DWRITE_TEXT_METRICS m{};
    layout(s, (float)size, 20000, 10000, bold, false)->GetMetrics(&m);
    return m.widthIncludingTrailingWhitespace;
}
void Renderer::text(const std::wstring &s, float x, float y, float w, float h, float size, D2D1_COLOR_F color,
                    bool bold, bool wrap) {
    brush->SetColor(color);
    target->DrawTextLayout(D2D1::Point2F(x, y), layout(s, size, w, h, bold, wrap).Get(), brush.Get(),
                           D2D1_DRAW_TEXT_OPTIONS_CLIP);
}
void Renderer::rounded(Box b, float radius, D2D1_COLOR_F c) {
    brush->SetColor(c);
    target->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(b.x, b.y, b.x + b.w, b.y + b.h), radius, radius), brush.Get());
}
bool Renderer::draw(Scene &s) {
    Measure probe(Draw);
    if (!target || !width)
        return false;
    target->BeginDraw();
    target->SetTransform(D2D1::Matrix3x2F::Identity());
    target->Clear(D2D1::ColorF(0, 0, 0, 0));
    float x = (width / scale - (float)s.width) / 2, w = (float)s.width, h = (float)s.height,
          mh = (float)s.musicH;
    rounded({x, 0, w, h}, (float)s.radius, D2D1::ColorF(0, 0, 0, 1));
    ComPtr<ID2D1RoundedRectangleGeometry> geometry;
    factory->CreateRoundedRectangleGeometry(
        D2D1::RoundedRect(D2D1::RectF(x, 0, x + w, h), (float)s.radius, (float)s.radius), &geometry);
    auto layer = D2D1::LayerParameters();
    layer.geometricMask = geometry.Get();
    target->PushLayer(layer, nullptr);
    s.buttons.clear();
    auto m = s.music;
    const float musicOrigin = x;
    if (m && s.musicAlpha > .001 && mh > 0) {
        // Scale the complete music layout and its hit boxes together; notifications
        // and the idle shell stay in the original DIP coordinate system.
        const float x = musicOrigin / MusicScale, w = (float)s.width / MusicScale,
                    mh = (float)s.musicH / MusicScale;
        target->SetTransform(D2D1::Matrix3x2F::Scale(MusicScale, MusicScale));
        float alpha = (float)s.musicAlpha;
        auto white = D2D1::ColorF(1, 1, 1, alpha), muted = D2D1::ColorF(.75f, .76f, .79f, alpha);
        target->PushAxisAlignedClip(D2D1::RectF(x, 0, x + w, mh), D2D1_ANTIALIAS_MODE_ALIASED);
        float cy = 9.5f, cs = 29.4f;
        if (m->cover != coverData) {
            coverData = m->cover;
            cover.Reset();
            coverBrush.Reset();
            if (coverData && coverData->size() == 96 * 96 * 4) {
                auto props = D2D1::BitmapProperties(
                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
                if (SUCCEEDED(
                        target->CreateBitmap(D2D1::SizeU(96, 96), coverData->data(), 96 * 4, &props, &cover)))
                    target->CreateBitmapBrush(cover.Get(), &coverBrush);
            }
        }

        rounded({x + 14.7f, cy, cs, cs}, 6.3f, D2D1::ColorF(.145f, .153f, .17f, alpha));
        if (coverBrush) {
            coverBrush->SetOpacity(alpha);
            coverBrush->SetTransform(D2D1::Matrix3x2F::Scale(cs / 96, cs / 96) *
                                     D2D1::Matrix3x2F::Translation(x + 14.7f, cy));
            target->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(x + 14.7f, cy, x + 14.7f + cs, cy + cs), 6.3f, 6.3f),
                coverBrush.Get());
        } else
            text(L"♪", x + 22, cy + 1, 20, 30, 19, white);
        float textW = std::max(20.f, w - 117);
        text(m->loading ? L"正在切换歌曲" : m->title, x + 58, 8, textW, 20, 13.125f, white, true);
        std::wstring second =
            !s.expanded && !s.lyric.empty() ? s.lyric : (m->artist.empty() ? m->platform : m->artist);
        text(second, x + 58, 27, textW, 17, 11.025f, muted);
        for (int i = 0; i < 6; i++) {
            float bh = 14.7f * std::max(.08f, s.bars[i]);
            rounded({x + w - 47 + i * 5.25f, 32 - bh, 2.1f, bh}, 1, D2D1::ColorF(.765f, .839f, .973f, alpha));
        }
        s.buttons.emplace_back(0, Box{x, 0, w, 48.384f});
        if (s.expanded || mh > 49) {
            std::wstring line = !s.feedback.empty() ? s.feedback
                                : !s.lyric.empty()  ? s.lyric
                                : m->timeline       ? L"暂无歌词"
                                                    : L"歌词进度不可用";
            text(line, x + 19, 45, w - 38, 17, 12.6f, D2D1::ColorF(.9f, .915f, .96f, alpha));
            std::vector<int> ids;
            if (m->prev)
                ids.push_back(1);
            if (m->toggle)
                ids.push_back(2);
            if (m->next)
                ids.push_back(3);
            if (m->mode)
                ids.push_back(4);
            float bw = 42, group = (float)ids.size() * bw, start = x + (w - group) / 2;
            for (size_t i = 0; i < ids.size(); i++) {
                int id = ids[i];
                Box b{start + i * bw, 64, bw, 31.5f};
                s.buttons.emplace_back(id, b);
                if (s.hover == id)
                    rounded(b, 10, D2D1::ColorF(.125f, .133f, .153f, alpha));
                if (s.keyboard && s.focus == id) {
                    brush->SetColor(white);
                    target->DrawRoundedRectangle(
                        D2D1::RoundedRect(D2D1::RectF(b.x + 2, b.y + 2, b.x + b.w - 2, b.y + b.h - 2), 8, 8),
                        brush.Get());
                }
                float cx = b.x + b.w / 2, yy = b.y + 9;
                brush->SetColor(D2D1::ColorF(1, 1, 1, s.busy ? .3f : alpha));
                if (id == 2 && m->playing) {
                    target->FillRectangle(D2D1::RectF(cx - 6, yy, cx - 2, yy + 14), brush.Get());
                    target->FillRectangle(D2D1::RectF(cx + 2, yy, cx + 6, yy + 14), brush.Get());
                } else if (id < 4) {
                    ComPtr<ID2D1PathGeometry> g;
                    factory->CreatePathGeometry(&g);
                    ComPtr<ID2D1GeometrySink> sink;
                    g->Open(&sink);
                    float dir = id == 1 ? -1.f : 1.f;
                    sink->BeginFigure(D2D1::Point2F(cx - 6 * dir, yy), D2D1_FIGURE_BEGIN_FILLED);
                    sink->AddLine(D2D1::Point2F(cx + 6 * dir, yy + 7));
                    sink->AddLine(D2D1::Point2F(cx - 6 * dir, yy + 14));
                    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                    sink->Close();
                    target->FillGeometry(g.Get(), brush.Get());
                    if (id != 2)
                        target->FillRectangle(
                            D2D1::RectF(cx + (id == 1 ? -9 : 7), yy, cx + (id == 1 ? -7 : 9), yy + 14),
                            brush.Get());
                } else
                    text(m->shuffle       ? L"⤨"
                         : m->repeat == 1 ? L"↻1"
                                          : L"↻",
                         cx - 11, yy - 4, 24, 24, 18, white);
            }
            if (m->timeline) {
                float p = (float)(m->duration > 0 ? m->progress() / m->duration : 0);
                rounded({x + 19, 102, w - 38, 2}, 0, D2D1::ColorF(.2f, .21f, .24f, alpha));
                rounded({x + 19, 102, (w - 38) * p, 2}, 0, D2D1::ColorF(.75f, .82f, .96f, alpha));
                auto time = [](double n) {
                    int t = (int)n;
                    wchar_t a[40];
                    swprintf_s(a, L"%02d:%02d", t / 60, t % 60);
                    return std::wstring(a);
                };
                text(time(m->progress()) + L" / " + time(m->duration), x + w - 109, 106, 90, 14, 10, muted);
            }
        }
        if (s.keyboard && s.focus == 0) {
            brush->SetColor(white);
            target->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x + 2, 2, x + w - 2, 46), 10, 10),
                                         brush.Get());
        }
        target->PopAxisAlignedClip();
        target->SetTransform(D2D1::Matrix3x2F::Identity());
        for (auto &[id, b] : s.buttons) {
            b.x *= MusicScale;
            b.y *= MusicScale;
            b.w *= MusicScale;
            b.h *= MusicScale;
        }
    }
    if (s.notice && s.noticeAlpha > .001 && h > mh) {
        float alpha = (float)s.noticeAlpha;
        target->PushAxisAlignedClip(D2D1::RectF(x, mh, x + w, h), D2D1_ANTIALIAS_MODE_ALIASED);
        if (mh > 0)
            rounded({x + 24, mh, w - 48, 1}, 0, D2D1::ColorF(.19f, .19f, .21f, alpha));
        float y = mh + 16.8f + 4 * (1 - alpha);
        float tw = (float)s.noticeTextWidth;
        float titleH = (float)std::min(76.8, textHeight(s.notice->title, 14, tw, true));
        text(s.notice->title, x + 24, y, tw, titleH, 14, D2D1::ColorF(1, 1, 1, alpha), true, true);
        if (!s.notice->body.empty())
            text(s.notice->body, x + 24, y + titleH + 6.4f, tw, (float)s.noticeBodyHeight, 12,
                 D2D1::ColorF(.875f, .875f, .895f, alpha), false, true);
        target->PopAxisAlignedClip();
    }
    target->PopLayer();
    HRESULT end = target->EndDraw();
    if (end == D2DERR_RECREATE_TARGET) {
        monitor.event("硬件渲染不可用或资源重建，切换 Direct2D 软件渲染");
        releaseSurface();
        init(true);
        RECT rc;
        GetClientRect(hwnd, &rc);
        resize(rc.right, rc.bottom, scale);
        return false;
    }
    hr(end);
    if (software) {
        RECT rect;
        GetWindowRect(hwnd, &rect);
        POINT dest{rect.left, rect.top}, src{0, 0};
        SIZE size{(LONG)width, (LONG)height};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        UpdateLayeredWindow(hwnd, nullptr, &dest, &size, memory, &src, 0, &blend, ULW_ALPHA);
    } else {
        HRESULT p = swap->Present(0, DXGI_PRESENT_DO_NOT_WAIT);
        if (p == DXGI_ERROR_WAS_STILL_DRAWING)
            return false;
        if (p == DXGI_ERROR_DEVICE_REMOVED || p == DXGI_ERROR_DEVICE_RESET) {
            releaseSurface();
            init(true);
            RECT rc;
            GetClientRect(hwnd, &rc);
            resize(rc.right, rc.bottom, scale);
            return false;
        }
    }
    return true;
}
void Renderer::capture(const fs::path &path) {
    ComPtr<IWICBitmap> image;
    if (software)
        hr(wic->CreateBitmapFromHBITMAP(bitmap, nullptr, WICBitmapUseAlpha, &image));
    else {
        ComPtr<ID3D11Texture2D> back;
        hr(swap->GetBuffer(0, IID_PPV_ARGS(&back)));
        D3D11_TEXTURE2D_DESC desc;
        back->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;
        ComPtr<ID3D11Texture2D> stage;
        hr(d3d->CreateTexture2D(&desc, nullptr, &stage));
        ComPtr<ID3D11DeviceContext> ctx;
        d3d->GetImmediateContext(&ctx);
        ctx->CopyResource(stage.Get(), back.Get());
        D3D11_MAPPED_SUBRESOURCE map{};
        hr(ctx->Map(stage.Get(), 0, D3D11_MAP_READ, 0, &map));
        HRESULT result =
            wic->CreateBitmapFromMemory(width, height, GUID_WICPixelFormat32bppPBGRA, map.RowPitch,
                                        map.RowPitch * height, (BYTE *)map.pData, &image);
        ctx->Unmap(stage.Get(), 0);
        hr(result);
    }
    ComPtr<IWICStream> out;
    wic->CreateStream(&out);
    hr(out->InitializeFromFilename(path.c_str(), GENERIC_WRITE));
    ComPtr<IWICBitmapEncoder> encoder;
    wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    hr(encoder->Initialize(out.Get(), WICBitmapEncoderNoCache));
    ComPtr<IWICBitmapFrameEncode> frame;
    encoder->CreateNewFrame(&frame, nullptr);
    frame->Initialize(nullptr);
    frame->SetSize(width, height);
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    frame->SetPixelFormat(&format);
    frame->WriteSource(image.Get(), nullptr);
    frame->Commit();
    encoder->Commit();
}
} // namespace wi
