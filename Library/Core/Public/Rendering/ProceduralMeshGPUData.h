#pragma once

#include "Rendering/RenderTypes.h"
#include "Container/Containers.h"
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
        Container::FixedArray<SubMeshRange, MAX_MATERIAL_SLOTS> SubMeshes;
        uint32_t SubMeshCount = 0;
    };

} // namespace NorvesLib::Core::Rendering
