#pragma once

#if defined(NORVES_BRIDGE_ENABLED)

#include <cstddef>
#include <cstdint>
#include <string>

namespace Game::Bridge
{
    std::string EncodeBridgeBase64(const uint8_t* bytes, std::size_t byteCount);
} // namespace Game::Bridge

#endif // NORVES_BRIDGE_ENABLED
