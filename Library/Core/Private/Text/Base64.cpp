#include "Text/Base64.h"

namespace NorvesLib::Core::Text
{
    namespace
    {
        int Digit(uint8_t value) noexcept
        {
            if (value >= 'A' && value <= 'Z')
            {
                return value-'A';
            }
            if (value >= 'a' && value <= 'z')
            {
                return value-'a'+26;
            }
            if (value >= '0' && value <= '9')
            {
                return value-'0'+52;
            }
            if (value == '+')
            {
                return 62;
            }
            if (value == '/')
            {
                return 63;
            }
            return -1;
        }
    }

    Base64DecodeOutcome GetBase64DecodedSize(Container::Span<const uint8_t> encoded) noexcept
    {
        if (!encoded.empty() && encoded.data() == nullptr)
        {
            return {Base64DecodeResult::InvalidArgument, 0};
        }
        if ((encoded.size() & 3u) != 0)
        {
            return {Base64DecodeResult::InvalidLength, 0};
        }
        size_t padding = 0;
        for (size_t index = 0; index < encoded.size(); index += 4)
        {
            const uint8_t a = encoded[index];
            const uint8_t b = encoded[index+1];
            const uint8_t c = encoded[index+2];
            const uint8_t d = encoded[index+3];
            const bool last = encoded.size()-index == 4;
            if (a == '=' || b == '=' || (!last && (c == '=' || d == '=')))
            {
                return {Base64DecodeResult::InvalidPadding, 0};
            }
            if (Digit(a) < 0 || Digit(b) < 0)
            {
                return {Base64DecodeResult::InvalidCharacter, 0};
            }
            if (c == '=')
            {
                if (d != '=')
                {
                    return {Base64DecodeResult::InvalidPadding, 0};
                }
                if ((Digit(b) & 15) != 0)
                {
                    return {Base64DecodeResult::NonCanonicalPadding, 0};
                }
                padding = 2;
            }
            else
            {
                if (Digit(c) < 0)
                {
                    return {Base64DecodeResult::InvalidCharacter, 0};
                }
                if (d == '=')
                {
                    if ((Digit(c) & 3) != 0)
                    {
                        return {Base64DecodeResult::NonCanonicalPadding, 0};
                    }
                    padding = 1;
                }
                else if (Digit(d) < 0)
                {
                    return {Base64DecodeResult::InvalidCharacter, 0};
                }
            }
        }
        // 先に4で割るため、任意のsize_t長でも3倍でoverflowしない。
        return {Base64DecodeResult::Success, (encoded.size()/4)*3-padding};
    }

    Base64DecodeOutcome DecodeBase64(Container::Span<const uint8_t> encoded,
        Container::Span<uint8_t> output) noexcept
    {
        const auto validation = GetBase64DecodedSize(encoded);
        if (validation.Result != Base64DecodeResult::Success)
        {
            return validation;
        }
        const size_t needed = validation.Size;
        if (!output.empty() && output.data() == nullptr)
        {
            return {Base64DecodeResult::InvalidArgument, 0};
        }
        if (output.size() < needed)
        {
            return {Base64DecodeResult::InsufficientBuffer, 0};
        }
        if (!encoded.empty() && !output.empty())
        {
            const auto inputAddress = reinterpret_cast<uintptr_t>(encoded.data());
            const auto outputAddress = reinterpret_cast<uintptr_t>(output.data());
            // 終端アドレスを足さず、2つの開始アドレスの差で交差を判定する。
            const bool overlap = outputAddress >= inputAddress ? outputAddress-inputAddress < encoded.size()
                : inputAddress-outputAddress < output.size();
            if (overlap)
            {
                return {Base64DecodeResult::OverlappingBuffers, 0};
            }
        }
        size_t written = 0;
        for (size_t index = 0; index < encoded.size(); index += 4)
        {
            const auto a = static_cast<uint32_t>(Digit(encoded[index]));
            const auto b = static_cast<uint32_t>(Digit(encoded[index+1]));
            const auto c = encoded[index+2] == '=' ? 0u : static_cast<uint32_t>(Digit(encoded[index+2]));
            const auto d = encoded[index+3] == '=' ? 0u : static_cast<uint32_t>(Digit(encoded[index+3]));
            const uint32_t triplet = (a << 18) | (b << 12) | (c << 6) | d;
            output[written++] = static_cast<uint8_t>(triplet >> 16);
            if (written < needed)
            {
                output[written++] = static_cast<uint8_t>(triplet >> 8);
            }
            if (written < needed)
            {
                output[written++] = static_cast<uint8_t>(triplet);
            }
        }
        return {Base64DecodeResult::Success, written};
    }
}
