#include "Resource/ModelStaging.h"
#include "Resource/ImportedOpaqueRuntime.h"
#include "Rendering/TextureAssetLoader.h"
#include "RHI/ITexture.h"
#include "Resource/GltfImageSource.h"

#include "FileStream/FileStream.h"
#include "Logging/LogMacros.h"
#include "Rendering/RenderResources.h"
#include "Rendering/TextureUploadProfile.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <utility>
#include <limits>

#include "stb_image.h"

namespace NorvesLib::Core::ResourceIO::ModelStaging
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
        struct ImagePixelOwner
        {
            unsigned char* Data;
            explicit ImagePixelOwner(unsigned char* data) noexcept : Data(data)
            {
            }
            ImagePixelOwner(const ImagePixelOwner&) = delete;
            ImagePixelOwner& operator=(const ImagePixelOwner&) = delete;
            ~ImagePixelOwner()
            {
                if (Data != nullptr)
                {
                    stbi_image_free(Data);
                }
            }
        };

        bool DecodeImageBytes(Span<const uint8_t> fileData, const String& filePath,
                              VariableArray<uint8_t>& outPixels, uint32_t& outWidth, uint32_t& outHeight,
                              const char* role, uint32_t requestId, bool bEmbedded)
        {
            if (fileData.empty() || fileData.data() == nullptr ||
                fileData.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
            {
                return false;
            }
            if (bEmbedded)
            {
                const auto mime = Gltf::ProbeEmbeddedImageMime(fileData);
                if (mime != Gltf::DataUriMime::Png && mime != Gltf::DataUriMime::Jpeg)
                {
                    return false;
                }
            }
            int width = 0;
            int height = 0;
            int channels = 0;
            auto decodeStartTime = LoadProfileNow();
            ImagePixelOwner pixels(stbi_load_from_memory(
                fileData.data(),
                static_cast<int>(fileData.size()),
                &width,
                &height,
                &channels,
                4));
            unsigned char* pPixels = pixels.Data;
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

            if (static_cast<size_t>(width) > std::numeric_limits<size_t>::max() / static_cast<size_t>(height) / 4)
            {
                return false;
            }
            outWidth = static_cast<uint32_t>(width);
            outHeight = static_cast<uint32_t>(height);

            size_t pixelDataSize = static_cast<size_t>(outWidth) * static_cast<size_t>(outHeight) * 4;
            auto copyStartTime = LoadProfileNow();
            outPixels.resize(pixelDataSize);
            std::memcpy(outPixels.data(), pPixels, pixelDataSize);
            double copyMs = LoadProfileElapsedMs(copyStartTime);
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

        bool DecodeImageFile(const String& filePath,
                             VariableArray<uint8_t>& outPixels, uint32_t& outWidth, uint32_t& outHeight,
                             const char* role, uint32_t requestId)
        {
            VariableArray<uint8_t> fileData;
            if (!ReadBinaryFile(filePath, fileData, role, requestId, "gltf_image_read") || fileData.empty())
            {
                NORVES_LOG_ERROR("GLTFAnalyzer", "Failed to read image file: %s", filePath.c_str());
                return false;
            }
            // 既存外部fileの対応形式は狭めず、bytes入口だけをPNG/JPEGに限定する。
            return DecodeImageBytes({fileData.data(), fileData.size()}, filePath, outPixels, outWidth, outHeight, role, requestId, false);
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

        bool StageArmPixels(VariableArray<uint8_t>&& pixels, uint32_t width, uint32_t height,
                            const String& debugNamePrefix, StagedTextureData& outAOTexture,
                            StagedTextureData& outRoughnessTexture, StagedTextureData& outMetallicTexture)
        {
            if (&outAOTexture == &outRoughnessTexture || &outAOTexture == &outMetallicTexture ||
                &outRoughnessTexture == &outMetallicTexture)
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

            StagedTextureData aoTexture, roughnessTexture, metallicTexture;
            SetStagedTextureData(
                aoTexture,
                std::move(aoPixels),
                width,
                height,
                Rendering::TextureCreateInfo::Format::R8_UNORM,
                debugNamePrefix + "_AO");
            SetStagedTextureData(
                roughnessTexture,
                std::move(roughnessPixels),
                width,
                height,
                Rendering::TextureCreateInfo::Format::R8_UNORM,
                debugNamePrefix + "_Roughness");
            SetStagedTextureData(
                metallicTexture,
                std::move(metallicPixels),
                width,
                height,
                Rendering::TextureCreateInfo::Format::R8_UNORM,
                debugNamePrefix + "_Metallic");
            outAOTexture = std::move(aoTexture);
            outRoughnessTexture = std::move(roughnessTexture);
            outMetallicTexture = std::move(metallicTexture);
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

            return StageArmPixels(std::move(pixels), width, height, debugNamePrefix,
                outAOTexture, outRoughnessTexture, outMetallicTexture);
        }
    } // anonymous namespace

    bool StageStandardTextureBytes(Span<const uint8_t> bytes, const String& debugName,
                                   StagedTextureData& outTexture, const char* role, uint32_t requestId)
    {
        VariableArray<uint8_t> pixels;
        uint32_t width = 0, height = 0;
        if (!DecodeImageBytes(bytes, debugName, pixels, width, height, role, requestId, true))
        {
            return false;
        }
        StagedTextureData candidate;
        SetStagedTextureData(candidate, std::move(pixels), width, height,
            Rendering::TextureCreateInfo::Format::RGBA8_UNORM, debugName);
        outTexture = std::move(candidate);
        return true;
    }

    bool StageArmTextureBytes(Span<const uint8_t> bytes, const String& debugNamePrefix,
                              StagedTextureData& outAOTexture, StagedTextureData& outRoughnessTexture,
                              StagedTextureData& outMetallicTexture, const char* role, uint32_t requestId)
    {
        VariableArray<uint8_t> pixels;
        uint32_t width = 0, height = 0;
        if (!DecodeImageBytes(bytes, debugNamePrefix, pixels, width, height, role, requestId, true))
        {
            return false;
        }
        return StageArmPixels(std::move(pixels), width, height, debugNamePrefix,
            outAOTexture, outRoughnessTexture, outMetallicTexture);
    }

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
        bool FinalizeImportedMaterial(const ModelStagingData& staging, Rendering::TextureResources& textures,
                                      Rendering::MegaGeometry::MegaMeshMaterial& material, const char* role,
                                      uint32_t requestId, AnsiString& reason)
        {
            const auto& source = staging.ImportedMaterial;
            if (!ValidateImportedOpaqueMaterial(source, reason))
            {
                return false;
            }
            Rendering::PreparedTextureAsset prepared[3];
            const AnsiString* paths[] = {&source.AlbedoPath, &source.NormalPath, &source.ArmPath};
            const char* roles[] = {"albedo", "normal", "arm"};
            for (size_t i = 0; i < 3; ++i)
            {
                if (paths[i]->empty() || (i == 2 && source.ArmMask == 0))
                {
                    continue;
                }
                const String path(StringView(paths[i]->data(), paths[i]->size()));
                prepared[i] = textures.PrepareTextureAssetForWorker(path, {}, role, requestId);
                if (prepared[i].Status != Rendering::PreparedTextureAssetStatus::CookedReady || !prepared[i].Payload ||
                    !textures.IsPreparedTextureAssetCurrent(prepared[i]))
                {
                    reason = "imported_opaque: cooked_texture_required role=";
                    reason += roles[i];
                    return false;
                }
                if (!ValidateImportedTexture(prepared[i].Payload->Texture,
                                             i == 0 ? Asset::CookedTextureColorSpace::SRGB
                                                    : Asset::CookedTextureColorSpace::Linear,
                                             i == 0, reason))
                {
                    reason += " role=";
                    reason += roles[i];
                    return false;
                }
            }
            ImportedArmChannels arm;
            if (source.ArmMask && (!prepared[2].Payload || !SplitImportedArmChannels(prepared[2].Payload->Texture,
                                                                                     source.ArmMask, arm, reason)))
            {
                return false;
            }
            // 全CPU検査を終えてからGPUを作る。画像の不正や未対応値で部分uploadしない。
            for (const auto& texture : prepared)
            {
                if (texture.Payload && !textures.IsPreparedTextureAssetCurrent(texture))
                {
                    reason = "imported_opaque: texture_generation_changed";
                    return false;
                }
            }
            for (size_t i = 0; i < 4; ++i)
            {
                material.BaseColor[i] = source.BaseColor[i];
            }
            for (size_t i = 0; i < 3; ++i)
            {
                material.EmissiveColor[i] = source.EmissiveColor[i];
            }
            material.EmissiveLuminanceNits = source.EmissiveLuminanceNits;
            material.Metallic = source.Metallic;
            material.Roughness = source.Roughness;
            material.OcclusionStrength = source.OcclusionStrength;
            NorvesLib::RHI::TexturePtr* standard[] = {&material.AlbedoTexture, &material.NormalTexture};
            for (size_t i = 0; i < 2; ++i)
            {
                if (!prepared[i].Payload)
                {
                    continue;
                }
                StagedTextureData staged;
                staged.PreparedTexture = prepared[i];
                staged.bHasPreparedTexture = true;
                if (!CreateTextureFromStagedData(textures, staged, *standard[i], role, requestId))
                {
                    reason = "imported_opaque: texture_finalize role=";
                    reason += roles[i];
                    return false;
                }
            }
            NorvesLib::RHI::TexturePtr* channels[] = {&material.AOTexture, &material.RoughnessTexture,
                                                      &material.MetallicTexture};
            const char* suffixes[] = {"_AO", "_Roughness", "_Metallic"};
            for (size_t i = 0; i < 3; ++i)
            {
                if ((source.ArmMask & (1u << i)) == 0)
                {
                    continue;
                }
                Rendering::TextureCreateInfo info;
                info.Width = arm.Mips[0].Width;
                info.Height = arm.Mips[0].Height;
                info.MipLevels = static_cast<uint32_t>(arm.Mips.size());
                info.PixelFormat = Rendering::TextureCreateInfo::Format::R8_UNORM;
                info.DebugName = staging.DebugName + suffixes[i];
                const auto handle = textures.CreateTexture(info);
                if (!handle.IsValid())
                {
                    reason = "imported_opaque: arm_channel_finalize";
                    return false;
                }
                *channels[i] = textures.GetRHITexturePtr(handle);
                // 派生textureはmodelだけが所有する。registryへ匿名handleを残さない。
                textures.ReleaseTexture(handle);
                if (!*channels[i])
                {
                    reason = "imported_opaque: arm_channel_pointer";
                    return false;
                }
                // cook済み全mipを直接uploadし、mip0からのGPU再生成失敗を隠さない。
                for (size_t level = 0; level < arm.Mips.size(); ++level)
                {
                    const auto& mip = arm.Mips[level];
                    (*channels[i])
                        ->Update(mip.Pixels[i].data(), mip.Width, static_cast<uint32_t>(mip.Pixels[i].size()),
                                 static_cast<uint32_t>(level), 0);
                }
            }
            // 発光画像は、正nitsならprofileで拒否済み、0nitsなら寄与0なので一切読み込まない。
            return true;
        }
    } // anonymous namespace
    Rendering::ModelHandle FinalizeModelStaging(const ModelStagingData& staging,
                                                Rendering::ModelLoadResourceContext resources, const char* role,
                                                uint32_t requestId, ModelFinalizeStatus* outStatus)
    {
        if (outStatus)
        {
            *outStatus = ModelFinalizeStatus::Failed;
        }
        const bool bImported = staging.ImportedMaterial.Layout != ImportedMaterialLayout::Absent;
        AnsiString importedReason;
        if (bImported && !ValidateImportedOpaqueMaterial(staging.ImportedMaterial, importedReason))
        {
            if (outStatus)
            {
                *outStatus = ModelFinalizeStatus::UnsupportedImportedMaterial;
            }
            NORVES_LOG_ERROR("ModelAsset", "asset=%s material=0 %s", staging.DebugName.c_str(), importedReason.c_str());
            return Rendering::ModelHandle::Invalid();
        }
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
        if (bImported)
        {
            Rendering::ScopedTextureCreateUploadProfileRole profileRole(role);
            bAlbedoFinalizeSuccess = bNormalFinalizeSuccess = bAOFinalizeSuccess = bRoughnessFinalizeSuccess =
                bMetallicFinalizeSuccess =
                    FinalizeImportedMaterial(staging, resources.Textures, material, role, requestId, importedReason);
            if (!bAlbedoFinalizeSuccess)
            {
                NORVES_LOG_ERROR("ModelAsset", "asset=%s material=0 %s", staging.DebugName.c_str(),
                                 importedReason.c_str());
            }
        }
        else
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
                        "stage=model_finalize_textures role=%s request_id=%u debug_name=\"%s\" textures=%u prepared_textures=%u loose_texture_bytes=%zu ms=%.3f success=%d",
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
                        "stage=model_finalize_megamesh role=%s request_id=%u debug_name=\"%s\" vertices=%zu indices=%zu clusters=%zu ms=%.3f success=%d",
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
                        "stage=model_finalize_register role=%s request_id=%u debug_name=\"%s\" path=\"%s\" ms=%.3f success=%d",
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
                        "stage=model_finalize_total role=%s request_id=%u debug_name=\"%s\" path=\"%s\" loose_texture_bytes=%zu prepared_textures=%u ms=%.3f success=1",
                        role,
                        static_cast<unsigned int>(requestId),
                        staging.DebugName.c_str(),
                        staging.ResolvedPath.c_str(),
                        GetStagedLooseTextureBytes(staging),
                        static_cast<unsigned int>(GetStagedPreparedTextureCount(staging)),
                        LoadProfileElapsedMs(totalStartTime));
        if (outStatus)
        {
            *outStatus = ModelFinalizeStatus::Success;
        }
        return modelHandle;
    }
} // namespace NorvesLib::Core::ResourceIO::ModelStaging
