#pragma once
#include <cstdint>

namespace NorvesLib
{
    enum class ECursorMode : uint8_t { Normal, Hidden, Confined, Locked };
    inline constexpr bool IsValidCursorMode(ECursorMode mode)
    {
        return mode == ECursorMode::Normal || mode == ECursorMode::Hidden ||
            mode == ECursorMode::Confined || mode == ECursorMode::Locked;
    }
} // namespace NorvesLib
