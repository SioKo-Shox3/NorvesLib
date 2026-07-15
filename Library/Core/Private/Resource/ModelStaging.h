// Shares private model-loader staging payloads and finalization without expanding the public Resource API.
#pragma once

#include "Container/Containers.h"
#include "Rendering/GpuResourceTypes.h"
#include "Rendering/MegaGeometry/MegaGeometryTypes.h"
#include "Rendering/ProceduralMeshGenerator.h"
#include "Rendering/RenderResourceContexts.h"
#include "Rendering/RenderTypes.h"
#include "Rendering/TextureAssetTypes.h"

#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core::Resource::ModelStaging
{
    struct TextureReference
    {
        Container::String RequestPath;
        Container::String ResolvedFallbackPath;

        bool HasReference() const
        {
            return !RequestPath.empty() || !ResolvedFallbackPath.empty();
        }
    };

    struct MaterialTextureInfo
    {
        TextureReference Albedo;
        TextureReference Normal;
        TextureReference Arm;
        bool bDoubleSided = false;
    };

    struct StagedTextureData
    {
        Container::VariableArray<uint8_t> PixelData;
        Rendering::PreparedTextureAsset PreparedTexture;
        uint32_t Width = 0;
        uint32_t Height = 0;
        Rendering::TextureCreateInfo::Format Format = Rendering::TextureCreateInfo::Format::RGBA8_UNORM;
        Container::String DebugName;
        bool bHasPreparedTexture = false;

        bool HasLoosePixelData() const
        {
            return !PixelData.empty() && Width > 0 && Height > 0;
        }

        bool HasPreparedTexture() const
        {
            return bHasPreparedTexture && PreparedTexture.HasCookedPayload();
        }

        bool HasData() const
        {
            return HasLoosePixelData() || HasPreparedTexture();
        }
    };

    struct ModelStagingData
    {
        Container::VariableArray<Rendering::Mesh3DVertex> Vertices;
        Container::VariableArray<uint32_t> ClusterizedIndices;
        Container::VariableArray<Rendering::MegaGeometry::MeshCluster> Clusters;
        Rendering::BoundingSphere TotalBounds;
        Container::String DebugName;
        Container::String ResolvedPath;
        MaterialTextureInfo TextureReferences;

        StagedTextureData AlbedoTexture;
        StagedTextureData NormalTexture;
        StagedTextureData AOTexture;
        StagedTextureData RoughnessTexture;
        StagedTextureData MetallicTexture;
    };

    size_t GetStagedLooseTextureBytes(const ModelStagingData& staging);
    uint32_t GetStagedPreparedTextureCount(const ModelStagingData& staging);
    uint32_t GetStagedTextureCount(const ModelStagingData& staging);

    bool StageStandardTexture(const TextureReference& textureReference,
                              const Container::String& debugName,
                              StagedTextureData& outTexture,
                              const char* role,
                              uint32_t requestId);
    bool StageArmTextures(const TextureReference& textureReference,
                          const Container::String& debugNamePrefix,
                          StagedTextureData& outAOTexture,
                          StagedTextureData& outRoughnessTexture,
                          StagedTextureData& outMetallicTexture,
                          const char* role,
                          uint32_t requestId);
    bool ReadBinaryFile(const Container::String& path,
                        Container::VariableArray<uint8_t>& outData,
                        const char* role,
                        uint32_t requestId,
                        const char* stage);
    Rendering::ModelHandle FinalizeModelStaging(const ModelStagingData& staging,
                                                Rendering::ModelLoadResourceContext resources,
                                                const char* role,
                                                uint32_t requestId);
} // namespace NorvesLib::Core::Resource::ModelStaging
