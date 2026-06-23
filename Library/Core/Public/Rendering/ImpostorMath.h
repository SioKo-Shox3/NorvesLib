#pragma once

#include "Math/Vector2.h"
#include "Math/Vector3.h"
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    struct ImpostorAtlasCellSelection
    {
        Math::Vector2 EncodedUV;
        uint32_t CellX0 = 0;
        uint32_t CellY0 = 0;
        uint32_t CellX1 = 0;
        uint32_t CellY1 = 0;
        float BlendX = 0.0f;
        float BlendY = 0.0f;
        bool bValid = false;
    };

    Math::Vector2 EncodeOctahedralDirection(const Math::Vector3 &direction);
    ImpostorAtlasCellSelection SelectImpostorAtlasCells(const Math::Vector3 &direction,
                                                        uint32_t axisCellCountX,
                                                        uint32_t axisCellCountY);

} // namespace NorvesLib::Core::Rendering
