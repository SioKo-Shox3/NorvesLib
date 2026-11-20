// UTF-8 の文字列を行に分け、左上原点・下向きの座標系に配置する。
#pragma once

#include "Container/StringView.h"
#include "Container/VariableArray.h"
#include "Text/TextMetrics.h"

namespace NorvesLib::Core
{
    enum class TextAlign : uint8_t { Left, Center, Right };

    struct TextLayoutSettings
    {
        float MaxWidthPx = 0.0f;  ///< 0 以下では自動折り返しを行わない。
        TextAlign Align = TextAlign::Left;
        float LineSpacing = 1.0f; ///< 行送りの倍率。
        uint32_t MaxLines = 0;    ///< 0 で行数を無制限にする。
        bool bEllipsis = true;
    };

    struct TextLayoutGlyph
    {
        uint32_t Codepoint = 0;
        Math::Vector2 PenPx;      ///< この字を描き始める基準線上の位置。
        float AdvancePx = 0.0f;
        uint32_t SourceByteOffset = 0; ///< 省略記号の場合は省略した範囲の先頭。
        uint32_t LineIndex = 0;
    };

    struct TextLayoutLine
    {
        uint32_t FirstGlyph = 0;
        uint32_t GlyphCount = 0;
        float WidthPx = 0.0f;     ///< 行末の空白を除いた幅。
        float BaselineYPx = 0.0f;
    };

    struct TextLayoutResult
    {
        Container::VariableArray<TextLayoutGlyph> Glyphs;
        Container::VariableArray<TextLayoutLine> Lines;
        Math::Vector2 SizePx;
        bool bTruncated = false;
    };

    class TextLayout
    {
    public:
        /// 空文字列は 0 行。明示改行で終わる場合は、末尾の空行も配置する。
        static void Layout(Container::AnsiStringView utf8, const ITextMetrics& metrics,
                           const TextLayoutSettings& settings, TextLayoutResult& out);
    };
}
