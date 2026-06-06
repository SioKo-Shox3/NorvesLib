#include "Rendering/RenderResourceManager.h"
#include "Rendering/MegaGeometry/LODHierarchyBuilder.h"
#include "Rendering/CookedTextureUpload.h"
#include "Asset/AssetFileReader.h"
#include "Asset/AssetPath.h"
#include "Asset/AssetResolveResult.h"
#include "Asset/AssetSystem.h"
#include "Asset/CookedTextureFormat.h"
#include "RHI/IDevice.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/ITexture.h"
#include "RHI/ISampler.h"
#include "RHI/IShader.h"
#include "RHI/IGPUResourceAllocator.h"
#include "Logging/LogMacros.h"
#include "Thread/JobSystem.h"
#include "FileStream/FileStream.h"
#include "FileStream/Package.h"

// stb_image（テクスチャファイル読み込み用）
#include "stb_image.h"
#include <algorithm>
#include <chrono>
#include <climits>
#include <cstring>
#include <limits>
#include <string>
#include <utility>

namespace NorvesLib::Core::Rendering
{
    struct CookedTextureAsyncPayload
    {
        Asset::CookedTextureData Texture;
    };

    struct RenderResourceManager::TextureAssetState
    {
        Container::AnsiString AssetRoot;
        Container::String ManifestJson;
        Container::String ManifestSourceName;
        bool bManifestLoadAttempted = false;
        Asset::AssetFallbackMode FallbackMode = Asset::AssetFallbackMode::FailOnCookedFailure;
        uint64_t Generation = 1;
        Container::TSharedPtr<const Asset::AssetSystem> System;
    };

    namespace
    {
        using LoadProfileClock = std::chrono::steady_clock;

        struct TextureAssetLoadPlan
        {
            bool bUseAssetSystem = false;
            bool bPathValid = false;
            Container::String RequestPath;
            Container::String ResolvedPath;
            Container::String CacheKey;
            Container::AnsiString LogicalPath;
            uint64_t Generation = 0;
            Asset::AssetFallbackMode FallbackMode = Asset::AssetFallbackMode::FailOnCookedFailure;
            Container::TSharedPtr<const Asset::AssetSystem> AssetSystem;
        };

        struct PreparedTextureAssetPlan
        {
            PreparedTextureAsset Prepared;
            Container::AnsiString AssetRoot;
            Container::TSharedPtr<const Asset::AssetSystem> AssetSystem;
        };

        struct DecodedTextureMemory
        {
            TextureCreateInfo CreateInfo;
            Container::VariableArray<uint8_t> Pixels;
            int Width = 0;
            int Height = 0;
            int Channels = 0;
            double DecodeMs = 0.0;
            double CopyMs = 0.0;
            bool bSuccess = false;
        };

        struct TextureFileReadMemory
        {
            Container::VariableArray<uint8_t> Bytes;
            double ReadMs = 0.0;
            size_t FileBytes = 0;
            bool bSuccess = false;
        };

        LoadProfileClock::time_point LoadProfileNow()
        {
            return LoadProfileClock::now();
        }

        double LoadProfileElapsedMs(LoadProfileClock::time_point startTime)
        {
            return std::chrono::duration<double, std::milli>(LoadProfileClock::now() - startTime).count();
        }

        thread_local const char* g_TextureCreateUploadProfileRole = "caller";

        const char* GetTextureCreateUploadProfileRole()
        {
            return g_TextureCreateUploadProfileRole;
        }

        class ScopedTextureCreateUploadProfileRole
        {
        public:
            explicit ScopedTextureCreateUploadProfileRole(const char* role)
                : m_PreviousRole(g_TextureCreateUploadProfileRole)
            {
                g_TextureCreateUploadProfileRole = (role != nullptr && role[0] != '\0') ? role : "caller";
            }

            ~ScopedTextureCreateUploadProfileRole()
            {
                g_TextureCreateUploadProfileRole = m_PreviousRole;
            }

        private:
            const char* m_PreviousRole = "caller";
        };

        uint32_t CalculateFullMipCount(uint32_t width, uint32_t height)
        {
            uint32_t mipLevels = 1;
            while (width > 1 || height > 1)
            {
                width = std::max(1u, width / 2);
                height = std::max(1u, height / 2);
                ++mipLevels;
            }
            return mipLevels;
        }

        uint32_t GetTextureBytesPerPixel(TextureCreateInfo::Format format)
        {
            switch (format)
            {
            case TextureCreateInfo::Format::R8_UNORM:
                return 1;
            case TextureCreateInfo::Format::RG8_UNORM:
                return 2;
            case TextureCreateInfo::Format::RGBA8_UNORM:
            case TextureCreateInfo::Format::RGBA8_SRGB:
                return 4;
            case TextureCreateInfo::Format::RGBA16_FLOAT:
                return 8;
            case TextureCreateInfo::Format::RGBA32_FLOAT:
                return 16;
            default:
                return 4;
            }
        }

        Container::AnsiString ToAnsiString(const Container::String &value)
        {
            return Container::AnsiString(value.c_str());
        }

        Container::String ToString(const Container::AnsiString &value)
        {
            return Container::String(value.c_str());
        }

        Container::AnsiString GetDefaultTextureAssetRoot()
        {
#ifdef NORVES_ASSET_DIR
            return Container::AnsiString(NORVES_ASSET_DIR);
#else
            return {};
#endif
        }

        Container::TSharedPtr<const Asset::AssetSystem> CreateTextureAssetSystemSnapshot(
            const Container::AnsiString &assetRoot,
            const Container::String &manifestJson,
            const Container::String &manifestSourceName,
            bool bManifestLoadAttempted)
        {
            auto system = Container::MakeShared<Asset::AssetSystem>(assetRoot);
            if (bManifestLoadAttempted)
            {
                const bool bLoaded = system->LoadManifestFromJsonText(manifestJson, ToAnsiString(manifestSourceName));
                (void)bLoaded;
            }
            return system;
        }

        Asset::AssetFallbackMode ToAssetFallbackMode(TextureAssetFallbackMode mode)
        {
            switch (mode)
            {
            case TextureAssetFallbackMode::DebugAllowLooseFallback:
                return Asset::AssetFallbackMode::DebugAllowLooseFallback;
            case TextureAssetFallbackMode::FailOnCookedFailure:
            default:
                return Asset::AssetFallbackMode::FailOnCookedFailure;
            }
        }

        TextureAssetFallbackMode ToTextureAssetFallbackMode(Asset::AssetFallbackMode mode)
        {
            switch (mode)
            {
            case Asset::AssetFallbackMode::DebugAllowLooseFallback:
                return TextureAssetFallbackMode::DebugAllowLooseFallback;
            case Asset::AssetFallbackMode::FailOnCookedFailure:
            default:
                return TextureAssetFallbackMode::FailOnCookedFailure;
            }
        }

        bool AllowsDebugLooseFallback(TextureAssetFallbackMode mode)
        {
            return mode == TextureAssetFallbackMode::DebugAllowLooseFallback;
        }

        const char *NormalizeProfileRole(const char *role, const char *fallback)
        {
            return (role != nullptr && role[0] != '\0') ? role : fallback;
        }

        const char *GetTextureLoadSourceName(TextureLoadSource source)
        {
            switch (source)
            {
            case TextureLoadSource::CookedNvtex:
                return "cooked_nvtex";
            case TextureLoadSource::LooseStbi:
                return "loose_stbi";
            case TextureLoadSource::LegacyFile:
            default:
                return "legacy_file";
            }
        }

        const char *GetAssetManifestResolveStatusName(Asset::AssetManifestResolveStatus status)
        {
            switch (status)
            {
            case Asset::AssetManifestResolveStatus::CookedReferenceFound:
                return "CookedReferenceFound";
            case Asset::AssetManifestResolveStatus::LooseFallbackManifestMissing:
                return "LooseFallbackManifestMissing";
            case Asset::AssetManifestResolveStatus::LooseFallbackVariantMissing:
                return "LooseFallbackVariantMissing";
            case Asset::AssetManifestResolveStatus::InvalidRequest:
                return "InvalidRequest";
            case Asset::AssetManifestResolveStatus::InvalidManifest:
                return "InvalidManifest";
            default:
                return "Unknown";
            }
        }

        const char *GetAssetReadStatusName(Asset::AssetReadStatus status)
        {
            switch (status)
            {
            case Asset::AssetReadStatus::Success:
                return "Success";
            case Asset::AssetReadStatus::InvalidRequest:
                return "InvalidRequest";
            case Asset::AssetReadStatus::InvalidAssetRoot:
                return "InvalidAssetRoot";
            case Asset::AssetReadStatus::InvalidPath:
                return "InvalidPath";
            case Asset::AssetReadStatus::FileNotFound:
                return "FileNotFound";
            case Asset::AssetReadStatus::OpenFailed:
                return "OpenFailed";
            case Asset::AssetReadStatus::SizeQueryFailed:
                return "SizeQueryFailed";
            case Asset::AssetReadStatus::SizeTooLarge:
                return "SizeTooLarge";
            case Asset::AssetReadStatus::ReadFailed:
                return "ReadFailed";
            default:
                return "Unknown";
            }
        }

        const char *GetAssetResolveStatusName(Asset::AssetResolveStatus status)
        {
            switch (status)
            {
            case Asset::AssetResolveStatus::SuccessCooked:
                return "SuccessCooked";
            case Asset::AssetResolveStatus::SuccessLoose:
                return "SuccessLoose";
            case Asset::AssetResolveStatus::InvalidRequest:
                return "InvalidRequest";
            case Asset::AssetResolveStatus::InvalidManifest:
                return "InvalidManifest";
            case Asset::AssetResolveStatus::LooseReadFailed:
                return "LooseReadFailed";
            case Asset::AssetResolveStatus::CookedPackageReadFailed:
                return "CookedPackageReadFailed";
            case Asset::AssetResolveStatus::CookedPackageParseFailed:
                return "CookedPackageParseFailed";
            case Asset::AssetResolveStatus::CookedEntryMissing:
                return "CookedEntryMissing";
            case Asset::AssetResolveStatus::CookedEntryHashMismatch:
                return "CookedEntryHashMismatch";
            default:
                return "Unknown";
            }
        }

        const char *GetPreparedTextureAssetStatusName(PreparedTextureAssetStatus status)
        {
            switch (status)
            {
            case PreparedTextureAssetStatus::InvalidRequest:
                return "InvalidRequest";
            case PreparedTextureAssetStatus::InvalidPath:
                return "InvalidPath";
            case PreparedTextureAssetStatus::AbsolutePathUnsupported:
                return "AbsolutePathUnsupported";
            case PreparedTextureAssetStatus::ManifestInvalid:
                return "ManifestInvalid";
            case PreparedTextureAssetStatus::ManifestMissingLooseFallback:
                return "ManifestMissingLooseFallback";
            case PreparedTextureAssetStatus::VariantMissingLooseFallback:
                return "VariantMissingLooseFallback";
            case PreparedTextureAssetStatus::CookedPackageReadFailed:
                return "CookedPackageReadFailed";
            case PreparedTextureAssetStatus::CookedPackageParseFailed:
                return "CookedPackageParseFailed";
            case PreparedTextureAssetStatus::CookedEntryMissing:
                return "CookedEntryMissing";
            case PreparedTextureAssetStatus::CookedEntryHashMismatch:
                return "CookedEntryHashMismatch";
            case PreparedTextureAssetStatus::CookedTextureParseFailed:
                return "CookedTextureParseFailed";
            case PreparedTextureAssetStatus::DebugLooseFallback:
                return "DebugLooseFallback";
            case PreparedTextureAssetStatus::CookedReady:
                return "CookedReady";
            default:
                return "Unknown";
            }
        }

        bool IsPreparedTextureAssetLooseFallbackStatus(PreparedTextureAssetStatus status)
        {
            return status == PreparedTextureAssetStatus::ManifestMissingLooseFallback ||
                   status == PreparedTextureAssetStatus::VariantMissingLooseFallback ||
                   status == PreparedTextureAssetStatus::DebugLooseFallback;
        }

        bool IsPreparedTextureAssetFailureStatus(PreparedTextureAssetStatus status)
        {
            switch (status)
            {
            case PreparedTextureAssetStatus::InvalidRequest:
            case PreparedTextureAssetStatus::InvalidPath:
            case PreparedTextureAssetStatus::AbsolutePathUnsupported:
            case PreparedTextureAssetStatus::ManifestInvalid:
            case PreparedTextureAssetStatus::CookedPackageReadFailed:
            case PreparedTextureAssetStatus::CookedPackageParseFailed:
            case PreparedTextureAssetStatus::CookedEntryMissing:
            case PreparedTextureAssetStatus::CookedEntryHashMismatch:
            case PreparedTextureAssetStatus::CookedTextureParseFailed:
                return true;
            case PreparedTextureAssetStatus::ManifestMissingLooseFallback:
            case PreparedTextureAssetStatus::VariantMissingLooseFallback:
            case PreparedTextureAssetStatus::DebugLooseFallback:
            case PreparedTextureAssetStatus::CookedReady:
            default:
                return false;
            }
        }

        PreparedTextureAssetStatus ApplyPreparedCookedFailureFallback(
            PreparedTextureAssetStatus cookedFailureStatus,
            TextureAssetFallbackMode fallbackMode)
        {
            if (AllowsDebugLooseFallback(fallbackMode))
            {
                return PreparedTextureAssetStatus::DebugLooseFallback;
            }

            return cookedFailureStatus;
        }

        void SetPreparedTextureAssetStatus(PreparedTextureAsset &prepared,
                                           PreparedTextureAssetStatus status,
                                           const char *reason)
        {
            prepared.Status = status;
            prepared.Reason = reason != nullptr ? Container::String(reason) : Container::String();
            if (IsPreparedTextureAssetLooseFallbackStatus(status))
            {
                prepared.Source = TextureLoadSource::LooseStbi;
            }
            else if (status == PreparedTextureAssetStatus::CookedReady ||
                     status == PreparedTextureAssetStatus::CookedPackageReadFailed ||
                     status == PreparedTextureAssetStatus::CookedPackageParseFailed ||
                     status == PreparedTextureAssetStatus::CookedEntryMissing ||
                     status == PreparedTextureAssetStatus::CookedEntryHashMismatch ||
                     status == PreparedTextureAssetStatus::CookedTextureParseFailed)
            {
                prepared.Source = TextureLoadSource::CookedNvtex;
            }
        }

        const char *GetCookedTextureParseStatusName(Asset::CookedTextureParseStatus status)
        {
            switch (status)
            {
            case Asset::CookedTextureParseStatus::Success:
                return "Success";
            case Asset::CookedTextureParseStatus::BadMagic:
                return "BadMagic";
            case Asset::CookedTextureParseStatus::PayloadHashMismatch:
                return "PayloadHashMismatch";
            case Asset::CookedTextureParseStatus::InvalidBlob:
                return "InvalidBlob";
            default:
                return "Failure";
            }
        }

        const char *GetCookedTextureUploadStatusName(CookedTextureUploadStatus status)
        {
            switch (status)
            {
            case CookedTextureUploadStatus::Success:
                return "Success";
            case CookedTextureUploadStatus::InvalidDevice:
                return "InvalidDevice";
            case CookedTextureUploadStatus::InvalidTexture:
                return "InvalidTexture";
            case CookedTextureUploadStatus::InvalidDimensions:
                return "InvalidDimensions";
            case CookedTextureUploadStatus::UnsupportedFormat:
                return "UnsupportedFormat";
            case CookedTextureUploadStatus::InvalidMipData:
                return "InvalidMipData";
            case CookedTextureUploadStatus::IntegerOverflow:
                return "IntegerOverflow";
            case CookedTextureUploadStatus::TextureCreationFailed:
                return "TextureCreationFailed";
            case CookedTextureUploadStatus::UploadFailed:
                return "UploadFailed";
            default:
                return "Unknown";
            }
        }

        Container::String MakeLegacyTextureCacheKey(const Container::String &resolvedPath)
        {
            Container::String cacheKey("legacy:");
            cacheKey += resolvedPath;
            return cacheKey;
        }

        Container::String MakeAssetTextureCacheKey(uint64_t generation, const Container::AnsiString &logicalPath)
        {
            Container::String cacheKey("asset:");
            const std::string generationText = std::to_string(generation);
            cacheKey += generationText.c_str();
            cacheKey += ":default:";
            cacheKey += logicalPath.c_str();
            return cacheKey;
        }

        bool DecodeStbiBytes(const uint8_t *bytes,
                             size_t byteCount,
                             const Container::String &debugName,
                             bool bFullMipChain,
                             DecodedTextureMemory &outDecoded)
        {
            outDecoded = {};
            if (bytes == nullptr || byteCount == 0 || byteCount > static_cast<size_t>(INT_MAX))
            {
                return false;
            }

            auto decodeStartTime = LoadProfileNow();
            unsigned char *pixels = stbi_load_from_memory(
                bytes,
                static_cast<int>(byteCount),
                &outDecoded.Width,
                &outDecoded.Height,
                &outDecoded.Channels,
                4);
            outDecoded.DecodeMs = LoadProfileElapsedMs(decodeStartTime);
            if (!pixels || outDecoded.Width <= 0 || outDecoded.Height <= 0)
            {
                if (pixels)
                {
                    stbi_image_free(pixels);
                }
                return false;
            }

            const size_t pixelBytes = static_cast<size_t>(outDecoded.Width) *
                                      static_cast<size_t>(outDecoded.Height) *
                                      4u;
            auto copyStartTime = LoadProfileNow();
            outDecoded.Pixels.resize(pixelBytes);
            std::memcpy(outDecoded.Pixels.data(), pixels, pixelBytes);
            outDecoded.CopyMs = LoadProfileElapsedMs(copyStartTime);
            stbi_image_free(pixels);

            outDecoded.CreateInfo.Width = static_cast<uint32_t>(outDecoded.Width);
            outDecoded.CreateInfo.Height = static_cast<uint32_t>(outDecoded.Height);
            outDecoded.CreateInfo.MipLevels = bFullMipChain
                                                  ? CalculateFullMipCount(outDecoded.CreateInfo.Width, outDecoded.CreateInfo.Height)
                                                  : 1;
            outDecoded.CreateInfo.PixelFormat = TextureCreateInfo::Format::RGBA8_UNORM;
            outDecoded.CreateInfo.DebugName = debugName;
            outDecoded.bSuccess = true;
            return true;
        }

        bool DecodeStbiFromMemory(Asset::AssetBlob blob,
                                  const Container::String &debugName,
                                  bool bFullMipChain,
                                  DecodedTextureMemory &outDecoded)
        {
            if (!blob.IsValid())
            {
                outDecoded = {};
                return false;
            }

            return DecodeStbiBytes(blob.GetData(), blob.GetSize(), debugName, bFullMipChain, outDecoded);
        }

        TextureFileReadMemory ReadTextureFileBytes(const Container::String &resolvedPath)
        {
            TextureFileReadMemory result;
            auto readStartTime = LoadProfileNow();
            auto fileStream = NorvesLib::FileStream::FileStream::Create(
                resolvedPath,
                NorvesLib::FileStream::FileMode::Read,
                NorvesLib::FileStream::FileAccess::Read,
                NorvesLib::FileStream::FileShare::Read);

            if (!fileStream || !fileStream->IsOpen())
            {
                result.ReadMs = LoadProfileElapsedMs(readStartTime);
                return result;
            }

            const int64_t fileSize = fileStream->GetSize();
            if (fileSize <= 0 || static_cast<uint64_t>(fileSize) > static_cast<uint64_t>(INT_MAX))
            {
                fileStream->Close();
                result.ReadMs = LoadProfileElapsedMs(readStartTime);
                return result;
            }

            result.Bytes.resize(static_cast<size_t>(fileSize));
            const size_t bytesRead = fileStream->Read(result.Bytes.data(), static_cast<size_t>(fileSize));
            fileStream->Close();
            result.ReadMs = LoadProfileElapsedMs(readStartTime);
            result.FileBytes = bytesRead;

            if (bytesRead != static_cast<size_t>(fileSize))
            {
                result.Bytes.clear();
                return result;
            }

            result.bSuccess = true;
            return result;
        }
    }

    const char* SetTextureCreateUploadProfileRoleForCurrentThread(const char* role)
    {
        const char* previousRole = g_TextureCreateUploadProfileRole;
        g_TextureCreateUploadProfileRole = (role != nullptr && role[0] != '\0') ? role : "caller";
        return previousRole;
    }

    bool PreparedTextureAsset::HasCookedPayload() const noexcept
    {
        return Status == PreparedTextureAssetStatus::CookedReady && Payload != nullptr;
    }

    bool PreparedTextureAsset::ShouldUseLooseFallback() const noexcept
    {
        return IsPreparedTextureAssetLooseFallbackStatus(Status);
    }

    bool PreparedTextureAsset::Failed() const noexcept
    {
        return IsPreparedTextureAssetFailureStatus(Status);
    }

    RenderResourceManager::RenderResourceManager() = default;

    RenderResourceManager::~RenderResourceManager() = default;

    RenderResourceManager::TextureAssetState &RenderResourceManager::GetTextureAssetStateLocked()
    {
        if (!m_TextureAssetState)
        {
            m_TextureAssetState = Container::MakeUnique<TextureAssetState>();
            m_TextureAssetState->AssetRoot = GetDefaultTextureAssetRoot();
            m_TextureAssetState->System = CreateTextureAssetSystemSnapshot(
                m_TextureAssetState->AssetRoot,
                m_TextureAssetState->ManifestJson,
                m_TextureAssetState->ManifestSourceName,
                m_TextureAssetState->bManifestLoadAttempted);
        }

        return *m_TextureAssetState;
    }

    TextureHandle RenderResourceManager::RegisterUploadedTexture(
        Container::TSharedPtr<RHI::ITexture> rhiTexture,
        const TextureCreateInfo &createInfo)
    {
        if (!m_bInitialized || !rhiTexture)
        {
            return TextureHandle::Invalid();
        }

        auto handle = AllocateHandle<TextureHandle>();

        TextureResourceData data;
        data.RHITexture = std::move(rhiTexture);
        data.Width = createInfo.Width;
        data.Height = createInfo.Height;
        data.Format = createInfo.PixelFormat;
        data.RefCount = 1;
        data.DebugName = createInfo.DebugName;

        Thread::ScopedLock lock(m_ResourceMutex);
        m_Textures[handle.Id] = std::move(data);
        return handle;
    }

    bool RenderResourceManager::SetTextureAssetRoot(const Container::String &assetRoot)
    {
        Thread::ScopedLock assetLock(m_TextureAssetMutex);
        Thread::ScopedLock asyncLock(m_AsyncLoadMutex);
        if (!m_PendingTextureLoads.empty() || m_ActiveTextureLoadFlushCount != 0)
        {
            NORVES_LOG_WARNING("RenderResourceManager",
                               "SetTextureAssetRoot rejected while async texture loads are pending");
            return false;
        }

        TextureAssetState &state = GetTextureAssetStateLocked();
        const Container::AnsiString newRoot = ToAnsiString(assetRoot);
        state.System = CreateTextureAssetSystemSnapshot(
            newRoot,
            state.ManifestJson,
            state.ManifestSourceName,
            state.bManifestLoadAttempted);
        state.AssetRoot = newRoot;
        ++state.Generation;

        Thread::ScopedLock resourceLock(m_ResourceMutex);
        m_TextureCache.clear();
        return true;
    }

    bool RenderResourceManager::LoadTextureAssetManifestFromJsonText(
        const Container::String &jsonText,
        const Container::String &sourceName)
    {
        Thread::ScopedLock assetLock(m_TextureAssetMutex);
        Thread::ScopedLock asyncLock(m_AsyncLoadMutex);
        if (!m_PendingTextureLoads.empty() || m_ActiveTextureLoadFlushCount != 0)
        {
            NORVES_LOG_WARNING("RenderResourceManager",
                               "LoadTextureAssetManifestFromJsonText rejected while async texture loads are pending");
            return false;
        }

        TextureAssetState &state = GetTextureAssetStateLocked();
        auto newSystem = Container::MakeShared<Asset::AssetSystem>(state.AssetRoot);
        const bool bLoaded = newSystem->LoadManifestFromJsonText(jsonText, ToAnsiString(sourceName));
        state.System = newSystem;
        state.ManifestJson = jsonText;
        state.ManifestSourceName = sourceName;
        state.bManifestLoadAttempted = true;
        ++state.Generation;

        Thread::ScopedLock resourceLock(m_ResourceMutex);
        m_TextureCache.clear();
        return bLoaded;
    }

    bool RenderResourceManager::ResetTextureAssetManifest()
    {
        Thread::ScopedLock assetLock(m_TextureAssetMutex);
        Thread::ScopedLock asyncLock(m_AsyncLoadMutex);
        if (!m_PendingTextureLoads.empty() || m_ActiveTextureLoadFlushCount != 0)
        {
            NORVES_LOG_WARNING("RenderResourceManager",
                               "ResetTextureAssetManifest rejected while async texture loads are pending");
            return false;
        }

        TextureAssetState &state = GetTextureAssetStateLocked();
        state.System = Container::MakeShared<Asset::AssetSystem>(state.AssetRoot);
        state.ManifestJson.clear();
        state.ManifestSourceName.clear();
        state.bManifestLoadAttempted = false;
        ++state.Generation;

        Thread::ScopedLock resourceLock(m_ResourceMutex);
        m_TextureCache.clear();
        return true;
    }

    bool RenderResourceManager::SetTextureAssetFallbackMode(TextureAssetFallbackMode mode)
    {
        Thread::ScopedLock assetLock(m_TextureAssetMutex);
        Thread::ScopedLock asyncLock(m_AsyncLoadMutex);
        if (!m_PendingTextureLoads.empty() || m_ActiveTextureLoadFlushCount != 0)
        {
            NORVES_LOG_WARNING("RenderResourceManager",
                               "SetTextureAssetFallbackMode rejected while async texture loads are pending");
            return false;
        }

        TextureAssetState &state = GetTextureAssetStateLocked();
        state.FallbackMode = ToAssetFallbackMode(mode);
        ++state.Generation;

        Thread::ScopedLock resourceLock(m_ResourceMutex);
        m_TextureCache.clear();
        return true;
    }

    PreparedTextureAsset RenderResourceManager::PrepareTextureAssetForWorker(
        const Container::String &requestPath,
        const Container::String &resolvedFallbackPath,
        const char *role,
        uint32_t requestId)
    {
        const char *profileRole = NormalizeProfileRole(role, "worker");

        PreparedTextureAssetPlan plan;
        plan.Prepared.RequestPath = requestPath;
        plan.Prepared.ResolvedFallbackPath = resolvedFallbackPath;

        auto finish = [&](PreparedTextureAssetStatus status, const char *reason)
        {
            SetPreparedTextureAssetStatus(plan.Prepared, status, reason);
            NORVES_LOG_INFO("AssetLoadProfile",
                            "stage=texture_prepare_asset role=%s request_id=%u path=\"%s\" logical_path=\"%s\" cache_key=\"%s\" generation=%llu status=%s source=%s reason=\"%s\"",
                            profileRole,
                            static_cast<unsigned int>(requestId),
                            plan.Prepared.RequestPath.c_str(),
                            plan.Prepared.LogicalPath.c_str(),
                            plan.Prepared.CacheKey.c_str(),
                            static_cast<unsigned long long>(plan.Prepared.Generation),
                            GetPreparedTextureAssetStatusName(plan.Prepared.Status),
                            GetTextureLoadSourceName(plan.Prepared.Source),
                            plan.Prepared.Reason.c_str());
            return plan.Prepared;
        };

        if (requestPath.empty())
        {
            return finish(PreparedTextureAssetStatus::InvalidRequest, "request path is empty");
        }

        Asset::AssetPath assetPath;
        {
            Thread::ScopedLock assetLock(m_TextureAssetMutex);
            TextureAssetState &state = GetTextureAssetStateLocked();
            plan.Prepared.Generation = state.Generation;
            plan.Prepared.FallbackMode = ToTextureAssetFallbackMode(state.FallbackMode);
            plan.AssetRoot = state.AssetRoot;
            plan.AssetSystem = state.System;
            assetPath = Asset::AssetPath::Normalize(ToAnsiString(requestPath), state.AssetRoot);
        }

        if (!assetPath.IsValid())
        {
            return finish(PreparedTextureAssetStatus::InvalidPath, "asset path is invalid");
        }

        if (assetPath.IsAbsolute())
        {
            if (plan.Prepared.ResolvedFallbackPath.empty() && assetPath.HasResolvedPath())
            {
                plan.Prepared.ResolvedFallbackPath = ToString(assetPath.GetResolvedPath());
            }
            return finish(PreparedTextureAssetStatus::AbsolutePathUnsupported, "absolute asset path is unsupported");
        }

        if (!assetPath.HasLogicalPath())
        {
            return finish(PreparedTextureAssetStatus::InvalidPath, "asset path has no logical path");
        }

        plan.Prepared.LogicalPath = assetPath.GetLogicalPath();
        plan.Prepared.CacheKey = MakeAssetTextureCacheKey(plan.Prepared.Generation, plan.Prepared.LogicalPath);
        if (plan.Prepared.ResolvedFallbackPath.empty())
        {
            plan.Prepared.ResolvedFallbackPath = assetPath.HasResolvedPath()
                                                    ? ToString(assetPath.GetResolvedPath())
                                                    : ResolveTexturePath(requestPath);
        }

        if (!plan.AssetSystem)
        {
            return finish(PreparedTextureAssetStatus::ManifestInvalid, "asset system is unavailable");
        }

        auto resolveStartTime = LoadProfileNow();
        const Asset::AssetManifestResolveResult manifestResult = plan.AssetSystem->FindCookedVariant(
            plan.Prepared.LogicalPath,
            Asset::AssetKind::Texture,
            Asset::AssetManifest::DefaultVariant);
        const double resolveMs = LoadProfileElapsedMs(resolveStartTime);
        NORVES_LOG_INFO("AssetLoadProfile",
                        "stage=texture_prepare_manifest role=%s request_id=%u path=\"%s\" logical_path=\"%s\" resolve_ms=%.3f manifest_status=%s",
                        profileRole,
                        static_cast<unsigned int>(requestId),
                        plan.Prepared.RequestPath.c_str(),
                        plan.Prepared.LogicalPath.c_str(),
                        resolveMs,
                        GetAssetManifestResolveStatusName(manifestResult.Status));

        switch (manifestResult.Status)
        {
        case Asset::AssetManifestResolveStatus::CookedReferenceFound:
            break;
        case Asset::AssetManifestResolveStatus::LooseFallbackManifestMissing:
            return finish(PreparedTextureAssetStatus::ManifestMissingLooseFallback, "asset manifest is not loaded");
        case Asset::AssetManifestResolveStatus::LooseFallbackVariantMissing:
            return finish(PreparedTextureAssetStatus::VariantMissingLooseFallback, "asset manifest variant is missing");
        case Asset::AssetManifestResolveStatus::InvalidManifest:
            return finish(PreparedTextureAssetStatus::ManifestInvalid, "asset manifest is invalid");
        case Asset::AssetManifestResolveStatus::InvalidRequest:
        default:
            return finish(PreparedTextureAssetStatus::InvalidPath, "asset manifest request is invalid");
        }

        plan.Prepared.Source = TextureLoadSource::CookedNvtex;

        Asset::AssetFileReader packageReader(plan.AssetRoot);
        Asset::AssetReadRequest packageReadRequest;
        packageReadRequest.InputPath = manifestResult.Reference.CookedPackage;
        packageReadRequest.AssetRoot = {};
        packageReadRequest.bAllowAbsolutePath = false;

        auto packageReadStartTime = LoadProfileNow();
        const Asset::AssetReadResult packageRead = packageReader.Read(packageReadRequest);
        const double packageReadMs = LoadProfileElapsedMs(packageReadStartTime);
        NORVES_LOG_INFO("AssetLoadProfile",
                        "stage=texture_prepare_package_read role=%s request_id=%u path=\"%s\" logical_path=\"%s\" package=\"%s\" bytes=%zu read_ms=%.3f status=%s success=%d",
                        profileRole,
                        static_cast<unsigned int>(requestId),
                        plan.Prepared.RequestPath.c_str(),
                        plan.Prepared.LogicalPath.c_str(),
                        manifestResult.Reference.CookedPackage.c_str(),
                        packageRead.BytesRead,
                        packageReadMs,
                        GetAssetReadStatusName(packageRead.Status),
                        packageRead.Succeeded() ? 1 : 0);

        if (!packageRead.Succeeded())
        {
            const PreparedTextureAssetStatus status = ApplyPreparedCookedFailureFallback(
                PreparedTextureAssetStatus::CookedPackageReadFailed,
                plan.Prepared.FallbackMode);
            return finish(status, "cooked package read failed");
        }

        NorvesLib::FileStream::Package package;
        auto packageParseStartTime = LoadProfileNow();
        const bool bPackageParsed = package.LoadFromMemory(
            packageRead.Blob.GetSpan(),
            ToString(packageRead.Blob.GetSourcePath()));
        const double packageParseMs = LoadProfileElapsedMs(packageParseStartTime);
        NORVES_LOG_INFO("AssetLoadProfile",
                        "stage=texture_prepare_package_parse role=%s request_id=%u path=\"%s\" logical_path=\"%s\" package=\"%s\" parse_ms=%.3f success=%d",
                        profileRole,
                        static_cast<unsigned int>(requestId),
                        plan.Prepared.RequestPath.c_str(),
                        plan.Prepared.LogicalPath.c_str(),
                        manifestResult.Reference.CookedPackage.c_str(),
                        packageParseMs,
                        bPackageParsed ? 1 : 0);

        if (!bPackageParsed)
        {
            const PreparedTextureAssetStatus status = ApplyPreparedCookedFailureFallback(
                PreparedTextureAssetStatus::CookedPackageParseFailed,
                plan.Prepared.FallbackMode);
            return finish(status, "cooked package parse failed");
        }

        NorvesLib::FileStream::PackageEntry entry;
        if (!package.FindEntry(manifestResult.Reference.EntryName, manifestResult.Reference.EntryType, entry))
        {
            const PreparedTextureAssetStatus status = ApplyPreparedCookedFailureFallback(
                PreparedTextureAssetStatus::CookedEntryMissing,
                plan.Prepared.FallbackMode);
            return finish(status, "cooked entry is missing");
        }

        Asset::AssetBlob cookedBlob = package.OpenEntry(entry);
        if (!cookedBlob.IsValid())
        {
            const PreparedTextureAssetStatus status = ApplyPreparedCookedFailureFallback(
                PreparedTextureAssetStatus::CookedEntryMissing,
                plan.Prepared.FallbackMode);
            return finish(status, "cooked entry open failed");
        }

        const uint64_t cookedHash = Asset::ComputeAssetPackagePayloadHash(cookedBlob.GetData(), cookedBlob.GetSize());
        if (cookedHash != manifestResult.Reference.CookedHash)
        {
            const PreparedTextureAssetStatus status = ApplyPreparedCookedFailureFallback(
                PreparedTextureAssetStatus::CookedEntryHashMismatch,
                plan.Prepared.FallbackMode);
            return finish(status, "cooked entry hash mismatch");
        }

        const size_t cookedBlobBytes = cookedBlob.GetSize();
        auto parseStartTime = LoadProfileNow();
        Asset::CookedTextureParseResult parseResult = Asset::ParseCookedTexture(std::move(cookedBlob));
        const double parseMs = LoadProfileElapsedMs(parseStartTime);
        NORVES_LOG_INFO("AssetLoadProfile",
                        "stage=texture_prepare_cooked_parse role=%s source=cooked_nvtex request_id=%u path=\"%s\" logical_path=\"%s\" file_bytes=%zu parse_ms=%.3f status=%s success=%d",
                        profileRole,
                        static_cast<unsigned int>(requestId),
                        plan.Prepared.RequestPath.c_str(),
                        plan.Prepared.LogicalPath.c_str(),
                        cookedBlobBytes,
                        parseMs,
                        GetCookedTextureParseStatusName(parseResult.Status),
                        parseResult.Succeeded() ? 1 : 0);

        if (!parseResult.Succeeded())
        {
            const PreparedTextureAssetStatus status = ApplyPreparedCookedFailureFallback(
                PreparedTextureAssetStatus::CookedTextureParseFailed,
                plan.Prepared.FallbackMode);
            return finish(status, "cooked texture parse failed");
        }

        plan.Prepared.Payload = Container::MakeShared<CookedTextureAsyncPayload>();
        plan.Prepared.Payload->Texture = std::move(parseResult.Texture);
        return finish(PreparedTextureAssetStatus::CookedReady, "");
    }

    bool RenderResourceManager::IsPreparedTextureAssetCurrent(const PreparedTextureAsset &prepared) const
    {
        if (prepared.Generation == 0)
        {
            return false;
        }

        Thread::ScopedLock assetLock(m_TextureAssetMutex);
        return m_TextureAssetState && m_TextureAssetState->Generation == prepared.Generation;
    }

    TextureHandle RenderResourceManager::FinalizePreparedTextureAsset(
        const PreparedTextureAsset &prepared,
        const char *role,
        uint32_t requestId)
    {
        const char *profileRole = NormalizeProfileRole(role, "main_render");
        if (prepared.Status != PreparedTextureAssetStatus::CookedReady || !prepared.Payload)
        {
            return TextureHandle::Invalid();
        }

        {
            Thread::ScopedLock assetLock(m_TextureAssetMutex);
            if (GetTextureAssetStateLocked().Generation != prepared.Generation)
            {
                return TextureHandle::Invalid();
            }

            Thread::ScopedLock resourceLock(m_ResourceMutex);
            auto cacheIt = m_TextureCache.find(prepared.CacheKey);
            if (cacheIt != m_TextureCache.end())
            {
                return cacheIt->second;
            }
        }

        auto payload = prepared.Payload;
        auto uploadStartTime = LoadProfileNow();
        CookedTextureUploadResult uploadResult = CreateAndUploadCookedTexture(
            m_Device.get(),
            payload->Texture,
            prepared.RequestPath);
        const double uploadMs = LoadProfileElapsedMs(uploadStartTime);
        NORVES_LOG_INFO("AssetLoadProfile",
                        "stage=texture_prepared_cooked_upload role=%s source=cooked_nvtex request_id=%u path=\"%s\" logical_path=\"%s\" width=%u height=%u mip_levels=%u layers=%u uploaded_bytes=%zu upload_ms=%.3f status=%s success=%d",
                        profileRole,
                        static_cast<unsigned int>(requestId),
                        prepared.RequestPath.c_str(),
                        prepared.LogicalPath.c_str(),
                        uploadResult.CreateInfo.Width,
                        uploadResult.CreateInfo.Height,
                        uploadResult.CreateInfo.MipLevels,
                        uploadResult.CreateInfo.ArraySize,
                        uploadResult.UploadedBytes,
                        uploadMs,
                        GetCookedTextureUploadStatusName(uploadResult.Status),
                        uploadResult.Succeeded() ? 1 : 0);

        if (!uploadResult.Succeeded())
        {
            return TextureHandle::Invalid();
        }

        if (!IsPreparedTextureAssetCurrent(prepared))
        {
            return TextureHandle::Invalid();
        }

        TextureHandle handle = RegisterUploadedTexture(std::move(uploadResult.Texture), uploadResult.CreateInfo);
        if (!handle.IsValid())
        {
            return TextureHandle::Invalid();
        }

        TextureHandle resultHandle = handle;
        bool bCached = false;
        bool bReleasedNewHandle = false;
        {
            Thread::ScopedLock assetLock(m_TextureAssetMutex);
            const bool bCurrent = GetTextureAssetStateLocked().Generation == prepared.Generation;

            Thread::ScopedLock resourceLock(m_ResourceMutex);
            if (!bCurrent)
            {
                m_Textures.erase(handle.Id);
                resultHandle = TextureHandle::Invalid();
                bReleasedNewHandle = true;
            }
            else
            {
                auto cacheIt = m_TextureCache.find(prepared.CacheKey);
                if (cacheIt != m_TextureCache.end())
                {
                    m_Textures.erase(handle.Id);
                    resultHandle = cacheIt->second;
                    bReleasedNewHandle = true;
                }
                else
                {
                    m_TextureCache[prepared.CacheKey] = handle;
                    bCached = true;
                }
            }
        }

        NORVES_LOG_INFO("AssetLoadProfile",
                        "stage=texture_prepared_finalize role=%s source=cooked_nvtex request_id=%u path=\"%s\" cache_key=\"%s\" generation=%llu cached=%d released_new_handle=%d success=%d",
                        profileRole,
                        static_cast<unsigned int>(requestId),
                        prepared.RequestPath.c_str(),
                        prepared.CacheKey.c_str(),
                        static_cast<unsigned long long>(prepared.Generation),
                        bCached ? 1 : 0,
                        bReleasedNewHandle ? 1 : 0,
                        resultHandle.IsValid() ? 1 : 0);

        return resultHandle;
    }

    bool RenderResourceManager::TrySplitPreparedCookedTextureMip0RGBA8UNormLinear(
        const PreparedTextureAsset &prepared,
        PreparedCookedTextureMip0RGBA8UNormLinearSplit &outSplit,
        Container::String *pOutReason,
        const char *role,
        uint32_t requestId) const
    {
        const char *profileRole = NormalizeProfileRole(role, "worker");

        auto fail = [&](const char *reason)
        {
            outSplit = PreparedCookedTextureMip0RGBA8UNormLinearSplit();
            if (pOutReason != nullptr)
            {
                *pOutReason = reason != nullptr ? Container::String(reason) : Container::String();
            }
            NORVES_LOG_INFO("AssetLoadProfile",
                            "stage=texture_prepared_split role=%s request_id=%u path=\"%s\" logical_path=\"%s\" success=0 reason=\"%s\"",
                            profileRole,
                            static_cast<unsigned int>(requestId),
                            prepared.RequestPath.c_str(),
                            prepared.LogicalPath.c_str(),
                            reason != nullptr ? reason : "");
            return false;
        };

        if (prepared.Status != PreparedTextureAssetStatus::CookedReady)
        {
            return fail("not cooked ready");
        }

        if (!prepared.Payload)
        {
            return fail("payload missing");
        }

        const Asset::CookedTextureData &texture = prepared.Payload->Texture;
        if (texture.LayerCount != 1)
        {
            return fail("unsupported layer count");
        }

        if (texture.PixelFormat != Asset::CookedTexturePixelFormat::RGBA8UNorm)
        {
            return fail("unsupported pixel format");
        }

        if (texture.ColorSpace != Asset::CookedTextureColorSpace::Linear)
        {
            return fail("unsupported color space");
        }

        if (texture.Width == 0 || texture.Height == 0 || texture.Mips.empty())
        {
            return fail("mip0 missing");
        }

        uint64_t pixelCount64 = 0;
        if (texture.Width > std::numeric_limits<uint64_t>::max() / texture.Height)
        {
            return fail("mip0 size overflow");
        }
        pixelCount64 = static_cast<uint64_t>(texture.Width) * static_cast<uint64_t>(texture.Height);
        if (pixelCount64 > static_cast<uint64_t>(std::numeric_limits<size_t>::max()) ||
            pixelCount64 > static_cast<uint64_t>(std::numeric_limits<size_t>::max() / 4u))
        {
            return fail("mip0 size overflow");
        }

        const size_t pixelCount = static_cast<size_t>(pixelCount64);
        const size_t expectedMipBytes = pixelCount * 4u;
        const Container::Span<const uint8_t> mip0 = texture.GetMipBytes(0);
        if (mip0.size() != expectedMipBytes)
        {
            return fail("mip0 size mismatch");
        }

        PreparedCookedTextureMip0RGBA8UNormLinearSplit split;
        split.Width = texture.Width;
        split.Height = texture.Height;
        split.R.resize(pixelCount);
        split.G.resize(pixelCount);
        split.B.resize(pixelCount);
        split.A.resize(pixelCount);
        for (size_t index = 0; index < pixelCount; ++index)
        {
            const size_t baseIndex = index * 4u;
            split.R[index] = mip0[baseIndex + 0];
            split.G[index] = mip0[baseIndex + 1];
            split.B[index] = mip0[baseIndex + 2];
            split.A[index] = mip0[baseIndex + 3];
        }

        outSplit = std::move(split);
        if (pOutReason != nullptr)
        {
            pOutReason->clear();
        }

        NORVES_LOG_INFO("AssetLoadProfile",
                        "stage=texture_prepared_split role=%s request_id=%u path=\"%s\" logical_path=\"%s\" width=%u height=%u pixels=%zu success=1",
                        profileRole,
                        static_cast<unsigned int>(requestId),
                        prepared.RequestPath.c_str(),
                        prepared.LogicalPath.c_str(),
                        outSplit.Width,
                        outSplit.Height,
                        pixelCount);
        return true;
    }


    bool RenderResourceManager::Initialize(Container::TSharedPtr<RHI::IDevice> device)
    {
        if (m_bInitialized)
        {
            return true;
        }

        m_Device = device;
        if (!m_Device)
        {
            NORVES_LOG_ERROR("RenderResourceManager", "Device is null");
            return false;
        }

        m_bInitialized = true;
        LOG_INFO("RenderResourceManager initialized");
        return true;
    }

    void RenderResourceManager::Shutdown()
    {
        if (!m_bInitialized)
        {
            return;
        }

        ClearAllResources();
        m_Device.reset();
        m_bInitialized = false;
        LOG_INFO("RenderResourceManager shutdown");
    }

    // ========================================
    // バッファ操作
    // ========================================

    BufferHandle RenderResourceManager::CreateBuffer(const BufferCreateInfo &createInfo)
    {
        if (!m_bInitialized)
        {
            return BufferHandle::Invalid();
        }

        RHI::ResourceUsage usage = RHI::ResourceUsage::VertexBuffer;
        switch (createInfo.UsageType)
        {
        case BufferCreateInfo::Usage::Vertex:
            usage = RHI::ResourceUsage::VertexBuffer;
            break;
        case BufferCreateInfo::Usage::Index:
            usage = RHI::ResourceUsage::IndexBuffer;
            break;
        case BufferCreateInfo::Usage::Constant:
            usage = RHI::ResourceUsage::ConstantBuffer;
            break;
        case BufferCreateInfo::Usage::Structured:
            usage = RHI::ResourceUsage::ShaderRead;
            break;
        case BufferCreateInfo::Usage::Storage:
            usage = RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::ShaderWrite;
            break;
        default:
            usage = RHI::ResourceUsage::VertexBuffer;
            break;
        }

        RHI::BufferDesc desc(
            static_cast<uint64_t>(createInfo.Size),
            usage,
            createInfo.bHostVisible,
            createInfo.DebugName.c_str());

        auto buffer = m_Device->CreateBuffer(desc);
        if (!buffer)
        {
            return BufferHandle::Invalid();
        }

        auto handle = AllocateHandle<BufferHandle>();

        BufferResourceData data;
        data.RHIBuffer = buffer;
        data.Size = createInfo.Size;
        data.Usage = createInfo.UsageType;
        data.RefCount = 1;
        data.DebugName = createInfo.DebugName;

        Thread::ScopedLock lock(m_ResourceMutex);
        m_Buffers[handle.Id] = std::move(data);

        return handle;
    }

    BufferHandle RenderResourceManager::CreateBuffer(const BufferCreateInfo &createInfo,
                                                     const void *data, size_t dataSize)
    {
        auto handle = CreateBuffer(createInfo);
        if (handle.IsValid() && data && dataSize > 0)
        {
            UpdateBuffer(handle, data, dataSize);
        }
        return handle;
    }

    bool RenderResourceManager::UpdateBuffer(BufferHandle handle, const void *data,
                                             size_t dataSize, size_t offset)
    {
        if (!handle.IsValid() || !data)
        {
            return false;
        }

        Thread::ScopedLock lock(m_ResourceMutex);
        auto it = m_Buffers.find(handle.Id);
        if (it == m_Buffers.end() || !it->second.RHIBuffer)
        {
            return false;
        }

        it->second.RHIBuffer->Update(data, dataSize);
        return true;
    }

    void RenderResourceManager::ReleaseBuffer(BufferHandle handle)
    {
        if (!handle.IsValid())
        {
            return;
        }

        Thread::ScopedLock lock(m_ResourceMutex);
        m_Buffers.erase(handle.Id);
    }

    // ========================================
    // テクスチャ操作
    // ========================================

    TextureHandle RenderResourceManager::CreateTexture(const TextureCreateInfo &createInfo)
    {
        if (!m_bInitialized)
        {
            return TextureHandle::Invalid();
        }

        uint32_t mipLevels = std::max(1u, createInfo.MipLevels);

        // TextureCreateInfo::Format → RHI::Format 変換
        RHI::Format rhiFormat = RHI::Format::R8G8B8A8_UNORM;
        switch (createInfo.PixelFormat)
        {
        case TextureCreateInfo::Format::RGBA8_UNORM:
            rhiFormat = RHI::Format::R8G8B8A8_UNORM;
            break;
        case TextureCreateInfo::Format::RGBA8_SRGB:
            rhiFormat = RHI::Format::R8G8B8A8_SRGB;
            break;
        case TextureCreateInfo::Format::RGBA16_FLOAT:
            rhiFormat = RHI::Format::R16G16B16A16_FLOAT;
            break;
        case TextureCreateInfo::Format::RGBA32_FLOAT:
            rhiFormat = RHI::Format::R32G32B32A32_FLOAT;
            break;
        case TextureCreateInfo::Format::R8_UNORM:
            rhiFormat = RHI::Format::R8_UNORM;
            break;
        case TextureCreateInfo::Format::RG8_UNORM:
            rhiFormat = RHI::Format::R8G8_UNORM;
            break;
        case TextureCreateInfo::Format::D24_S8:
            rhiFormat = RHI::Format::D24_UNORM_S8_UINT;
            break;
        case TextureCreateInfo::Format::D32_FLOAT:
            rhiFormat = RHI::Format::D32_FLOAT;
            break;
        }

        RHI::TextureDesc desc;
        desc.Width = createInfo.Width;
        desc.Height = createInfo.Height;
        desc.Depth = createInfo.Depth;
        desc.MipLevels = mipLevels;
        desc.ArraySize = createInfo.ArraySize;
        desc.TextureFormat = rhiFormat;
        desc.Usage = RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::TransferDst;
        if (mipLevels > 1)
        {
            desc.Usage = desc.Usage | RHI::ResourceUsage::TransferSrc;
        }
        desc.DebugName = createInfo.DebugName.c_str();

        if (createInfo.bRenderTarget)
        {
            desc.Usage = desc.Usage | RHI::ResourceUsage::RenderTarget;
        }
        if (createInfo.bDepthStencil)
        {
            desc.Usage = desc.Usage | RHI::ResourceUsage::DepthStencil;
        }

        auto rhiTexture = m_Device->CreateTexture(desc);
        if (!rhiTexture)
        {
            NORVES_LOG_ERROR("RenderResourceManager", "Failed to create texture");
            return TextureHandle::Invalid();
        }

        auto handle = AllocateHandle<TextureHandle>();

        TextureResourceData data;
        data.RHITexture = rhiTexture;
        data.Width = createInfo.Width;
        data.Height = createInfo.Height;
        data.Format = createInfo.PixelFormat;
        data.RefCount = 1;
        data.DebugName = createInfo.DebugName;

        Thread::ScopedLock lock(m_ResourceMutex);
        m_Textures[handle.Id] = std::move(data);

        return handle;
    }

    TextureHandle RenderResourceManager::CreateTexture(const TextureCreateInfo &createInfo,
                                                       const void *data, size_t dataSize)
    {
        uint32_t effectiveMipLevels = std::max(1u, createInfo.MipLevels);
        auto createStartTime = LoadProfileNow();
        auto handle = CreateTexture(createInfo);
        double textureCreateMs = LoadProfileElapsedMs(createStartTime);
        const char* profileRole = GetTextureCreateUploadProfileRole();
        if (!handle.IsValid() || !data || dataSize == 0)
        {
            NORVES_LOG_INFO("AssetLoadProfile",
                            "stage=texture_create_upload role=%s debug_name=\"%s\" data_size=%zu mip_levels=%u texture_create_ms=%.3f upload_ms=0.000 mipgen_ms=0.000 success=%d",
                            profileRole,
                            createInfo.DebugName.c_str(),
                            dataSize,
                            effectiveMipLevels,
                            textureCreateMs,
                            handle.IsValid() ? 1 : 0);
            return handle;
        }

        // テクスチャにデータをアップロード
        double uploadMs = 0.0;
        double mipgenMs = 0.0;
        bool bTextureFound = false;
        bool bUploadAttempted = false;
        bool bMipgenSuccess = true;
        {
            Thread::ScopedLock lock(m_ResourceMutex);
            auto it = m_Textures.find(handle.Id);
            if (it != m_Textures.end() && it->second.RHITexture)
            {
                bTextureFound = true;
                uint32_t bytesPerPixel = GetTextureBytesPerPixel(createInfo.PixelFormat);

                uint32_t rowPitch = createInfo.Width * bytesPerPixel;
                uint32_t slicePitch = rowPitch * createInfo.Height;
                auto uploadStartTime = LoadProfileNow();
                it->second.RHITexture->Update(data, rowPitch, slicePitch);
                uploadMs = LoadProfileElapsedMs(uploadStartTime);
                bUploadAttempted = true;

                if (effectiveMipLevels > 1)
                {
                    auto mipgenStartTime = LoadProfileNow();
                    auto commandList = m_Device->CreateCommandList();
                    if (!commandList)
                    {
                        bMipgenSuccess = false;
                        NORVES_LOG_ERROR("RenderResourceManager", "Failed to create command list for mip generation");
                    }
                    else
                    {
                        commandList->Begin();
                        commandList->GenerateMipmaps(it->second.RHITexture);
                        commandList->End();
                        commandList->Submit(true);
                    }
                    mipgenMs = LoadProfileElapsedMs(mipgenStartTime);
                }
            }
        }

        bool bSuccess = handle.IsValid() && bTextureFound && bUploadAttempted && bMipgenSuccess;
        NORVES_LOG_INFO("AssetLoadProfile",
                        "stage=texture_create_upload role=%s debug_name=\"%s\" data_size=%zu mip_levels=%u texture_create_ms=%.3f upload_ms=%.3f mipgen_ms=%.3f success=%d",
                        profileRole,
                        createInfo.DebugName.c_str(),
                        dataSize,
                        effectiveMipLevels,
                        textureCreateMs,
                        uploadMs,
                        mipgenMs,
                        bSuccess ? 1 : 0);
        return handle;
    }

    Container::String RenderResourceManager::ResolveTexturePath(const Container::String &path) const
    {
        Container::String resolvedPath = path;
#ifdef NORVES_ASSET_DIR
        // 相対パスの場合（ドライブレター/UNCパスでない場合）NORVES_ASSET_DIRをベースにする
        if (path.size() > 0 && path[0] != '/' && path[0] != '\\' &&
            (path.size() < 2 || path[1] != ':'))
        {
            // "Assets/" プレフィックスを除去してNORVES_ASSET_DIRに結合
            Container::String relativePath = path;
            if (relativePath.size() > 7)
            {
                Container::String prefix = relativePath.substr(0, 7);
                if (prefix == "Assets/" || prefix == "Assets\\")
                {
                    relativePath = relativePath.substr(7);
                }
            }
            resolvedPath = Container::String(NORVES_ASSET_DIR) + "/" + relativePath;
        }
#endif
        return resolvedPath;
    }

    TextureHandle RenderResourceManager::LoadTexture(const Container::String &path)
    {
        if (!m_bInitialized)
        {
            return TextureHandle::Invalid();
        }

        auto buildPlan = [this](const Container::String &requestPath)
        {
            TextureAssetLoadPlan plan;
            plan.RequestPath = requestPath;

            Thread::ScopedLock assetLock(m_TextureAssetMutex);
            TextureAssetState &state = GetTextureAssetStateLocked();
            const Asset::AssetPath assetPath = Asset::AssetPath::Normalize(ToAnsiString(requestPath), state.AssetRoot);
            plan.Generation = state.Generation;
            plan.FallbackMode = state.FallbackMode;

            if (assetPath.IsValid() && assetPath.HasLogicalPath() && !assetPath.IsAbsolute())
            {
                plan.bUseAssetSystem = true;
                plan.bPathValid = true;
                plan.LogicalPath = assetPath.GetLogicalPath();
                plan.ResolvedPath = assetPath.HasResolvedPath()
                                        ? ToString(assetPath.GetResolvedPath())
                                        : ResolveTexturePath(requestPath);
                plan.CacheKey = MakeAssetTextureCacheKey(plan.Generation, plan.LogicalPath);
                plan.AssetSystem = state.System;
                return plan;
            }

            plan.bUseAssetSystem = false;
            plan.bPathValid = assetPath.IsValid();
            plan.ResolvedPath = (assetPath.IsValid() && assetPath.HasResolvedPath())
                                    ? ToString(assetPath.GetResolvedPath())
                                    : ResolveTexturePath(requestPath);
            plan.CacheKey = MakeLegacyTextureCacheKey(plan.ResolvedPath);
            return plan;
        };

        auto isGenerationCurrent = [this](uint64_t generation)
        {
            Thread::ScopedLock assetLock(m_TextureAssetMutex);
            return GetTextureAssetStateLocked().Generation == generation;
        };

        TextureAssetLoadPlan plan = buildPlan(path);

        // キャッシュチェック
        {
            Thread::ScopedLock lock(m_ResourceMutex);
            auto it = m_TextureCache.find(plan.CacheKey);
            if (it != m_TextureCache.end())
            {
                return it->second;
            }
        }

        auto cacheTextureIfCurrent = [this, &isGenerationCurrent](const TextureAssetLoadPlan &loadPlan, TextureHandle handle)
        {
            if (!handle.IsValid() || !isGenerationCurrent(loadPlan.Generation))
            {
                return;
            }

            Thread::ScopedLock lock(m_ResourceMutex);
            m_TextureCache[loadPlan.CacheKey] = handle;
        };

        auto loadStbiFile = [this, &cacheTextureIfCurrent](const TextureAssetLoadPlan &loadPlan,
                                                           TextureLoadSource source)
        {
            int width = 0;
            int height = 0;
            int channels = 0;
            auto stbiStartTime = LoadProfileNow();
            unsigned char *pixels = stbi_load(loadPlan.ResolvedPath.c_str(), &width, &height, &channels, 4);
            double stbiFileMs = LoadProfileElapsedMs(stbiStartTime);
            size_t pixelDataSize = pixels && width > 0 && height > 0
                                       ? static_cast<size_t>(width) * static_cast<size_t>(height) * 4u
                                       : 0;
            NORVES_LOG_INFO("AssetLoadProfile",
                            "stage=texture_sync_stbi_file role=caller source=%s path=\"%s\" resolved_path=\"%s\" width=%d height=%d channels=%d pixel_bytes=%zu ms=%.3f success=%d",
                            GetTextureLoadSourceName(source),
                            loadPlan.RequestPath.c_str(),
                            loadPlan.ResolvedPath.c_str(),
                            width,
                            height,
                            channels,
                            pixelDataSize,
                            stbiFileMs,
                            pixels ? 1 : 0);
            if (!pixels)
            {
                NORVES_LOG_ERROR("RenderResourceManager", "Failed to load texture file: %s", loadPlan.ResolvedPath.c_str());
                return TextureHandle::Invalid();
            }

            TextureCreateInfo createInfo;
            createInfo.Width = static_cast<uint32_t>(width);
            createInfo.Height = static_cast<uint32_t>(height);
            createInfo.MipLevels = CalculateFullMipCount(createInfo.Width, createInfo.Height);
            createInfo.PixelFormat = TextureCreateInfo::Format::RGBA8_UNORM;
            createInfo.DebugName = loadPlan.RequestPath;

            TextureHandle handle = CreateTexture(createInfo, pixels, pixelDataSize);
            stbi_image_free(pixels);

            if (handle.IsValid())
            {
                cacheTextureIfCurrent(loadPlan, handle);
                NORVES_LOG_INFO("RenderResourceManager", "Texture loaded successfully");
            }

            return handle;
        };

        auto loadStbiBlob = [this, &cacheTextureIfCurrent](const TextureAssetLoadPlan &loadPlan,
                                                           const Asset::AssetBlob &blob,
                                                           TextureLoadSource source,
                                                           bool bExplicitDebugFallback)
        {
            DecodedTextureMemory decoded;
            const bool bDecoded = DecodeStbiFromMemory(blob, loadPlan.RequestPath, true, decoded);
            const size_t pixelDataSize = decoded.Pixels.size();
            NORVES_LOG_INFO("AssetLoadProfile",
                            "stage=texture_sync_stbi_memory role=caller source=%s path=\"%s\" logical_path=\"%s\" resolved_path=\"%s\" file_bytes=%zu decode_ms=%.3f copy_ms=%.3f pixel_bytes=%zu width=%d height=%d channels=%d debug_fallback=%d success=%d",
                            GetTextureLoadSourceName(source),
                            loadPlan.RequestPath.c_str(),
                            loadPlan.LogicalPath.c_str(),
                            loadPlan.ResolvedPath.c_str(),
                            blob.GetSize(),
                            decoded.DecodeMs,
                            decoded.CopyMs,
                            pixelDataSize,
                            decoded.Width,
                            decoded.Height,
                            decoded.Channels,
                            bExplicitDebugFallback ? 1 : 0,
                            bDecoded ? 1 : 0);
            if (!bDecoded)
            {
                return TextureHandle::Invalid();
            }

            TextureHandle handle = CreateTexture(decoded.CreateInfo, decoded.Pixels.data(), decoded.Pixels.size());
            if (handle.IsValid())
            {
                cacheTextureIfCurrent(loadPlan, handle);
                NORVES_LOG_INFO("RenderResourceManager", "Texture loaded successfully");
            }

            return handle;
        };

        if (!plan.bUseAssetSystem || !plan.AssetSystem)
        {
            return loadStbiFile(plan, TextureLoadSource::LegacyFile);
        }

        auto resolveStartTime = LoadProfileNow();
        Asset::AssetResolveResult resolveResult = plan.AssetSystem->ResolveAsset(
            plan.LogicalPath,
            Asset::AssetKind::Texture,
            Asset::AssetManifest::DefaultVariant,
            plan.FallbackMode);
        const double resolveMs = LoadProfileElapsedMs(resolveStartTime);
        NORVES_LOG_INFO("AssetLoadProfile",
                        "stage=texture_asset_resolve role=caller path=\"%s\" logical_path=\"%s\" source=%s resolve_ms=%.3f status=%s manifest_status=%u success=%d explicit_fallback=%d",
                        plan.RequestPath.c_str(),
                        plan.LogicalPath.c_str(),
                        resolveResult.UsedCooked() ? "cooked_nvtex" : (resolveResult.UsedLoose() ? "loose_stbi" : "none"),
                        resolveMs,
                        GetAssetResolveStatusName(resolveResult.Status),
                        static_cast<unsigned int>(resolveResult.ManifestStatus),
                        resolveResult.Succeeded() ? 1 : 0,
                        resolveResult.RequiresExplicitLog() ? 1 : 0);

        if (resolveResult.RequiresExplicitLog())
        {
            NORVES_LOG_INFO("AssetLoadProfile",
                            "stage=texture_asset_debug_fallback role=caller source=loose_stbi path=\"%s\" logical_path=\"%s\" reason=\"%s\"",
                            plan.RequestPath.c_str(),
                            plan.LogicalPath.c_str(),
                            resolveResult.FallbackDecision.Reason.c_str());
        }

        if (!resolveResult.Succeeded())
        {
            NORVES_LOG_ERROR("RenderResourceManager", "Texture asset resolve failed: %s", resolveResult.Reason.c_str());
            return TextureHandle::Invalid();
        }

        if (resolveResult.UsedLoose())
        {
            return loadStbiBlob(plan,
                                resolveResult.Blob,
                                TextureLoadSource::LooseStbi,
                                resolveResult.Source == Asset::AssetResolveSource::DebugLooseFallback);
        }

        auto parseStartTime = LoadProfileNow();
        Asset::CookedTextureParseResult parseResult = Asset::ParseCookedTexture(resolveResult.Blob);
        const double parseMs = LoadProfileElapsedMs(parseStartTime);
        NORVES_LOG_INFO("AssetLoadProfile",
                        "stage=texture_cooked_parse role=caller source=cooked_nvtex path=\"%s\" logical_path=\"%s\" file_bytes=%zu parse_ms=%.3f status=%s success=%d",
                        plan.RequestPath.c_str(),
                        plan.LogicalPath.c_str(),
                        resolveResult.Blob.GetSize(),
                        parseMs,
                        GetCookedTextureParseStatusName(parseResult.Status),
                        parseResult.Succeeded() ? 1 : 0);

        if (!parseResult.Succeeded())
        {
            if (plan.FallbackMode == Asset::AssetFallbackMode::DebugAllowLooseFallback)
            {
                NORVES_LOG_INFO("AssetLoadProfile",
                                "stage=texture_asset_debug_fallback role=caller source=loose_stbi path=\"%s\" logical_path=\"%s\" reason=\"cooked texture parse failed\"",
                                plan.RequestPath.c_str(),
                                plan.LogicalPath.c_str());
                return loadStbiFile(plan, TextureLoadSource::LooseStbi);
            }

            return TextureHandle::Invalid();
        }

        auto uploadStartTime = LoadProfileNow();
        CookedTextureUploadResult uploadResult = CreateAndUploadCookedTexture(
            m_Device.get(),
            parseResult.Texture,
            plan.RequestPath);
        const double uploadMs = LoadProfileElapsedMs(uploadStartTime);
        TextureHandle handle = uploadResult.Succeeded()
                                   ? RegisterUploadedTexture(uploadResult.Texture, uploadResult.CreateInfo)
                                   : TextureHandle::Invalid();
        NORVES_LOG_INFO("AssetLoadProfile",
                        "stage=texture_cooked_upload role=caller source=cooked_nvtex path=\"%s\" logical_path=\"%s\" width=%u height=%u mip_levels=%u layers=%u uploaded_bytes=%zu upload_ms=%.3f status=%s success=%d",
                        plan.RequestPath.c_str(),
                        plan.LogicalPath.c_str(),
                        uploadResult.CreateInfo.Width,
                        uploadResult.CreateInfo.Height,
                        uploadResult.CreateInfo.MipLevels,
                        uploadResult.CreateInfo.ArraySize,
                        uploadResult.UploadedBytes,
                        uploadMs,
                        GetCookedTextureUploadStatusName(uploadResult.Status),
                        handle.IsValid() ? 1 : 0);

        if (!handle.IsValid() && plan.FallbackMode == Asset::AssetFallbackMode::DebugAllowLooseFallback)
        {
            NORVES_LOG_INFO("AssetLoadProfile",
                            "stage=texture_asset_debug_fallback role=caller source=loose_stbi path=\"%s\" logical_path=\"%s\" reason=\"cooked texture upload failed\"",
                            plan.RequestPath.c_str(),
                            plan.LogicalPath.c_str());
            return loadStbiFile(plan, TextureLoadSource::LooseStbi);
        }

        cacheTextureIfCurrent(plan, handle);
        return handle;
    }

    TextureHandle RenderResourceManager::RegisterExternalTexture(
        Container::TSharedPtr<RHI::ITexture> rhiTexture,
        const Container::String &debugName)
    {
        if (!m_bInitialized || !rhiTexture)
        {
            return TextureHandle::Invalid();
        }

        auto handle = AllocateHandle<TextureHandle>();

        TextureResourceData data;
        data.RHITexture = rhiTexture;
        data.Width = 0; // 外部テクスチャのため詳細不明（必要に応じて拡張）
        data.Height = 0;
        data.Format = TextureCreateInfo::Format::RGBA8_UNORM; // デフォルト
        data.RefCount = 1;
        data.DebugName = debugName;

        Thread::ScopedLock lock(m_ResourceMutex);
        m_Textures[handle.Id] = std::move(data);

        NORVES_LOG_DEBUG("RenderResourceManager", "External texture registered: %s (handle=%llu)",
                         debugName.c_str(), handle.Id);

        return handle;
    }

    void RenderResourceManager::ReleaseTexture(TextureHandle handle)
    {
        if (!handle.IsValid())
        {
            return;
        }

        Thread::ScopedLock lock(m_ResourceMutex);
        m_Textures.erase(handle.Id);
    }

    // ========================================
    // サンプラー操作
    // ========================================

    SamplerHandle RenderResourceManager::GetDefaultSampler()
    {
        if (m_DefaultSampler.IsValid())
        {
            return m_DefaultSampler;
        }

        // Linear + Wrap サンプラーを作成
        RHI::SamplerDesc desc;
        desc.filterMin = RHI::FilterMode::Anisotropic;
        desc.filterMag = RHI::FilterMode::Anisotropic;
        desc.filterMip = RHI::FilterMode::Anisotropic;
        desc.addressU = RHI::TextureAddressMode::Wrap;
        desc.addressV = RHI::TextureAddressMode::Wrap;
        desc.addressW = RHI::TextureAddressMode::Wrap;
        desc.maxAnisotropy = 4;

        auto rhiSampler = m_Device->CreateSampler(desc);
        if (!rhiSampler)
        {
            NORVES_LOG_ERROR("RenderResourceManager", "Failed to create default sampler");
            return SamplerHandle::Invalid();
        }

        m_DefaultSampler = AllocateHandle<SamplerHandle>();

        SamplerResourceData data;
        data.RHISampler = rhiSampler;
        data.RefCount = 1;
        data.DebugName = "DefaultSampler";

        Thread::ScopedLock lock(m_ResourceMutex);
        m_Samplers[m_DefaultSampler.Id] = std::move(data);

        return m_DefaultSampler;
    }

    SamplerHandle RenderResourceManager::GetPointSampler()
    {
        if (m_PointSampler.IsValid())
        {
            return m_PointSampler;
        }

        // Point + Clamp サンプラーを作成
        RHI::SamplerDesc desc;
        desc.filterMin = RHI::FilterMode::Point;
        desc.filterMag = RHI::FilterMode::Point;
        desc.filterMip = RHI::FilterMode::Point;
        desc.addressU = RHI::TextureAddressMode::Clamp;
        desc.addressV = RHI::TextureAddressMode::Clamp;
        desc.addressW = RHI::TextureAddressMode::Clamp;

        auto rhiSampler = m_Device->CreateSampler(desc);
        if (!rhiSampler)
        {
            NORVES_LOG_ERROR("RenderResourceManager", "Failed to create point sampler");
            return SamplerHandle::Invalid();
        }

        m_PointSampler = AllocateHandle<SamplerHandle>();

        SamplerResourceData data;
        data.RHISampler = rhiSampler;
        data.RefCount = 1;
        data.DebugName = "PointSampler";

        Thread::ScopedLock lock(m_ResourceMutex);
        m_Samplers[m_PointSampler.Id] = std::move(data);

        return m_PointSampler;
    }

    void RenderResourceManager::ReleaseSampler(SamplerHandle handle)
    {
        if (!handle.IsValid())
        {
            return;
        }

        Thread::ScopedLock lock(m_ResourceMutex);
        m_Samplers.erase(handle.Id);
    }

    // ========================================
    // シェーダー操作
    // ========================================

    ShaderHandle RenderResourceManager::CreateShader(const ShaderCreateInfo &createInfo)
    {
        // TODO: シェーダー作成の実装
        return ShaderHandle::Invalid();
    }

    ShaderHandle RenderResourceManager::LoadShader(const Container::String &path, ShaderStage stage)
    {
        // TODO: ファイルからシェーダーロードの実装
        return ShaderHandle::Invalid();
    }

    void RenderResourceManager::ReleaseShader(ShaderHandle handle)
    {
        if (!handle.IsValid())
        {
            return;
        }

        Thread::ScopedLock lock(m_ResourceMutex);
        m_Shaders.erase(handle.Id);
    }

    // ========================================
    // 頂点レイアウト操作
    // ========================================

    VertexLayoutHandle RenderResourceManager::RegisterVertexLayout(const VertexLayout &layout)
    {
        auto handle = AllocateHandle<VertexLayoutHandle>();
        Thread::ScopedLock lock(m_ResourceMutex);
        m_VertexLayouts[handle.Id] = layout;
        return handle;
    }

    const VertexLayout *RenderResourceManager::GetVertexLayout(VertexLayoutHandle handle) const
    {
        Thread::ScopedLock lock(m_ResourceMutex);
        auto it = m_VertexLayouts.find(handle.Id);
        if (it != m_VertexLayouts.end())
        {
            return &it->second;
        }
        return nullptr;
    }

    // ========================================
    // 内部リソースアクセス
    // ========================================

    RHI::IBuffer *RenderResourceManager::GetRHIBuffer(BufferHandle handle) const
    {
        Thread::ScopedLock lock(m_ResourceMutex);
        auto it = m_Buffers.find(handle.Id);
        if (it != m_Buffers.end())
        {
            return it->second.RHIBuffer.get();
        }
        return nullptr;
    }

    RHI::ITexture *RenderResourceManager::GetRHITexture(TextureHandle handle) const
    {
        Thread::ScopedLock lock(m_ResourceMutex);
        auto it = m_Textures.find(handle.Id);
        if (it != m_Textures.end())
        {
            return it->second.RHITexture.get();
        }
        return nullptr;
    }

    Container::TSharedPtr<RHI::ITexture> RenderResourceManager::GetRHITexturePtr(TextureHandle handle) const
    {
        Thread::ScopedLock lock(m_ResourceMutex);
        auto it = m_Textures.find(handle.Id);
        if (it != m_Textures.end())
        {
            return it->second.RHITexture;
        }
        return nullptr;
    }

    RHI::IShader *RenderResourceManager::GetRHIShader(ShaderHandle handle) const
    {
        Thread::ScopedLock lock(m_ResourceMutex);
        auto it = m_Shaders.find(handle.Id);
        if (it != m_Shaders.end())
        {
            return it->second.RHIShader.get();
        }
        return nullptr;
    }

    // ========================================
    // メッシュ操作
    // ========================================

    bool RenderResourceManager::RegisterMesh(MeshDataHandle handle,
                                             const void *vertices, size_t vertexSize,
                                             const uint32_t *indices, uint32_t indexCount)
    {
        if (!m_bInitialized || !handle.IsValid() || !vertices || !indices || indexCount == 0)
        {
            return false;
        }

        // 既に登録済みなら上書き
        UnregisterMesh(handle);

        // 頂点バッファ作成
        RHI::BufferDesc vbDesc(
            static_cast<uint64_t>(vertexSize),
            RHI::ResourceUsage::VertexBuffer,
            true,
            "MeshVB");
        auto vertexBuffer = m_Device->CreateBuffer(vbDesc);
        if (!vertexBuffer)
        {
            NORVES_LOG_ERROR("RenderResourceManager", "Failed to create vertex buffer for mesh");
            return false;
        }
        vertexBuffer->Update(vertices, vertexSize);

        // インデックスバッファ作成
        size_t ibSize = static_cast<size_t>(indexCount) * sizeof(uint32_t);
        RHI::BufferDesc ibDesc(
            static_cast<uint64_t>(ibSize),
            RHI::ResourceUsage::IndexBuffer,
            true,
            "MeshIB");
        auto indexBuffer = m_Device->CreateBuffer(ibDesc);
        if (!indexBuffer)
        {
            NORVES_LOG_ERROR("RenderResourceManager", "Failed to create index buffer for mesh");
            return false;
        }
        indexBuffer->Update(indices, ibSize);

        // 登録
        MeshGPUData gpuData;
        gpuData.VertexBuffer = vertexBuffer;
        gpuData.IndexBuffer = indexBuffer;
        gpuData.IndexCount = indexCount;

        Thread::ScopedLock lock(m_ResourceMutex);
        m_MeshGPUDataMap[handle.Id] = std::move(gpuData);

        NORVES_LOG_INFO("RenderResourceManager", "Mesh registered successfully");
        return true;
    }

    const RenderResourceManager::MeshGPUData *RenderResourceManager::GetMeshGPUData(MeshDataHandle handle) const
    {
        Thread::ScopedLock lock(m_ResourceMutex);
        auto it = m_MeshGPUDataMap.find(handle.Id);
        if (it != m_MeshGPUDataMap.end())
        {
            return &it->second;
        }
        return nullptr;
    }

    void RenderResourceManager::UnregisterMesh(MeshDataHandle handle)
    {
        if (!handle.IsValid())
        {
            return;
        }

        Thread::ScopedLock lock(m_ResourceMutex);
        m_MeshGPUDataMap.erase(handle.Id);
    }

    // ========================================
    // リソース管理
    // ========================================

    void RenderResourceManager::CleanupUnusedResources()
    {
        // TODO: 参照カウントベースのクリーンアップ
    }

    void RenderResourceManager::ClearAllResources()
    {
        Thread::ScopedLock asyncLock(m_AsyncLoadMutex);
        Thread::ScopedLock lock(m_ResourceMutex);

        // ニューラルマテリアルはGPUリソースを持つため、先にShutdownしてから解放
        for (auto &[id, resource] : m_NeuralMaterials)
        {
            if (resource)
            {
                resource->Shutdown();
            }
        }
        m_NeuralMaterials.clear();

        m_Models.clear();
        m_MegaMeshGPUDataMap.clear();
        m_MeshGPUDataMap.clear();
        m_Materials.clear();
        m_Buffers.clear();
        m_Textures.clear();
        m_Samplers.clear();
        m_Shaders.clear();
        m_Pipelines.clear();
        m_VertexLayouts.clear();
        m_TextureCache.clear();
        m_ShaderCache.clear();
        m_PendingTextureLoads.clear();
        m_PendingTextureLoadsByPath.clear();
        m_ActiveTextureLoadFlushCount = 0;
    }

    RenderResourceManager::ResourceStats RenderResourceManager::GetResourceStats() const
    {
        Thread::ScopedLock lock(m_ResourceMutex);

        ResourceStats stats;
        stats.BufferCount = static_cast<uint32_t>(m_Buffers.size());
        stats.TextureCount = static_cast<uint32_t>(m_Textures.size());
        stats.ShaderCount = static_cast<uint32_t>(m_Shaders.size());
        stats.SamplerCount = static_cast<uint32_t>(m_Samplers.size());

        for (const auto &[id, data] : m_Buffers)
        {
            stats.TotalBufferMemory += data.Size;
        }

        return stats;
    }

    // ========================================
    // マテリアル操作
    // ========================================

    MaterialHandle RenderResourceManager::CreateMaterial(const MaterialCreateData &createInfo)
    {
        auto handle = AllocateHandle<MaterialHandle>();

        MaterialResourceData data;
        data.AlbedoTexture = createInfo.AlbedoTexture;
        data.NormalTexture = createInfo.NormalTexture;
        data.MetallicTexture = createInfo.MetallicTexture;
        data.RoughnessTexture = createInfo.RoughnessTexture;
        data.AOTexture = createInfo.AOTexture;
        data.HeightTexture = createInfo.HeightTexture;
        data.HeightScale = createInfo.HeightScale;
        data.EmissiveColor[0] = createInfo.EmissiveColor[0];
        data.EmissiveColor[1] = createInfo.EmissiveColor[1];
        data.EmissiveColor[2] = createInfo.EmissiveColor[2];
        data.EmissiveStrength = createInfo.EmissiveStrength;
        data.Blend = createInfo.Blend;
        data.Shading = createInfo.Shading;
        data.bTwoSided = createInfo.bTwoSided;
        data.bCastShadows = createInfo.bCastShadows;
        data.RefCount = 1;
        data.DebugName = createInfo.DebugName;

        Thread::ScopedLock lock(m_ResourceMutex);
        m_Materials[handle.Id] = std::move(data);

        return handle;
    }

    const MaterialResourceData *RenderResourceManager::GetMaterialData(MaterialHandle handle) const
    {
        Thread::ScopedLock lock(m_ResourceMutex);
        auto it = m_Materials.find(handle.Id);
        if (it != m_Materials.end())
        {
            return &it->second;
        }
        return nullptr;
    }

    bool RenderResourceManager::UpdateMaterial(MaterialHandle handle, const MaterialCreateData &createInfo)
    {
        Thread::ScopedLock lock(m_ResourceMutex);
        auto it = m_Materials.find(handle.Id);
        if (it == m_Materials.end())
        {
            return false;
        }

        auto &data = it->second;
        data.AlbedoTexture = createInfo.AlbedoTexture;
        data.NormalTexture = createInfo.NormalTexture;
        data.MetallicTexture = createInfo.MetallicTexture;
        data.RoughnessTexture = createInfo.RoughnessTexture;
        data.AOTexture = createInfo.AOTexture;
        data.HeightTexture = createInfo.HeightTexture;
        data.HeightScale = createInfo.HeightScale;
        data.EmissiveColor[0] = createInfo.EmissiveColor[0];
        data.EmissiveColor[1] = createInfo.EmissiveColor[1];
        data.EmissiveColor[2] = createInfo.EmissiveColor[2];
        data.EmissiveStrength = createInfo.EmissiveStrength;
        data.Blend = createInfo.Blend;
        data.Shading = createInfo.Shading;
        data.bTwoSided = createInfo.bTwoSided;
        data.bCastShadows = createInfo.bCastShadows;
        data.DebugName = createInfo.DebugName;

        return true;
    }

    void RenderResourceManager::ReleaseMaterial(MaterialHandle handle)
    {
        if (!handle.IsValid())
        {
            return;
        }

        Thread::ScopedLock lock(m_ResourceMutex);
        m_NeuralMaterials.erase(handle.Id);
        m_Materials.erase(handle.Id);
    }

    // ========================================
    // ニューラルマテリアル操作
    // ========================================

    MaterialHandle RenderResourceManager::CreateNeuralMaterial(const NeuralMaterialDesc &desc)
    {
        if (!m_bInitialized || !m_Device)
        {
            return MaterialHandle::Invalid();
        }

        auto neuralMat = MakeShared<NeuralMaterialResource>();
        if (!neuralMat->Initialize(m_Device.get(), desc))
        {
            NORVES_LOG_WARNING("RenderResourceManager", "Failed to initialize neural material: %s",
                               desc.DebugName.c_str());
            return MaterialHandle::Invalid();
        }

        if (!neuralMat->RegisterOutputTextures(*this))
        {
            NORVES_LOG_WARNING("RenderResourceManager", "Failed to register neural material output textures: %s",
                               desc.DebugName.c_str());
            return MaterialHandle::Invalid();
        }

        // 出力TextureHandleでマテリアルを作成
        MaterialCreateData matInfo;
        matInfo.DebugName = desc.DebugName;

        // Albedoスロット(0)
        TextureHandle albedoHandle = neuralMat->GetOutputTextureHandle(0);
        if (albedoHandle.IsValid())
        {
            matInfo.AlbedoTexture = albedoHandle;
        }

        // Normalスロット(1)
        if (neuralMat->GetOutputSlotCount() > 1)
        {
            TextureHandle normalHandle = neuralMat->GetOutputTextureHandle(1);
            if (normalHandle.IsValid())
            {
                matInfo.NormalTexture = normalHandle;
            }
        }

        MaterialHandle handle = CreateMaterial(matInfo);
        if (!handle.IsValid())
        {
            return MaterialHandle::Invalid();
        }

        // 内部でNeuralMaterialResourceを保持
        {
            Thread::ScopedLock lock(m_ResourceMutex);
            m_NeuralMaterials[handle.Id] = std::move(neuralMat);
        }

        NORVES_LOG_INFO("RenderResourceManager", "Neural material created: %s (handle=%llu)",
                        desc.DebugName.c_str(), handle.Id);
        return handle;
    }

    Container::VariableArray<NeuralMaterialResource *> RenderResourceManager::GetNeuralMaterialResources() const
    {
        Thread::ScopedLock lock(m_ResourceMutex);
        Container::VariableArray<NeuralMaterialResource *> result;
        result.reserve(m_NeuralMaterials.size());
        for (const auto &[id, resource] : m_NeuralMaterials)
        {
            if (resource && resource->IsInitialized())
            {
                result.push_back(resource.get());
            }
        }
        return result;
    }

    // ========================================
    // MegaGeometry操作
    // ========================================

    MegaGeometry::MegaMeshHandle RenderResourceManager::CreateMegaMesh(
        const MegaGeometry::MegaMeshCreateInfo &createInfo)
    {
        if (!m_bInitialized || !createInfo.VertexData || !createInfo.IndexData)
        {
            return MegaGeometry::MegaMeshHandle::Invalid();
        }

        if (createInfo.VertexDataSize == 0 || createInfo.IndexCount == 0 || createInfo.Clusters.empty())
        {
            NORVES_LOG_ERROR("RenderResourceManager", "MegaMesh作成情報が不正です: %s",
                             createInfo.DebugName.c_str());
            return MegaGeometry::MegaMeshHandle::Invalid();
        }

        // LOD階層構築が有効な場合
        const void *uploadVertexData = createInfo.VertexData;
        size_t uploadVertexDataSize = createInfo.VertexDataSize;
        const uint32_t *uploadIndexData = createInfo.IndexData;
        uint32_t uploadIndexCount = createInfo.IndexCount;
        uint32_t uploadVertexCount = createInfo.VertexCount;
        const Container::VariableArray<MegaGeometry::MeshCluster> *uploadClusters = &createInfo.Clusters;
        BoundingSphere uploadTotalBounds = createInfo.TotalBounds;

        MegaGeometry::LODHierarchy lodHierarchy;

        if (createInfo.bBuildLODHierarchy && createInfo.Clusters.size() > 1)
        {
            MegaGeometry::LODBuildSettings lodSettings;
            lodSettings.SimplificationRatio = createInfo.LODSimplificationRatio;
            lodSettings.MaxLODLevels = createInfo.MaxLODLevels;
            lodSettings.MinTrianglesForLOD = createInfo.MinTrianglesForLOD;

            lodHierarchy = MegaGeometry::LODHierarchyBuilder::Build(
                createInfo.VertexData,
                createInfo.VertexCount,
                createInfo.VertexStride,
                createInfo.IndexData,
                createInfo.IndexCount,
                lodSettings);

            if (!lodHierarchy.AllClusters.empty())
            {
                uploadVertexData = lodHierarchy.AllVertices.data();
                uploadVertexDataSize = lodHierarchy.AllVertices.size();
                uploadIndexData = lodHierarchy.AllIndices.data();
                uploadIndexCount = static_cast<uint32_t>(lodHierarchy.AllIndices.size());
                uploadVertexCount = lodHierarchy.TotalVertexCount;
                uploadClusters = &lodHierarchy.AllClusters;
                uploadTotalBounds = lodHierarchy.TotalBounds;

                NORVES_LOG_INFO("RenderResourceManager",
                                "LOD階層構築成功: %s (%u レベル, %u クラスタ)",
                                createInfo.DebugName.c_str(),
                                lodHierarchy.LODLevelCount,
                                static_cast<uint32_t>(lodHierarchy.AllClusters.size()));
            }
        }

        // 頂点バッファ作成
        double vertexUploadMs = 0.0;
        double indexUploadMs = 0.0;
        double clusterUploadMs = 0.0;
        Container::String vbName = createInfo.DebugName + "_VB";
        RHI::BufferDesc vbDesc(
            static_cast<uint64_t>(uploadVertexDataSize),
            RHI::ResourceUsage::VertexBuffer | RHI::ResourceUsage::StorageBuffer,
            true,
            vbName.c_str());
        auto vertexUploadStartTime = LoadProfileNow();
        auto vertexBuffer = m_Device->CreateBuffer(vbDesc);
        if (!vertexBuffer)
        {
            vertexUploadMs = LoadProfileElapsedMs(vertexUploadStartTime);
            NORVES_LOG_INFO("AssetLoadProfile",
                            "stage=megamesh_gpu_upload role=main_render debug_name=\"%s\" vertex_bytes=%zu index_bytes=0 cluster_bytes=0 vertex_ms=%.3f index_ms=0.000 cluster_ms=0.000 success=0",
                            createInfo.DebugName.c_str(),
                            uploadVertexDataSize,
                            vertexUploadMs);
            NORVES_LOG_ERROR("RenderResourceManager", "MegaMeshの頂点バッファ作成に失敗: %s",
                             createInfo.DebugName.c_str());
            return MegaGeometry::MegaMeshHandle::Invalid();
        }
        vertexBuffer->Update(uploadVertexData, uploadVertexDataSize);
        vertexUploadMs = LoadProfileElapsedMs(vertexUploadStartTime);

        // インデックスバッファ作成
        size_t ibSize = static_cast<size_t>(uploadIndexCount) * sizeof(uint32_t);
        Container::String ibName = createInfo.DebugName + "_IB";
        RHI::BufferDesc ibDesc(
            static_cast<uint64_t>(ibSize),
            RHI::ResourceUsage::IndexBuffer | RHI::ResourceUsage::StorageBuffer,
            true,
            ibName.c_str());
        auto indexUploadStartTime = LoadProfileNow();
        auto indexBuffer = m_Device->CreateBuffer(ibDesc);
        if (!indexBuffer)
        {
            indexUploadMs = LoadProfileElapsedMs(indexUploadStartTime);
            NORVES_LOG_INFO("AssetLoadProfile",
                            "stage=megamesh_gpu_upload role=main_render debug_name=\"%s\" vertex_bytes=%zu index_bytes=%zu cluster_bytes=0 vertex_ms=%.3f index_ms=%.3f cluster_ms=0.000 success=0",
                            createInfo.DebugName.c_str(),
                            uploadVertexDataSize,
                            ibSize,
                            vertexUploadMs,
                            indexUploadMs);
            NORVES_LOG_ERROR("RenderResourceManager", "MegaMeshのインデックスバッファ作成に失敗: %s",
                             createInfo.DebugName.c_str());
            return MegaGeometry::MegaMeshHandle::Invalid();
        }
        indexBuffer->Update(uploadIndexData, ibSize);
        indexUploadMs = LoadProfileElapsedMs(indexUploadStartTime);

        // クラスタデータSSBO作成
        // MeshCluster → GPUClusterData に変換
        Container::VariableArray<MegaGeometry::GPUClusterData> gpuClusters;
        gpuClusters.reserve(uploadClusters->size());
        for (const auto &cluster : *uploadClusters)
        {
            MegaGeometry::GPUClusterData gpuCluster{};
            gpuCluster.BoundsCenterX = cluster.Bounds.CenterX;
            gpuCluster.BoundsCenterY = cluster.Bounds.CenterY;
            gpuCluster.BoundsCenterZ = cluster.Bounds.CenterZ;
            gpuCluster.BoundsRadius = cluster.Bounds.Radius;
            gpuCluster.ConeAxisX = cluster.ConeAxisX;
            gpuCluster.ConeAxisY = cluster.ConeAxisY;
            gpuCluster.ConeAxisZ = cluster.ConeAxisZ;
            gpuCluster.ConeCutoff = cluster.ConeCutoff;
            gpuCluster.IndexOffset = cluster.IndexOffset;
            gpuCluster.IndexCount = cluster.IndexCount;
            gpuCluster.VertexOffset = cluster.VertexOffset;
            gpuCluster.MaterialIndex = cluster.MaterialIndex;
            gpuCluster.LODLevel = cluster.LODLevel;
            gpuCluster.LODError = cluster.LODError;
            gpuCluster.ParentStart = cluster.ParentStart;
            gpuCluster.ParentCount = cluster.ParentCount;
            gpuClusters.push_back(gpuCluster);
        }

        size_t clusterBufferSize = gpuClusters.size() * sizeof(MegaGeometry::GPUClusterData);
        Container::String cbName = createInfo.DebugName + "_ClusterSSBO";
        RHI::BufferDesc cbDesc(
            static_cast<uint64_t>(clusterBufferSize),
            RHI::ResourceUsage::StorageBuffer,
            true,
            cbName.c_str());
        auto clusterUploadStartTime = LoadProfileNow();
        auto clusterBuffer = m_Device->CreateBuffer(cbDesc);
        if (!clusterBuffer)
        {
            clusterUploadMs = LoadProfileElapsedMs(clusterUploadStartTime);
            NORVES_LOG_INFO("AssetLoadProfile",
                            "stage=megamesh_gpu_upload role=main_render debug_name=\"%s\" vertex_bytes=%zu index_bytes=%zu cluster_bytes=%zu vertex_ms=%.3f index_ms=%.3f cluster_ms=%.3f success=0",
                            createInfo.DebugName.c_str(),
                            uploadVertexDataSize,
                            ibSize,
                            clusterBufferSize,
                            vertexUploadMs,
                            indexUploadMs,
                            clusterUploadMs);
            NORVES_LOG_ERROR("RenderResourceManager", "MegaMeshのクラスタバッファ作成に失敗: %s",
                             createInfo.DebugName.c_str());
            return MegaGeometry::MegaMeshHandle::Invalid();
        }
        clusterBuffer->Update(gpuClusters.data(), clusterBufferSize);
        clusterUploadMs = LoadProfileElapsedMs(clusterUploadStartTime);

        // ハンドル割り当てとGPUデータ登録
        auto handle = AllocateHandle<MegaGeometry::MegaMeshHandle>();

        MegaGeometry::MegaMeshGPUData gpuData;
        gpuData.VertexBuffer = vertexBuffer;
        gpuData.IndexBuffer = indexBuffer;
        gpuData.ClusterBuffer = clusterBuffer;
        gpuData.VertexCount = uploadVertexCount;
        gpuData.IndexCount = uploadIndexCount;
        gpuData.ClusterCount = static_cast<uint32_t>(uploadClusters->size());
        gpuData.TotalBounds = uploadTotalBounds;
        gpuData.Material = createInfo.Material;
        gpuData.DebugName = createInfo.DebugName;

        {
            Thread::ScopedLock lock(m_ResourceMutex);
            m_MegaMeshGPUDataMap[handle.Id] = std::move(gpuData);
        }

        NORVES_LOG_INFO("RenderResourceManager",
                        "MegaMesh作成完了: %s (頂点: %u, インデックス: %u, クラスタ: %u)",
                        createInfo.DebugName.c_str(),
                        createInfo.VertexCount,
                        createInfo.IndexCount,
                        static_cast<uint32_t>(createInfo.Clusters.size()));

        NORVES_LOG_INFO("AssetLoadProfile",
                        "stage=megamesh_gpu_upload role=main_render debug_name=\"%s\" vertex_bytes=%zu index_bytes=%zu cluster_bytes=%zu vertex_ms=%.3f index_ms=%.3f cluster_ms=%.3f success=1",
                        createInfo.DebugName.c_str(),
                        uploadVertexDataSize,
                        ibSize,
                        clusterBufferSize,
                        vertexUploadMs,
                        indexUploadMs,
                        clusterUploadMs);

        return handle;
    }

    const MegaGeometry::MegaMeshGPUData *RenderResourceManager::GetMegaMeshGPUData(
        MegaGeometry::MegaMeshHandle handle) const
    {
        Thread::ScopedLock lock(m_ResourceMutex);
        auto it = m_MegaMeshGPUDataMap.find(handle.Id);
        if (it == m_MegaMeshGPUDataMap.end())
        {
            return nullptr;
        }
        return &it->second;
    }

    void RenderResourceManager::ReleaseMegaMesh(MegaGeometry::MegaMeshHandle handle)
    {
        Thread::ScopedLock lock(m_ResourceMutex);
        m_MegaMeshGPUDataMap.erase(handle.Id);
    }

    ModelHandle RenderResourceManager::RegisterModel(MegaGeometry::MegaMeshHandle megaMeshHandle,
                                                     const Container::String &debugName,
                                                     const Container::String &sourcePath)
    {
        if (!megaMeshHandle.IsValid())
        {
            return ModelHandle::Invalid();
        }

        auto handle = AllocateHandle<ModelHandle>();

        ModelResourceData modelData;
        modelData.MegaMesh = megaMeshHandle;
        modelData.DebugName = debugName;
        modelData.SourcePath = sourcePath;

        Thread::ScopedLock lock(m_ResourceMutex);
        m_Models[handle.Id] = std::move(modelData);
        return handle;
    }

    MegaGeometry::MegaMeshHandle RenderResourceManager::GetModelMegaMeshHandle(ModelHandle handle) const
    {
        if (!handle.IsValid())
        {
            return MegaGeometry::MegaMeshHandle::Invalid();
        }

        Thread::ScopedLock lock(m_ResourceMutex);
        auto it = m_Models.find(handle.Id);
        if (it == m_Models.end())
        {
            return MegaGeometry::MegaMeshHandle::Invalid();
        }

        return it->second.MegaMesh;
    }

    void RenderResourceManager::ReleaseModel(ModelHandle handle)
    {
        if (!handle.IsValid())
        {
            return;
        }

        MegaGeometry::MegaMeshHandle megaMeshHandle = MegaGeometry::MegaMeshHandle::Invalid();
        {
            Thread::ScopedLock lock(m_ResourceMutex);
            auto it = m_Models.find(handle.Id);
            if (it == m_Models.end())
            {
                return;
            }

            megaMeshHandle = it->second.MegaMesh;
            m_Models.erase(it);
        }

        if (megaMeshHandle.IsValid())
        {
            ReleaseMegaMesh(megaMeshHandle);
        }
    }

    // ========================================
    // 非同期テクスチャ読み込み
    // ========================================

    uint32_t RenderResourceManager::LoadTextureAsync(const Container::String &path,
                                                     NorvesLib::Core::Delegate<void, TextureHandle> callback)
    {
        if (!m_bInitialized)
        {
            return 0;
        }

        auto buildPlan = [this](const Container::String &requestPath)
        {
            TextureAssetLoadPlan plan;
            plan.RequestPath = requestPath;

            Thread::ScopedLock assetLock(m_TextureAssetMutex);
            TextureAssetState &state = GetTextureAssetStateLocked();
            const Asset::AssetPath assetPath = Asset::AssetPath::Normalize(ToAnsiString(requestPath), state.AssetRoot);
            plan.Generation = state.Generation;
            plan.FallbackMode = state.FallbackMode;

            if (assetPath.IsValid() && assetPath.HasLogicalPath() && !assetPath.IsAbsolute())
            {
                plan.bUseAssetSystem = true;
                plan.bPathValid = true;
                plan.LogicalPath = assetPath.GetLogicalPath();
                plan.ResolvedPath = assetPath.HasResolvedPath()
                                        ? ToString(assetPath.GetResolvedPath())
                                        : ResolveTexturePath(requestPath);
                plan.CacheKey = MakeAssetTextureCacheKey(plan.Generation, plan.LogicalPath);
                plan.AssetSystem = state.System;
                return plan;
            }

            plan.bUseAssetSystem = false;
            plan.bPathValid = assetPath.IsValid();
            plan.ResolvedPath = (assetPath.IsValid() && assetPath.HasResolvedPath())
                                    ? ToString(assetPath.GetResolvedPath())
                                    : ResolveTexturePath(requestPath);
            plan.CacheKey = MakeLegacyTextureCacheKey(plan.ResolvedPath);
            return plan;
        };

        TextureAssetLoadPlan plan = buildPlan(path);

        // キャッシュチェック（既に読み込み済みなら即コールバック）
        {
            Thread::ScopedLock lock(m_ResourceMutex);
            auto it = m_TextureCache.find(plan.CacheKey);
            if (it != m_TextureCache.end())
            {
                callback.InvokeIfBound(it->second);
                return 0; // 即完了のためリクエストIDは不要
            }
        }

        {
            Thread::ScopedLock lock(m_AsyncLoadMutex);
            auto pendingIt = m_PendingTextureLoadsByPath.find(plan.CacheKey);
            if (pendingIt != m_PendingTextureLoadsByPath.end() && pendingIt->second)
            {
                if (callback.IsBound())
                {
                    pendingIt->second->Callbacks.push_back(std::move(callback));
                }
                NORVES_LOG_INFO("AssetLoadProfile",
                                "stage=texture_async_duplicate_collapsed role=caller request_id=%u cache_key=\"%s\" completed=%d",
                                static_cast<unsigned int>(pendingIt->second->RequestId),
                                plan.CacheKey.c_str(),
                                (pendingIt->second->Task && pendingIt->second->Task->IsCompleted()) ? 1 : 0);
                return pendingIt->second->RequestId;
            }
        }

        auto request = Container::MakeShared<AsyncTextureRequest>();
        request->RequestId = m_NextAsyncRequestId.FetchAdd(1, std::memory_order_relaxed);
        request->Path = path;
        request->CacheKey = plan.CacheKey;
        request->Result.Path = path;
        request->Result.ResolvedPath = plan.ResolvedPath;
        request->Result.CacheKey = plan.CacheKey;
        request->Result.LogicalPath = plan.LogicalPath;
        request->Result.AssetGeneration = plan.Generation;
        request->Result.FallbackMode = ToTextureAssetFallbackMode(plan.FallbackMode);
        if (callback.IsBound())
        {
            request->Callbacks.push_back(std::move(callback));
        }

        // ファイルI/O + デコードをワーカースレッドで実行するタスクを作成
        auto taskFunction = [request, plan]()
        {
            auto &result = request->Result;
            double readMs = 0.0;
            double resolveMs = 0.0;
            double parseMs = 0.0;
            double decodeMs = 0.0;
            double copyMs = 0.0;
            size_t fileBytes = 0;
            size_t pixelBytes = 0;
            int width = 0;
            int height = 0;
            int channels = 0;
            uint32_t mipLevels = 0;
            uint32_t layers = 0;

            auto logAsyncTextureProfile = [&]()
            {
                NORVES_LOG_INFO("AssetLoadProfile",
                                "stage=texture_async_worker role=worker source=%s request_id=%u path=\"%s\" logical_path=\"%s\" resolved_path=\"%s\" read_ms=%.3f resolve_ms=%.3f parse_ms=%.3f file_bytes=%zu decode_ms=%.3f copy_ms=%.3f pixel_bytes=%zu width=%d height=%d channels=%d mip_levels=%u layers=%u success=%d",
                                GetTextureLoadSourceName(result.Source),
                                static_cast<unsigned int>(request->RequestId),
                                result.Path.c_str(),
                                result.LogicalPath.c_str(),
                                result.ResolvedPath.c_str(),
                                readMs,
                                resolveMs,
                                parseMs,
                                fileBytes,
                                decodeMs,
                                copyMs,
                                pixelBytes,
                                width,
                                height,
                                channels,
                                mipLevels,
                                layers,
                                result.bSuccess ? 1 : 0);
            };

            auto decodeFileWithStbi = [&](TextureLoadSource source, bool bExplicitDebugFallback)
            {
                result.Source = source;
                const TextureFileReadMemory fileRead = ReadTextureFileBytes(result.ResolvedPath);
                readMs = fileRead.ReadMs;
                fileBytes = fileRead.FileBytes;
                if (!fileRead.bSuccess)
                {
                    result.bSuccess = false;
                    logAsyncTextureProfile();
                    return;
                }

                DecodedTextureMemory decoded;
                const bool bDecoded = DecodeStbiBytes(
                    fileRead.Bytes.data(),
                    fileRead.Bytes.size(),
                    result.Path,
                    false,
                    decoded);
                decodeMs = decoded.DecodeMs;
                copyMs = decoded.CopyMs;
                pixelBytes = decoded.Pixels.size();
                width = decoded.Width;
                height = decoded.Height;
                channels = decoded.Channels;
                mipLevels = decoded.CreateInfo.MipLevels;
                layers = decoded.CreateInfo.ArraySize;

                if (!bDecoded)
                {
                    result.bSuccess = false;
                    logAsyncTextureProfile();
                    return;
                }

                if (bExplicitDebugFallback)
                {
                    NORVES_LOG_INFO("AssetLoadProfile",
                                    "stage=texture_asset_debug_fallback role=worker source=loose_stbi path=\"%s\" logical_path=\"%s\" reason=\"worker cooked failure fallback\"",
                                    result.Path.c_str(),
                                    result.LogicalPath.c_str());
                }

                result.CreateInfo = std::move(decoded.CreateInfo);
                result.PixelData = std::move(decoded.Pixels);
                result.bSuccess = true;
                logAsyncTextureProfile();
            };

            auto decodeLooseBlobWithStbi = [&](const Asset::AssetBlob &blob,
                                               TextureLoadSource source,
                                               bool bExplicitDebugFallback)
            {
                result.Source = source;
                fileBytes = blob.GetSize();

                DecodedTextureMemory decoded;
                const bool bDecoded = DecodeStbiFromMemory(blob, result.Path, false, decoded);
                decodeMs = decoded.DecodeMs;
                copyMs = decoded.CopyMs;
                pixelBytes = decoded.Pixels.size();
                width = decoded.Width;
                height = decoded.Height;
                channels = decoded.Channels;
                mipLevels = decoded.CreateInfo.MipLevels;
                layers = decoded.CreateInfo.ArraySize;

                if (bExplicitDebugFallback)
                {
                    NORVES_LOG_INFO("AssetLoadProfile",
                                    "stage=texture_asset_debug_fallback role=worker source=loose_stbi path=\"%s\" logical_path=\"%s\" reason=\"asset system cooked failure fallback\"",
                                    result.Path.c_str(),
                                    result.LogicalPath.c_str());
                }

                if (!bDecoded)
                {
                    result.bSuccess = false;
                    logAsyncTextureProfile();
                    return;
                }

                result.CreateInfo = std::move(decoded.CreateInfo);
                result.PixelData = std::move(decoded.Pixels);
                result.bSuccess = true;
                logAsyncTextureProfile();
            };

            if (!plan.bUseAssetSystem || !plan.AssetSystem)
            {
                decodeFileWithStbi(TextureLoadSource::LegacyFile, false);
                return;
            }

            auto resolveStartTime = LoadProfileNow();
            Asset::AssetResolveResult resolveResult = plan.AssetSystem->ResolveAsset(
                plan.LogicalPath,
                Asset::AssetKind::Texture,
                Asset::AssetManifest::DefaultVariant,
                plan.FallbackMode);
            resolveMs = LoadProfileElapsedMs(resolveStartTime);
            NORVES_LOG_INFO("AssetLoadProfile",
                            "stage=texture_asset_resolve role=worker path=\"%s\" logical_path=\"%s\" source=%s resolve_ms=%.3f status=%s manifest_status=%u success=%d explicit_fallback=%d",
                            result.Path.c_str(),
                            result.LogicalPath.c_str(),
                            resolveResult.UsedCooked() ? "cooked_nvtex" : (resolveResult.UsedLoose() ? "loose_stbi" : "none"),
                            resolveMs,
                            GetAssetResolveStatusName(resolveResult.Status),
                            static_cast<unsigned int>(resolveResult.ManifestStatus),
                            resolveResult.Succeeded() ? 1 : 0,
                            resolveResult.RequiresExplicitLog() ? 1 : 0);

            if (!resolveResult.Succeeded())
            {
                if (resolveResult.RequiresExplicitLog())
                {
                    NORVES_LOG_INFO("AssetLoadProfile",
                                    "stage=texture_asset_debug_fallback role=worker source=loose_stbi path=\"%s\" logical_path=\"%s\" reason=\"%s\"",
                                    result.Path.c_str(),
                                    result.LogicalPath.c_str(),
                                    resolveResult.FallbackDecision.Reason.c_str());
                }

                result.bSuccess = false;
                logAsyncTextureProfile();
                return;
            }

            if (resolveResult.UsedLoose())
            {
                decodeLooseBlobWithStbi(
                    resolveResult.Blob,
                    TextureLoadSource::LooseStbi,
                    resolveResult.Source == Asset::AssetResolveSource::DebugLooseFallback);
                return;
            }

            result.Source = TextureLoadSource::CookedNvtex;
            fileBytes = resolveResult.Blob.GetSize();
            auto parseStartTime = LoadProfileNow();
            Asset::CookedTextureParseResult parseResult = Asset::ParseCookedTexture(resolveResult.Blob);
            parseMs = LoadProfileElapsedMs(parseStartTime);
            NORVES_LOG_INFO("AssetLoadProfile",
                            "stage=texture_cooked_parse role=worker source=cooked_nvtex request_id=%u path=\"%s\" logical_path=\"%s\" file_bytes=%zu parse_ms=%.3f status=%s success=%d",
                            static_cast<unsigned int>(request->RequestId),
                            result.Path.c_str(),
                            result.LogicalPath.c_str(),
                            resolveResult.Blob.GetSize(),
                            parseMs,
                            GetCookedTextureParseStatusName(parseResult.Status),
                            parseResult.Succeeded() ? 1 : 0);

            if (!parseResult.Succeeded())
            {
                if (AllowsDebugLooseFallback(result.FallbackMode))
                {
                    NORVES_LOG_INFO("AssetLoadProfile",
                                    "stage=texture_asset_debug_fallback role=worker source=loose_stbi path=\"%s\" logical_path=\"%s\" reason=\"cooked texture parse failed\"",
                                    result.Path.c_str(),
                                    result.LogicalPath.c_str());
                    decodeFileWithStbi(TextureLoadSource::LooseStbi, false);
                    return;
                }

                result.bSuccess = false;
                logAsyncTextureProfile();
                return;
            }

            result.CookedTexture = Container::MakeShared<CookedTextureAsyncPayload>();
            result.CookedTexture->Texture = std::move(parseResult.Texture);
            width = static_cast<int>(result.CookedTexture->Texture.Width);
            height = static_cast<int>(result.CookedTexture->Texture.Height);
            channels = static_cast<int>(Asset::GetCookedTextureBytesPerPixel(result.CookedTexture->Texture.PixelFormat));
            mipLevels = result.CookedTexture->Texture.MipCount;
            layers = result.CookedTexture->Texture.LayerCount;
            result.bSuccess = true;
            logAsyncTextureProfile();
        };

        request->Task = Thread::Task::Create(taskFunction, Thread::TaskPriority::NORMAL);

        // ペンディングリストに追加
        {
            Thread::ScopedLock lock(m_AsyncLoadMutex);
            auto pendingIt = m_PendingTextureLoadsByPath.find(plan.CacheKey);
            if (pendingIt != m_PendingTextureLoadsByPath.end() && pendingIt->second)
            {
                for (auto &pendingCallback : request->Callbacks)
                {
                    if (pendingCallback.IsBound())
                    {
                        pendingIt->second->Callbacks.push_back(std::move(pendingCallback));
                    }
                }

                NORVES_LOG_INFO("AssetLoadProfile",
                                "stage=texture_async_duplicate_collapsed role=caller request_id=%u cache_key=\"%s\" completed=%d insert_recheck=1",
                                static_cast<unsigned int>(pendingIt->second->RequestId),
                                plan.CacheKey.c_str(),
                                (pendingIt->second->Task && pendingIt->second->Task->IsCompleted()) ? 1 : 0);
                return pendingIt->second->RequestId;
            }

            m_PendingTextureLoads.push_back(request);
            m_PendingTextureLoadsByPath[plan.CacheKey] = request;
        }

        // JobSystemに投入
        Thread::JobSystem::Get().SubmitTask(request->Task);

        NORVES_LOG_INFO("RenderResourceManager", "Async texture load started: %s (RequestId=%u)",
                        path.c_str(), static_cast<unsigned int>(request->RequestId));

        return request->RequestId;
    }

    uint32_t RenderResourceManager::FlushCompletedTextureLoads()
    {
        auto flushStartTime = LoadProfileNow();
        Container::VariableArray<Container::TSharedPtr<AsyncTextureRequest>> completedRequests;
        uint32_t processedCount = 0;
        uint32_t successCount = 0;
        uint32_t failedCount = 0;
        double detachMs = 0.0;

        {
            auto detachStartTime = LoadProfileNow();
            Thread::ScopedLock lock(m_AsyncLoadMutex);

            // 完了したリクエストを切り離す
            for (auto it = m_PendingTextureLoads.begin(); it != m_PendingTextureLoads.end();)
            {
                auto &request = *it;
                if (!request || !request->Task || !request->Task->IsCompleted())
                {
                    ++it;
                    continue;
                }

                completedRequests.push_back(request);
                it = m_PendingTextureLoads.erase(it);
            }
            if (!completedRequests.empty())
            {
                ++m_ActiveTextureLoadFlushCount;
            }
            detachMs = LoadProfileElapsedMs(detachStartTime);
        }

        auto isGenerationCurrent = [this](uint64_t generation)
        {
            Thread::ScopedLock assetLock(m_TextureAssetMutex);
            return GetTextureAssetStateLocked().Generation == generation;
        };

        auto cacheTextureIfCurrent = [this, &isGenerationCurrent](const AsyncTextureResult &result, TextureHandle handle)
        {
            if (!handle.IsValid() || !isGenerationCurrent(result.AssetGeneration))
            {
                return false;
            }

            Thread::ScopedLock resourceLock(m_ResourceMutex);
            m_TextureCache[result.CacheKey] = handle;
            return true;
        };

        auto loadLooseFallbackOnMain = [this](const AsyncTextureResult &result)
        {
            TextureHandle handle = TextureHandle::Invalid();
            const TextureFileReadMemory fileRead = ReadTextureFileBytes(result.ResolvedPath);
            DecodedTextureMemory decoded;
            bool bDecoded = false;
            if (fileRead.bSuccess)
            {
                bDecoded = DecodeStbiBytes(
                    fileRead.Bytes.data(),
                    fileRead.Bytes.size(),
                    result.Path,
                    false,
                    decoded);
            }

            NORVES_LOG_INFO("AssetLoadProfile",
                            "stage=texture_async_upload_fallback_decode role=main_render source=loose_stbi path=\"%s\" logical_path=\"%s\" resolved_path=\"%s\" read_ms=%.3f file_bytes=%zu decode_ms=%.3f copy_ms=%.3f pixel_bytes=%zu width=%d height=%d channels=%d success=%d",
                            result.Path.c_str(),
                            result.LogicalPath.c_str(),
                            result.ResolvedPath.c_str(),
                            fileRead.ReadMs,
                            fileRead.FileBytes,
                            decoded.DecodeMs,
                            decoded.CopyMs,
                            decoded.Pixels.size(),
                            decoded.Width,
                            decoded.Height,
                            decoded.Channels,
                            bDecoded ? 1 : 0);

            if (!bDecoded)
            {
                return handle;
            }

            ScopedTextureCreateUploadProfileRole profileRole("main_render");
            return CreateTexture(decoded.CreateInfo, decoded.Pixels.data(), decoded.Pixels.size());
        };

        auto takeCallbacksAndReleasePendingMap = [this](const Container::TSharedPtr<AsyncTextureRequest> &request)
        {
            Container::VariableArray<NorvesLib::Core::Delegate<void, TextureHandle>> callbacks;
            if (!request)
            {
                return callbacks;
            }

            Thread::ScopedLock lock(m_AsyncLoadMutex);
            auto pendingIt = m_PendingTextureLoadsByPath.find(request->CacheKey);
            if (pendingIt != m_PendingTextureLoadsByPath.end() && pendingIt->second == request)
            {
                m_PendingTextureLoadsByPath.erase(pendingIt);
            }

            callbacks = std::move(request->Callbacks);
            request->Callbacks.clear();
            return callbacks;
        };

        for (auto &request : completedRequests)
        {
            auto &result = request->Result;
            if (!isGenerationCurrent(result.AssetGeneration))
            {
                ++failedCount;
                NORVES_LOG_INFO("AssetLoadProfile",
                                "stage=texture_async_stale role=main_render source=%s request_id=%u path=\"%s\" cache_key=\"%s\" result_generation=%llu success=0",
                                GetTextureLoadSourceName(result.Source),
                                static_cast<unsigned int>(request->RequestId),
                                result.Path.c_str(),
                                result.CacheKey.c_str(),
                                static_cast<unsigned long long>(result.AssetGeneration));

                auto callbacks = takeCallbacksAndReleasePendingMap(request);
                for (const auto &callback : callbacks)
                {
                    callback.InvokeIfBound(TextureHandle::Invalid());
                }

                result.PixelData.clear();
                result.PixelData.shrink_to_fit();
                result.CookedTexture.reset();
                ++processedCount;
                continue;
            }

            if (result.bSuccess)
            {
                // メインスレッドでGPUアップロード
                TextureHandle handle = TextureHandle::Invalid();
                bool bDebugFallbackAttempted = false;
                bool bCached = false;
                if (result.Source == TextureLoadSource::CookedNvtex && result.CookedTexture)
                {
                    auto uploadStartTime = LoadProfileNow();
                    CookedTextureUploadResult uploadResult = CreateAndUploadCookedTexture(
                        m_Device.get(),
                        result.CookedTexture->Texture,
                        result.Path);
                    const double uploadMs = LoadProfileElapsedMs(uploadStartTime);
                    handle = uploadResult.Succeeded()
                                 ? RegisterUploadedTexture(uploadResult.Texture, uploadResult.CreateInfo)
                                 : TextureHandle::Invalid();
                    NORVES_LOG_INFO("AssetLoadProfile",
                                    "stage=texture_cooked_upload role=main_render source=cooked_nvtex request_id=%u path=\"%s\" logical_path=\"%s\" width=%u height=%u mip_levels=%u layers=%u uploaded_bytes=%zu upload_ms=%.3f status=%s success=%d",
                                    static_cast<unsigned int>(request->RequestId),
                                    result.Path.c_str(),
                                    result.LogicalPath.c_str(),
                                    uploadResult.CreateInfo.Width,
                                    uploadResult.CreateInfo.Height,
                                    uploadResult.CreateInfo.MipLevels,
                                    uploadResult.CreateInfo.ArraySize,
                                    uploadResult.UploadedBytes,
                                    uploadMs,
                                    GetCookedTextureUploadStatusName(uploadResult.Status),
                                    handle.IsValid() ? 1 : 0);

                    if (!handle.IsValid() && AllowsDebugLooseFallback(result.FallbackMode))
                    {
                        bDebugFallbackAttempted = true;
                        NORVES_LOG_INFO("AssetLoadProfile",
                                        "stage=texture_asset_debug_fallback role=main_render source=loose_stbi path=\"%s\" logical_path=\"%s\" reason=\"cooked texture upload failed\"",
                                        result.Path.c_str(),
                                        result.LogicalPath.c_str());
                        handle = loadLooseFallbackOnMain(result);
                    }
                }
                else
                {
                    ScopedTextureCreateUploadProfileRole profileRole("main_render");
                    handle = CreateTexture(
                        result.CreateInfo,
                        result.PixelData.data(),
                        result.PixelData.size());
                }

                if (handle.IsValid())
                {
                    ++successCount;
                    bCached = cacheTextureIfCurrent(result, handle);

                    NORVES_LOG_INFO("RenderResourceManager", "Async texture loaded: %s",
                                    result.Path.c_str());
                }
                else
                {
                    ++failedCount;
                    NORVES_LOG_ERROR("RenderResourceManager", "Async texture GPU upload failed: %s",
                                     result.Path.c_str());
                }

                NORVES_LOG_INFO("AssetLoadProfile",
                                "stage=texture_async_upload_result role=main_render source=%s request_id=%u path=\"%s\" cache_key=\"%s\" generation=%llu debug_fallback=%d cached=%d success=%d",
                                GetTextureLoadSourceName(result.Source),
                                static_cast<unsigned int>(request->RequestId),
                                result.Path.c_str(),
                                result.CacheKey.c_str(),
                                static_cast<unsigned long long>(result.AssetGeneration),
                                bDebugFallbackAttempted ? 1 : 0,
                                bCached ? 1 : 0,
                                handle.IsValid() ? 1 : 0);

                // コールバック実行
                auto callbacks = takeCallbacksAndReleasePendingMap(request);
                for (const auto &callback : callbacks)
                {
                    callback.InvokeIfBound(handle);
                }
            }
            else
            {
                ++failedCount;
                NORVES_LOG_ERROR("RenderResourceManager", "Async texture load failed: %s",
                                 result.ResolvedPath.c_str());

                // 失敗コールバック
                auto callbacks = takeCallbacksAndReleasePendingMap(request);
                for (const auto &callback : callbacks)
                {
                    callback.InvokeIfBound(TextureHandle::Invalid());
                }
            }

            // ピクセルデータ解放（GPUに転送済み）
            result.PixelData.clear();
            result.PixelData.shrink_to_fit();
            result.CookedTexture.reset();
            ++processedCount;
        }

        if (!completedRequests.empty())
        {
            Thread::ScopedLock lock(m_AsyncLoadMutex);
            if (m_ActiveTextureLoadFlushCount > 0)
            {
                --m_ActiveTextureLoadFlushCount;
            }
        }

        if (processedCount > 0)
        {
            NORVES_LOG_INFO("AssetLoadProfile",
                            "stage=texture_async_flush role=main_render processed=%u success=%u failed=%u detach_ms=%.3f flush_ms=%.3f",
                            static_cast<unsigned int>(processedCount),
                            static_cast<unsigned int>(successCount),
                            static_cast<unsigned int>(failedCount),
                            detachMs,
                            LoadProfileElapsedMs(flushStartTime));
        }

        return processedCount;
    }

    uint32_t RenderResourceManager::GetPendingAsyncLoadCount() const
    {
        Thread::ScopedLock lock(m_AsyncLoadMutex);
        return static_cast<uint32_t>(m_PendingTextureLoads.size());
    }

} // namespace NorvesLib::Core::Rendering
