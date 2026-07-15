#include "Resource/ModelStaging.h"

#include "FileStream/FileStream.h"
#include "Logging/LogMacros.h"
#include "Rendering/RenderResources.h"
#include "Rendering/TextureUploadProfile.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <utility>

#include "stb_image.h"

namespace NorvesLib::Core::Resource::ModelStaging
{
    using namespace NorvesLib::Core::Container;

    namespace
    {
        // Keep profiling helpers TU-local so the private header exposes only the shared staging surface.
        using LoadProfileClock = std::chrono::steady_clock;

        LoadProfileClock::time_point LoadProfileNow()
        {
            return LoadProfileClock::now();
        }

        double LoadProfileElapsedMs(LoadProfileClock::time_point startTime)
        {
            return std::chrono::duration<double, std::milli>(LoadProfileClock::now() - startTime).count();
        }
    } // anonymous namespace

    size_t GetStagedLooseTextureBytes(const ModelStagingData& staging)
    {
        return staging.AlbedoTexture.PixelData.size() +
               staging.NormalTexture.PixelData.size() +
               staging.AOTexture.PixelData.size() +
               staging.RoughnessTexture.PixelData.size() +
               staging.MetallicTexture.PixelData.size();
    }

    uint32_t GetStagedPreparedTextureCount(const ModelStagingData& staging)
    {
        uint32_t count = 0;
        count += staging.AlbedoTexture.HasPreparedTexture() ? 1u : 0u;
        count += staging.NormalTexture.HasPreparedTexture() ? 1u : 0u;
        count += staging.AOTexture.HasPreparedTexture() ? 1u : 0u;
        count += staging.RoughnessTexture.HasPreparedTexture() ? 1u : 0u;
        count += staging.MetallicTexture.HasPreparedTexture() ? 1u : 0u;
        return count;
    }

    uint32_t GetStagedTextureCount(const ModelStagingData& staging)
    {
        uint32_t count = 0;
        count += staging.AlbedoTexture.HasData() ? 1u : 0u;
        count += staging.NormalTexture.HasData() ? 1u : 0u;
        count += staging.AOTexture.HasData() ? 1u : 0u;
        count += staging.RoughnessTexture.HasData() ? 1u : 0u;
        count += staging.MetallicTexture.HasData() ? 1u : 0u;
        return count;
    }

    bool ReadBinaryFile(const String& path,
                        VariableArray<uint8_t>& outData,
                        const char* role,
                        uint32_t requestId,
                        const char* stage)
    {
        auto readStartTime = LoadProfileNow();
        size_t bytesRead = 0;
        auto fileStream = NorvesLib::FileStream::FileStream::Create(
            path,
            NorvesLib::FileStream::FileMode::Read,
            NorvesLib::FileStream::FileAccess::Read,
            NorvesLib::FileStream::FileShare::Read);
        if (!fileStream || !fileStream->IsOpen())
        {
            NORVES_LOG_INFO("AssetLoadProfile",
                            "stage=%s role=%s request_id=%u path=\"%s\" bytes=%zu ms=%.3f success=0",
                            stage,
                            role,
                            static_cast<unsigned int>(requestId),
                            path.c_str(),
                            bytesRead,
                            LoadProfileElapsedMs(readStartTime));
            return false;
        }

        int64_t fileSize = fileStream->GetSize();
        if (fileSize < 0)
        {
            fileStream->Close();
            NORVES_LOG_INFO("AssetLoadProfile",
                            "stage=%s role=%s request_id=%u path=\"%s\" bytes=%zu file_size=%lld ms=%.3f success=0",
                            stage,
                            role,
                            static_cast<unsigned int>(requestId),
                            path.c_str(),
                            bytesRead,
                            static_cast<long long>(fileSize),
                            LoadProfileElapsedMs(readStartTime));
            return false;
        }

        outData.resize(static_cast<size_t>(fileSize));
        if (fileSize == 0)
        {
            fileStream->Close();
            NORVES_LOG_INFO("AssetLoadProfile",
                            "stage=%s role=%s request_id=%u path=\"%s\" bytes=0 file_size=0 ms=%.3f success=1",
                            stage,
                            role,
                            static_cast<unsigned int>(requestId),
                            path.c_str(),
                            LoadProfileElapsedMs(readStartTime));
            return true;
        }

        bytesRead = fileStream->Read(outData.data(), outData.size());
        fileStream->Close();
        bool bSuccess = bytesRead == outData.size();
        NORVES_LOG_INFO("AssetLoadProfile",
                        "stage=%s role=%s request_id=%u path=\"%s\" bytes=%zu file_size=%lld ms=%.3f success=%d",
                        stage,
                        role,
                        static_cast<unsigned int>(requestId),
                        path.c_str(),
                        bytesRead,
                        static_cast<long long>(fileSize),
                        LoadProfileElapsedMs(readStartTime),
                        bSuccess ? 1 : 0);
        return bSuccess;
    }
    namespace
    {
        bool DecodeImageFile(const String& filePath,
                             VariableArray<uint8_t>& outPixels,
                             uint32_t& outWidth,
                             uint32_t& outHeight,
                             const char* role,
                             uint32_t requestId)
        {
            VariableArray<uint8_t> fileData;
            if (!ReadBinaryFile(filePath, fileData, role, requestId, "gltf_image_read") || fileData.empty())
            {
                NORVES_LOG_ERROR("GLTFAnalyzer", "Failed to read image file: %s", filePath.c_str());
                return false;
            }

            int width = 0;
            int height = 0;
            int channels = 0;
            auto decodeStartTime = LoadProfileNow();
            unsigned char* pPixels = stbi_load_from_memory(
                fileData.data(),
                static_cast<int>(fileData.size()),
                &width,
                &height,
                &channels,
                4);
            double decodeMs = LoadProfileElapsedMs(decodeStartTime);
            if (pPixels == nullptr || width <= 0 || height <= 0)
            {
                NORVES_LOG_INFO("AssetLoadProfile",
                                "stage=gltf_image_decode role=%s request_id=%u path=\"%s\" file_bytes=%zu width=%d height=%d channels=%d ms=%.3f success=0",
                                role,
                                static_cast<unsigned int>(requestId),
                                filePath.c_str(),
                                fileData.size(),
                                width,
                                height,
                                channels,
                                decodeMs);
                NORVES_LOG_ERROR("GLTFAnalyzer", "Failed to decode image file: %s", filePath.c_str());
                if (pPixels != nullptr)
                {
                    stbi_image_free(pPixels);
                }
                return false;
            }

            NORVES_LOG_INFO("AssetLoadProfile",
                            "stage=gltf_image_decode role=%s request_id=%u path=\"%s\" file_bytes=%zu width=%d height=%d channels=%d ms=%.3f success=1",
                            role,
                            static_cast<unsigned int>(requestId),
                            filePath.c_str(),
                            fileData.size(),
                            width,
                            height,
                            channels,
                            decodeMs);

            outWidth = static_cast<uint32_t>(width);
            outHeight = static_cast<uint32_t>(height);

            size_t pixelDataSize = static_cast<size_t>(outWidth) * static_cast<size_t>(outHeight) * 4;
            auto copyStartTime = LoadProfileNow();
            outPixels.resize(pixelDataSize);
            std::memcpy(outPixels.data(), pPixels, pixelDataSize);
            double copyMs = LoadProfileElapsedMs(copyStartTime);
            stbi_image_free(pPixels);
            NORVES_LOG_INFO("AssetLoadProfile",
                            "stage=gltf_image_copy role=%s request_id=%u path=\"%s\" pixel_bytes=%zu width=%u height=%u ms=%.3f success=1",
                            role,
                            static_cast<unsigned int>(requestId),
                            filePath.c_str(),
                            pixelDataSize,
                            outWidth,
                            outHeight,
                            copyMs);
            return true;
        }

        bool CreateTextureFromPixels(Rendering::TextureResources& textures,
                                     const String& debugName,
                                     uint32_t width,
                                     uint32_t height,
                                     Rendering::TextureCreateInfo::Format format,
                                     const void* pPixelData,
                                     size_t pixelDataSize,
                                     NorvesLib::RHI::TexturePtr& outTexture)
        {
            auto calculateMipCount = [](uint32_t textureWidth, uint32_t textureHeight) -> uint32_t
            {
                uint32_t mipLevels = 1;
                while (textureWidth > 1 || textureHeight > 1)
                {
                    textureWidth = std::max(1u, textureWidth / 2);
                    textureHeight = std::max(1u, textureHeight / 2);
                    ++mipLevels;
                }
                return mipLevels;
            };

            Rendering::TextureCreateInfo createInfo;
            createInfo.Width = width;
            createInfo.Height = height;
            createInfo.MipLevels = calculateMipCount(width, height);
            createInfo.PixelFormat = format;
            createInfo.DebugName = debugName;

            Rendering::TextureHandle textureHandle = textures.CreateTexture(createInfo, pPixelData, pixelDataSize);
            if (!textureHandle.IsValid())
            {
                return false;
            }

            outTexture = textures.GetRHITexturePtr(textureHandle);
            return static_cast<bool>(outTexture);
        }

        void SetStagedTextureData(StagedTextureData& outTexture,
                                  VariableArray<uint8_t>&& pixels,
                                  uint32_t width,
                                  uint32_t height,
                                  Rendering::TextureCreateInfo::Format format,
                                  const String& debugName)
        {
            outTexture.PixelData = std::move(pixels);
            outTexture.PreparedTexture = {};
            outTexture.Width = width;
            outTexture.Height = height;
            outTexture.Format = format;
            outTexture.DebugName = debugName;
            outTexture.bHasPreparedTexture = false;
        }

        void SetPreparedStagedTextureData(StagedTextureData& outTexture,
                                          Rendering::PreparedTextureAsset&& preparedTexture,
                                          const String& debugName)
        {
            outTexture.PixelData.clear();
            outTexture.PreparedTexture = std::move(preparedTexture);
            outTexture.Width = 0;
            outTexture.Height = 0;
            outTexture.Format = Rendering::TextureCreateInfo::Format::RGBA8_UNORM;
            outTexture.DebugName = debugName;
            outTexture.bHasPreparedTexture = true;
        }

        bool ShouldUseLooseFallbackForPreparedStatus(Rendering::PreparedTextureAssetStatus status)
        {
            switch (status)
            {
            case Rendering::PreparedTextureAssetStatus::ManifestMissingLooseFallback:
            case Rendering::PreparedTextureAssetStatus::VariantMissingLooseFallback:
            case Rendering::PreparedTextureAssetStatus::DebugLooseFallback:
            case Rendering::PreparedTextureAssetStatus::InvalidRequest:
            case Rendering::PreparedTextureAssetStatus::InvalidPath:
            case Rendering::PreparedTextureAssetStatus::AbsolutePathUnsupported:
                return true;
            default:
                return false;
            }
        }

        bool DecodeStandardTextureFallback(const TextureReference& reference,
                                           const String& debugName,
                                           StagedTextureData& outTexture,
                                           const char* role,
                                           uint32_t requestId)
        {
            if (reference.ResolvedFallbackPath.empty())
            {
                return false;
            }

            VariableArray<uint8_t> pixels;
            uint32_t width = 0;
            uint32_t height = 0;
            if (!DecodeImageFile(reference.ResolvedFallbackPath, pixels, width, height, role, requestId))
            {
                return false;
            }

            SetStagedTextureData(
                outTexture,
                std::move(pixels),
                width,
                height,
                Rendering::TextureCreateInfo::Format::RGBA8_UNORM,
                debugName);
            return true;
        }

        bool DecodeArmTextureFallback(const TextureReference& reference,
                                      const String& debugNamePrefix,
                                      StagedTextureData& outAOTexture,
                                      StagedTextureData& outRoughnessTexture,
                                      StagedTextureData& outMetallicTexture,
                                      const char* role,
                                      uint32_t requestId)
        {
            if (reference.ResolvedFallbackPath.empty())
            {
                return false;
            }

            VariableArray<uint8_t> pixels;
            uint32_t width = 0;
            uint32_t height = 0;
            if (!DecodeImageFile(reference.ResolvedFallbackPath, pixels, width, height, role, requestId))
            {
                return false;
            }

            size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
            VariableArray<uint8_t> aoPixels(pixelCount);
            VariableArray<uint8_t> roughnessPixels(pixelCount);
            VariableArray<uint8_t> metallicPixels(pixelCount);

            for (size_t pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex)
            {
                aoPixels[pixelIndex] = pixels[pixelIndex * 4 + 0];
                roughnessPixels[pixelIndex] = pixels[pixelIndex * 4 + 1];
                metallicPixels[pixelIndex] = pixels[pixelIndex * 4 + 2];
            }

            SetStagedTextureData(
                outAOTexture,
                std::move(aoPixels),
                width,
                height,
                Rendering::TextureCreateInfo::Format::R8_UNORM,
                debugNamePrefix + "_AO");
            SetStagedTextureData(
                outRoughnessTexture,
                std::move(roughnessPixels),
                width,
                height,
                Rendering::TextureCreateInfo::Format::R8_UNORM,
                debugNamePrefix + "_Roughness");
            SetStagedTextureData(
                outMetallicTexture,
                std::move(metallicPixels),
                width,
                height,
                Rendering::TextureCreateInfo::Format::R8_UNORM,
                debugNamePrefix + "_Metallic");
            return true;
        }
    } // anonymous namespace

    bool StageStandardTexture(const TextureReference& textureReference,
                              const String& debugName,
                              StagedTextureData& outTexture,
                              const char* role,
                              uint32_t requestId)
    {
        if (!textureReference.HasReference())
        {
            return true;
        }

        if (!textureReference.RequestPath.empty())
        {
            return true;
        }

        return DecodeStandardTextureFallback(textureReference, debugName, outTexture, role, requestId);
    }

    bool StageArmTextures(const TextureReference& textureReference,
                          const String& debugNamePrefix,
                          StagedTextureData& outAOTexture,
                          StagedTextureData& outRoughnessTexture,
                          StagedTextureData& outMetallicTexture,
                          const char* role,
                          uint32_t requestId)
    {
        if (!textureReference.HasReference())
        {
            return true;
        }

        if (!textureReference.RequestPath.empty())
        {
            return true;
        }

        return DecodeArmTextureFallback(
            textureReference,
            debugNamePrefix,
            outAOTexture,
            outRoughnessTexture,
            outMetallicTexture,
            role,
            requestId);
    }
    namespace
    {
        bool CreateTextureFromStagedData(Rendering::TextureResources& textures,
                                         const StagedTextureData& stagedTexture,
                                         NorvesLib::RHI::TexturePtr& outTexture,
                                         const char* role,
                                         uint32_t requestId)
        {
            if (!stagedTexture.HasData())
            {
                return true;
            }

            if (stagedTexture.HasPreparedTexture())
            {
                if (!textures.IsPreparedTextureAssetCurrent(stagedTexture.PreparedTexture))
                {
                    return false;
                }

                Rendering::TextureHandle textureHandle = textures.FinalizePreparedTextureAsset(
                    stagedTexture.PreparedTexture,
                    role,
                    requestId);
                if (!textureHandle.IsValid())
                {
                    return false;
                }

                outTexture = textures.GetRHITexturePtr(textureHandle);
                return static_cast<bool>(outTexture);
            }

            if (!stagedTexture.HasLoosePixelData())
            {
                return false;
            }

            return CreateTextureFromPixels(
                textures,
                stagedTexture.DebugName,
                stagedTexture.Width,
                stagedTexture.Height,
                stagedTexture.Format,
                stagedTexture.PixelData.data(),
                stagedTexture.PixelData.size(),
                outTexture);
        }

        bool CreateStandardTextureFromReference(Rendering::TextureResources& textures,
                                                const TextureReference& textureReference,
                                                const String& debugName,
                                                NorvesLib::RHI::TexturePtr& outTexture,
                                                const char* role,
                                                uint32_t requestId)
        {
            if (!textureReference.HasReference())
            {
                return true;
            }

            if (!textureReference.RequestPath.empty())
            {
                Rendering::PreparedTextureAsset prepared = textures.PrepareTextureAssetForWorker(
                    textureReference.RequestPath,
                    textureReference.ResolvedFallbackPath,
                    role,
                    requestId);

                if (prepared.Status == Rendering::PreparedTextureAssetStatus::CookedReady)
                {
                    if (!textures.IsPreparedTextureAssetCurrent(prepared))
                    {
                        return false;
                    }

                    Rendering::TextureHandle textureHandle = textures.FinalizePreparedTextureAsset(
                        prepared,
                        role,
                        requestId);
                    if (!textureHandle.IsValid())
                    {
                        return false;
                    }

                    outTexture = textures.GetRHITexturePtr(textureHandle);
                    return static_cast<bool>(outTexture);
                }

                if (!ShouldUseLooseFallbackForPreparedStatus(prepared.Status))
                {
                    return false;
                }
            }

            StagedTextureData looseFallback;
            if (!DecodeStandardTextureFallback(textureReference, debugName, looseFallback, role, requestId))
            {
                return false;
            }

            return CreateTextureFromStagedData(textures, looseFallback, outTexture, role, requestId);
        }

        bool CreateArmTexturesFromReference(Rendering::TextureResources& textures,
                                            const TextureReference& textureReference,
                                            const String& debugNamePrefix,
                                            NorvesLib::RHI::TexturePtr& outAOTexture,
                                            NorvesLib::RHI::TexturePtr& outRoughnessTexture,
                                            NorvesLib::RHI::TexturePtr& outMetallicTexture,
                                            const char* role,
                                            uint32_t requestId)
        {
            if (!textureReference.HasReference())
            {
                return true;
            }

            StagedTextureData aoStaging;
            StagedTextureData roughnessStaging;
            StagedTextureData metallicStaging;

            if (!textureReference.RequestPath.empty())
            {
                Rendering::PreparedTextureAsset prepared = textures.PrepareTextureAssetForWorker(
                    textureReference.RequestPath,
                    textureReference.ResolvedFallbackPath,
                    role,
                    requestId);

                if (prepared.Status == Rendering::PreparedTextureAssetStatus::CookedReady)
                {
                    if (!textures.IsPreparedTextureAssetCurrent(prepared))
                    {
                        return false;
                    }

                    Rendering::PreparedCookedTextureMip0RGBA8UNormLinearSplit split;
                    String splitReason;
                    if (!textures.TrySplitPreparedCookedTextureMip0RGBA8UNormLinear(
                            prepared,
                            split,
                            &splitReason,
                            role,
                            requestId))
                    {
                        if (prepared.FallbackMode == Rendering::TextureAssetFallbackMode::DebugAllowLooseFallback)
                        {
                            if (!DecodeArmTextureFallback(
                                    textureReference,
                                    debugNamePrefix,
                                    aoStaging,
                                    roughnessStaging,
                                    metallicStaging,
                                    role,
                                    requestId))
                            {
                                return false;
                            }
                        }
                        else
                        {
                            NORVES_LOG_ERROR("GLTFAnalyzer",
                                             "Failed to split cooked ARM texture: %s (%s)",
                                             textureReference.RequestPath.c_str(),
                                             splitReason.c_str());
                            return false;
                        }
                    }
                    else
                    {
                        SetStagedTextureData(
                            aoStaging,
                            std::move(split.R),
                            split.Width,
                            split.Height,
                            Rendering::TextureCreateInfo::Format::R8_UNORM,
                            debugNamePrefix + "_AO");
                        SetStagedTextureData(
                            roughnessStaging,
                            std::move(split.G),
                            split.Width,
                            split.Height,
                            Rendering::TextureCreateInfo::Format::R8_UNORM,
                            debugNamePrefix + "_Roughness");
                        SetStagedTextureData(
                            metallicStaging,
                            std::move(split.B),
                            split.Width,
                            split.Height,
                            Rendering::TextureCreateInfo::Format::R8_UNORM,
                            debugNamePrefix + "_Metallic");
                    }
                }
                else if (!ShouldUseLooseFallbackForPreparedStatus(prepared.Status))
                {
                    return false;
                }
            }

            if (!aoStaging.HasData() &&
                !DecodeArmTextureFallback(
                    textureReference,
                    debugNamePrefix,
                    aoStaging,
                    roughnessStaging,
                    metallicStaging,
                    role,
                    requestId))
            {
                return false;
            }

            return CreateTextureFromStagedData(textures, aoStaging, outAOTexture, role, requestId) &&
                   CreateTextureFromStagedData(textures, roughnessStaging, outRoughnessTexture, role, requestId) &&
                   CreateTextureFromStagedData(textures, metallicStaging, outMetallicTexture, role, requestId);
        }
    } // anonymous namespace
    Rendering::ModelHandle FinalizeModelStaging(const ModelStagingData& staging,
                                                Rendering::ModelLoadResourceContext resources,
                                                const char* role,
                                                uint32_t requestId)
    {
        auto totalStartTime = LoadProfileNow();
        Rendering::MegaGeometry::MegaMeshMaterial material;
        material.BaseColor[0] = 1.0f;
        material.BaseColor[1] = 1.0f;
        material.BaseColor[2] = 1.0f;
        material.BaseColor[3] = 1.0f;

        auto textureFinalizeStartTime = LoadProfileNow();
        bool bAlbedoFinalizeSuccess = false;
        bool bNormalFinalizeSuccess = false;
        bool bAOFinalizeSuccess = false;
        bool bRoughnessFinalizeSuccess = false;
        bool bMetallicFinalizeSuccess = false;
        {
            Rendering::ScopedTextureCreateUploadProfileRole profileRole(role);
            bAlbedoFinalizeSuccess = staging.AlbedoTexture.HasData()
                                          ? CreateTextureFromStagedData(resources.Textures, staging.AlbedoTexture, material.AlbedoTexture, role, requestId)
                                          : CreateStandardTextureFromReference(resources.Textures, staging.TextureReferences.Albedo, staging.DebugName + "_Albedo", material.AlbedoTexture, role, requestId);
            bNormalFinalizeSuccess = staging.NormalTexture.HasData()
                                         ? CreateTextureFromStagedData(resources.Textures, staging.NormalTexture, material.NormalTexture, role, requestId)
                                         : CreateStandardTextureFromReference(resources.Textures, staging.TextureReferences.Normal, staging.DebugName + "_Normal", material.NormalTexture, role, requestId);
            if (staging.AOTexture.HasData() ||
                staging.RoughnessTexture.HasData() ||
                staging.MetallicTexture.HasData())
            {
                bAOFinalizeSuccess = CreateTextureFromStagedData(resources.Textures, staging.AOTexture, material.AOTexture, role, requestId);
                bRoughnessFinalizeSuccess = CreateTextureFromStagedData(resources.Textures, staging.RoughnessTexture, material.RoughnessTexture, role, requestId);
                bMetallicFinalizeSuccess = CreateTextureFromStagedData(resources.Textures, staging.MetallicTexture, material.MetallicTexture, role, requestId);
            }
            else
            {
                bAOFinalizeSuccess = bRoughnessFinalizeSuccess = bMetallicFinalizeSuccess =
                    CreateArmTexturesFromReference(
                        resources.Textures,
                        staging.TextureReferences.Arm,
                        staging.DebugName,
                        material.AOTexture,
                        material.RoughnessTexture,
                        material.MetallicTexture,
                        role,
                        requestId);
            }
        }
        bool bTextureFinalizeSuccess =
            bAlbedoFinalizeSuccess &&
            bNormalFinalizeSuccess &&
            bAOFinalizeSuccess &&
            bRoughnessFinalizeSuccess &&
            bMetallicFinalizeSuccess;
        NORVES_LOG_INFO("AssetLoadProfile",
                        "stage=gltf_finalize_textures role=%s request_id=%u debug_name=\"%s\" textures=%u prepared_textures=%u loose_texture_bytes=%zu ms=%.3f success=%d",
                        role,
                        static_cast<unsigned int>(requestId),
                        staging.DebugName.c_str(),
                        static_cast<unsigned int>(GetStagedTextureCount(staging)),
                        static_cast<unsigned int>(GetStagedPreparedTextureCount(staging)),
                        GetStagedLooseTextureBytes(staging),
                        LoadProfileElapsedMs(textureFinalizeStartTime),
                        bTextureFinalizeSuccess ? 1 : 0);
        if (!bTextureFinalizeSuccess)
        {
            return Rendering::ModelHandle::Invalid();
        }

        Rendering::MegaGeometry::MegaMeshCreateInfo createInfo;
        createInfo.VertexData = staging.Vertices.data();
        createInfo.VertexDataSize = staging.Vertices.size() * sizeof(Rendering::Mesh3DVertex);
        createInfo.VertexCount = static_cast<uint32_t>(staging.Vertices.size());
        createInfo.VertexStride = sizeof(Rendering::Mesh3DVertex);
        createInfo.IndexData = staging.ClusterizedIndices.data();
        createInfo.IndexCount = static_cast<uint32_t>(staging.ClusterizedIndices.size());
        createInfo.Clusters = staging.Clusters;
        createInfo.TotalBounds = staging.TotalBounds;
        createInfo.bBuildLODHierarchy = false;
        createInfo.Material = material;
        createInfo.DebugName = staging.DebugName;

        auto megaMeshCreateStartTime = LoadProfileNow();
        Rendering::MegaGeometry::MegaMeshHandle megaMeshHandle = resources.MegaGeometry.CreateMegaMesh(createInfo);
        double megaMeshCreateMs = LoadProfileElapsedMs(megaMeshCreateStartTime);
        NORVES_LOG_INFO("AssetLoadProfile",
                        "stage=gltf_finalize_megamesh role=%s request_id=%u debug_name=\"%s\" vertices=%zu indices=%zu clusters=%zu ms=%.3f success=%d",
                        role,
                        static_cast<unsigned int>(requestId),
                        staging.DebugName.c_str(),
                        staging.Vertices.size(),
                        staging.ClusterizedIndices.size(),
                        staging.Clusters.size(),
                        megaMeshCreateMs,
                        megaMeshHandle.IsValid() ? 1 : 0);
        if (!megaMeshHandle.IsValid())
        {
            NORVES_LOG_ERROR("GLTFAnalyzer", "Failed to create MegaMesh: %s", staging.DebugName.c_str());
            return Rendering::ModelHandle::Invalid();
        }

        auto modelRegisterStartTime = LoadProfileNow();
        Rendering::ModelHandle modelHandle = resources.MegaGeometry.RegisterModel(
            megaMeshHandle,
            staging.DebugName,
            staging.ResolvedPath);
        double modelRegisterMs = LoadProfileElapsedMs(modelRegisterStartTime);
        NORVES_LOG_INFO("AssetLoadProfile",
                        "stage=gltf_finalize_register role=%s request_id=%u debug_name=\"%s\" path=\"%s\" ms=%.3f success=%d",
                        role,
                        static_cast<unsigned int>(requestId),
                        staging.DebugName.c_str(),
                        staging.ResolvedPath.c_str(),
                        modelRegisterMs,
                        modelHandle.IsValid() ? 1 : 0);
        if (!modelHandle.IsValid())
        {
            resources.MegaGeometry.ReleaseMegaMesh(megaMeshHandle);
            NORVES_LOG_ERROR("GLTFAnalyzer", "Failed to register model: %s", staging.DebugName.c_str());
            return Rendering::ModelHandle::Invalid();
        }

        NORVES_LOG_INFO("GLTFAnalyzer", "glTF model loaded: %s", staging.DebugName.c_str());
        NORVES_LOG_INFO("AssetLoadProfile",
                        "stage=gltf_finalize_total role=%s request_id=%u debug_name=\"%s\" path=\"%s\" loose_texture_bytes=%zu prepared_textures=%u ms=%.3f success=1",
                        role,
                        static_cast<unsigned int>(requestId),
                        staging.DebugName.c_str(),
                        staging.ResolvedPath.c_str(),
                        GetStagedLooseTextureBytes(staging),
                        static_cast<unsigned int>(GetStagedPreparedTextureCount(staging)),
                        LoadProfileElapsedMs(totalStartTime));
        return modelHandle;
    }
} // namespace NorvesLib::Core::Resource::ModelStaging
