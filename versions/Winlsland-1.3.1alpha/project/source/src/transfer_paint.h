#pragma once
#include "render.h"
#include "transfer_outline.h"
namespace wi {
// Same Direct2D software / premultiplied-BGRA presentation used by the main
// island's software path. Text is rendered at native DPI, never bitmap-scaled.
struct TransferPaint {
    HDC dc=nullptr,pathDc=CreateCompatibleDC(nullptr);HBITMAP bitmap=nullptr;HGDIOBJ originalBitmap=nullptr;
    uint32_t* pixels=nullptr;int width=0,height=0;double dpi=0;unsigned rebuilds=0;
    ComPtr<ID2D1Factory> factory;ComPtr<ID2D1DCRenderTarget> target;ComPtr<ID2D1SolidColorBrush> brush;
    ComPtr<IDWriteFactory> write;ComPtr<IDWriteTextFormat> font,compactFont;
    std::map<std::wstring,ComPtr<IDWriteTextLayout>> texts;
    COLORREF black=RGB(0,0,0),card=RGB(24,25,29),hover=RGB(40,43,50),button=RGB(28,31,37),pressed=RGB(31,70,122),edge=RGB(49,52,60),focus=RGB(112,162,237);
    static void checked(HRESULT r){if(FAILED(r))throw std::runtime_error("File transfer Direct2D failure: "+std::to_string(r));}
    void ensure(HDC,int w,int h,double scale){
        if(!factory)checked(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory.GetAddressOf()));
        if(!write)checked(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),(IUnknown**)write.GetAddressOf()));
        if(!target){auto props=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));checked(factory->CreateDCRenderTarget(&props,&target));checked(target->CreateSolidColorBrush(D2D1::ColorF(0),&brush));target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);++rebuilds;}
        if(!dc)dc=CreateCompatibleDC(nullptr);
        if(w>width||h>height){if(bitmap){SelectObject(dc,originalBitmap);DeleteObject(bitmap);}width=std::max(width,((w+127)/128)*128);height=std::max(height,((h+127)/128)*128);
            BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),width,-height,1,32,BI_RGB};bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,(void**)&pixels,nullptr,0);if(!bitmap)throw std::runtime_error("File transfer surface allocation failed");originalBitmap=SelectObject(dc,bitmap);++rebuilds;}
        if(!font||dpi!=scale){dpi=scale;font.Reset();compactFont.Reset();texts.clear();checked(write->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,(float)(14*scale),L"zh-CN",&font));checked(write->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,(float)(12*scale),L"zh-CN",&compactFont));++rebuilds;}
        RECT rc{0,0,w,h};checked(target->BindDC(dc,&rc));target->SetDpi(96,96);
    }
    void color(COLORREF c){brush->SetColor(D2D1::ColorF(GetRValue(c)/255.f,GetGValue(c)/255.f,GetBValue(c)/255.f));}
    static D2D1_RECT_F box(RECT r){return D2D1::RectF((float)r.left,(float)r.top,(float)r.right,(float)r.bottom);}
    void round(RECT r,COLORREF fill,bool focused,double scale){auto b=box(r);auto rr=D2D1::RoundedRect(b,(float)(7*scale),(float)(7*scale));color(fill);target->FillRoundedRectangle(rr,brush.Get());color(focused?focus:edge);target->DrawRoundedRectangle(rr,brush.Get(),(float)((focused?2:1)*scale));}
    void text(const std::wstring& value,RECT r,COLORREF c,UINT flags,bool smallFont){
        float w=(float)std::max(1L,r.right-r.left),h=(float)std::max(1L,r.bottom-r.top);auto key=value+L"\x1f"+std::to_wstring((int)w)+L":"+std::to_wstring((int)h)+L":"+std::to_wstring(flags)+(smallFont?L"s":L"n");
        auto it=texts.find(key);ComPtr<IDWriteTextLayout> layout;
        if(it!=texts.end())layout=it->second;else{auto format=smallFont?compactFont.Get():font.Get();checked(write->CreateTextLayout(value.c_str(),(UINT)value.size(),format,w,h,&layout));layout->SetWordWrapping(flags&DT_WORDBREAK?DWRITE_WORD_WRAPPING_WRAP:DWRITE_WORD_WRAPPING_NO_WRAP);layout->SetTextAlignment(flags&DT_CENTER?DWRITE_TEXT_ALIGNMENT_CENTER:DWRITE_TEXT_ALIGNMENT_LEADING);layout->SetParagraphAlignment(flags&DT_VCENTER?DWRITE_PARAGRAPH_ALIGNMENT_CENTER:DWRITE_PARAGRAPH_ALIGNMENT_NEAR);if(flags&DT_END_ELLIPSIS){ComPtr<IDWriteInlineObject> ellipsis;write->CreateEllipsisTrimmingSign(format,&ellipsis);DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER,0,0};layout->SetTrimming(&trim,ellipsis.Get());}if(texts.size()>512)texts.clear();texts.emplace(key,layout);}
        color(c);target->DrawTextLayout(D2D1::Point2F((float)r.left,(float)r.top),layout.Get(),brush.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    ~TransferPaint(){if(dc){SelectObject(dc,originalBitmap);DeleteDC(dc);}if(bitmap)DeleteObject(bitmap);if(pathDc)DeleteDC(pathDc);}
};
}
