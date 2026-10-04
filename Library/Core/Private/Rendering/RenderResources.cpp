#include "Rendering/RenderResources.h"

#include "Rendering/GpuResourceStore.h"
#include "Rendering/GpuRetireQueue.h"
#include "Rendering/SparsePagePool.h"
#include "Rendering/SkinnedMeshGpuStore.h"
#include "Rendering/VideoMemoryBudgetLogGate.h"
#include "Rendering/MegaGeometryResourceStore.h"
#include "Rendering/ProceduralMeshGpuStore.h"
#include "Rendering/RenderMaterialStore.h"
#include "Rendering/TextureAssetRuntime.h"
#include "Rendering/TextureAssetResolver.h"
#include "Logging/LogMacros.h"
#include "RHI/IBuffer.h"
#include "RHI/IDevice.h"
#include "RHI/IShader.h"
#include "RHI/ITexture.h"
#include "Resource/ModelAssetLoader.h"
#include "Resource/ModelAssetRuntime.h"
#include "Thread/Atomic.h"

#include <chrono>
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

    struct RenderResources::Impl
    {
        Impl()
            : MaterialStore(Container::MakeUnique<RenderMaterialStore>(NextHandleId)),
              TextureAssets(Container::MakeUnique<TextureAssetRuntime>()),
              ModelAssets(Container::MakeUnique<ModelAssetRuntime>())
        {
        }

        Thread::Atomic<uint64_t> NextHandleId{1};
        Container::TSharedPtr<RHI::IDevice> Device;
        // GpuResources より先に宣言する（各ストアが破棄された後に最後まで残る）。
        GpuRetireQueue RetireQueue;
        // sparse テクスチャへ結ぶ物理メモリのページ。sparse に対応しないデバイスでは作らない。
        // 期限の来た返却は RetireQueue を通ってここへ戻るので、Shutdown では RetireQueue を片付けてから手放す。
        Container::TUniquePtr<SparsePagePool> SparsePool;
        Container::TUniquePtr<SkinnedMeshGpuStore> SkinnedMeshes;
        Container::TUniquePtr<GpuResourceStore> GpuResources;
        Container::TUniquePtr<ProceduralMeshGpuStore> ProceduralMeshes;
        Container::TUniquePtr<RenderMaterialStore> MaterialStore;
        Container::TUniquePtr<MegaGeometryResourceStore> MegaGeometryResources;
        Container::TUniquePtr<TextureAssetRuntime> TextureAssets;
        Container::TUniquePtr<ModelAssetRuntime> ModelAssets;
        bool bInitialized = false;
        bool bShuttingDown = false;

        // VRAM の上限（MB。0 は上限なし）と、予算ログの間引き状態（GameThread だけが触る）
        uint64_t VideoMemoryCapMb = 0;
        VideoMemoryBudgetLogGate VideoMemoryLogGate;
    };

    GpuResources::GpuResources(RenderResources *pOwner)
        : m_pOwner(pOwner)
    {
    }

    BufferHandle GpuResources::CreateBuffer(const BufferCreateInfo &createInfo)
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        if (!impl || !impl->bInitialized || !impl->GpuResources)
        {
            return BufferHandle::Invalid();
        }

        return impl->GpuResources->CreateBuffer(createInfo);
    }

    BufferHandle GpuResources::CreateBuffer(const BufferCreateInfo &createInfo,
                                            const void *data,
                                            size_t dataSize)
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        if (!impl || !impl->bInitialized || !impl->GpuResources)
        {
            return BufferHandle::Invalid();
        }

        return impl->GpuResources->CreateBuffer(createInfo, data, dataSize);
    }

    bool GpuResources::UpdateBuffer(BufferHandle handle,
                                    const void *data,
                                    size_t dataSize,
                                    size_t offset)
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->GpuResources
                   ? impl->GpuResources->UpdateBuffer(handle, data, dataSize, offset)
                   : false;
    }

    void GpuResources::ReleaseBuffer(BufferHandle handle)
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        if (impl && impl->GpuResources)
        {
            impl->GpuResources->ReleaseBuffer(handle);
        }
    }

    SamplerHandle GpuResources::GetDefaultSampler()
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->GpuResources
                   ? impl->GpuResources->GetDefaultSampler()
                   : SamplerHandle::Invalid();
    }

    SamplerHandle GpuResources::GetPointSampler()
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->GpuResources
                   ? impl->GpuResources->GetPointSampler()
                   : SamplerHandle::Invalid();
    }

    void GpuResources::ReleaseSampler(SamplerHandle handle)
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        if (impl && impl->GpuResources)
        {
            impl->GpuResources->ReleaseSampler(handle);
        }
    }

    ShaderHandle GpuResources::CreateShader(const ShaderCreateInfo &createInfo)
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->GpuResources
                   ? impl->GpuResources->CreateShader(createInfo)
                   : ShaderHandle::Invalid();
    }

    ShaderHandle GpuResources::LoadShader(const Container::String &path, ShaderStage stage)
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->GpuResources
                   ? impl->GpuResources->LoadShader(path, stage)
                   : ShaderHandle::Invalid();
    }

    void GpuResources::ReleaseShader(ShaderHandle handle)
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        if (impl && impl->GpuResources)
        {
            impl->GpuResources->ReleaseShader(handle);
        }
    }

    VertexLayoutHandle GpuResources::RegisterVertexLayout(const VertexLayout &layout)
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->GpuResources
                   ? impl->GpuResources->RegisterVertexLayout(layout)
                   : VertexLayoutHandle::Invalid();
    }

    const VertexLayout *GpuResources::GetVertexLayout(VertexLayoutHandle handle) const
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->GpuResources
                   ? impl->GpuResources->GetVertexLayout(handle)
                   : nullptr;
    }

    RHI::IBuffer *GpuResources::GetRHIBuffer(BufferHandle handle) const
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->GpuResources
                   ? impl->GpuResources->GetRHIBuffer(handle)
                   : nullptr;
    }

    RHI::IShader *GpuResources::GetRHIShader(ShaderHandle handle) const
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->GpuResources
                   ? impl->GpuResources->GetRHIShader(handle)
                   : nullptr;
    }

    ResourceStats GpuResources::GetResourceStats() const
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        if (!impl || !impl->GpuResources)
        {
            return ResourceStats();
        }

        ResourceStats stats = impl->GpuResources->GetResourceStats();
        if (impl->SparsePool)
        {
            const SparsePagePool::Stats pool = impl->SparsePool->GetStats();
            stats.SparsePoolCapacityBytes = static_cast<size_t>(pool.CapacityBytes);
            stats.SparsePoolUsedBytes = static_cast<size_t>(pool.UsedBytes);
        }
        return stats;
    }

    TextureResources::TextureResources(RenderResources *pOwner)
        : m_pOwner(pOwner)
    {
    }

    TextureHandle TextureResources::CreateTexture(const TextureCreateInfo &createInfo)
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        if (!impl || !impl->bInitialized || !impl->GpuResources)
        {
            return TextureHandle::Invalid();
        }

        return impl->GpuResources->CreateTexture(createInfo);
    }

    TextureHandle TextureResources::CreateTexture(const TextureCreateInfo &createInfo,
                                                  const void *data,
                                                  size_t dataSize)
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->TextureAssets
                   ? impl->TextureAssets->CreateTexture(createInfo, data, dataSize)
                   : TextureHandle::Invalid();
    }

    TextureHandle TextureResources::LoadTexture(const Container::String &path)
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->TextureAssets
                   ? impl->TextureAssets->LoadTexture(path)
                   : TextureHandle::Invalid();
    }

    uint32_t TextureResources::LoadTextureAsync(
        const Container::String &path,
        NorvesLib::Core::Delegate<void, TextureHandle> callback)
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->TextureAssets
                   ? impl->TextureAssets->LoadTextureAsync(path, std::move(callback))
                   : 0;
    }

    uint32_t TextureResources::FlushCompletedTextureLoads()
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->TextureAssets
                   ? impl->TextureAssets->FlushCompletedTextureLoads()
                   : 0;
    }

    uint32_t TextureResources::GetPendingAsyncLoadCount() const
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->TextureAssets
                   ? impl->TextureAssets->GetPendingAsyncLoadCount()
                   : 0;
    }

    bool TextureResources::SetTextureAssetRoot(const Container::String &assetRoot)
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->TextureAssets
                   ? impl->TextureAssets->SetTextureAssetRoot(assetRoot)
                   : false;
    }

    bool TextureResources::LoadTextureAssetManifestFromJsonText(
        const Container::String &jsonText,
        const Container::String &sourceName)
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->TextureAssets
                   ? impl->TextureAssets->LoadTextureAssetManifestFromJsonText(jsonText, sourceName)
                   : false;
    }

    bool TextureResources::ResetTextureAssetManifest()
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->TextureAssets
                   ? impl->TextureAssets->ResetTextureAssetManifest()
                   : false;
    }

    bool TextureResources::SetTextureAssetFallbackMode(TextureAssetFallbackMode mode)
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->TextureAssets
                   ? impl->TextureAssets->SetTextureAssetFallbackMode(mode)
                   : false;
    }

    PreparedTextureAsset TextureResources::PrepareTextureAssetForWorker(
        const Container::String &requestPath,
        const Container::String &resolvedFallbackPath,
        const char *role,
        uint32_t requestId)
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->TextureAssets
                   ? impl->TextureAssets->PrepareTextureAssetForWorker(
                         requestPath,
                         resolvedFallbackPath,
                         role,
                         requestId)
                   : PreparedTextureAsset();
    }

    bool TextureResources::IsPreparedTextureAssetCurrent(const PreparedTextureAsset &prepared) const
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->TextureAssets
                   ? impl->TextureAssets->IsPreparedTextureAssetCurrent(prepared)
                   : false;
    }

    TextureHandle TextureResources::FinalizePreparedTextureAsset(
        const PreparedTextureAsset &prepared,
        const char *role,
        uint32_t requestId)
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->TextureAssets
                   ? impl->TextureAssets->FinalizePreparedTextureAsset(prepared, role, requestId)
                   : TextureHandle::Invalid();
    }

    bool TextureResources::TrySplitPreparedCookedTextureMip0RGBA8UNormLinear(
        const PreparedTextureAsset &prepared,
        PreparedCookedTextureMip0RGBA8UNormLinearSplit &outSplit,
        Container::String *pOutReason,
        const char *role,
        uint32_t requestId) const
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->TextureAssets
                   ? impl->TextureAssets->TrySplitPreparedCookedTextureMip0RGBA8UNormLinear(
                         prepared,
                         outSplit,
                         pOutReason,
                         role,
                         requestId)
                   : false;
    }

    TextureHandle TextureResources::RegisterExternalTexture(
        Container::TSharedPtr<RHI::ITexture> rhiTexture,
        const Container::String &debugName)
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        if (!impl || !impl->bInitialized || !impl->GpuResources || !rhiTexture)
        {
            return TextureHandle::Invalid();
        }

        return impl->GpuResources->RegisterExternalTexture(std::move(rhiTexture), debugName);
    }

    void TextureResources::ReleaseTexture(TextureHandle handle)
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        if (impl && impl->GpuResources)
        {
            impl->GpuResources->ReleaseTexture(handle);
        }
    }

    RHI::ITexture *TextureResources::GetRHITexture(TextureHandle handle) const
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->GpuResources
                   ? impl->GpuResources->GetRHITexture(handle)
                   : nullptr;
    }

    Container::TSharedPtr<RHI::ITexture> TextureResources::GetRHITexturePtr(TextureHandle handle) const
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->GpuResources
                   ? impl->GpuResources->GetRHITexturePtr(handle)
                   : nullptr;
    }

    MaterialResources::MaterialResources(RenderResources *pOwner)
        : m_pOwner(pOwner)
    {
    }

    MaterialHandle MaterialResources::Create(const MaterialCreateData &createInfo)
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->MaterialStore
                   ? impl->MaterialStore->CreateMaterial(createInfo)
                   : MaterialHandle::Invalid();
    }

    const MaterialResourceData *MaterialResources::GetData(MaterialHandle handle) const
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->MaterialStore
                   ? impl->MaterialStore->GetMaterialData(handle)
                   : nullptr;
    }

    bool MaterialResources::Update(MaterialHandle handle, const MaterialCreateData &createInfo)
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->MaterialStore
                   ? impl->MaterialStore->UpdateMaterial(handle, createInfo)
                   : false;
    }

    void MaterialResources::Release(MaterialHandle handle)
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        if (impl && impl->MaterialStore && m_pOwner)
        {
            impl->MaterialStore->ReleaseMaterial(handle, m_pOwner->Textures());
        }
    }

    MaterialHandle MaterialResources::CreateNeural(const NeuralMaterialDesc &desc)
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        if (!impl || !impl->bInitialized || !impl->Device || !impl->MaterialStore || !m_pOwner)
        {
            return MaterialHandle::Invalid();
        }

        return impl->MaterialStore->CreateNeuralMaterial(impl->Device.get(), m_pOwner->Textures(), desc);
    }

    Container::VariableArray<NeuralMaterialResource *> MaterialResources::GetNeuralResources() const
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->MaterialStore
                   ? impl->MaterialStore->GetNeuralMaterialResources()
                   : Container::VariableArray<NeuralMaterialResource *>();
    }

    MeshResources::MeshResources(RenderResources *pOwner)
        : m_pOwner(pOwner)
    {
    }

    bool MeshResources::Register(MeshDataHandle handle,
                                 const void *vertices,
                                 size_t vertexSize,
                                 const uint32_t *indices,
                                 uint32_t indexCount)
    {
        return Register(handle, vertices, vertexSize, indices, indexCount, nullptr, 0);
    }

    bool MeshResources::Register(MeshDataHandle handle,
                                 const void *vertices,
                                 size_t vertexSize,
                                 const uint32_t *indices,
                                 uint32_t indexCount,
                                 const SubMesh* subMeshes,
                                 uint32_t subMeshCount)
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        if (!impl ||
            !impl->bInitialized ||
            !impl->Device ||
            !impl->ProceduralMeshes ||
            !handle.IsValid() ||
            !vertices ||
            !indices ||
            indexCount == 0 ||
            (subMeshCount > 0 && subMeshes == nullptr))
        {
            return false;
        }

        return impl->ProceduralMeshes->RegisterMesh(handle,
                                                   vertices,
                                                   vertexSize,
                                                   indices,
                                                   indexCount,
                                                   subMeshes,
                                                   subMeshCount);
    }

    const MeshResources::MeshGPUData *MeshResources::GetGPUData(MeshDataHandle handle) const
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->ProceduralMeshes
                   ? impl->ProceduralMeshes->GetMeshGPUData(handle)
                   : nullptr;
    }

    bool MeshResources::TryGetSubMeshRanges(
        MeshDataHandle handle,
        Container::FixedArray<SubMeshRange, MAX_MATERIAL_SLOTS>& out,
        uint32_t& outCount) const
    {
        outCount = 0;
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->ProceduralMeshes
                   ? impl->ProceduralMeshes->TryGetSubMeshRanges(handle, out, outCount)
                   : false;
    }

    bool MeshResources::TryGetLocalBounds(MeshDataHandle handle, BoundingBox &outBounds) const
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->ProceduralMeshes
                   ? impl->ProceduralMeshes->TryGetLocalBounds(handle, outBounds)
                   : false;
    }

    void MeshResources::Unregister(MeshDataHandle handle)
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        if (impl && impl->ProceduralMeshes)
        {
            impl->ProceduralMeshes->UnregisterMesh(handle);
        }
    }

    SkinnedMeshResources::SkinnedMeshResources(RenderResources* pOwner)
        : m_pOwner(pOwner)
    {
    }

    void SkinnedMeshResources::BeginFrame(uint64_t completedSubmissionSerial)
    {
        auto* impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        if (impl && impl->SkinnedMeshes)
        {
            impl->SkinnedMeshes->BeginFrame(completedSubmissionSerial);
        }
    }

    bool SkinnedMeshResources::PrepareDraw(
        const Container::TSharedPtr<const SkinnedMeshFrameLease>& frameLease,
        const Container::VariableArray<Math::Matrix4x4>& bonePalette,
        const Math::Matrix4x4& worldTransform,
        SkinnedMeshPreparedDraw& outPrepared,
        const Container::VariableArray<Math::Matrix4x4>* previousBonePalette,
        const Math::Matrix4x4* previousWorldTransform)
    {
        auto* impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->SkinnedMeshes
                   ? impl->SkinnedMeshes->PrepareDraw(frameLease, bonePalette, worldTransform, outPrepared,
                                                      previousBonePalette, previousWorldTransform)
                   : false;
    }

    bool SkinnedMeshResources::MarkLastUse(
        const SkinnedMeshPreparedDraw& prepared,
        const Container::TSharedPtr<const SkinnedMeshFrameLease>& frameLease)
    {
        auto* impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->SkinnedMeshes
                   ? impl->SkinnedMeshes->MarkLastUse(prepared, frameLease)
                   : false;
    }

    bool SkinnedMeshResources::CommitSubmittedFrame(uint64_t submissionSerial)
    {
        auto* impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->SkinnedMeshes
                   ? impl->SkinnedMeshes->CommitSubmittedFrame(submissionSerial)
                   : false;
    }

    void SkinnedMeshResources::AbortFrame()
    {
        auto* impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        if (impl && impl->SkinnedMeshes)
        {
            impl->SkinnedMeshes->AbortFrame();
        }
    }

    bool SkinnedMeshResources::GetLifetimeSnapshot(
        SkinnedMeshHandle handle,
        SkinnedMeshGpuLifetimeSnapshot& outSnapshot) const
    {
        auto* impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->SkinnedMeshes
                   ? impl->SkinnedMeshes->GetLifetimeSnapshot(handle, outSnapshot)
                   : false;
    }

    bool SkinnedMeshResources::IsResident(SkinnedMeshHandle handle) const
    {
        auto* impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->SkinnedMeshes && impl->SkinnedMeshes->IsResident(handle);
    }

    MegaGeometryResources::MegaGeometryResources(RenderResources *pOwner)
        : m_pOwner(pOwner)
    {
    }

    MegaGeometry::MegaMeshHandle MegaGeometryResources::CreateMegaMesh(
        const MegaGeometry::MegaMeshCreateInfo &createInfo)
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        if (!impl || !impl->bInitialized || !impl->MegaGeometryResources)
        {
            return MegaGeometry::MegaMeshHandle::Invalid();
        }

        return impl->MegaGeometryResources->CreateMegaMesh(createInfo);
    }

    const MegaGeometry::MegaMeshGPUData *MegaGeometryResources::GetMegaMeshGPUData(
        MegaGeometry::MegaMeshHandle handle) const
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->MegaGeometryResources
                   ? impl->MegaGeometryResources->GetMegaMeshGPUData(handle)
                   : nullptr;
    }

    void MegaGeometryResources::ReleaseMegaMesh(MegaGeometry::MegaMeshHandle handle)
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        if (impl && impl->MegaGeometryResources)
        {
            impl->MegaGeometryResources->ReleaseMegaMesh(handle);
        }
    }

    ModelHandle MegaGeometryResources::RegisterModel(MegaGeometry::MegaMeshHandle megaMeshHandle,
                                                     const Container::String &debugName,
                                                     const Container::String &sourcePath)
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->MegaGeometryResources
                   ? impl->MegaGeometryResources->RegisterModel(megaMeshHandle, debugName, sourcePath)
                   : ModelHandle::Invalid();
    }

    ModelHandle MegaGeometryResources::LoadModel(
        const Asset::AssetSystem& assetSystem,
        const Container::String& logicalPath)
    {
        auto* impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        if (!impl || !impl->bInitialized || !impl->MegaGeometryResources || !m_pOwner)
        {
            return ModelHandle::Invalid();
        }

        return Resource::LoadCookedModel(
            assetSystem,
            logicalPath,
            ModelLoadResourceContext{m_pOwner->Textures(), *this});
    }

    bool MegaGeometryResources::SetModelAssetSystem(
        Container::TSharedPtr<const Asset::AssetSystem> assetSystem)
    {
        auto* impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->ModelAssets
                   ? impl->ModelAssets->SetAssetSystem(std::move(assetSystem))
                   : false;
    }

    uint32_t MegaGeometryResources::LoadModelAsync(
        const Container::String& logicalPath,
        Delegate<void, ModelHandle> callback)
    {
        auto* impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->ModelAssets
                   ? impl->ModelAssets->LoadModelAsync(logicalPath, std::move(callback))
                   : 0;
    }

    uint32_t MegaGeometryResources::FlushCompletedModelLoads(uint32_t maxLoadsPerFrame)
    {
        auto* impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->ModelAssets
                   ? impl->ModelAssets->FlushCompletedModelLoads(maxLoadsPerFrame)
                   : 0;
    }

    void MegaGeometryResources::CancelModelLoad(uint32_t requestId)
    {
        auto* impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        if (impl && impl->ModelAssets)
        {
            impl->ModelAssets->CancelModelLoad(requestId);
        }
    }

    bool MegaGeometryResources::CancelPendingModelLoadsAndWait()
    {
        auto* impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->ModelAssets
                   ? impl->ModelAssets->CancelPendingModelLoadsAndWait()
                   : false;
    }

    uint32_t MegaGeometryResources::GetPendingAsyncModelLoadCount() const
    {
        auto* impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->ModelAssets
                   ? impl->ModelAssets->GetPendingAsyncModelLoadCount()
                   : 0;
    }

    MegaGeometry::MegaMeshHandle MegaGeometryResources::GetModelMegaMeshHandle(ModelHandle handle) const
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->MegaGeometryResources
                   ? impl->MegaGeometryResources->GetModelMegaMeshHandle(handle)
                   : MegaGeometry::MegaMeshHandle::Invalid();
    }

    void MegaGeometryResources::ReleaseModel(ModelHandle handle)
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        if (!impl || !impl->MegaGeometryResources)
        {
            return;
        }

        if (!impl->ModelAssets || !impl->ModelAssets->IsBound())
        {
            ReleaseModelUnmanaged(handle);
            return;
        }

        Resource::ModelCacheReleaseResult released = impl->ModelAssets->ReleaseManagedModel(handle);
        if (!released.bManaged)
        {
            ReleaseModelUnmanaged(handle);
        }
        else if (released.HandleToRelease.IsValid())
        {
            ReleaseModelUnmanaged(released.HandleToRelease);
        }
    }

    void MegaGeometryResources::ReleaseModelUnmanaged(ModelHandle handle)
    {
        auto* impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        if (impl && impl->MegaGeometryResources)
        {
            impl->MegaGeometryResources->ReleaseModel(handle);
        }
    }

    RenderResources::RenderResources()
        : m_Impl(Container::MakeUnique<Impl>()),
          m_Gpu(this),
          m_Textures(this),
          m_SkinnedMeshes(this),
          m_Materials(this),
          m_Meshes(this),
          m_MegaGeometry(this)
    {
    }

    RenderResources::~RenderResources()
    {
        Shutdown();
    }

    bool RenderResources::Initialize(Container::TSharedPtr<RHI::IDevice> device)
    {
        if (m_Impl->bInitialized)
        {
            return true;
        }

        m_Impl->bShuttingDown = false;
        m_Impl->Device = std::move(device);
        if (!m_Impl->Device)
        {
            NORVES_LOG_ERROR("RenderResources", "Device is null");
            return false;
        }

        m_Impl->GpuResources = Container::MakeUnique<GpuResourceStore>(m_Impl->Device, m_Impl->NextHandleId);
        m_Impl->GpuResources->SetRetireQueue(&m_Impl->RetireQueue);
        {
            const RHI::SparseCapabilities &sparse = m_Impl->Device->GetCapabilities().Sparse;
            if (sparse.bSparseBinding && sparse.bResidencyImage2D)
            {
                m_Impl->SparsePool = Container::MakeUnique<SparsePagePool>(m_Impl->Device);
            }
        }
        m_Impl->SkinnedMeshes = Container::MakeUnique<SkinnedMeshGpuStore>(m_Impl->Device);
        m_Impl->MegaGeometryResources =
            Container::MakeUnique<MegaGeometryResourceStore>(m_Impl->Device, m_Impl->NextHandleId);
        m_Impl->ProceduralMeshes = Container::MakeUnique<ProceduralMeshGpuStore>(m_Impl->Device);
        if (m_Impl->TextureAssets)
        {
            m_Impl->TextureAssets->Bind(m_Impl->Device.get(), m_Impl->GpuResources.get());
        }

        m_Impl->bInitialized = true;
        if (m_Impl->ModelAssets)
        {
            m_Impl->ModelAssets->Bind(&m_Textures, &m_MegaGeometry);
        }
        LOG_INFO("RenderResources initialized");
        return true;
    }

    void RenderResources::Shutdown()
    {
        if (!m_Impl->bInitialized)
        {
            return;
        }

        m_Impl->bShuttingDown = true;
        CloseAsyncAssetLoadAdmissionAndWait();
        ClearAllResources();
        if (m_Impl->Device)
        {
            m_Impl->Device->WaitIdle();
        }
        // GPU が止まったので、待っていた RHI 資源を期限を問わず全部破棄する。
        m_Impl->RetireQueue.Clear();
        m_Impl->SparsePool.reset();
        if (m_Impl->SkinnedMeshes)
        {
            m_Impl->SkinnedMeshes->ForceClearAfterWaitIdle();
        }
        if (m_Impl->ModelAssets)
        {
            m_Impl->ModelAssets->Unbind();
        }
        if (m_Impl->TextureAssets)
        {
            m_Impl->TextureAssets->Unbind();
        }
        m_Impl->SkinnedMeshes.reset();

        m_Impl->ProceduralMeshes.reset();
        m_Impl->MegaGeometryResources.reset();
        m_Impl->GpuResources.reset();
        m_Impl->Device.reset();
        m_Impl->bInitialized = false;
        m_Impl->bShuttingDown = false;
        LOG_INFO("RenderResources shutdown");
    }

    bool RenderResources::IsInitialized() const
    {
        return m_Impl->bInitialized;
    }

    void RenderResources::BeginRetireFrame(uint64_t completedSubmissionSerial)
    {
        m_Impl->RetireQueue.BeginFrame(completedSubmissionSerial);
    }

    void RenderResources::CommitRetireFrame(uint64_t submissionSerial)
    {
        m_Impl->RetireQueue.CommitFrame(submissionSerial);
    }

    void RenderResources::AbortRetireFrame()
    {
        m_Impl->RetireQueue.AbortFrame();
    }

    SparsePagePool *RenderResources::GetSparsePagePool() const
    {
        return m_Impl->SparsePool.get();
    }

    size_t RenderResources::GetPendingRetireCount() const
    {
        return m_Impl->RetireQueue.GetPendingCount();
    }

    void RenderResources::SetVideoMemoryCapMb(uint64_t capMb)
    {
        m_Impl->VideoMemoryCapMb = capMb;
    }

    uint64_t RenderResources::GetVideoMemoryCapMb() const
    {
        return m_Impl->VideoMemoryCapMb;
    }

    void RenderResources::PollVideoMemoryBudget()
    {
        Impl* impl = m_Impl.get();
        if (!impl || !impl->bInitialized || !impl->Device)
        {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        if (!impl->VideoMemoryLogGate.IsPollDue(now))
        {
            return;
        }
        impl->VideoMemoryLogGate.MarkPolled(now);

        const RHI::VideoMemoryBudget budget = impl->Device->GetVideoMemoryBudget();
        const uint64_t budgetBytes = budget.bValid ? budget.BudgetBytes : 0;
        const uint64_t usageBytes = budget.bValid ? budget.UsageBytes : 0;

        // 前回ログした値からの変化が 1% 未満なら出さない（初回は必ず出す）。
        if (!impl->VideoMemoryLogGate.CommitIfChanged(budgetBytes, usageBytes))
        {
            return;
        }

        constexpr uint64_t kBytesPerMb = 1024ull * 1024ull;
        if (impl->VideoMemoryCapMb > 0)
        {
            NORVES_LOG_INFO(
                "RenderResources",
                "VRAM_BUDGET heap_budget_mb=%llu heap_usage_mb=%llu cap_mb=%llu source=%s",
                static_cast<unsigned long long>(budgetBytes / kBytesPerMb),
                static_cast<unsigned long long>(usageBytes / kBytesPerMb),
                static_cast<unsigned long long>(impl->VideoMemoryCapMb),
                budget.bValid ? "ext" : "none");
        }
        else
        {
            NORVES_LOG_INFO(
                "RenderResources",
                "VRAM_BUDGET heap_budget_mb=%llu heap_usage_mb=%llu cap_mb=none source=%s",
                static_cast<unsigned long long>(budgetBytes / kBytesPerMb),
                static_cast<unsigned long long>(usageBytes / kBytesPerMb),
                budget.bValid ? "ext" : "none");
        }
    }

    bool RenderResources::ReloadAssetRuntimeSnapshot(
        const Container::String& assetRoot,
        Container::TSharedPtr<const Asset::AssetSystem> candidate)
    {
        if (!m_Impl || !m_Impl->bInitialized || m_Impl->bShuttingDown)
        {
            NORVES_LOG_WARNING(
                "RenderResources",
                "Asset runtime snapshot reload rejected: reason=runtime_not_ready");
            return false;
        }
        if (assetRoot.empty())
        {
            NORVES_LOG_WARNING(
                "RenderResources",
                "Asset runtime snapshot reload rejected: reason=invalid_asset_root");
            return false;
        }
        if (!candidate)
        {
            NORVES_LOG_WARNING(
                "RenderResources",
                "Asset runtime snapshot reload rejected: reason=null_candidate");
            return false;
        }
        if (!m_Impl->TextureAssets || !m_Impl->ModelAssets)
        {
            NORVES_LOG_WARNING(
                "RenderResources",
                "Asset runtime snapshot reload rejected: reason=runtime_missing");
            return false;
        }

        TextureAssetRuntime& textureRuntime = *m_Impl->TextureAssets;
        ModelAssetRuntime& modelRuntime = *m_Impl->ModelAssets;
        Resource::ModelCacheHandleBatch retired;
        const char* pRejectedReason = nullptr;
        uint64_t textureGeneration = 0;
        uint64_t modelGeneration = 0;
        {
            Thread::ScopedLock textureLock(textureRuntime.m_TextureAssetMutex);
            Thread::ScopedLock modelLock(modelRuntime.m_AssetMutex);
            const bool bTextureReady = textureRuntime.CanReloadSnapshotLocked();
            const bool bModelReady = modelRuntime.CanReloadSnapshotLocked();
            if (!bTextureReady)
            {
                pRejectedReason = "texture_busy_or_unbound";
            }
            else if (!bModelReady)
            {
                pRejectedReason = "model_busy_or_unbound";
            }
            else
            {
                textureRuntime.ApplyReloadSnapshotLocked(assetRoot, candidate);
                retired = modelRuntime.ApplyReloadSnapshotLocked(assetRoot, candidate);
                textureGeneration = textureRuntime.GetTextureAssetResolverLocked().GetGeneration();
                modelGeneration = modelRuntime.m_Generation;
            }
        }

        if (pRejectedReason != nullptr)
        {
            NORVES_LOG_WARNING(
                "RenderResources",
                "Asset runtime snapshot reload rejected: reason=%s",
                pRejectedReason);
            return false;
        }

        modelRuntime.ReleaseRetiredAfterReload(std::move(retired));
        NORVES_LOG_INFO(
            "RenderResources",
            "Asset runtime snapshot reload accepted: texture_generation=%llu model_generation=%llu",
            static_cast<unsigned long long>(textureGeneration),
            static_cast<unsigned long long>(modelGeneration));
        return true;
    }

    TextureAssetRuntime* RenderResources::GetTextureAssetRuntimeForTesting()
    {
        return m_Impl ? m_Impl->TextureAssets.get() : nullptr;
    }

    ModelAssetRuntime* RenderResources::GetModelAssetRuntimeForTesting()
    {
        return m_Impl ? m_Impl->ModelAssets.get() : nullptr;
    }

    void RenderResources::CloseAsyncAssetLoadAdmissionAndWait()
    {
        if (!m_Impl)
        {
            return;
        }

        if (m_Impl->ModelAssets)
        {
            m_Impl->ModelAssets->CloseAndDrain();
        }
        if (m_Impl->TextureAssets)
        {
            m_Impl->TextureAssets->CloseAndWait();
        }
    }

    void RenderResources::ClearAllResources()
    {
        if (m_Impl->ModelAssets)
        {
            if (m_Impl->bShuttingDown)
            {
                m_Impl->ModelAssets->CloseAndDrain();
            }
            m_Impl->ModelAssets->CloseForResourceClear();
        }

        if (m_Impl->MaterialStore)
        {
            m_Impl->MaterialStore->Clear(m_Textures);
        }

        if (m_Impl->TextureAssets)
        {
            m_Impl->TextureAssets->ClearRuntimeResources();
        }

        if (m_Impl->ProceduralMeshes)
        {
            m_Impl->ProceduralMeshes->Clear();
        }

        if (m_Impl->SkinnedMeshes)
        {
            m_Impl->SkinnedMeshes->CollectReleasedResources();
        }

        if (m_Impl->MegaGeometryResources)
        {
            m_Impl->MegaGeometryResources->Clear();
        }

        if (m_Impl->GpuResources)
        {
            m_Impl->GpuResources->Clear();
        }

        if (!m_Impl->bShuttingDown && m_Impl->bInitialized &&
            m_Impl->ModelAssets && m_Impl->ModelAssets->IsBound())
        {
            m_Impl->ModelAssets->ReopenAfterClear();
        }
        if (!m_Impl->bShuttingDown && m_Impl->bInitialized && m_Impl->TextureAssets)
        {
            m_Impl->TextureAssets->ReopenAfterClear();
        }
    }

    void RenderResources::CleanupUnusedResources()
    {
        // TODO: reference-count based cleanup.
    }

    ResourceStats RenderResources::GetResourceStats() const
    {
        return m_Gpu.GetResourceStats();
    }

} // namespace NorvesLib::Core::Rendering
