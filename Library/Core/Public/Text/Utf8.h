#pragma once

#include "Container/Span.h"
#include "Container/String.h"
#include <cstdint>
#include <cstddef>

namespace NorvesLib::Core
{
    inline constexpr uint32_t ReplacementCodepoint = 0xFFFD;

    namespace Utf8Detail
    {
        /// UTF-8 の先頭の1文字を厳密に読み、成功した場合だけ出力を書き換える。
        template<typename Byte>
        [[nodiscard]] constexpr bool TryDecodeUtf8Strict(const Byte* data, size_t size,
            uint32_t& outScalar, size_t& outLength) noexcept
        {
            static_assert(sizeof(Byte) == 1);
            if (!data || size == 0)
            {
                return false;
            }

            uint32_t scalar = static_cast<uint8_t>(data[0]);
            uint32_t minimum = 0;
            size_t length = 1;
            if (scalar < 0x80)
            {
                // ASCII も通常のコードポイントとして扱う。
            }
            else if (scalar >= 0xc2 && scalar <= 0xdf)
            {
                scalar &= 0x1f;
                minimum = 0x80;
                length = 2;
            }
            else if (scalar >= 0xe0 && scalar <= 0xef)
            {
                scalar &= 0x0f;
                minimum = 0x800;
                length = 3;
            }
            else if (scalar >= 0xf0 && scalar <= 0xf4)
            {
                scalar &= 7;
                minimum = 0x10000;
                length = 4;
            }
            else
            {
                return false;
            }

            if (length > size)
            {
                return false;
            }
            for (size_t index = 1; index < length; ++index)
            {
                const uint32_t next = static_cast<uint8_t>(data[index]);
                if ((next & 0xc0) != 0x80)
                {
                    return false;
                }
                scalar = (scalar << 6) | (next & 0x3f);
            }
            if (scalar < minimum || scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff))
            {
                return false;
            }

            outScalar = scalar;
            outLength = length;
            return true;
        }
    } // namespace Utf8Detail

    /// UTF-8 の1文字の読み取り結果。不正なバイトは置換文字として1バイト進む。
    struct Utf8Decoded
    {
        uint32_t Codepoint = ReplacementCodepoint;
        size_t Length = 1;
        bool bValid = false;
    };

    /// 指定位置の1文字を読む。終端以降では長さ0を返し、位置を進めない。
    [[nodiscard]] Utf8Decoded DecodeUtf8(Container::Span<const char> text, size_t offset) noexcept;

    /// コードポイントを UTF-8 で追記する。不正な値は U+FFFD に置き換える。
    void AppendUtf8(Container::AnsiString& out, uint32_t codepoint);

    /// 空文字列を含め、全体が正しい UTF-8 かを判定する。
    [[nodiscard]] bool IsValidUtf8(Container::Span<const char> text) noexcept;

    /// 先頭から各コードポイントを渡す。不正な入力は1バイトごとに U+FFFD を渡す。
    template<typename Callback>
    void ForEachUtf8Codepoint(Container::Span<const char> text, Callback&& callback)
    {
        size_t offset = 0;
        while (offset < text.size())
        {
            const Utf8Decoded decoded = DecodeUtf8(text, offset);
            callback(decoded.Codepoint);
            offset += decoded.Length;
        }
    }
} // namespace NorvesLib::Core
