// UTF-8 の文字列を行に分け、左上原点・下向きの座標系に配置する。
#pragma once

#include "Container/List.h"
#include "Container/String.h"
#include "Container/StringView.h"
#include "Container/VariableArray.h"
#include "Math/Vector4.h"
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
        float IconAdvancePx = 0.0f; ///< 0 以下ではアセンダーとディセンダーの和を使う。
        Math::Vector4 DefaultColor{1.0f};

        bool operator==(const TextLayoutSettings&) const = default;
    };

    struct TextLayoutGlyph
    {
        uint32_t Codepoint = 0;   ///< インラインアイコンは 0。対応する要素は Icons から引く。
        Math::Vector2 PenPx;      ///< この字を描き始める基準線上の位置。
        float AdvancePx = 0.0f;
        uint32_t SourceByteOffset = 0; ///< 省略記号の場合は省略した範囲の先頭。
        uint32_t LineIndex = 0;
        Math::Vector4 Color{1.0f};
    };

    struct TextLayoutIcon
    {
        uint32_t GlyphIndex = 0; ///< Glyphs 内のアイコンの位置。
        Container::AnsiString Action;
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
        Container::VariableArray<TextLayoutIcon> Icons;
    };

    class TextLayout
    {
    public:
        /// 空文字列は 0 行。明示改行で終わる場合は、末尾の空行も配置する。
        static void Layout(Container::AnsiStringView utf8, const ITextMetrics& metrics,
                           const TextLayoutSettings& settings, TextLayoutResult& out);
    };

    /// ゲームスレッド用の組版キャッシュ。利用順に結果を保持する。
    class TextLayoutCache
    {
    public:
        /// 上限 0 では毎回組版する。
        explicit TextLayoutCache(size_t capacity = 256) : m_Capacity(capacity) {}

        /// 返した参照は次の GetOrLayout または Clear まで有効。
        const TextLayoutResult& GetOrLayout(Container::AnsiStringView utf8, const ITextMetrics& metrics,
                                            const TextLayoutSettings& settings);
        void Clear();

    private:
        struct Entry
        {
            uint64_t TextHash;
            uint64_t Generation;
            TextLayoutSettings Settings;
            Container::VariableArray<char> TextBytes;
            TextLayoutResult Result;
        };

        size_t m_Capacity;
        Container::List<Entry> m_Entries; ///< 先頭が直近に使われた項目。
    };
}
