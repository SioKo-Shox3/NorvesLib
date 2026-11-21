#include "Text/Utf8.h"
#include "Text/JsonUnicodeScalar.h"

namespace NorvesLib::Core
{
    Utf8Decoded DecodeUtf8(Container::Span<const char> text, size_t offset) noexcept
    {
        if (offset >= text.size())
        {
            return {ReplacementCodepoint, 0, false};
        }
        Utf8Decoded decoded;
        if (text.data())
        {
            decoded.bValid = Utf8Detail::TryDecodeUtf8Strict(text.data() + offset, text.size() - offset,
                decoded.Codepoint, decoded.Length);
        }
        return decoded;
    }

    void AppendUtf8(Container::AnsiString& out, uint32_t codepoint)
    {
        auto encoded = TextDetail::EncodeJsonUnicodeScalar<char>(codepoint);
        if (encoded.Count == 0)
        {
            encoded = TextDetail::EncodeJsonUnicodeScalar<char>(ReplacementCodepoint);
        }
        out.append(encoded.Units, encoded.Count);
    }

    bool IsValidUtf8(Container::Span<const char> text) noexcept
    {
        if (!text.data() && text.size() != 0)
        {
            return false;
        }
        size_t offset = 0;
        while (offset < text.size())
        {
            uint32_t scalar = 0;
            size_t length = 0;
            if (!Utf8Detail::TryDecodeUtf8Strict(text.data() + offset, text.size() - offset, scalar, length))
            {
                return false;
            }
            offset += length;
        }
        return true;
    }
} // namespace NorvesLib::Core
