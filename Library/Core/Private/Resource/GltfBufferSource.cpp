#include "Resource/GltfBufferSource.h"

namespace NorvesLib::Core::Gltf
{
    namespace
    {
        int Hex(uint8_t byte) noexcept
        {
            if (byte >= '0' && byte <= '9')
            {
                return byte-'0';
            }
            if (byte >= 'a' && byte <= 'f')
            {
                return byte-'a'+10;
            }
            if (byte >= 'A' && byte <= 'F')
            {
                return byte-'A'+10;
            }
            return -1;
        }
        bool NextByte(Container::Span<const uint8_t> encoded, size_t& index, uint8_t& value) noexcept
        {
            if (encoded[index] != '%')
            {
                value = encoded[index++];
                return true;
            }
            if (encoded.size()-index < 3 || Hex(encoded[index+1]) < 0 || Hex(encoded[index+2]) < 0)
            {
                return false;
            }
            value = static_cast<uint8_t>(Hex(encoded[index+1])*16+Hex(encoded[index+2]));
            index += 3;
            return true;
        }
        uint8_t Lower(uint8_t byte) noexcept
        {
            return byte >= 'A' && byte <= 'Z' ? static_cast<uint8_t>(byte-'A'+'a') : byte;
        }
        bool Equals(Container::Span<const uint8_t> bytes, const char* text, bool bPercentDecode) noexcept
        {
            size_t index = 0;
            size_t expected = 0;
            while (index < bytes.size() && text[expected] != 0)
            {
                uint8_t value;
                if (bPercentDecode)
                {
                    if (!NextByte(bytes, index, value))
                    {
                        return false;
                    }
                }
                else
                {
                    value = bytes[index++];
                }
                if (Lower(value) != static_cast<uint8_t>(text[expected++]))
                {
                    return false;
                }
            }
            return index == bytes.size() && text[expected] == 0;
        }
        bool Token(uint8_t value) noexcept
        {
            if (value <= 32 || value >= 127 || value == '\\')
            {
                return false;
            }
            for (const char* bad = "()<>@,;:\"/[]?="; *bad; ++bad)
            {
                if (value == static_cast<uint8_t>(*bad))
                {
                    return false;
                }
            }
            return true;
        }
        bool UrlCharacter(uint8_t value) noexcept
        {
            if ((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
                (value >= '0' && value <= '9'))
            {
                return true;
            }
            for (const char* allowed = "-_.!~*'();/?:@&=+$,"; *allowed; ++allowed)
            {
                if (value == static_cast<uint8_t>(*allowed))
                {
                    return true;
                }
            }
            return false;
        }
        bool ParameterPart(Container::Span<const uint8_t> part, bool bParameterValue) noexcept
        {
            if (part.empty())
            {
                return false;
            }
            size_t index = 0;
            while (index < part.size())
            {
                const bool bEscaped = part[index] == '%';
                if (!bEscaped && !UrlCharacter(part[index]))
                {
                    return false;
                }
                uint8_t byte;
                if (!NextByte(part, index, byte))
                {
                    return false;
                }
                if (bParameterValue && bEscaped)
                {
                    if (byte < 32 || byte >= 127)
                    {
                        return false;
                    }
                }
                else if (!Token(byte))
                {
                    return false;
                }
            }
            return true;
        }
    }

    ByteDecodeOutcome GetPercentDecodedSize(Container::Span<const uint8_t> encoded) noexcept
    {
        if (!encoded.empty() && encoded.data() == nullptr)
        {
            return {BufferSourceResult::InvalidArgument, 0};
        }
        size_t size = 0;
        size_t index = 0;
        while (index < encoded.size())
        {
            uint8_t value;
            if (!NextByte(encoded, index, value))
            {
                return {BufferSourceResult::InvalidPercentEscape, 0};
            }
            ++size;
        }
        return {BufferSourceResult::Success, size};
    }

    ByteDecodeOutcome DecodePercentBytes(Container::Span<const uint8_t> encoded,
        Container::Span<uint8_t> output) noexcept
    {
        const auto validation = GetPercentDecodedSize(encoded);
        if (validation.Result != BufferSourceResult::Success)
        {
            return validation;
        }
        if (!output.empty() && output.data() == nullptr)
        {
            return {BufferSourceResult::InvalidArgument, 0};
        }
        if (output.size() < validation.Size)
        {
            return {BufferSourceResult::InsufficientBuffer, 0};
        }
        if (!encoded.empty() && !output.empty())
        {
            const auto inputAddress = reinterpret_cast<uintptr_t>(encoded.data());
            const auto outputAddress = reinterpret_cast<uintptr_t>(output.data());
            const bool bOverlap = outputAddress >= inputAddress ? outputAddress-inputAddress < encoded.size()
                : inputAddress-outputAddress < output.size();
            if (bOverlap)
            {
                return {BufferSourceResult::OverlappingBuffers, 0};
            }
        }
        size_t index = 0;
        size_t written = 0;
        while (index < encoded.size())
        {
            uint8_t byte = 0;
            (void)NextByte(encoded, index, byte);
            output[written++] = byte;
        }
        return {BufferSourceResult::Success, written};
    }

    DataUriOutcome ParseDataUri(Container::Span<const uint8_t> uri) noexcept
    {
        if (!uri.empty() && uri.data() == nullptr)
        {
            return {BufferSourceResult::InvalidArgument, {}};
        }
        if (uri.size() < 5 || !Equals({uri.data(), 5}, "data:", false))
        {
            return {BufferSourceResult::NotDataUri, {}};
        }
        size_t comma = 5;
        while (comma < uri.size() && uri[comma] != ',')
        {
            ++comma;
        }
        if (comma == uri.size())
        {
            return {BufferSourceResult::InvalidDataUri, {}};
        }
        size_t separator = 5;
        while (separator < comma && uri[separator] != ';')
        {
            ++separator;
        }
        const Container::Span<const uint8_t> media(uri.data()+5, separator-5);
        DataUriMime mime = DataUriMime::Unknown;
        if (Equals(media, "application/octet-stream", true))
        {
            mime = DataUriMime::OctetStream;
        }
        else if (Equals(media, "application/gltf-buffer", true))
        {
            mime = DataUriMime::GltfBuffer;
        }
        else if (Equals(media, "image/png", true))
        {
            mime = DataUriMime::Png;
        }
        else if (Equals(media, "image/jpeg", true))
        {
            mime = DataUriMime::Jpeg;
        }
        if (mime == DataUriMime::Unknown)
        {
            return {BufferSourceResult::UnsupportedMediaType, {}};
        }
        bool bBase64 = false;
        while (separator < comma)
        {
            const size_t start = separator+1;
            size_t end = start;
            while (end < comma && uri[end] != ';')
            {
                ++end;
            }
            const Container::Span<const uint8_t> field(uri.data()+start, end-start);
            if (Equals(field, "base64", false) && end == comma)
            {
                bBase64 = true;
            }
            else
            {
                size_t equal = 0;
                while (equal < field.size() && field[equal] != '=')
                {
                    ++equal;
                }
                if (equal == field.size() || !ParameterPart({field.data(), equal}, false) ||
                    !ParameterPart({field.data()+equal+1, field.size()-equal-1}, true))
                {
                    return {BufferSourceResult::InvalidDataUri, {}};
                }
            }
            separator = end;
        }
        if (!bBase64)
        {
            return {BufferSourceResult::UnsupportedEncoding, {}};
        }
        const Container::Span<const uint8_t> payload(uri.data()+comma+1, uri.size()-comma-1);
        for (size_t index = 0; index < payload.size();)
        {
            if (payload[index] != '%' && !UrlCharacter(payload[index]))
            {
                return {BufferSourceResult::InvalidDataUri, {}};
            }
            uint8_t byte;
            if (!NextByte(payload, index, byte))
            {
                return {BufferSourceResult::InvalidPercentEscape, {}};
            }
        }
        const auto decoded = GetPercentDecodedSize(payload);
        if (decoded.Result != BufferSourceResult::Success)
        {
            return {decoded.Result, {}};
        }
        return {BufferSourceResult::Success, {mime, payload, decoded.Size}};
    }

    BinBufferOutcome BindGlbBuffer(const ContainerView& container, size_t bufferIndex,
        size_t declaredByteLength) noexcept
    {
        if (!container.IsGlb || !container.HasBin)
        {
            return {BufferSourceResult::MissingBin, {}};
        }
        if (bufferIndex != 0)
        {
            return {BufferSourceResult::InvalidBinIndex, {}};
        }
        if ((!container.Bin.empty() && container.Bin.data() == nullptr) || (container.Bin.size() & 3u) != 0)
        {
            return {BufferSourceResult::InvalidArgument, {}};
        }
        if (declaredByteLength == 0 || declaredByteLength > container.Bin.size() ||
            container.Bin.size()-declaredByteLength > 3)
        {
            return {BufferSourceResult::InvalidByteLength, {}};
        }
        for (size_t index = declaredByteLength; index < container.Bin.size(); ++index)
        {
            if (container.Bin[index] != 0)
            {
                return {BufferSourceResult::InvalidBinPadding, {}};
            }
        }
        return {BufferSourceResult::Success, {container.Bin.data(), declaredByteLength}};
    }
}
