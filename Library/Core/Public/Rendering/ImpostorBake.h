#pragma once

#include "Rendering/GpuResourceTypes.h"
#include "Rendering/ProceduralMeshGenerator.h"
#include "Rendering/RenderTypes.h"
#include "Container/Containers.h"
#include "Container/Span.h"
#include <cstdint>

namespace NorvesLib::RHI
{
    class IDevice;
}

namespace NorvesLib::Core::Rendering
{
    class RenderWorld;
    class TextureResources;

    struct ImpostorBakeMetadata
    {
        uint32_t CellResolution = 0;
        uint32_t AxisCellCountX = 0;
        uint32_t AxisCellCountY = 0;
        uint32_t AtlasWidth = 0;
        uint32_t AtlasHeight = 0;
        uint32_t VertexCount = 0;
        uint32_t IndexCount = 0;
        TextureCreateInfo::Format PixelFormat = TextureCreateInfo::Format::RGBA8_UNORM;

        bool IsValid() const
        {
            return CellResolution > 0 &&
                   AxisCellCountX > 0 &&
                   AxisCellCountY > 0 &&
                   AtlasWidth > 0 &&
                   AtlasHeight > 0 &&
                   VertexCount > 0 &&
                   IndexCount > 0;
        }
    };

    struct ImpostorBakeInput
    {
        Container::Span<const Mesh3DVertex> Vertices;
        Container::Span<const uint32_t> Indices;
        uint32_t CellResolution = 0;
        uint32_t AxisCellCountX = 0;
        uint32_t AxisCellCountY = 0;
        TextureCreateInfo::Format PixelFormat = TextureCreateInfo::Format::RGBA8_UNORM;
        Container::String DebugName = "ProceduralImpostorAtlas";
    };

    struct ImpostorBakeRequest
    {
        TextureResources *Textures = nullptr;
        RHI::IDevice *Device = nullptr;
        RenderWorld *RenderWorldForSync = nullptr;
        ImpostorBakeInput Input;
    };

    struct ImpostorBakeResult
    {
        TextureHandle AtlasTexture = TextureHandle::Invalid();
        ImpostorBakeMetadata Metadata;

        bool Succeeded() const
        {
            return AtlasTexture.IsValid() && Metadata.IsValid();
        }
    };

    class ImpostorBakeService
    {
    public:
        bool BakeProceduralAtlas(const ImpostorBakeRequest &request,
                                 ImpostorBakeResult &outResult) const;

    private:
        bool ValidateInput(const ImpostorBakeRequest &request,
                           ImpostorBakeMetadata &outMetadata) const;
    };

} // namespace NorvesLib::Core::Rendering
