// Shares private model-loader staging payloads and finalization without expanding the public Resource API.
#pragma once

#include "Container/Containers.h"
#include "Resource/ModelMaterialStaging.h"
#include "Rendering/GpuResourceTypes.h"
#include "Rendering/MegaGeometry/MegaGeometryTypes.h"
#include "Rendering/ProceduralMeshGenerator.h"
#include "Rendering/RenderResourceContexts.h"
#include "Rendering/RenderTypes.h"
#include "Rendering/TextureAssetTypes.h"

#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core::AssetImport
{
    struct ImportSettingsFileOptions;
}

namespace NorvesLib::Core::ResourceIO::ModelStaging
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
        ImportedMaterialStaging ImportedMaterial;

        StagedTextureData AlbedoTexture;
        StagedTextureData NormalTexture;
        StagedTextureData AOTexture;
        StagedTextureData RoughnessTexture;
        StagedTextureData MetallicTexture;
    };

    // GPUを作らずloose glTFをCPU stagingへ変換する。成功時だけ出力を置換する。
    bool BuildModelStagingFromLooseGltf(const Container::String& requestPath,
                                      const Container::String& resolvedPath,
                                      ModelStagingData& outStaging,
                                      const char* role,
                                      uint32_t requestId,
                                      const AssetImport::ImportSettingsFileOptions* importOptions = nullptr);

    size_t GetStagedLooseTextureBytes(const ModelStagingData& staging);
    uint32_t GetStagedPreparedTextureCount(const ModelStagingData& staging);
    uint32_t GetStagedTextureCount(const ModelStagingData& staging);

    // PNG/JPEG bytesを所有CPU stagingへ復号する。GPUは作らず、falseでは既存出力を保つ。
    bool StageStandardTextureBytes(Container::Span<const uint8_t> bytes,
                                   const Container::String& debugName,
                                   StagedTextureData& outTexture,
                                   const char* role,
                                   uint32_t requestId);
    // ARMの3出力は互いに異なるobjectを指定する。
    bool StageArmTextureBytes(Container::Span<const uint8_t> bytes,
                              const Container::String& debugNamePrefix,
                              StagedTextureData& outAOTexture,
                              StagedTextureData& outRoughnessTexture,
                              StagedTextureData& outMetallicTexture,
                              const char* role,
                              uint32_t requestId);

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
    enum class ModelFinalizeStatus : uint8_t
    {
        Failed,
        Success,
        UnsupportedImportedMaterial
    };
    // outStatusは呼出結果で置換する。未対応材質の早期拒否を、resource失敗と区別できる。
    Rendering::ModelHandle FinalizeModelStaging(const ModelStagingData& staging,
                                                Rendering::ModelLoadResourceContext resources, const char* role,
                                                uint32_t requestId, ModelFinalizeStatus* outStatus = nullptr);
} // namespace NorvesLib::Core::ResourceIO::ModelStaging
