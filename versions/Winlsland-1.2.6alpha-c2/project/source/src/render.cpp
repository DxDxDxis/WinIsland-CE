#include "render.h"
#include "outline.h"
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
#include "scene_render.inc"
bool Renderer::draw(Scene &s) {
    Measure probe(Draw);
    if (!target || !width)
        return false;
    target->BeginDraw();
    target->SetTransform(D2D1::Matrix3x2F::Identity());
    target->Clear(D2D1::ColorF(0, 0, 0, 0));
    // Render vectors/text at final DPI, not a resized bitmap. All content shares
    // one transform; layout bounds are restored before any internal measurement.
    const float islandScale = (float)s.contentScale;
    const auto islandTransform = D2D1::Matrix3x2F::Scale(islandScale, islandScale);
    target->SetTransform(islandTransform);
    float x = (width / scale - (float)s.width) / (2 * islandScale),
          w = (float)s.width / islandScale, h = (float)s.height / islandScale,
          mh = (float)s.musicH / islandScale, radius = (float)s.radius / islandScale;
    float top = (float)s.topRadius/islandScale;
    ComPtr<ID2D1PathGeometry> geometry;
    factory->CreatePathGeometry(&geometry);
    ComPtr<ID2D1GeometrySink> sink;
    geometry->Open(&sink);
    const auto outline = islandOutline(x, w, h, radius, top);
    auto point = [](OutlinePoint p) { return D2D1::Point2F((float)p.x, (float)p.y); };
    sink->BeginFigure(point(outline.back().end), D2D1_FIGURE_BEGIN_FILLED);
    for (const auto &corner : outline) {
        sink->AddLine(point(corner.start));
        sink->AddBezier(D2D1::BezierSegment(point(corner.c1), point(corner.c2), point(corner.end)));
    }
    sink->EndFigure(D2D1_FIGURE_END_CLOSED); sink->Close();
    auto rootStyle=s.openPlan.resolve(sceneNode(1,"island",WI_CONTAINER,0,0,(float)s.width,(float)s.height));
    float rootAlpha=rootStyle.n(WI_VISIBLE,0,1)?(float)rootStyle.n(WI_OPACITY,0,1):0;
    brush->SetColor(D2D1::ColorF(s.modTint,rootAlpha)); target->FillGeometry(geometry.Get(),brush.Get());
    if (s.press > .001 && !s.music && !s.notice) {
        brush->SetColor(D2D1::ColorF(1,1,1,(float)s.press*.09f));
        target->FillGeometry(geometry.Get(),brush.Get());
    }
    auto layer = D2D1::LayerParameters();
    layer.geometricMask = geometry.Get();
    layer.opacity=rootAlpha;
    target->PushLayer(layer, nullptr);
    s.buttons.clear();
    s.hostNodes.clear();s.sceneHits.clear();s.visibleNodes.clear();
    auto hostContainer=[&](WiElement id,const char* key,float yy,float hh,bool visible){
        auto n=sceneNode(id,key,WI_CONTAINER,0,yy,(float)s.width,hh);n.values[WI_VISIBLE]=sceneNumber(visible);n.values[WI_PARENT]=sceneNumber(0);n.values[WI_PARENT].handle=id==1?0:1;
        s.hostNodes.push_back(n);return s.openPlan.resolve(n).n(WI_VISIBLE)!=0;
    };
    hostContainer(1,"island",0,(float)s.height,true);
    bool musicVisible=hostContainer(10,"host.music",0,(float)s.musicH,s.music&&s.musicAlpha>.001);
    hostContainer(11,"host.idle",0,(float)s.height,!s.music&&!s.notice);
    bool noticeVisible=hostContainer(12,"host.notice",(float)(s.musicH+s.infoH),(float)(s.height-s.musicH-s.infoH),s.notice&&s.noticeAlpha>.001);
    auto nativeText=[&](WiElement id,const char* key,WiElement parent,const std::wstring& str,float xx,float yy,float ww,float hh,float sz,D2D1_COLOR_F color,bool bold=false){
        D2D1_MATRIX_3X2_F matrix;target->GetTransform(&matrix);
        float parentY=parent==12?(float)(s.musicH+s.infoH):0;
        auto n=sceneNode(id,key,WI_TEXT,xx*matrix._11+matrix._31-x*islandScale,yy*matrix._22+matrix._32-parentY,ww*matrix._11,hh*matrix._22);
        n.values[WI_PARENT]=sceneNumber(0);n.values[WI_PARENT].handle=parent;
        n.values[WI_TEXT_VALUE]=sceneText(utf8(str));n.values[WI_FONT_SIZE]=sceneNumber(sz*matrix._11);n.values[WI_FONT_WEIGHT]=sceneNumber(bold?600:400);
        n.values[WI_TEXT_COLOR]=sceneNumber(color.r,color.g,color.b,color.a);s.hostNodes.push_back(n);
    };
    auto m = s.music;
    const float musicOrigin = x;
    auto metrics = [&](float right, float y, float size, float cell, float opacity, float fpsAlpha,
                       float pingAlpha) {
        if (pingAlpha > .001) {
            auto value = L"Ping " + (s.ping < 0    ? std::wstring(L"--")
                                     : s.ping == 0 ? std::wstring(L"<1 ms")
                                                   : std::to_wstring(s.ping) + L" ms");
            nativeText(30,"host.telemetry.ping",1,value, right - cell, y, cell - 3, 20, size,
                 D2D1::ColorF(.75f, .76f, .79f, opacity * pingAlpha));
        }
        if (fpsAlpha > .001) {
            auto value = L"FPS " + (s.fps < 0 ? std::wstring(L"--") : std::to_wstring(s.fps));
            nativeText(31,"host.telemetry.fps",1,value, right - cell * (1 + pingAlpha), y, cell - 3, 20, size,
                 D2D1::ColorF(.75f, .76f, .79f, opacity * fpsAlpha));
        }
    };
    if (m && s.musicAlpha > .001 && mh > 0 && musicVisible) {
        // Preserve the existing music proportions within the uniform island scale.
        // MusicScale is applied once by App geometry; render in the island space
        // so text, cover, spectrum and hit targets share the same 0.9 scale.
        const float contentScale = islandScale;
        const float x = musicOrigin, w = (float)s.width / contentScale,
                    mh = (float)s.musicH / contentScale;
        target->SetTransform(D2D1::Matrix3x2F::Scale(contentScale, contentScale));
        float alpha = (float)s.musicAlpha;
        auto white = D2D1::ColorF(1, 1, 1, alpha), muted = D2D1::ColorF(.75f, .76f, .79f, alpha);
        target->PushAxisAlignedClip(D2D1::RectF(x, 0, x + w, mh), D2D1_ANTIALIAS_MODE_ALIASED);
        float cy = 9.5f, cs = 29.4f;
        auto image=sceneNode(50,"host.music.cover",WI_IMAGE,14.7f*contentScale,cy*contentScale,cs*contentScale,cs*contentScale);
        image.values[WI_PARENT]=sceneNumber(0);image.values[WI_PARENT].handle=10;
        image.values[WI_OPACITY]=sceneNumber(alpha);image.values[WI_RADIUS]=sceneNumber(6.3*contentScale);image.values[WI_BACKGROUND]=sceneNumber(.145,.153,.17,1);
        if(m->cover&&m->cover->size()==96*96*4){image.pixels=*m->cover;image.pixelWidth=image.pixelHeight=96;}
        else {image.type=WI_ICON;image.values[WI_TEXT_VALUE]=sceneText("♪");}
        s.hostNodes.push_back(image);
        // Physical DIP sizes remain legible despite the smaller music shell.
        // Anchor the right edge; all added bar area grows to the left.
        constexpr float barWidth = 2.1f, maxBarHeight = 18.f;
        float fpsAlpha = (float)s.fpsAlpha, pingAlpha = (float)s.pingAlpha;
        float metricWidth = 78 * (fpsAlpha + pingAlpha);
        float gap = std::clamp((w - 18.65f - 70 - metricWidth - 96 - MusicBarCount * barWidth) /
                                   (MusicBarCount - 1),
                               .8f, 2.4f);
        float region = MusicBarCount * barWidth + (MusicBarCount - 1) * gap;
        float barLeft = x + w - 18.65f - region;
        float metricRight = barLeft - 8;
        if (metricRight - metricWidth - x - 62 < 64) {
            fpsAlpha = 0;
            metricWidth = 78 * pingAlpha;
        }
        if (metricRight - metricWidth - x - 62 < 64) {
            pingAlpha = 0;
            metricWidth = 0;
        }
        float textW = std::max(1.f, metricRight - metricWidth - x - 62);
        metrics(metricRight, 17, 10.5f, 78, alpha, fpsAlpha, pingAlpha);
        s.barArea = {(barLeft - x) * contentScale, (48.384f - maxBarHeight) * contentScale / 2,
                     region * contentScale, maxBarHeight * contentScale};
        s.songTextRight = (58 + textW) * contentScale;
        s.barGap = gap * contentScale;
        nativeText(20,"host.music.title",10,m->loading ? L"正在切换歌曲" : m->title, x + 58, 8, textW, 20, 13.125f, white, true);
        std::wstring second =
            !s.expanded && !s.lyric.empty() ? s.lyric : (m->artist.empty() ? m->platform : m->artist);
        nativeText(21,"host.music.subtitle",10,second, x + 58, 27, textW, 17, 11.025f, muted);
        for (size_t i = 0; i < MusicBarCount; i++) {
            float bh = maxBarHeight * std::max(.08f, s.bars[i]);
            auto bar=sceneNode(60+i,"host.music.spectrum."+std::to_string(i),WI_SHAPE,(barLeft-x+i*(barWidth+gap))*contentScale,(48.384f-bh)/2*contentScale,barWidth*contentScale,bh*contentScale);
            bar.values[WI_PARENT]=sceneNumber(0);bar.values[WI_PARENT].handle=10;bar.values[WI_BACKGROUND]=sceneNumber(.765,.839,.973,alpha);bar.values[WI_RADIUS]=sceneNumber(barWidth/2*contentScale);s.hostNodes.push_back(bar);
        }
        s.buttons.emplace_back(0, Box{x, 0, w, 48.384f});
        if (s.expanded || mh > 49) {
              std::wstring line = !s.feedback.empty() ? s.feedback
                                : !s.lyric.empty()  ? s.lyric
                                : m->lyricState == "loading" ? L"正在获取歌词"
                                : m->lyricState == "disabled" ? L"歌词已关闭"
                                : m->lyricState == "network_unavailable" ? L"歌词网络暂不可用"
                                : m->lyricState == "no_progress" ? L"缺少播放进度"
                                : m->lyricState == "synced" ? L"前奏 / 间奏"
                                                            : L"暂无歌词";
            nativeText(22,"host.lyric",10,line, x + 19, 45, w - 38, 17, 12.6f, D2D1::ColorF(.9f, .915f, .96f, alpha));
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
                auto node=sceneNode(100+id,"host.music.button."+std::to_string(id),WI_SCENE_BUTTON,(b.x-x)*islandScale,b.y*islandScale,b.w*islandScale,b.h*islandScale);
                node.values[WI_PARENT]=sceneNumber(0);node.values[WI_PARENT].handle=10;
                node.values[WI_RECEIVE_INPUT]=sceneNumber(1);node.values[WI_BLOCK_INPUT]=sceneNumber(1);
                node.values[WI_TEXT_VALUE]=sceneText(id==1?"◀":id==2?(m->playing?"Ⅱ":"▶"):id==3?"▶|":"↻");
                s.hostNodes.push_back(node);
            }
            if (m->timeline) {
                float p = (float)(m->duration > 0 ? m->progress() / m->duration : 0);
                auto progress=sceneNode(80,"host.music.progress",WI_PROGRESS,19*contentScale,100*contentScale,(w-38)*contentScale,4*contentScale);
                progress.values[WI_PARENT]=sceneNumber(0);progress.values[WI_PARENT].handle=10;progress.values[WI_VALUE]=sceneNumber(p);progress.values[WI_TEXT_COLOR]=sceneNumber(.75,.82,.96,alpha);progress.values[WI_BACKGROUND]=sceneNumber(.2,.21,.24,alpha);s.hostNodes.push_back(progress);
                auto time = [](double n) {
                    int t = (int)n;
                    wchar_t a[40];
                    swprintf_s(a, L"%02d:%02d", t / 60, t % 60);
                    return std::wstring(a);
                };
                nativeText(82,"host.music.position",10,time(m->progress()) + L" / " + time(m->duration), x + w - 109, 106, 90, 14, 10, muted);
            }
        }
        if (s.keyboard && s.focus == 0) {
            brush->SetColor(white);
            target->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x + 2, 2, x + w - 2, 46), 10, 10),
                                         brush.Get());
        }
        target->PopAxisAlignedClip();
        target->SetTransform(islandTransform);
        for (auto &[id, b] : s.buttons) {
            b.x *= contentScale;
            b.y *= contentScale;
            b.w *= contentScale;
            b.h *= contentScale;
        }
    }
    if (s.infoH > .001) {
        float combined = 75 * (float)(s.fpsAlpha + s.pingAlpha);
        target->PushAxisAlignedClip(D2D1::RectF(x, 0, x + w, (float)s.infoH / islandScale), D2D1_ANTIALIAS_MODE_ALIASED);
        metrics(x + (w + combined) / 2, ((float)s.infoH / islandScale - 20) / 2, 10.5f, 75, (float)s.infoAlpha, (float)s.fpsAlpha,
                (float)s.pingAlpha);
        target->PopAxisAlignedClip();
    }
    mh += (float)s.infoH / islandScale;
    if (s.notice && s.noticeAlpha > .001 && h > mh && noticeVisible) {
        const float noticeScale = (float)NoticeScale;
        const float nx = x / noticeScale, nw = w / noticeScale, ny = mh / noticeScale;
        float alpha = (float)s.noticeAlpha;
        target->PushAxisAlignedClip(D2D1::RectF(x, mh, x + w, h), D2D1_ANTIALIAS_MODE_ALIASED);
        target->SetTransform(D2D1::Matrix3x2F::Scale(islandScale * noticeScale, islandScale * noticeScale));
        if (mh > 0)
            rounded({nx + 24, ny, nw - 48, 1}, 0, D2D1::ColorF(.19f, .19f, .21f, alpha));
        float y = ny + 16.8f + 4 * (1 - alpha);
        float tw = (float)s.noticeTextWidth;
        float titleH = (float)std::min(76.8, textHeight(s.notice->title, 14, tw, true));
        nativeText(23,"host.notice.title",12,s.notice->title, nx + 24, y, tw, titleH, 14, D2D1::ColorF(1, 1, 1, alpha), true);
        if (!s.notice->body.empty())
            nativeText(24,"host.notice.body",12,s.notice->body, nx + 24, y + titleH + 6.4f, tw, (float)s.noticeBodyHeight, 12,
                 D2D1::ColorF(.875f, .875f, .895f, alpha), false);
        target->PopAxisAlignedClip();
        target->SetTransform(islandTransform);
    }
    if(s.modH>.1){
        target->SetTransform(islandTransform);
        float panel=(float)s.modH/islandScale,y=h-panel;
        target->PushAxisAlignedClip(D2D1::RectF(x,y,x+w,h),D2D1_ANTIALIAS_MODE_ALIASED);
        int row=0;
        for(auto& r:s.modResources){if(r.kind!=WI_BUTTON&&r.kind!=WI_LAYER)continue;if(row>=3)break;
            Box b{x+12,y+row*28+2,w-24,24};++row;
            auto node=sceneNode(r.handle,"legacy."+r.owner+"."+r.key,r.kind==WI_BUTTON?WI_SCENE_BUTTON:WI_TEXT,(b.x-x)*islandScale,b.y*islandScale,b.w*islandScale,b.h*islandScale);
            node.owner=r.owner;node.values[WI_PARENT]=sceneNumber(0);node.values[WI_PARENT].handle=1;
            node.values[WI_TEXT_VALUE]=sceneText(r.label+(r.kind==WI_LAYER&&!r.value.empty()?": "+r.value:""));node.values[WI_OPACITY]=sceneNumber(r.alpha);node.values[WI_FONT_SIZE]=sceneNumber(11*islandScale);
            if(r.kind==WI_BUTTON){node.values[WI_RADIUS]=sceneNumber(7*islandScale);node.values[WI_BACKGROUND]=sceneNumber(.18,.2,.24,1);node.values[WI_RECEIVE_INPUT]=sceneNumber(r.alpha>=.99f);node.values[WI_BLOCK_INPUT]=sceneNumber(1);}
            s.hostNodes.push_back(node);
        }
        target->PopAxisAlignedClip();
    }
    drawOpenScene(s,x*islandScale);
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

