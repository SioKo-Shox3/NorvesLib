#include "Rendering/RenderResourceManager.h"
#include "Rendering/CookedTextureUpload.h"
#include "Rendering/GpuResourceStore.h"
#include "Rendering/MegaGeometryResourceStore.h"
#include "Rendering/ProceduralMeshGpuStore.h"
#include "Rendering/RenderMaterialStore.h"
#include "Rendering/TextureAsyncLoadQueue.h"
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

    RenderResourceManager::RenderResourceManager()
        : m_MaterialStore(Container::MakeUnique<RenderMaterialStore>(m_NextHandleId))
    {
    }

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
        if (m_TextureAsyncLoads && m_TextureAsyncLoads->HasPendingOrActiveFlush())
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
        if (m_TextureAsyncLoads && m_TextureAsyncLoads->HasPendingOrActiveFlush())
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
        if (m_TextureAsyncLoads && m_TextureAsyncLoads->HasPendingOrActiveFlush())
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
        if (m_TextureAsyncLoads && m_TextureAsyncLoads->HasPendingOrActiveFlush())
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
        m_MegaGeometryResources = Container::MakeUnique<MegaGeometryResourceStore>(m_Device, m_NextHandleId);
        m_ProceduralMeshes = Container::MakeUnique<ProceduralMeshGpuStore>(m_Device);
        m_TextureAsyncLoads = Container::MakeUnique<TextureAsyncLoadQueue>();
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
        m_TextureAsyncLoads.reset();
        m_ProceduralMeshes.reset();
        m_MegaGeometryResources.reset();
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
        if (!m_bInitialized ||
            !m_Device ||
            !m_ProceduralMeshes ||
            !handle.IsValid() ||
            !vertices ||
            !indices ||
            indexCount == 0)
        {
            return false;
        }

        return m_ProceduralMeshes->RegisterMesh(handle, vertices, vertexSize, indices, indexCount);
    }

    const RenderResourceManager::MeshGPUData *RenderResourceManager::GetMeshGPUData(MeshDataHandle handle) const
    {
        return m_ProceduralMeshes ? m_ProceduralMeshes->GetMeshGPUData(handle) : nullptr;
    }

    void RenderResourceManager::UnregisterMesh(MeshDataHandle handle)
    {
        if (!m_ProceduralMeshes)
        {
            return;
        }

        m_ProceduralMeshes->UnregisterMesh(handle);
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
        if (m_TextureAsyncLoads)
        {
            m_TextureAsyncLoads->ClearPending();
        }

        if (m_MaterialStore)
        {
            m_MaterialStore->Clear();
        }

        if (m_ProceduralMeshes)
        {
            m_ProceduralMeshes->Clear();
        }

        if (m_MegaGeometryResources)
        {
            m_MegaGeometryResources->Clear();
        }

        Thread::ScopedLock lock(m_ResourceMutex);
        if (m_GpuResources)
        {
            m_GpuResources->Clear();
        }
        m_TextureCache.clear();
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
        return m_MaterialStore ? m_MaterialStore->CreateMaterial(createInfo) : MaterialHandle::Invalid();
    }

    const MaterialResourceData *RenderResourceManager::GetMaterialData(MaterialHandle handle) const
    {
        return m_MaterialStore ? m_MaterialStore->GetMaterialData(handle) : nullptr;
    }

    bool RenderResourceManager::UpdateMaterial(MaterialHandle handle, const MaterialCreateData &createInfo)
    {
        return m_MaterialStore ? m_MaterialStore->UpdateMaterial(handle, createInfo) : false;
    }

    void RenderResourceManager::ReleaseMaterial(MaterialHandle handle)
    {
        if (!m_MaterialStore)
        {
            return;
        }

        m_MaterialStore->ReleaseMaterial(handle);
    }

    // ========================================
    // ニューラルマテリアル操作
    // ========================================

    MaterialHandle RenderResourceManager::CreateNeuralMaterial(const NeuralMaterialDesc &desc)
    {
        if (!m_bInitialized || !m_Device || !m_MaterialStore)
        {
            return MaterialHandle::Invalid();
        }

        return m_MaterialStore->CreateNeuralMaterial(m_Device.get(), *this, desc);
    }

    Container::VariableArray<NeuralMaterialResource *> RenderResourceManager::GetNeuralMaterialResources() const
    {
        return m_MaterialStore ? m_MaterialStore->GetNeuralMaterialResources()
                               : Container::VariableArray<NeuralMaterialResource *>();
    }

    // ========================================
    // MegaGeometry操作
    // ========================================

    MegaGeometry::MegaMeshHandle RenderResourceManager::CreateMegaMesh(
        const MegaGeometry::MegaMeshCreateInfo &createInfo)
    {
        if (!m_bInitialized || !m_MegaGeometryResources)
        {
            return MegaGeometry::MegaMeshHandle::Invalid();
        }

        return m_MegaGeometryResources->CreateMegaMesh(createInfo);
    }

    const MegaGeometry::MegaMeshGPUData *RenderResourceManager::GetMegaMeshGPUData(
        MegaGeometry::MegaMeshHandle handle) const
    {
        return m_MegaGeometryResources ? m_MegaGeometryResources->GetMegaMeshGPUData(handle) : nullptr;
    }

    void RenderResourceManager::ReleaseMegaMesh(MegaGeometry::MegaMeshHandle handle)
    {
        if (!m_MegaGeometryResources)
        {
            return;
        }

        m_MegaGeometryResources->ReleaseMegaMesh(handle);
    }

    ModelHandle RenderResourceManager::RegisterModel(MegaGeometry::MegaMeshHandle megaMeshHandle,
                                                     const Container::String &debugName,
                                                     const Container::String &sourcePath)
    {
        return m_MegaGeometryResources ? m_MegaGeometryResources->RegisterModel(megaMeshHandle, debugName, sourcePath)
                                       : ModelHandle::Invalid();
    }

    MegaGeometry::MegaMeshHandle RenderResourceManager::GetModelMegaMeshHandle(ModelHandle handle) const
    {
        return m_MegaGeometryResources ? m_MegaGeometryResources->GetModelMegaMeshHandle(handle)
                                       : MegaGeometry::MegaMeshHandle::Invalid();
    }

    void RenderResourceManager::ReleaseModel(ModelHandle handle)
    {
        if (!m_MegaGeometryResources)
        {
            return;
        }

        m_MegaGeometryResources->ReleaseModel(handle);
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

        if (!m_TextureAsyncLoads)
        {
            m_TextureAsyncLoads = Container::MakeUnique<TextureAsyncLoadQueue>();
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

        const uint32_t duplicateRequestId = m_TextureAsyncLoads->TryAppendDuplicate(plan.CacheKey, callback);
        if (duplicateRequestId != 0)
        {
            return duplicateRequestId;
        }

        auto request = m_TextureAsyncLoads->CreateRequest(
            plan,
            TextureAssetResolver::ToTextureAssetFallbackMode(plan.FallbackMode),
            std::move(callback));
        if (!request)
        {
            return 0;
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

        const TextureAsyncLoadQueue::EnqueueResult enqueueResult =
            m_TextureAsyncLoads->EnqueueOrAppendDuplicateAndSubmit(request);
        if (enqueueResult.bSubmitted)
        {
            NORVES_LOG_INFO("RenderResourceManager", "Async texture load started: %s (RequestId=%u)",
                            path.c_str(), static_cast<unsigned int>(request->RequestId));
        }

        return enqueueResult.RequestId;
    }

    uint32_t RenderResourceManager::FlushCompletedTextureLoads()
    {
        auto flushStartTime = LoadProfileNow();
        TextureAsyncLoadQueue::CompletedBatch completedBatch;
        uint32_t processedCount = 0;
        uint32_t successCount = 0;
        uint32_t failedCount = 0;
        double detachMs = 0.0;

        {
            auto detachStartTime = LoadProfileNow();
            if (m_TextureAsyncLoads)
            {
                completedBatch = m_TextureAsyncLoads->DetachCompletedRequests();
            }
            detachMs = LoadProfileElapsedMs(detachStartTime);
        }
        auto &completedRequests = completedBatch.Requests;

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
            TextureAsyncLoadQueue::CallbackList callbacks;
            if (!request)
            {
                return callbacks;
            }

            if (m_TextureAsyncLoads)
            {
                return m_TextureAsyncLoads->TakeCallbacksAndRelease(request);
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
        return m_TextureAsyncLoads ? m_TextureAsyncLoads->GetPendingCount() : 0;
    }

} // namespace NorvesLib::Core::Rendering
