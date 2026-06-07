#include "Rendering/RenderResourceManager.h"
#include "Rendering/MegaGeometry/LODHierarchyBuilder.h"
#include "Rendering/CookedTextureUpload.h"
#include "Rendering/GpuResourceStore.h"
#include "Rendering/TextureAssetLoader.h"
#include "Rendering/TextureAssetResolver.h"
#include "Asset/AssetSystem.h"
#include "RHI/IDevice.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/ITexture.h"
#include "RHI/ISampler.h"
#include "RHI/IShader.h"
#include "RHI/IGPUResourceAllocator.h"
#include "Logging/LogMacros.h"
#include "Thread/JobSystem.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        using LoadProfileClock = std::chrono::steady_clock;

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

    TextureAssetResolver &RenderResourceManager::GetTextureAssetResolverLocked()
    {
        if (!m_TextureAssetResolver)
        {
            m_TextureAssetResolver = Container::MakeUnique<TextureAssetResolver>();
        }

        return *m_TextureAssetResolver;
    }

    TextureHandle RenderResourceManager::RegisterUploadedTexture(
        Container::TSharedPtr<RHI::ITexture> rhiTexture,
        const TextureCreateInfo &createInfo)
    {
        if (!m_bInitialized || !m_GpuResources || !rhiTexture)
        {
            return TextureHandle::Invalid();
        }

        return m_GpuResources->RegisterUploadedTexture(std::move(rhiTexture), createInfo);
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

        GetTextureAssetResolverLocked().SetAssetRoot(assetRoot);

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

        const bool bLoaded = GetTextureAssetResolverLocked().LoadManifestFromJsonText(jsonText, sourceName);

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

        GetTextureAssetResolverLocked().ResetManifest();

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

        GetTextureAssetResolverLocked().SetFallbackMode(mode);

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
        PreparedTextureAssetPlan plan;
        plan.Prepared.RequestPath = requestPath;
        plan.Prepared.ResolvedFallbackPath = resolvedFallbackPath;

        if (requestPath.empty())
        {
            plan.BlockedStatus = PreparedTextureAssetStatus::InvalidRequest;
            plan.BlockedReason = "request path is empty";
            return TextureAssetLoader::PrepareForWorker(plan, role, requestId);
        }

        {
            Thread::ScopedLock assetLock(m_TextureAssetMutex);
            plan = GetTextureAssetResolverLocked().BuildPreparedTexturePlan(requestPath, resolvedFallbackPath);
        }

        return TextureAssetLoader::PrepareForWorker(plan, role, requestId);
    }

    bool RenderResourceManager::IsPreparedTextureAssetCurrent(const PreparedTextureAsset &prepared) const
    {
        if (prepared.Generation == 0)
        {
            return false;
        }

        Thread::ScopedLock assetLock(m_TextureAssetMutex);
        return m_TextureAssetResolver && m_TextureAssetResolver->IsGenerationCurrent(prepared.Generation);
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
            if (!GetTextureAssetResolverLocked().IsGenerationCurrent(prepared.Generation))
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
            const bool bCurrent = GetTextureAssetResolverLocked().IsGenerationCurrent(prepared.Generation);

            Thread::ScopedLock resourceLock(m_ResourceMutex);
            if (!bCurrent)
            {
                if (m_GpuResources)
                {
                    m_GpuResources->ReleaseTexture(handle);
                }
                resultHandle = TextureHandle::Invalid();
                bReleasedNewHandle = true;
            }
            else
            {
                auto cacheIt = m_TextureCache.find(prepared.CacheKey);
                if (cacheIt != m_TextureCache.end())
                {
                    if (m_GpuResources)
                    {
                        m_GpuResources->ReleaseTexture(handle);
                    }
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
        return TextureAssetLoader::TrySplitPreparedCookedTextureMip0RGBA8UNormLinear(
            prepared,
            outSplit,
            pOutReason,
            role,
            requestId);
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

        m_GpuResources = Container::MakeUnique<GpuResourceStore>(m_Device, m_NextHandleId);
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
        m_GpuResources.reset();
        m_Device.reset();
        m_bInitialized = false;
        LOG_INFO("RenderResourceManager shutdown");
    }

    // ========================================
    // バッファ操作
    // ========================================

    BufferHandle RenderResourceManager::CreateBuffer(const BufferCreateInfo &createInfo)
    {
        if (!m_bInitialized || !m_GpuResources)
        {
            return BufferHandle::Invalid();
        }

        return m_GpuResources->CreateBuffer(createInfo);
    }

    BufferHandle RenderResourceManager::CreateBuffer(const BufferCreateInfo &createInfo,
                                                     const void *data, size_t dataSize)
    {
        if (!m_bInitialized || !m_GpuResources)
        {
            return BufferHandle::Invalid();
        }

        return m_GpuResources->CreateBuffer(createInfo, data, dataSize);
    }

    bool RenderResourceManager::UpdateBuffer(BufferHandle handle, const void *data,
                                             size_t dataSize, size_t offset)
    {
        if (!m_GpuResources)
        {
            return false;
        }

        return m_GpuResources->UpdateBuffer(handle, data, dataSize, offset);
    }

    void RenderResourceManager::ReleaseBuffer(BufferHandle handle)
    {
        if (!m_GpuResources)
        {
            return;
        }

        m_GpuResources->ReleaseBuffer(handle);
    }

    // ========================================
    // テクスチャ操作
    // ========================================

    TextureHandle RenderResourceManager::CreateTexture(const TextureCreateInfo &createInfo)
    {
        if (!m_bInitialized || !m_GpuResources)
        {
            return TextureHandle::Invalid();
        }

        return m_GpuResources->CreateTexture(createInfo);
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

        const GpuResourceStore::TextureUploadResult uploadResult =
            m_GpuResources
                ? m_GpuResources->UploadTextureData(handle, createInfo, data, dataSize)
                : GpuResourceStore::TextureUploadResult();

        bool bSuccess = handle.IsValid() &&
                        uploadResult.bTextureFound &&
                        uploadResult.bUploadAttempted &&
                        uploadResult.bMipgenSuccess;
        NORVES_LOG_INFO("AssetLoadProfile",
                        "stage=texture_create_upload role=%s debug_name=\"%s\" data_size=%zu mip_levels=%u texture_create_ms=%.3f upload_ms=%.3f mipgen_ms=%.3f success=%d",
                        profileRole,
                        createInfo.DebugName.c_str(),
                        dataSize,
                        effectiveMipLevels,
                        textureCreateMs,
                        uploadResult.UploadMs,
                        uploadResult.MipgenMs,
                        bSuccess ? 1 : 0);
        return handle;
    }

    TextureHandle RenderResourceManager::LoadTexture(const Container::String &path)
    {
        if (!m_bInitialized)
        {
            return TextureHandle::Invalid();
        }

        auto buildPlan = [this](const Container::String &requestPath)
        {
            Thread::ScopedLock assetLock(m_TextureAssetMutex);
            return GetTextureAssetResolverLocked().BuildTextureLoadPlan(requestPath);
        };

        auto isGenerationCurrent = [this](uint64_t generation)
        {
            Thread::ScopedLock assetLock(m_TextureAssetMutex);
            return GetTextureAssetResolverLocked().IsGenerationCurrent(generation);
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

        auto uploadCpuTexture = [this, &cacheTextureIfCurrent](const TextureAssetLoadPlan &loadPlan,
                                                               TextureAssetCpuLoadResult &loadResult)
        {
            TextureHandle handle = CreateTexture(
                loadResult.CreateInfo,
                loadResult.GetPixelData(),
                loadResult.PixelDataSize);
            if (handle.IsValid())
            {
                cacheTextureIfCurrent(loadPlan, handle);
                NORVES_LOG_INFO("RenderResourceManager", "Texture loaded successfully");
            }

            return handle;
        };

        TextureAssetCpuLoadResult loadResult = TextureAssetLoader::LoadForCaller(plan);
        if (!loadResult.bSuccess)
        {
            return TextureHandle::Invalid();
        }

        if (loadResult.Source != TextureLoadSource::CookedNvtex || !loadResult.CookedTexture)
        {
            return uploadCpuTexture(plan, loadResult);
        }

        auto uploadStartTime = LoadProfileNow();
        CookedTextureUploadResult uploadResult = CreateAndUploadCookedTexture(
            m_Device.get(),
            loadResult.CookedTexture->Texture,
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
            TextureAssetCpuLoadResult fallbackResult = TextureAssetLoader::LoadLooseFileForCaller(
                plan,
                TextureLoadSource::LooseStbi);
            if (!fallbackResult.bSuccess)
            {
                return TextureHandle::Invalid();
            }
            return uploadCpuTexture(plan, fallbackResult);
        }

        cacheTextureIfCurrent(plan, handle);
        return handle;
    }

    TextureHandle RenderResourceManager::RegisterExternalTexture(
        Container::TSharedPtr<RHI::ITexture> rhiTexture,
        const Container::String &debugName)
    {
        if (!m_bInitialized || !m_GpuResources || !rhiTexture)
        {
            return TextureHandle::Invalid();
        }

        return m_GpuResources->RegisterExternalTexture(std::move(rhiTexture), debugName);
    }

    void RenderResourceManager::ReleaseTexture(TextureHandle handle)
    {
        if (!m_GpuResources)
        {
            return;
        }

        m_GpuResources->ReleaseTexture(handle);
    }

    // ========================================
    // サンプラー操作
    // ========================================

    SamplerHandle RenderResourceManager::GetDefaultSampler()
    {
        if (!m_GpuResources)
        {
            return SamplerHandle::Invalid();
        }

        return m_GpuResources->GetDefaultSampler();
    }

    SamplerHandle RenderResourceManager::GetPointSampler()
    {
        if (!m_GpuResources)
        {
            return SamplerHandle::Invalid();
        }

        return m_GpuResources->GetPointSampler();
    }

    void RenderResourceManager::ReleaseSampler(SamplerHandle handle)
    {
        if (!m_GpuResources)
        {
            return;
        }

        m_GpuResources->ReleaseSampler(handle);
    }

    // ========================================
    // シェーダー操作
    // ========================================

    ShaderHandle RenderResourceManager::CreateShader(const ShaderCreateInfo &createInfo)
    {
        return m_GpuResources ? m_GpuResources->CreateShader(createInfo) : ShaderHandle::Invalid();
    }

    ShaderHandle RenderResourceManager::LoadShader(const Container::String &path, ShaderStage stage)
    {
        return m_GpuResources ? m_GpuResources->LoadShader(path, stage) : ShaderHandle::Invalid();
    }

    void RenderResourceManager::ReleaseShader(ShaderHandle handle)
    {
        if (!m_GpuResources)
        {
            return;
        }

        m_GpuResources->ReleaseShader(handle);
    }

    // ========================================
    // 頂点レイアウト操作
    // ========================================

    VertexLayoutHandle RenderResourceManager::RegisterVertexLayout(const VertexLayout &layout)
    {
        return m_GpuResources ? m_GpuResources->RegisterVertexLayout(layout) : VertexLayoutHandle::Invalid();
    }

    const VertexLayout *RenderResourceManager::GetVertexLayout(VertexLayoutHandle handle) const
    {
        return m_GpuResources ? m_GpuResources->GetVertexLayout(handle) : nullptr;
    }

    // ========================================
    // 内部リソースアクセス
    // ========================================

    RHI::IBuffer *RenderResourceManager::GetRHIBuffer(BufferHandle handle) const
    {
        return m_GpuResources ? m_GpuResources->GetRHIBuffer(handle) : nullptr;
    }

    RHI::ITexture *RenderResourceManager::GetRHITexture(TextureHandle handle) const
    {
        return m_GpuResources ? m_GpuResources->GetRHITexture(handle) : nullptr;
    }

    Container::TSharedPtr<RHI::ITexture> RenderResourceManager::GetRHITexturePtr(TextureHandle handle) const
    {
        return m_GpuResources ? m_GpuResources->GetRHITexturePtr(handle) : nullptr;
    }

    RHI::IShader *RenderResourceManager::GetRHIShader(ShaderHandle handle) const
    {
        return m_GpuResources ? m_GpuResources->GetRHIShader(handle) : nullptr;
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
        if (m_GpuResources)
        {
            m_GpuResources->Clear();
        }
        m_TextureCache.clear();
        m_ShaderCache.clear();
        m_PendingTextureLoads.clear();
        m_PendingTextureLoadsByPath.clear();
        m_ActiveTextureLoadFlushCount = 0;
    }

    RenderResourceManager::ResourceStats RenderResourceManager::GetResourceStats() const
    {
        Thread::ScopedLock lock(m_ResourceMutex);
        return m_GpuResources ? m_GpuResources->GetResourceStats() : ResourceStats();
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
            Thread::ScopedLock assetLock(m_TextureAssetMutex);
            return GetTextureAssetResolverLocked().BuildTextureLoadPlan(requestPath);
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
        request->Result.FallbackMode = TextureAssetResolver::ToTextureAssetFallbackMode(plan.FallbackMode);
        if (callback.IsBound())
        {
            request->Callbacks.push_back(std::move(callback));
        }

        // ファイルI/O + デコードをワーカースレッドで実行するタスクを作成
        auto taskFunction = [request, plan]()
        {
            TextureAssetCpuLoadResult cpuResult = TextureAssetLoader::LoadForWorker(
                plan,
                request->RequestId);

            auto &result = request->Result;
            result.Path = std::move(cpuResult.Path);
            result.ResolvedPath = std::move(cpuResult.ResolvedPath);
            result.CacheKey = std::move(cpuResult.CacheKey);
            result.LogicalPath = std::move(cpuResult.LogicalPath);
            result.CreateInfo = std::move(cpuResult.CreateInfo);
            result.PixelData = std::move(cpuResult.PixelData);
            result.CookedTexture = std::move(cpuResult.CookedTexture);
            result.Source = cpuResult.Source;
            result.FallbackMode = cpuResult.FallbackMode;
            result.AssetGeneration = cpuResult.AssetGeneration;
            result.bSuccess = cpuResult.bSuccess;
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
            return GetTextureAssetResolverLocked().IsGenerationCurrent(generation);
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
            TextureAssetLooseDecodeResult decoded = TextureAssetLoader::DecodeLooseFallbackForMainRender(
                result.Path,
                result.LogicalPath,
                result.ResolvedPath);
            if (!decoded.bSuccess)
            {
                return TextureHandle::Invalid();
            }

            ScopedTextureCreateUploadProfileRole profileRole("main_render");
            return CreateTexture(decoded.CreateInfo, decoded.PixelData.data(), decoded.PixelData.size());
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

                    if (!handle.IsValid() && TextureAssetResolver::AllowsDebugLooseFallback(result.FallbackMode))
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
