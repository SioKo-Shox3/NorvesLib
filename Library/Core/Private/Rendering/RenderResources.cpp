#include "Rendering/RenderResources.h"

#include "Rendering/GpuResourceStore.h"
#include "Rendering/MegaGeometryResourceStore.h"
#include "Rendering/ProceduralMeshGpuStore.h"
#include "Rendering/RenderMaterialStore.h"
#include "Rendering/TextureAssetRuntime.h"
#include "Logging/LogMacros.h"
#include "RHI/IBuffer.h"
#include "RHI/IDevice.h"
#include "RHI/IShader.h"
#include "RHI/ITexture.h"
#include "Thread/Atomic.h"

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
              TextureAssets(Container::MakeUnique<TextureAssetRuntime>())
        {
        }

        Thread::Atomic<uint64_t> NextHandleId{1};
        Container::TSharedPtr<RHI::IDevice> Device;
        Container::TUniquePtr<GpuResourceStore> GpuResources;
        Container::TUniquePtr<ProceduralMeshGpuStore> ProceduralMeshes;
        Container::TUniquePtr<RenderMaterialStore> MaterialStore;
        Container::TUniquePtr<MegaGeometryResourceStore> MegaGeometryResources;
        Container::TUniquePtr<TextureAssetRuntime> TextureAssets;
        bool bInitialized = false;
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
        return impl && impl->GpuResources
                   ? impl->GpuResources->GetResourceStats()
                   : ResourceStats();
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

    void MeshResources::Unregister(MeshDataHandle handle)
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        if (impl && impl->ProceduralMeshes)
        {
            impl->ProceduralMeshes->UnregisterMesh(handle);
        }
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
        if (impl && impl->MegaGeometryResources)
        {
            impl->MegaGeometryResources->ReleaseModel(handle);
        }
    }

    RenderResources::RenderResources()
        : m_Impl(Container::MakeUnique<Impl>()),
          m_Gpu(this),
          m_Textures(this),
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

        m_Impl->Device = std::move(device);
        if (!m_Impl->Device)
        {
            NORVES_LOG_ERROR("RenderResources", "Device is null");
            return false;
        }

        m_Impl->GpuResources = Container::MakeUnique<GpuResourceStore>(m_Impl->Device, m_Impl->NextHandleId);
        m_Impl->MegaGeometryResources =
            Container::MakeUnique<MegaGeometryResourceStore>(m_Impl->Device, m_Impl->NextHandleId);
        m_Impl->ProceduralMeshes = Container::MakeUnique<ProceduralMeshGpuStore>(m_Impl->Device);
        if (m_Impl->TextureAssets)
        {
            m_Impl->TextureAssets->Bind(m_Impl->Device.get(), m_Impl->GpuResources.get());
        }

        m_Impl->bInitialized = true;
        LOG_INFO("RenderResources initialized");
        return true;
    }

    void RenderResources::Shutdown()
    {
        if (!m_Impl->bInitialized)
        {
            return;
        }

        ClearAllResources();
        if (m_Impl->TextureAssets)
        {
            m_Impl->TextureAssets->Unbind();
        }

        m_Impl->ProceduralMeshes.reset();
        m_Impl->MegaGeometryResources.reset();
        m_Impl->GpuResources.reset();
        m_Impl->Device.reset();
        m_Impl->bInitialized = false;
        LOG_INFO("RenderResources shutdown");
    }

    bool RenderResources::IsInitialized() const
    {
        return m_Impl->bInitialized;
    }

    void RenderResources::ClearAllResources()
    {
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

        if (m_Impl->MegaGeometryResources)
        {
            m_Impl->MegaGeometryResources->Clear();
        }

        if (m_Impl->GpuResources)
        {
            m_Impl->GpuResources->Clear();
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
