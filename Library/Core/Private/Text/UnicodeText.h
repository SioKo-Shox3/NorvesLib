#pragma once
#include "Container/Span.h"
#include "Text/Utf8.h"
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
                size_t length = 0;
                if (!Utf8Detail::TryDecodeUtf8Strict(text.data() + cursor - 1, text.size() - cursor + 1,
                    scalar, length))
                {
                    return false;
                }
                cursor += length - 1;
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
