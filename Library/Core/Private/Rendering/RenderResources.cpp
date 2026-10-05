#include "Rendering/RenderResources.h"

#include "Asset/AssetSystem.h"
#include "Rendering/CookedVirtualTexture.h"
#include "Rendering/GeometryPageRequestRing.h"
#include "Rendering/GeometryPool.h"
#include "Rendering/GpuResourceStore.h"
#include "Rendering/GpuRetireQueue.h"
#include "Rendering/SparsePagePool.h"
#include "Rendering/TileUploader.h"
#include "Rendering/VirtualTextureFeedbackRing.h"
#include "Rendering/VirtualTextureRequestSet.h"
#include "Rendering/VirtualTextureStreamer.h"
#include "Rendering/SkinnedMeshGpuStore.h"
#include "Rendering/VideoMemoryBudgetLogGate.h"
#include "Rendering/VideoMemoryBudgetManager.h"
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
        // 1フレームにジオメトリの区画の中身をリングへ積む量の上限（バイト）。リング（32 MiB）に収まる提出中の
        // 数フレーム分とテクスチャのタイルの分を残すため、フレームのコピー量の上限（24 MiB）より小さくする。
        constexpr uint64_t MegaGeometryUploadBytesPerFrame = 8ull * 1024ull * 1024ull;

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
        // ジオメトリが共有する DeviceLocal のバッファのプール。区画の期限の来た返却は RetireQueue を通ってここへ戻るので、
        // SparsePool と同じく Shutdown では RetireQueue を片付けてから手放す。
        Container::TUniquePtr<GeometryPool> GeometryBuffers;
        // プールの1つの塊の大きさ（Initialize の前に SetGeometryPoolBlockBytes で替えられる）
        uint64_t GeometryPoolBlockBytes = GeometryPool::DefaultBlockBytes;
        // タイル・ミップテイルのデータと、ジオメトリの区画の中身を、ステージングのリング経由でテクスチャの領域・バッファへ書く経路。
        // リングのバッファは最初の書き込みまで作らない。GPU が止まってから手放す（Shutdown の WaitIdle の後）。
        Container::TUniquePtr<TileUploader> TileUpload;
        // VT の要求のバッファ（3つ）の読み戻しと集計。sparse に対応しないデバイスでは作らない。GPU が止まってから手放す。
        Container::TUniquePtr<VirtualTextureFeedbackRing> VtFeedback;
        // ジオメトリのページの要求のバッファ（3つ）の読み戻しと集計。GPU が止まってから手放す。
        Container::TUniquePtr<GeometryPageRequestRing> GeometryPageFeedback;
        // VT のストリーマと、その結び付け・コピーの窓口。ページのプール・リング・RetireQueue より先に手放す。
        Container::TUniquePtr<DeviceVirtualTextureGpu> VtGpu;
        Container::TUniquePtr<VirtualTextureStreamer> VtStreamer;
        // テクスチャのハンドルの番号から VT の表の添字へ
        mutable Thread::Mutex VtMutex;
        Container::UnorderedMap<uint64_t, uint32_t> VtIndexByTexture;
        // CreateVirtualTextureAsync で作り、ミップテイルの常駐を待っている VT（VtMutex で守る）
        struct PendingVirtualTexture
        {
            TextureHandle Handle;
            NorvesLib::Core::Delegate<void, TextureHandle> Callback;
            // 常駐を待った FlushCompletedTextureLoads の回数（打ち切りの判定）
            uint32_t WaitedPolls = 0;
        };
        Container::VariableArray<PendingVirtualTexture> VtPendingReady;
        // ストリーマに渡すフレームの番号（RenderThread だけが進める）
        uint64_t VtFrame = 0;
        // VT_STREAMER ログの間引き（RenderThread だけが触る）
        uint64_t VtLoggedFrame = 0;
        uint64_t VtLoggedResident = 0;
        // VRAM_POOLS に最後に出した VT の追い出し数（変わったときにも出し直す）
        uint64_t VtLoggedEvictedTiles = 0;
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
        // 予算をプールへ割り振る計算と、その直近の結果（GameThread だけが触る）
        VideoMemoryBudgetManager VideoMemoryBudget;
        VideoMemoryBudgetResult VideoMemoryBudgetLast;
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
        uint32_t processed = impl && impl->TextureAssets
                                 ? impl->TextureAssets->FlushCompletedTextureLoads()
                                 : 0;
        if (!impl || !impl->VtStreamer)
        {
            return processed;
        }

        // ミップテイルが常駐した VT（または待ち切れなかった VT）を、ロックの外で通知する
        // （callback が新しい読み込みを始めても VtMutex を持たないようにする）。
        // 常駐の判定は RenderThread が進めるストリーマの状態を見るだけなので、描画の同期を要さない。
        constexpr uint32_t kMaxWaitedPolls = 1800;
        struct Completion
        {
            TextureHandle Handle;
            NorvesLib::Core::Delegate<void, TextureHandle> Callback;
            bool bTimedOut = false;
        };
        Container::VariableArray<Completion> completions;
        {
            Thread::ScopedLock lock(impl->VtMutex);
            for (size_t i = 0; i < impl->VtPendingReady.size();)
            {
                RenderResources::Impl::PendingVirtualTexture &pending = impl->VtPendingReady[i];
                const auto indexIt = impl->VtIndexByTexture.find(pending.Handle.Id);
                const bool bReady = indexIt != impl->VtIndexByTexture.end() &&
                                    impl->VtStreamer->IsMipTailResident(indexIt->second);
                const bool bTimedOut = !bReady && ++pending.WaitedPolls >= kMaxWaitedPolls;
                if (bReady || bTimedOut)
                {
                    Completion completion;
                    completion.Handle = pending.Handle;
                    completion.Callback = std::move(pending.Callback);
                    completion.bTimedOut = bTimedOut;
                    completions.push_back(std::move(completion));
                    impl->VtPendingReady.erase(impl->VtPendingReady.begin() + static_cast<std::ptrdiff_t>(i));
                    continue;
                }
                ++i;
            }
        }
        for (Completion &completion : completions)
        {
            TextureHandle result = completion.Handle;
            if (completion.bTimedOut)
            {
                NORVES_LOG_ERROR("RenderResources",
                                 "VTのミップテイルが常駐しないため、VTを解放して全常駐へ戻します handle=%llu",
                                 static_cast<unsigned long long>(completion.Handle.Id));
                ReleaseTexture(completion.Handle);
                result = TextureHandle::Invalid();
            }
            if (completion.Callback.IsBound())
            {
                completion.Callback.Invoke(result);
            }
            ++processed;
        }
        return processed;
    }

    uint32_t TextureResources::GetPendingAsyncLoadCount() const
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        uint32_t count = impl && impl->TextureAssets
                             ? impl->TextureAssets->GetPendingAsyncLoadCount()
                             : 0;
        if (impl)
        {
            Thread::ScopedLock lock(impl->VtMutex);
            count += static_cast<uint32_t>(impl->VtPendingReady.size());
        }
        return count;
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
        if (impl && impl->VtStreamer)
        {
            // VT なら先にストリーマの登録を解く（結んだページは最後に使った提出の完了までプールへ戻らない）
            uint32_t virtualIndex = 0;
            bool bVirtual = false;
            {
                Thread::ScopedLock lock(impl->VtMutex);
                const auto it = impl->VtIndexByTexture.find(handle.Id);
                if (it != impl->VtIndexByTexture.end())
                {
                    virtualIndex = it->second;
                    bVirtual = true;
                    impl->VtIndexByTexture.erase(it);
                }
            }
            if (bVirtual)
            {
                impl->VtStreamer->UnregisterTexture(virtualIndex);
            }
        }
        if (impl && impl->GpuResources)
        {
            impl->GpuResources->ReleaseTexture(handle);
        }
    }

    TextureHandle TextureResources::CreateVirtualTexture(const Container::String &path, uint32_t *pOutVirtualTextureIndex)
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        if (!impl || !impl->bInitialized || !impl->GpuResources || !impl->TextureAssets)
        {
            return TextureHandle::Invalid();
        }
        if (!impl->VtStreamer)
        {
            NORVES_LOG_ERROR("RenderResources",
                             "VTを作れません: デバイスが sparse に対応していません texture=%s",
                             path.c_str());
            return TextureHandle::Invalid();
        }

        Asset::AssetCookedRange range;
        Container::TSharedPtr<const Asset::AssetSystem> assetSystem;
        Container::String reason;
        if (!impl->TextureAssets->ResolveCookedTextureRange(path, range, assetSystem, &reason))
        {
            NORVES_LOG_ERROR("RenderResources",
                             "VTを作れません: クック済みのテクスチャの位置を求められません texture=%s reason=%s",
                             path.c_str(), reason.c_str());
            return TextureHandle::Invalid();
        }

        CookedVirtualTexturePlan plan;
        // 全常駐で読むクック済みと同じ標本値にするため、sRGB を UNORM として上げる互換設定も合わせる
        if (!PrepareCookedVirtualTexture(assetSystem->GetCookedFileReader(), range.Request, range.BaseOffset, range.Size,
                                         path, plan, &reason, assetSystem->GetTreatSrgbTexturesAsLinear()))
        {
            NORVES_LOG_ERROR("RenderResources",
                             "VTを作れません: クック済みのテクスチャを開けません texture=%s reason=%s",
                             path.c_str(), reason.c_str());
            return TextureHandle::Invalid();
        }

        const TextureHandle handle = impl->GpuResources->CreateTexture(plan.CreateInfo);
        if (!handle.IsValid())
        {
            return TextureHandle::Invalid();
        }

        VirtualTextureRegistration registration;
        registration.Texture = impl->GpuResources->GetRHITexturePtr(handle);
        registration.Format = plan.Format;
        registration.Width = plan.Width;
        registration.Height = plan.Height;
        registration.Source = std::move(plan.Source);
        registration.TailData = std::move(plan.TailData);
        const uint32_t index = impl->VtStreamer->RegisterTexture(std::move(registration));
        if (index == VirtualTextureStreamer::InvalidIndex)
        {
            impl->GpuResources->ReleaseTexture(handle);
            return TextureHandle::Invalid();
        }

        {
            Thread::ScopedLock lock(impl->VtMutex);
            impl->VtIndexByTexture[handle.Id] = index;
        }
        {
            const RHI::ITexture *created = impl->GpuResources->GetRHITexture(handle);
            LOG_INFO("VT_CREATE path=%s size=%ux%u mips=%u rhi_format=%u create_format=%u index=%u", path.c_str(),
                     static_cast<unsigned>(created ? created->GetWidth() : 0),
                     static_cast<unsigned>(created ? created->GetHeight() : 0),
                     static_cast<unsigned>(created ? created->GetMipLevels() : 0),
                     static_cast<unsigned>(created ? created->GetFormat() : RHI::Format::UNKNOWN),
                     static_cast<unsigned>(registration.Format), static_cast<unsigned>(index));
        }
        // 要求のバッファを確保して有効にする。有効にできないと材質が要求を書けず、ミップテイルより細かいタイルが
        // 結ばれないままぼけるので、VT を解放して失敗として返す（呼び出し側が全常駐へ戻す）。
        if (!EnableVirtualTextureFeedback())
        {
            NORVES_LOG_ERROR("RenderResources",
                             "VTを作れません: フィードバックを有効にできないため、VTを解放します texture=%s", path.c_str());
            ReleaseTexture(handle);
            return TextureHandle::Invalid();
        }
        if (pOutVirtualTextureIndex != nullptr)
        {
            *pOutVirtualTextureIndex = index;
        }
        return handle;
    }

    bool TextureResources::TryGetVirtualTextureIndex(TextureHandle handle, uint32_t &outIndex) const
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        if (!impl)
        {
            return false;
        }
        Thread::ScopedLock lock(impl->VtMutex);
        const auto it = impl->VtIndexByTexture.find(handle.Id);
        if (it == impl->VtIndexByTexture.end())
        {
            return false;
        }
        outIndex = it->second;
        return true;
    }

    bool TextureResources::EnableVirtualTextureFeedback()
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        if (!impl || !impl->VtFeedback || !impl->Device || !impl->Device->GetCapabilities().SupportsVirtualTextureFeedback())
        {
            return false;
        }
        return impl->VtFeedback->SetEnabled(true);
    }

    TextureResources::VirtualTextureFeedbackTarget TextureResources::GetVirtualTextureFeedbackTarget() const
    {
        VirtualTextureFeedbackTarget target;
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        if (!impl || !impl->VtFeedback || !impl->Device || !impl->Device->GetCapabilities().SupportsVirtualTextureFeedback())
        {
            return target;
        }
        target.Buffer = impl->VtFeedback->GetCurrentBuffer();
        if (target.Buffer)
        {
            target.bWriting = true;
            target.Bytes = impl->VtFeedback->GetBufferBytes();
            target.Frame = impl->VtFeedback->GetFrameCounter();
            return target;
        }
        target.Buffer = impl->VtFeedback->GetIdleBuffer();
        target.Bytes = VirtualTextureFeedbackRing::IdleBufferBytes;
        return target;
    }

    bool TextureResources::IsVirtualTextureReady(TextureHandle handle) const
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        uint32_t index = 0;
        return impl && impl->VtStreamer && TryGetVirtualTextureIndex(handle, index) &&
               impl->VtStreamer->IsMipTailResident(index);
    }

    bool TextureResources::SupportsVirtualTexture() const
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        // 材質が要求を書けること（フィードバックの対応）も要る。無いデバイスの VT はミップテイルのまま粗く描かれ続ける。
        return impl && impl->bInitialized && impl->VtStreamer && impl->TextureAssets && impl->VtFeedback && impl->Device &&
               impl->Device->GetCapabilities().SupportsVirtualTextureFeedback();
    }

    bool TextureResources::CreateVirtualTextureAsync(const Container::String &path,
                                                     NorvesLib::Core::Delegate<void, TextureHandle> callback)
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        if (!SupportsVirtualTexture())
        {
            return false;
        }
        const TextureHandle handle = CreateVirtualTexture(path);
        if (!handle.IsValid())
        {
            return false;
        }

        RenderResources::Impl::PendingVirtualTexture pending;
        pending.Handle = handle;
        pending.Callback = std::move(callback);
        Thread::ScopedLock lock(impl->VtMutex);
        impl->VtPendingReady.push_back(std::move(pending));
        return true;
    }

    bool TextureResources::IsVirtualTextureStreamingIdle() const
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        if (!impl || !impl->VtStreamer)
        {
            return true;
        }
        {
            Thread::ScopedLock lock(impl->VtMutex);
            if (impl->VtIndexByTexture.empty())
            {
                return true;
            }
            if (!impl->VtPendingReady.empty())
            {
                return false;
            }
        }
        const VirtualTextureStreamerStats stats = impl->VtStreamer->GetStats();
        return stats.WantedTiles == 0 && stats.ReadingTiles == 0 && stats.ReadyTiles == 0;
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

        const MegaGeometry::MegaMeshHandle handle = impl->MegaGeometryResources->CreateMegaMesh(createInfo);
        // ページを2つ以上持つメッシュだけが、子のページを要求する。要求のバッファは最初のそのメッシュで確保する
        // （作れなくても描画は続く。要求を取らないだけ）
        if (handle.IsValid() && impl->GeometryPageFeedback)
        {
            const MegaGeometry::MegaMeshGPUData *gpuData = impl->MegaGeometryResources->GetMegaMeshGPUData(handle);
            if (gpuData && gpuData->PageCount > 1)
            {
                impl->GeometryPageFeedback->SetEnabled(true);
            }
        }
        return handle;
    }

    const MegaGeometry::MegaMeshGPUData *MegaGeometryResources::GetMegaMeshGPUData(
        MegaGeometry::MegaMeshHandle handle) const
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->MegaGeometryResources
                   ? impl->MegaGeometryResources->GetMegaMeshGPUData(handle)
                   : nullptr;
    }

    const MegaGeometry::MegaMeshGPUData *MegaGeometryResources::GetReadyMegaMeshGPUData(
        MegaGeometry::MegaMeshHandle handle) const
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->MegaGeometryResources
                   ? impl->MegaGeometryResources->GetReadyMegaMeshGPUData(handle)
                   : nullptr;
    }

    bool MegaGeometryResources::HasPendingGpuUploads() const
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->MegaGeometryResources && impl->MegaGeometryResources->HasPendingGpuUploads();
    }

    void MegaGeometryResources::ReleaseMegaMesh(MegaGeometry::MegaMeshHandle handle)
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        if (impl && impl->MegaGeometryResources)
        {
            impl->MegaGeometryResources->ReleaseMegaMesh(handle);
        }
    }

    bool MegaGeometryResources::CopyPageTableIfChanged(
        uint64_t &inOutVersion, Container::VariableArray<MegaGeometry::GeometryPageTable::Entry> &out) const
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->MegaGeometryResources &&
               impl->MegaGeometryResources->CopyPageTableIfChanged(inOutVersion, out);
    }

    bool MegaGeometryResources::ResolvePageTableIndex(uint32_t globalIndex, uint64_t &outMeshId,
                                                      uint32_t &outPageId) const
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->MegaGeometryResources &&
               impl->MegaGeometryResources->ResolvePageTableIndex(globalIndex, outMeshId, outPageId);
    }

    bool MegaGeometryResources::SetMegaMeshPageRegion(MegaGeometry::MegaMeshHandle handle, uint32_t pageId,
                                                      uint32_t region)
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->MegaGeometryResources &&
               impl->MegaGeometryResources->SetMegaMeshPageRegion(handle, pageId, region);
    }

    RHI::BufferPtr MegaGeometryResources::GetCurrentPageRequestBuffer() const
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->GeometryPageFeedback ? impl->GeometryPageFeedback->GetCurrentBuffer() : RHI::BufferPtr{};
    }

    uint32_t MegaGeometryResources::GetCurrentPageRequestCapacity() const
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->GeometryPageFeedback ? impl->GeometryPageFeedback->GetCurrentCapacity() : 0u;
    }

    bool MegaGeometryResources::RecordPageRequestHostBarrier(RHI::ICommandList &commandList)
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->GeometryPageFeedback && impl->GeometryPageFeedback->RecordHostReadBarrier(commandList);
    }

    bool MegaGeometryResources::TakePageRequests(MegaGeometry::GeometryPageRequestSet &out)
    {
        auto *impl = m_pOwner ? m_pOwner->m_Impl.get() : nullptr;
        return impl && impl->GeometryPageFeedback && impl->GeometryPageFeedback->TakeRequests(out);
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
        // ステージングのリング。VT のタイルとジオメトリの区画の両方が使うので、sparse の対応に関わらず作る（リングのバッファは最初の書き込みまで作らない）
        m_Impl->TileUpload = Container::MakeUnique<TileUploader>(m_Impl->Device);
        {
            const RHI::SparseCapabilities &sparse = m_Impl->Device->GetCapabilities().Sparse;
            if (sparse.bSparseBinding && sparse.bResidencyImage2D)
            {
                m_Impl->SparsePool = Container::MakeUnique<SparsePagePool>(m_Impl->Device);
                m_Impl->VtFeedback = Container::MakeUnique<VirtualTextureFeedbackRing>(m_Impl->Device);
                m_Impl->VtGpu = Container::MakeUnique<DeviceVirtualTextureGpu>(m_Impl->Device, *m_Impl->TileUpload);
                m_Impl->VtStreamer = Container::MakeUnique<VirtualTextureStreamer>(
                    *m_Impl->SparsePool, *m_Impl->VtGpu, &m_Impl->RetireQueue);
            }
        }
        m_Impl->GeometryBuffers = Container::MakeUnique<GeometryPool>(m_Impl->Device, m_Impl->GeometryPoolBlockBytes);
        m_Impl->SkinnedMeshes = Container::MakeUnique<SkinnedMeshGpuStore>(m_Impl->Device);
        m_Impl->MegaGeometryResources = Container::MakeUnique<MegaGeometryResourceStore>(
            m_Impl->Device, m_Impl->NextHandleId, m_Impl->GeometryBuffers.get(), m_Impl->TileUpload.get(),
            &m_Impl->RetireQueue);
        // ページの要求のバッファ。バッファは、ページを2つ以上持つメッシュを作るまで確保しない（CreateMegaMesh が有効にする）
        m_Impl->GeometryPageFeedback = Container::MakeUnique<GeometryPageRequestRing>(m_Impl->Device);
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
        // GPU が止まったので、ストリーマが持つページを RetireQueue へ渡して手放し、待っていた RHI 資源を期限を問わず全部破棄する。
        m_Impl->VtStreamer.reset();
        m_Impl->VtGpu.reset();
        m_Impl->VtIndexByTexture.clear();
        m_Impl->VtPendingReady.clear();
        m_Impl->RetireQueue.Clear();
        m_Impl->TileUpload.reset();
        m_Impl->VtFeedback.reset();
        m_Impl->GeometryPageFeedback.reset();
        m_Impl->SparsePool.reset();
        m_Impl->GeometryBuffers.reset();
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
        if (m_Impl->TileUpload)
        {
            m_Impl->TileUpload->BeginFrame(completedSubmissionSerial);
        }
        if (m_Impl->VtFeedback)
        {
            m_Impl->VtFeedback->BeginFrame(completedSubmissionSerial);
        }
        if (m_Impl->GeometryPageFeedback)
        {
            m_Impl->GeometryPageFeedback->BeginFrame(completedSubmissionSerial);
        }
        // 期限の来たページがプールへ戻った後の使用量を、変わっていれば台帳へ出す
        if (m_Impl->SparsePool)
        {
            m_Impl->SparsePool->LogLedgerIfChanged();
        }
        if (m_Impl->GeometryBuffers)
        {
            m_Impl->GeometryBuffers->LogLedgerIfChanged();
        }
    }

    void RenderResources::CommitRetireFrame(uint64_t submissionSerial)
    {
        m_Impl->RetireQueue.CommitFrame(submissionSerial);
        if (m_Impl->TileUpload)
        {
            m_Impl->TileUpload->CommitFrame(submissionSerial);
        }
        if (m_Impl->VtFeedback)
        {
            m_Impl->VtFeedback->CommitFrame(submissionSerial);
        }
        if (m_Impl->GeometryPageFeedback)
        {
            m_Impl->GeometryPageFeedback->CommitFrame(submissionSerial);
        }
    }

    void RenderResources::AbortRetireFrame()
    {
        m_Impl->RetireQueue.AbortFrame();
        if (m_Impl->TileUpload)
        {
            m_Impl->TileUpload->AbortFrame();
        }
        if (m_Impl->VtFeedback)
        {
            m_Impl->VtFeedback->AbortFrame();
        }
        if (m_Impl->GeometryPageFeedback)
        {
            m_Impl->GeometryPageFeedback->AbortFrame();
        }
    }

    uint32_t RenderResources::RecordTileUploads(RHI::ICommandList &commandList)
    {
        // 書き込み待ちのジオメトリを、リングの空きとこのフレームの上限の範囲で積んでから、まとめて記録する
        if (m_Impl->MegaGeometryResources)
        {
            m_Impl->MegaGeometryResources->PumpUploads(MegaGeometryUploadBytesPerFrame);
        }
        return m_Impl->TileUpload ? m_Impl->TileUpload->RecordCopies(commandList) : 0u;
    }

    TileUploader *RenderResources::GetTileUploader() const
    {
        return m_Impl->TileUpload.get();
    }

    VirtualTextureFeedbackRing *RenderResources::GetVirtualTextureFeedback() const
    {
        return m_Impl->VtFeedback.get();
    }

    bool RenderResources::TakeVirtualTextureRequests(VirtualTextureRequestSet &out)
    {
        return m_Impl->VtFeedback ? m_Impl->VtFeedback->TakeRequests(out) : false;
    }

    void RenderResources::RecordVirtualTextureFeedbackBarrier(RHI::ICommandList &commandList)
    {
        if (m_Impl->VtFeedback)
        {
            m_Impl->VtFeedback->RecordHostReadBarrier(commandList);
        }
    }

    VirtualTextureStreamer *RenderResources::GetVirtualTextureStreamer() const
    {
        return m_Impl->VtStreamer.get();
    }

    void RenderResources::UpdateVirtualTextureStreaming()
    {
        Impl &impl = *m_Impl;
        if (!impl.VtStreamer)
        {
            return;
        }
        {
            Thread::ScopedLock lock(impl.VtMutex);
            if (impl.VtIndexByTexture.empty())
            {
                return;
            }
        }

        VirtualTextureRequestSet requests;
        const bool bHasRequests = TakeVirtualTextureRequests(requests);
        ++impl.VtFrame;
        impl.VtStreamer->Update(impl.VtFrame, bHasRequests ? &requests : nullptr);

        // 常駐するタイルの数が変わったときだけ、約1秒（60フレーム）に1回までログへ出す
        constexpr uint64_t LogIntervalFrames = 60;
        if (impl.VtFrame - impl.VtLoggedFrame >= LogIntervalFrames)
        {
            const VirtualTextureStreamerStats stats = impl.VtStreamer->GetStats();
            if (stats.ResidentTiles != impl.VtLoggedResident)
            {
                LOG_INFO("VT_STREAMER textures=%u resident=%u reading=%u ready=%u wanted=%u failed=%u bind_failures=%llu",
                         static_cast<unsigned>(stats.TextureCount), static_cast<unsigned>(stats.ResidentTiles),
                         static_cast<unsigned>(stats.ReadingTiles), static_cast<unsigned>(stats.ReadyTiles),
                         static_cast<unsigned>(stats.WantedTiles), static_cast<unsigned>(stats.FailedTiles),
                         static_cast<unsigned long long>(stats.BindFailures));
                impl.VtLoggedResident = stats.ResidentTiles;
            }
            impl.VtLoggedFrame = impl.VtFrame;
        }
    }

    SparsePagePool *RenderResources::GetSparsePagePool() const
    {
        return m_Impl->SparsePool.get();
    }

    GeometryPool *RenderResources::GetGeometryPool() const
    {
        return m_Impl->GeometryBuffers.get();
    }

    void RenderResources::SetGeometryPoolBlockBytes(uint64_t blockBytes)
    {
        m_Impl->GeometryPoolBlockBytes = blockBytes;
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

        constexpr uint64_t kBytesPerMb = 1024ull * 1024ull;

        // 前回ログした値からの変化が 1% 未満なら出さない（初回は必ず出す）。
        if (impl->VideoMemoryLogGate.CommitIfChanged(budgetBytes, usageBytes))
        {
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

        // 予算をプールへ割り振る。ヒープの使用量が取れないときは、台帳が解放待ちの資源とパスが直接作るテクスチャを
        // 数えないので、上限の一定割合をプール以外へ見込む（VideoMemoryBudgetManager::Compute）。
        VideoMemoryBudgetInput input;
        input.bHeapValid = budget.bValid;
        input.HeapBudgetBytes = budget.BudgetBytes;
        input.HeapUsageBytes = budget.UsageBytes;
        input.CapBytes = impl->VideoMemoryCapMb * kBytesPerMb;
        input.DeviceLocalHeapBytes = budget.DeviceLocalHeapBytes;
        if (impl->SparsePool)
        {
            // 貸し出し量ではなく、プールの塊として確保した量を渡す（ヒープの使用量にはその全部が入っている）
            const SparsePagePool::Stats pool = impl->SparsePool->GetStats();
            input.PoolCapacityBytes[static_cast<uint32_t>(VideoMemoryPool::VirtualTexture)] = pool.CapacityBytes;
        }
        if (impl->GeometryBuffers)
        {
            // ジオメトリのプールの塊も、ヒープの使用量に全部入っているので、プール以外から引くために渡す
            input.PoolCapacityBytes[static_cast<uint32_t>(VideoMemoryPool::Geometry)] =
                impl->GeometryBuffers->GetStats().CapacityBytes;
        }

        const VideoMemoryBudgetResult result = impl->VideoMemoryBudget.Compute(input);
        impl->VideoMemoryBudgetLast = result;

        // ジオメトリの枠の目標を、プールの台帳へ出す（取り分の決め方と、目標に合わせた確保・追い出しは後の段）。
        if (impl->GeometryBuffers)
        {
            impl->GeometryBuffers->SetBudgetTarget(result.bLimited, result.GetTargetBytes(VideoMemoryPool::Geometry));
        }

        // VT のプールの上限へ反映する。プールの 0 は「上限なし」なので、割り振りが 0 のときは 1 バイトで塞ぐ。
        if (impl->SparsePool)
        {
            const uint64_t vtTarget = result.GetTargetBytes(VideoMemoryPool::VirtualTexture);
            impl->SparsePool->SetCapacityLimitBytes(result.bLimited ? (vtTarget > 0 ? vtTarget : 1) : 0);

            // ストリーマの常駐の目標も同じ値にする。プールは塊の単位でしか増えないので、実際に持てる量を超えない。
            // 目標を超えたぶんは、ストリーマが次の Update で外す。
            if (impl->VtStreamer)
            {
                const uint64_t reachable = impl->SparsePool->GetReachableCapacityBytes();
                impl->VtStreamer->SetResidentBudget(result.bLimited, vtTarget < reachable ? vtTarget : reachable);
            }
        }

        // 予算の割り振りが変わったとき、または VT が新しくタイルを外したときに VRAM_POOLS を出す
        const uint64_t evictedTiles = impl->VtStreamer ? impl->VtStreamer->GetStats().EvictedTiles : 0;
        const bool bBudgetChanged = impl->VideoMemoryBudget.CommitLogIfChanged(result);
        if (bBudgetChanged || evictedTiles != impl->VtLoggedEvictedTiles)
        {
            impl->VtLoggedEvictedTiles = evictedTiles;
            const uint64_t vtUsedBytes = impl->SparsePool ? impl->SparsePool->GetStats().UsedBytes : 0;
            if (result.bLimited)
            {
                NORVES_LOG_INFO(
                    "RenderResources",
                    "VRAM_POOLS cap_mb=%llu non_pool_mb=%llu vt_target_mb=%llu vt_used_mb=%llu vt_evicted_tiles=%llu source=%s",
                    static_cast<unsigned long long>(result.CeilingBytes / kBytesPerMb),
                    static_cast<unsigned long long>(result.NonPoolBytes / kBytesPerMb),
                    static_cast<unsigned long long>(result.GetTargetBytes(VideoMemoryPool::VirtualTexture) / kBytesPerMb),
                    static_cast<unsigned long long>(vtUsedBytes / kBytesPerMb),
                    static_cast<unsigned long long>(evictedTiles),
                    result.bNonPoolEstimated ? "estimate" : "heap");
            }
            else
            {
                NORVES_LOG_INFO(
                    "RenderResources",
                    "VRAM_POOLS cap_mb=none non_pool_mb=%llu vt_target_mb=none vt_used_mb=%llu vt_evicted_tiles=%llu",
                    static_cast<unsigned long long>(result.NonPoolBytes / kBytesPerMb),
                    static_cast<unsigned long long>(vtUsedBytes / kBytesPerMb),
                    static_cast<unsigned long long>(evictedTiles));
            }
        }
    }

    const VideoMemoryBudgetResult &RenderResources::GetVideoMemoryBudgetResult() const
    {
        return m_Impl->VideoMemoryBudgetLast;
    }

    void RenderResources::SetVideoMemoryPoolShare(VideoMemoryPool pool, uint32_t weight)
    {
        m_Impl->VideoMemoryBudget.SetPoolShare(pool, weight);
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

        if (m_Impl->VtStreamer)
        {
            // VT のテクスチャは GpuResources と一緒に消えるので、ストリーマの登録も解く（結んだページは GPU が使い終わるまで戻らない）
            m_Impl->VtStreamer->Clear();
            Thread::ScopedLock vtLock(m_Impl->VtMutex);
            m_Impl->VtIndexByTexture.clear();
            m_Impl->VtPendingReady.clear();
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
