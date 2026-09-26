#pragma once
#include "core.h"

namespace wi {
// Only the host's music rows use this policy; plugin text retains its own layout.
inline ComPtr<IDWriteTextLayout> musicTextLayout(IDWriteFactory* factory, std::wstring text,
    float size, float width, float height, const std::wstring& family,
    DWRITE_FONT_WEIGHT weight, DWRITE_TEXT_ALIGNMENT alignment) {
    for (auto& c : text)
        if (c == L'\r' || c == L'\n' || c == L'\t' || c == 0x2028 || c == 0x2029) c = L' ';
    width = std::max(1.f, width); height = std::max(1.f, height);
    ComPtr<IDWriteTextFormat> format;
    if (FAILED(factory->CreateTextFormat(family.c_str(), nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, std::max(.1f, size), L"zh-CN", &format))) return {};
    ComPtr<IDWriteTextLayout> result;
    if (FAILED(factory->CreateTextLayout(text.c_str(), (UINT32)text.size(), format.Get(), width, height, &result))) return {};
    result->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    DWRITE_TEXT_METRICS metrics{};
    result->GetMetrics(&metrics);
    // Fit real font metrics vertically. Modestly shrink long rows, then trim
    // with DirectWrite ellipsis rather than deleting UTF-16 code units.
    float fit = std::min(1.f, std::min(height / std::max(1.f, metrics.height),
        std::max(.85f, width / std::max(1.f, metrics.widthIncludingTrailingWhitespace))));
    result->SetFontSize(std::max(.1f, size * fit), {0, (UINT32)text.size()});
    result->SetTextAlignment(alignment);
    result->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    ComPtr<IDWriteInlineObject> ellipsis;
    factory->CreateEllipsisTrimmingSign(format.Get(), &ellipsis);
    DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    result->SetTrimming(&trim, ellipsis.Get());
    return result;
}
}
