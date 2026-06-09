#pragma once

#include "Container/PointerTypes.h"

#include <cstdint>

namespace NorvesLib::RHI
{
    class IBuffer;
}

namespace NorvesLib::Core::Rendering
{
    struct ProceduralMeshGPUData
    {
        Container::TSharedPtr<RHI::IBuffer> VertexBuffer;
        Container::TSharedPtr<RHI::IBuffer> IndexBuffer;
        uint32_t IndexCount = 0;
    };

} // namespace NorvesLib::Core::Rendering
