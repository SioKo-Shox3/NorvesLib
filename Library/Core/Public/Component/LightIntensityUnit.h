#pragma once

#include <cstdint>

namespace NorvesLib::Core::Component
{
    /// ライトの強度の単位
    enum class LightIntensityUnit : uint8_t
    {
        Lux,
        Candela,
        Lumen
    };
}
