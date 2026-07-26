#include "BridgeBase64.h"

#if defined(NORVES_BRIDGE_ENABLED)

namespace Game::Bridge
{
    std::string EncodeBridgeBase64(const uint8_t* bytes, std::size_t byteCount)
    {
        if (bytes == nullptr || byteCount == 0)
        {
            return std::string{};
        }

        static constexpr char kAlphabet[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        std::string encoded;
        encoded.reserve(((byteCount + 2u) / 3u) * 4u);

        std::size_t index = 0;
        while (index + 3u <= byteCount)
        {
            const uint32_t triple =
                (static_cast<uint32_t>(bytes[index]) << 16u) |
                (static_cast<uint32_t>(bytes[index + 1u]) << 8u) |
                static_cast<uint32_t>(bytes[index + 2u]);
            encoded += kAlphabet[(triple >> 18u) & 0x3Fu];
            encoded += kAlphabet[(triple >> 12u) & 0x3Fu];
            encoded += kAlphabet[(triple >> 6u) & 0x3Fu];
            encoded += kAlphabet[triple & 0x3Fu];
            index += 3u;
        }

        const std::size_t remaining = byteCount - index;
        if (remaining > 0u)
        {
            uint32_t triple = static_cast<uint32_t>(bytes[index]) << 16u;
            if (remaining == 2u)
            {
                triple |= static_cast<uint32_t>(bytes[index + 1u]) << 8u;
            }
            encoded += kAlphabet[(triple >> 18u) & 0x3Fu];
            encoded += kAlphabet[(triple >> 12u) & 0x3Fu];
            encoded += remaining == 2u ? kAlphabet[(triple >> 6u) & 0x3Fu] : '=';
            encoded += '=';
        }

        return encoded;
    }
} // namespace Game::Bridge

#endif // NORVES_BRIDGE_ENABLED
