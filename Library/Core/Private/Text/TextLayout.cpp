// 文字の計量、改行位置の決定、基準線への配置を順に行う。
#include "Text/TextLayout.h"
#include "Text/Utf8.h"

namespace NorvesLib::Core
{
    namespace
    {
        constexpr char32_t NoLineStart[] =
            U"、。，．・：；？！ーぁぃぅぇぉっゃゅょゎゕゖァィゥェォッャュョヮヵヶ"
            U"ㇰㇱㇲㇳㇴㇵㇶㇷㇸㇹㇺㇻㇼㇽㇾㇿ）」』】〕〉》］｝〙〗”’)]},.!?:;";
        constexpr char32_t NoLineEnd[] = U"（「『【〔〈《［｛〘〖“‘([{";
        constexpr uint32_t Ellipsis = 0x2026;

        struct TextElement
        {
            uint32_t Codepoint;
            uint32_t ByteOffset;
            float AdvancePx;
            bool bCjk;
            bool bSpace;
            bool bNoLineStart;
            bool bNoLineEnd;
        };

        using Elements = Container::VariableArray<TextElement>;

        template<size_t N>
        bool Contains(const char32_t (&table)[N], uint32_t codepoint)
        {
            for (size_t i = 0; i + 1 < N; ++i)
            {
                if (table[i] == codepoint)
                {
                    return true;
                }
            }
            return false;
        }

        bool IsCjk(uint32_t c)
        {
            return (c >= 0x3000 && c <= 0x30FF) || (c >= 0x31F0 && c <= 0x31FF)
                || (c >= 0x3400 && c <= 0x4DBF) || (c >= 0x4E00 && c <= 0x9FFF)
                || (c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFF00 && c <= 0xFFEF)
                || (c >= 0x1AFF0 && c <= 0x1AFFF) || (c >= 0x1B000 && c <= 0x1B16F)
                || (c >= 0x20000 && c <= 0x2EE5F) || (c >= 0x2F800 && c <= 0x2FA1F)
                || (c >= 0x30000 && c <= 0x3347F);
        }

        void ReadElements(Container::AnsiStringView utf8, const ITextMetrics& metrics, Elements& elements)
        {
            const auto bytes = utf8.as_span();
            size_t offset = 0;
            while (offset < bytes.size())
            {
                const auto start = static_cast<uint32_t>(offset);
                const Utf8Decoded decoded = DecodeUtf8(bytes, offset);
                uint32_t c = decoded.Codepoint;
                offset += decoded.Length;
                if (c == '\r' && offset < bytes.size() && bytes[offset] == '\n')
                {
                    c = '\n';
                    ++offset;
                }
                const float advance = c == '\n' ? 0.0f : metrics.GetGlyphMetrics(c).AdvancePx;
                const bool space = c == ' ' || c == '\t' || c == 0x3000;
                // 行末空白を幅から除くため、その手前の開き括弧の禁則も引き継ぐ。
                const bool noEnd = space ? (!elements.empty() && elements.back().bNoLineEnd)
                                         : Contains(NoLineEnd, c);
                elements.push_back({c, start, advance, IsCjk(c), space, Contains(NoLineStart, c), noEnd});
            }
        }

        bool IsWordBoundary(const TextElement& left, const TextElement& right)
        {
            return left.Codepoint == ' ' || left.bCjk || right.bCjk;
        }

        size_t ChooseWrap(const Elements& elements, size_t first, size_t overflow)
        {
            size_t candidate = overflow;
            while (candidate > first && !IsWordBoundary(elements[candidate - 1], elements[candidate]))
            {
                --candidate;
            }
            // 幅を超えた一語の途中なら文字境界を使う。
            const bool splitWord = candidate == first;
            if (splitWord)
            {
                candidate = overflow;
            }

            size_t nearest = first;
            for (size_t cut = candidate; cut > first; --cut)
            {
                const auto& left = elements[cut - 1];
                const auto& right = elements[cut];
                if (left.bNoLineEnd || right.bNoLineStart || right.bSpace)
                {
                    continue;
                }
                if (splitWord || IsWordBoundary(left, right))
                {
                    return cut;
                }
                if (nearest == first)
                {
                    nearest = cut;
                }
            }
            // 語の途中でも禁則を守れるなら追い出し、空行になる場合だけ元の位置で切る。
            return nearest != first ? nearest : candidate;
        }

        size_t FindLineEnd(const Elements& elements, size_t first, const TextLayoutSettings& settings)
        {
            float width = 0.0f;
            for (size_t i = first; i < elements.size(); ++i)
            {
                const auto& element = elements[i];
                if (element.Codepoint == '\n')
                {
                    return i;
                }
                width += element.AdvancePx;
                if (settings.MaxWidthPx > 0.0f && !element.bSpace && width > settings.MaxWidthPx)
                {
                    if (settings.MaxLines == 1)
                    {
                        return i;
                    }
                    if (i > first)
                    {
                        return ChooseWrap(elements, first, i);
                    }
                    // 先頭の一字だけで幅を超えても、その行で必ず消費する。
                }
            }
            return elements.size();
        }

        void AppendLine(const Elements& elements, size_t first, size_t end, bool ellipsize,
                        const ITextMetrics& metrics, float maxWidth, TextLayoutResult& out)
        {
            const auto lineIndex = static_cast<uint32_t>(out.Lines.size());
            auto& line = out.Lines.emplace_back();
            line.FirstGlyph = static_cast<uint32_t>(out.Glyphs.size());
            float markerWidth = 0.0f;
            if (ellipsize)
            {
                markerWidth = metrics.GetGlyphMetrics(Ellipsis).AdvancePx;
                if (maxWidth > 0.0f && markerWidth > maxWidth)
                {
                    return;
                }
            }

            float pen = 0.0f;
            for (size_t i = first; i < end; ++i)
            {
                const auto& element = elements[i];
                if (ellipsize && maxWidth > 0.0f && pen + element.AdvancePx + markerWidth > maxWidth)
                {
                    break;
                }
                out.Glyphs.push_back({element.Codepoint, {}, element.AdvancePx, element.ByteOffset, lineIndex});
                pen += element.AdvancePx;
                if (!element.bSpace)
                {
                    line.WidthPx = pen;
                }
            }
            if (ellipsize)
            {
                size_t cut = first + (out.Glyphs.size() - line.FirstGlyph);
                while (cut > first && elements[cut - 1].bSpace)
                {
                    out.Glyphs.pop_back();
                    --cut;
                }
                out.Glyphs.push_back({Ellipsis, {}, markerWidth, elements[cut].ByteOffset, lineIndex});
                line.WidthPx += markerWidth;
            }
            line.GlyphCount = static_cast<uint32_t>(out.Glyphs.size() - line.FirstGlyph);
            if (line.WidthPx > out.SizePx.x)
            {
                out.SizePx.x = line.WidthPx;
            }
        }

        void BreakLines(const Elements& elements, const ITextMetrics& metrics,
                        const TextLayoutSettings& settings, TextLayoutResult& out)
        {
            size_t first = 0;
            for (;;)
            {
                const size_t end = FindLineEnd(elements, first, settings);
                out.bTruncated = end < elements.size() && settings.MaxLines == out.Lines.size() + 1;
                AppendLine(elements, first, end, out.bTruncated && settings.bEllipsis,
                           metrics, settings.MaxWidthPx, out);
                if (end == elements.size() || out.bTruncated)
                {
                    return;
                }
                first = elements[end].Codepoint == '\n' ? end + 1 : end;
            }
        }

        void PositionLines(const ITextMetrics& metrics, const TextLayoutSettings& settings, TextLayoutResult& out)
        {
            const float ascender = metrics.GetAscenderPx();
            const float step = metrics.GetLineAdvancePx() * settings.LineSpacing;
            const float boxWidth = settings.MaxWidthPx > 0.0f ? settings.MaxWidthPx : out.SizePx.x;
            for (size_t i = 0; i < out.Lines.size(); ++i)
            {
                auto& line = out.Lines[i];
                line.BaselineYPx = ascender + static_cast<float>(i) * step;
                float x = 0.0f;
                if (settings.Align == TextAlign::Center)
                {
                    x = (boxWidth - line.WidthPx) * 0.5f;
                }
                else if (settings.Align == TextAlign::Right)
                {
                    x = boxWidth - line.WidthPx;
                }
                for (size_t j = 0; j < line.GlyphCount; ++j)
                {
                    auto& glyph = out.Glyphs[line.FirstGlyph + j];
                    glyph.PenPx = {x, line.BaselineYPx};
                    x += glyph.AdvancePx;
                }
            }
            out.SizePx.y = out.Lines.back().BaselineYPx + metrics.GetDescenderPx();
        }
    }

    void TextLayout::Layout(Container::AnsiStringView utf8, const ITextMetrics& metrics,
                            const TextLayoutSettings& settings, TextLayoutResult& out)
    {
        out.Glyphs.clear();
        out.Lines.clear();
        out.SizePx = {};
        out.bTruncated = false;
        if (utf8.empty())
        {
            return;
        }
        Elements elements;
        ReadElements(utf8, metrics, elements);
        BreakLines(elements, metrics, settings, out);
        PositionLines(metrics, settings, out);
    }
}
