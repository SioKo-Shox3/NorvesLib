#include "Rendering/ImpostorBake.h"

#include "Rendering/RenderResources.h"
#include "Rendering/RenderWorld.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "Logging/LogMacros.h"

#include <limits>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        bool HasAtlasDimensionOverflow(uint32_t cellResolution, uint32_t axisCellCount)
        {
            return axisCellCount > std::numeric_limits<uint32_t>::max() / cellResolution;
        }

        bool IsSupportedImpostorAtlasColorFormat(TextureCreateInfo::Format format)
        {
            switch (format)
            {
            case TextureCreateInfo::Format::RGBA8_UNORM:
            case TextureCreateInfo::Format::RGBA8_SRGB:
            case TextureCreateInfo::Format::RGBA16_FLOAT:
            case TextureCreateInfo::Format::RGBA32_FLOAT:
            case TextureCreateInfo::Format::R8_UNORM:
            case TextureCreateInfo::Format::RG8_UNORM:
                return true;
            case TextureCreateInfo::Format::D24_S8:
            case TextureCreateInfo::Format::D32_FLOAT:
                return false;
            }

            return false;
        }
    } // namespace

    bool ImpostorBakeService::ValidateInput(const ImpostorBakeRequest &request,
                                            ImpostorBakeMetadata &outMetadata) const
    {
        outMetadata = ImpostorBakeMetadata{};

        if (!request.Textures || !request.Device)
        {
            NORVES_LOG_ERROR("ImpostorBake", "Missing texture resources or device");
            return false;
        }

        const ImpostorBakeInput &input = request.Input;
        if (input.Vertices.empty() ||
            input.Indices.empty() ||
            input.Indices.size() % 3u != 0u)
        {
            NORVES_LOG_ERROR("ImpostorBake", "Procedural impostor geometry must contain triangle indices");
            return false;
        }

        if (input.CellResolution == 0 ||
            input.AxisCellCountX == 0 ||
            input.AxisCellCountY == 0)
        {
            NORVES_LOG_ERROR("ImpostorBake", "Impostor atlas cell resolution and axis cell counts must be nonzero");
            return false;
        }

        if (HasAtlasDimensionOverflow(input.CellResolution, input.AxisCellCountX) ||
            HasAtlasDimensionOverflow(input.CellResolution, input.AxisCellCountY))
        {
            NORVES_LOG_ERROR("ImpostorBake", "Impostor atlas dimensions overflow uint32");
            return false;
        }

        if (!IsSupportedImpostorAtlasColorFormat(input.PixelFormat))
        {
            NORVES_LOG_ERROR("ImpostorBake", "Impostor atlas pixel format must be a color format");
            return false;
        }

        for (uint32_t index : input.Indices)
        {
            if (index >= input.Vertices.size())
            {
                NORVES_LOG_ERROR("ImpostorBake", "Procedural impostor geometry contains an out-of-range index");
                return false;
            }
        }

        outMetadata.CellResolution = input.CellResolution;
        outMetadata.AxisCellCountX = input.AxisCellCountX;
        outMetadata.AxisCellCountY = input.AxisCellCountY;
        outMetadata.AtlasWidth = input.CellResolution * input.AxisCellCountX;
        outMetadata.AtlasHeight = input.CellResolution * input.AxisCellCountY;
        outMetadata.VertexCount = static_cast<uint32_t>(input.Vertices.size());
        outMetadata.IndexCount = static_cast<uint32_t>(input.Indices.size());
        outMetadata.PixelFormat = input.PixelFormat;
        return true;
    }

    bool ImpostorBakeService::BakeProceduralAtlas(const ImpostorBakeRequest &request,
                                                  ImpostorBakeResult &outResult) const
    {
        outResult = ImpostorBakeResult{};

        ImpostorBakeMetadata metadata;
        if (!ValidateInput(request, metadata))
        {
            return false;
        }

        TextureCreateInfo textureInfo;
        textureInfo.Width = metadata.AtlasWidth;
        textureInfo.Height = metadata.AtlasHeight;
        textureInfo.Depth = 1;
        textureInfo.MipLevels = 1;
        textureInfo.ArraySize = 1;
        textureInfo.PixelFormat = metadata.PixelFormat;
        textureInfo.bRenderTarget = true;
        textureInfo.bDepthStencil = false;
        textureInfo.DebugName = request.Input.DebugName;

        const TextureHandle atlasTexture = request.Textures->CreateTexture(textureInfo);
        if (!atlasTexture.IsValid())
        {
            NORVES_LOG_ERROR("ImpostorBake", "Failed to create impostor atlas texture");
            return false;
        }

        auto commandList = request.Device->CreateCommandList();
        if (!commandList)
        {
            if (request.RenderWorldForSync)
            {
                request.RenderWorldForSync->WaitForRender();
            }
            request.Textures->ReleaseTexture(atlasTexture);
            NORVES_LOG_ERROR("ImpostorBake", "Failed to create command list for impostor bake");
            return false;
        }

        commandList->Begin();
        commandList->End();
        commandList->Submit(true);

        if (request.RenderWorldForSync)
        {
            request.RenderWorldForSync->WaitForRender();
        }

        outResult.AtlasTexture = atlasTexture;
        outResult.Metadata = metadata;
        return true;
    }

} // namespace NorvesLib::Core::Rendering
