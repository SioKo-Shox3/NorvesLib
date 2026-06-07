#include "Rendering/RenderResourceManager.h"
#include "Rendering/GpuResourceStore.h"
#include "Rendering/MegaGeometryResourceStore.h"
#include "Rendering/ProceduralMeshGpuStore.h"
#include "Rendering/RenderMaterialStore.h"
#include "Rendering/TextureAssetRuntime.h"
#include "Asset/AssetSystem.h"
#include "RHI/IDevice.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/ITexture.h"
#include "RHI/ISampler.h"
#include "RHI/IShader.h"
#include "RHI/IGPUResourceAllocator.h"
#include "Logging/LogMacros.h"

#include <utility>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
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
        : m_MaterialStore(Container::MakeUnique<RenderMaterialStore>(m_NextHandleId)),
          m_TextureAssets(Container::MakeUnique<TextureAssetRuntime>())
    {
    }

    RenderResourceManager::~RenderResourceManager() = default;

    bool RenderResourceManager::SetTextureAssetRoot(const Container::String &assetRoot)
    {
        return m_TextureAssets ? m_TextureAssets->SetTextureAssetRoot(assetRoot) : false;
    }

    bool RenderResourceManager::LoadTextureAssetManifestFromJsonText(
        const Container::String &jsonText,
        const Container::String &sourceName)
    {
        return m_TextureAssets ? m_TextureAssets->LoadTextureAssetManifestFromJsonText(jsonText, sourceName) : false;
    }

    bool RenderResourceManager::ResetTextureAssetManifest()
    {
        return m_TextureAssets ? m_TextureAssets->ResetTextureAssetManifest() : false;
    }

    bool RenderResourceManager::SetTextureAssetFallbackMode(TextureAssetFallbackMode mode)
    {
        return m_TextureAssets ? m_TextureAssets->SetTextureAssetFallbackMode(mode) : false;
    }

    PreparedTextureAsset RenderResourceManager::PrepareTextureAssetForWorker(
        const Container::String &requestPath,
        const Container::String &resolvedFallbackPath,
        const char *role,
        uint32_t requestId)
    {
        return m_TextureAssets
                   ? m_TextureAssets->PrepareTextureAssetForWorker(requestPath, resolvedFallbackPath, role, requestId)
                   : PreparedTextureAsset();
    }

    bool RenderResourceManager::IsPreparedTextureAssetCurrent(const PreparedTextureAsset &prepared) const
    {
        return m_TextureAssets ? m_TextureAssets->IsPreparedTextureAssetCurrent(prepared) : false;
    }

    TextureHandle RenderResourceManager::FinalizePreparedTextureAsset(
        const PreparedTextureAsset &prepared,
        const char *role,
        uint32_t requestId)
    {
        return m_TextureAssets
                   ? m_TextureAssets->FinalizePreparedTextureAsset(prepared, role, requestId)
                   : TextureHandle::Invalid();
    }

    bool RenderResourceManager::TrySplitPreparedCookedTextureMip0RGBA8UNormLinear(
        const PreparedTextureAsset &prepared,
        PreparedCookedTextureMip0RGBA8UNormLinearSplit &outSplit,
        Container::String *pOutReason,
        const char *role,
        uint32_t requestId) const
    {
        return m_TextureAssets
                   ? m_TextureAssets->TrySplitPreparedCookedTextureMip0RGBA8UNormLinear(
                         prepared,
                         outSplit,
                         pOutReason,
                         role,
                         requestId)
                   : false;
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
        if (m_TextureAssets)
        {
            m_TextureAssets->Bind(m_Device.get(), m_GpuResources.get());
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
        if (m_TextureAssets)
        {
            m_TextureAssets->Unbind();
        }
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
        return m_TextureAssets
                   ? m_TextureAssets->CreateTexture(createInfo, data, dataSize)
                   : TextureHandle::Invalid();
    }

    TextureHandle RenderResourceManager::LoadTexture(const Container::String &path)
    {
        return m_TextureAssets ? m_TextureAssets->LoadTexture(path) : TextureHandle::Invalid();
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
        if (m_TextureAssets)
        {
            m_TextureAssets->ClearRuntimeResources();
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

        if (m_GpuResources)
        {
            m_GpuResources->Clear();
        }
    }

    RenderResourceManager::ResourceStats RenderResourceManager::GetResourceStats() const
    {
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
        return m_TextureAssets ? m_TextureAssets->LoadTextureAsync(path, std::move(callback)) : 0;
    }

    uint32_t RenderResourceManager::FlushCompletedTextureLoads()
    {
        return m_TextureAssets ? m_TextureAssets->FlushCompletedTextureLoads() : 0;
    }

    uint32_t RenderResourceManager::GetPendingAsyncLoadCount() const
    {
        return m_TextureAssets ? m_TextureAssets->GetPendingAsyncLoadCount() : 0;
    }

} // namespace NorvesLib::Core::Rendering
