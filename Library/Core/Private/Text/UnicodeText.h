#pragma once
#include "Container/Span.h"
#include <cstdint>
#include <limits>
#include <type_traits>

namespace NorvesLib::Core::TextDetail
{
    template<typename Char>
    [[nodiscard]] constexpr bool IsJsonDigit(Char ch) noexcept
    {
        return ch >= '0' && ch <= '9';
    }
    template<typename Char>
    [[nodiscard]] constexpr bool IsJsonWhitespace(Char ch) noexcept
    {
        return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
    }
    // 1byteはUTF8、2byteはUTF16、4byteはscalar。NULはUnicodeとして許可し、JSON構文で別途判定する。
    // callbackは失敗前のprefixについても呼ばれ得る。入力の変更・失効を起こしてはならない。
    template<typename Char, typename Callback>
    [[nodiscard]] bool ForEachUnicodeScalar(Container::Span<const Char> text, Callback callback)
    {
        static_assert(std::is_integral_v<Char> && !std::is_same_v<Char,bool>);
        static_assert(sizeof(Char) == 1 || sizeof(Char) == 2 || sizeof(Char) == 4);
        const uintptr_t address = reinterpret_cast<uintptr_t>(text.data());
        if (text.size() != 0 && (!text.data() || address % alignof(Char) != 0 ||
            text.size() > SIZE_MAX / sizeof(Char) || text.size() * sizeof(Char) > UINTPTR_MAX - address))
        {
            return false;
        }
        using Unsigned = std::make_unsigned_t<Char>;
        size_t cursor = 0;
        while (cursor < text.size())
        {
            uint32_t scalar = static_cast<uint32_t>(static_cast<Unsigned>(text[cursor++]));
            if constexpr (sizeof(Char) == 1)
            {
                uint32_t minimum = 0;
                size_t extra = 0;
                if (scalar < 0x80)
                {
                    // ASCIIも通常のscalarとして渡す。
                }
                else if (scalar >= 0xc2 && scalar <= 0xdf)
                {
                    scalar &= 0x1f; minimum = 0x80; extra = 1;
                }
                else if (scalar >= 0xe0 && scalar <= 0xef)
                {
                    scalar &= 0x0f; minimum = 0x800; extra = 2;
                }
                else if (scalar >= 0xf0 && scalar <= 0xf4)
                {
                    scalar &= 7; minimum = 0x10000; extra = 3;
                }
                else
                {
                    return false;
                }
                if (extra > text.size() - cursor)
                {
                    return false;
                }
                for (size_t index = 0; index < extra; ++index)
                {
                    const uint32_t next = static_cast<uint32_t>(static_cast<Unsigned>(text[cursor++]));
                    if ((next & 0xc0) != 0x80)
                    {
                        return false;
                    }
                    scalar = (scalar << 6) | (next & 0x3f);
                }
                if (scalar < minimum)
                {
                    return false;
                }
            }
            else if constexpr (sizeof(Char) == 2)
            {
                if (scalar >= 0xd800 && scalar <= 0xdbff)
                {
                    if (cursor == text.size())
                    {
                        return false;
                    }
                    const uint32_t low = static_cast<uint32_t>(static_cast<Unsigned>(text[cursor++]));
                    if (low < 0xdc00 || low > 0xdfff)
                    {
                        return false;
                    }
                    scalar = 0x10000 + ((scalar - 0xd800) << 10) + (low - 0xdc00);
                }
            }
            if (scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff))
            {
                return false;
            }
            callback(scalar);
        }
        return true;
    }
}
