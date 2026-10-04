#pragma once

#include "Container/Span.h"
#include <cstdint>

namespace NorvesLib::Core::TextDetail
{
    struct JsonUnicodeEscape
    {
        uint32_t Scalar = 0;
        size_t Consumed = 0;
    };
    // 最初の\\uの直後から読む。surrogateは対だけを認め、失敗時outを保持する。
    template<typename Char>
    [[nodiscard]] bool DecodeJsonUnicodeEscape(Container::Span<const Char> source, JsonUnicodeEscape& out) noexcept
    {
        const auto hex4 = [&source](size_t start, uint32_t& value)
        {
            if (!source.data() || start > source.size() || source.size() - start < 4)
            {
                return false;
            }
            value = 0;
            for (size_t index = 0; index < 4; ++index)
            {
                const uint32_t ch = static_cast<uint32_t>(source[start + index]);
                uint32_t digit = 0;
                if (ch >= '0' && ch <= '9')
                {
                    digit = ch - '0';
                }
                else if (ch >= 'a' && ch <= 'f')
                {
                    digit = ch - 'a' + 10;
                }
                else if (ch >= 'A' && ch <= 'F')
                {
                    digit = ch - 'A' + 10;
                }
                else
                {
                    return false;
                }
                value = (value << 4) | digit;
            }
            return true;
        };
        uint32_t scalar = 0;
        if (!hex4(0, scalar) || (scalar >= 0xdc00 && scalar <= 0xdfff))
        {
            return false;
        }
        size_t consumed = 4;
        if (scalar >= 0xd800 && scalar <= 0xdbff)
        {
            uint32_t low = 0;
            if (source.size() < 10 || source[4] != '\\' || source[5] != 'u' ||
                !hex4(6, low) || low < 0xdc00 || low > 0xdfff)
            {
                return false;
            }
            scalar = 0x10000 + ((scalar - 0xd800) << 10) + (low - 0xdc00);
            consumed = 10;
        }
        out = {scalar, consumed};
        return true;
    }

    template<typename Char>
    struct JsonUnicodeUnits
    {
        Char Units[4]{};
        size_t Count = 0;
    };
    // 1byte CharはUTF-8、2byteはUTF-16、4byteはscalar。JSON内のU+0000も表現できる。
    template<typename Char>
    [[nodiscard]] JsonUnicodeUnits<Char> EncodeJsonUnicodeScalar(uint32_t scalar) noexcept
    {
        static_assert(sizeof(Char) == 1 || sizeof(Char) == 2 || sizeof(Char) == 4);
        JsonUnicodeUnits<Char> result;
        if (scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff))
        {
            return result;
        }
        if constexpr (sizeof(Char) == 1)
        {
            if (scalar < 0x80)
            {
                result.Units[0] = static_cast<Char>(scalar); result.Count = 1;
            }
            else if (scalar < 0x800)
            {
                result.Units[0] = static_cast<Char>(0xc0 | (scalar >> 6));
                result.Units[1] = static_cast<Char>(0x80 | (scalar & 0x3f)); result.Count = 2;
            }
            else if (scalar < 0x10000)
            {
                result.Units[0] = static_cast<Char>(0xe0 | (scalar >> 12));
                result.Units[1] = static_cast<Char>(0x80 | ((scalar >> 6) & 0x3f));
                result.Units[2] = static_cast<Char>(0x80 | (scalar & 0x3f)); result.Count = 3;
            }
            else
            {
                result.Units[0] = static_cast<Char>(0xf0 | (scalar >> 18));
                result.Units[1] = static_cast<Char>(0x80 | ((scalar >> 12) & 0x3f));
                result.Units[2] = static_cast<Char>(0x80 | ((scalar >> 6) & 0x3f));
                result.Units[3] = static_cast<Char>(0x80 | (scalar & 0x3f)); result.Count = 4;
            }
        }
        else if constexpr (sizeof(Char) == 2)
        {
            if (scalar >= 0x10000)
            {
                scalar -= 0x10000;
                result.Units[0] = static_cast<Char>(0xd800 + (scalar >> 10));
                result.Units[1] = static_cast<Char>(0xdc00 + (scalar & 0x3ff)); result.Count = 2;
            }
            else
            {
                result.Units[0] = static_cast<Char>(scalar); result.Count = 1;
            }
        }
        else
        {
            result.Units[0] = static_cast<Char>(scalar); result.Count = 1;
        }
        return result;
    }
} // namespace NorvesLib::Core::TextDetail
