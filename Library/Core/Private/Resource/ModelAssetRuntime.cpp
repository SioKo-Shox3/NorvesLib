#include "Resource/ModelAssetRuntime.h"

#include "Asset/AssetPath.h"
#include "Debug/Stats.h"
#include "Logging/LogMacros.h"
#include "Rendering/RenderResources.h"
#include "Resource/ModelStaging.h"

#include <charconv>
#include <utility>

namespace NorvesLib::Core::Rendering
{
    using Resource::CookedModelLoadPlan;
    using Resource::ModelAsyncLoadQueue;
    using Resource::ModelCacheAcquireResult;
    using Resource::ModelCacheHandleBatch;
    using Resource::ModelCachePublishResult;
    using Resource::ModelCacheReleaseResult;
    namespace ModelStaging = Resource::ModelStaging;
    namespace
    {
        bool BuildNormalizedPathAndKey(const Container::String& logicalPath,
                                       uint64_t generation,
                                       Container::AnsiString& outNormalizedPath,
                                       Container::String& outCacheKey)
        {
            const Asset::AssetPath path = Asset::AssetPath::Normalize(
                Container::AnsiStringView(logicalPath.data(), logicalPath.size()));
            if (!path.IsValid() || path.IsAbsolute() || !path.HasLogicalPath())
            {
                return false;
            }

            outNormalizedPath = path.GetLogicalPath();
            char generationText[32]{};
            const auto conversion = std::to_chars(
                generationText,
                generationText + sizeof(generationText),
                generation);
            if (conversion.ec != std::errc())
            {
                return false;
            }

            outCacheKey = "asset:";
            outCacheKey.append(generationText, static_cast<size_t>(conversion.ptr - generationText));
            outCacheKey += ":default:";
            outCacheKey.append(outNormalizedPath.data(), outNormalizedPath.size());
            return true;
        }
    }

    ModelAssetRuntime::~ModelAssetRuntime()
    {
        CloseAndDrain();
        CloseForResourceClear();
    }

    void ModelAssetRuntime::Bind(TextureResources* pTextures,
                                 MegaGeometryResources* pMegaGeometry)
    {
        Thread::ScopedLock lock(m_AssetMutex);
        m_pTextures = pTextures;
        m_pMegaGeometry = pMegaGeometry;
        m_bBound = pTextures != nullptr && pMegaGeometry != nullptr;
        m_bClosing = false;
        m_bTerminallyQuiesced = false;
        m_bAcceptingRequests = m_bBound;
        if (m_bAcceptingRequests)
        {
            m_Queue.Reopen();
        }
    }

    void ModelAssetRuntime::Unbind()
    {
        CloseAndDrain();
        CloseForResourceClear();
        Thread::ScopedLock lock(m_AssetMutex);
        m_pTextures = nullptr;
        m_pMegaGeometry = nullptr;
        m_AssetSystem.reset();
        m_AssetRoot.clear();
        m_bBound = false;
        m_bAcceptingRequests = false;
        m_bClosing = true;
    }

    bool ModelAssetRuntime::IsBound() const
    {
        Thread::ScopedLock lock(m_AssetMutex);
        return m_bBound && m_pTextures != nullptr && m_pMegaGeometry != nullptr;
    }

    bool ModelAssetRuntime::CanReloadSnapshotLocked() const
    {
        return m_bBound && !m_bClosing && m_bAcceptingRequests &&
               m_pTextures != nullptr && m_pMegaGeometry != nullptr &&
               !m_Queue.HasPendingOrActiveFlush();
    }

    ModelCacheHandleBatch ModelAssetRuntime::ApplyReloadSnapshotLocked(
        const Container::String& assetRoot,
        const Container::TSharedPtr<const Asset::AssetSystem>& assetSystem)
    {
        ModelCacheHandleBatch retired = m_Cache.RetireCurrent();
        m_AssetSystem = assetSystem;
        m_AssetRoot = assetRoot;
        ++m_Generation;
        return retired;
    }

    void ModelAssetRuntime::ReleaseRetiredAfterReload(ModelCacheHandleBatch batch)
    {
        ReleaseHandles(std::move(batch));
    }

    bool ModelAssetRuntime::SetAssetSystem(
        Container::TSharedPtr<const Asset::AssetSystem> assetSystem)
    {
        if (!assetSystem)
        {
            return false;
        }

        ModelCacheHandleBatch retired;
        {
            Thread::ScopedLock lock(m_AssetMutex);
            if (!m_bBound || m_bClosing || !m_bAcceptingRequests)
            {
                return false;
            }
            if (m_AssetSystem == assetSystem)
            {
                return true;
            }
            if (m_Queue.HasPendingOrActiveFlush())
            {
                return false;
            }

            retired = ApplyReloadSnapshotLocked({}, assetSystem);
        }
        ReleaseRetiredAfterReload(std::move(retired));
        return true;
    }

    bool ModelAssetRuntime::TryBuildPlan(const Container::String& logicalPath,
                                         CookedModelLoadPlan& outPlan) const
    {
        Thread::ScopedLock lock(m_AssetMutex);
        if (!m_bBound || m_bClosing || !m_bAcceptingRequests || !m_AssetSystem)
        {
            return false;
        }

        outPlan = {};
        outPlan.AssetSystem = m_AssetSystem;
        outPlan.RequestPath = logicalPath;
        outPlan.Generation = m_Generation;
        return BuildNormalizedPathAndKey(
            logicalPath,
            m_Generation,
            outPlan.NormalizedLogicalPath,
            outPlan.CacheKey);
    }

    uint32_t ModelAssetRuntime::LoadModelAsync(
        const Container::String& logicalPath,
        ModelAsyncLoadQueue::Callback callback)
    {
        CookedModelLoadPlan plan;
        ModelCacheAcquireResult acquired;
        uint32_t cacheHitRequestId = 0;
        {
            Thread::ScopedLock lock(m_AssetMutex);
            if (!m_bBound || m_bClosing || !m_bAcceptingRequests || !m_AssetSystem)
            {
                return 0;
            }

            plan.AssetSystem = m_AssetSystem;
            plan.RequestPath = logicalPath;
            plan.Generation = m_Generation;
            if (!BuildNormalizedPathAndKey(
                    logicalPath,
                    m_Generation,
                    plan.NormalizedLogicalPath,
                    plan.CacheKey))
            {
                return 0;
            }

            acquired = m_Cache.Acquire(plan.CacheKey, callback.IsBound());
            if (acquired.bFound)
            {
                cacheHitRequestId = m_NextCacheHitRequestId.FetchAdd(1, std::memory_order_relaxed);
            }
            else
            {
                uint32_t duplicateId = m_Queue.TryAppendDuplicate(plan.CacheKey, callback);
                if (duplicateId != 0)
                {
                    return duplicateId;
                }

                ModelAsyncLoadQueue::RequestPtr request = m_Queue.CreateRequest(plan, std::move(callback));
                ModelAsyncLoadQueue::EnqueueResult enqueued = m_Queue.EnqueueOrAppendDuplicateAndSubmit(request);
                return enqueued.RequestId;
            }
        }

        if (callback.IsBound())
        {
            ModelAsyncLoadQueue::CallbackContextGuard callbackContext;
            callback(acquired.Handle);
        }
        return cacheHitRequestId == 0 ? 0x80000000u : cacheHitRequestId;
    }

    bool ModelAssetRuntime::IsRequestCurrent(const ModelAsyncLoadQueue::RequestPtr& request) const
    {
        Thread::ScopedLock lock(m_AssetMutex);
        return request && !request->bCancelled.Load(std::memory_order_acquire) &&
               m_bBound && !m_bClosing && m_bAcceptingRequests &&
               request->Plan.AssetSystem == m_AssetSystem &&
               request->Plan.Generation == m_Generation;
    }

    uint32_t ModelAssetRuntime::FlushCompletedModelLoads(uint32_t maxLoadsPerFrame)
    {
        NORVES_STAT_SCOPE_CATEGORY("ModelAsset.AsyncFlush", "AssetLoad");
        {
            Thread::ScopedLock lock(m_AssetMutex);
            if (!m_bBound || m_bClosing || !m_bAcceptingRequests ||
                m_pTextures == nullptr || m_pMegaGeometry == nullptr)
            {
                return 0;
            }
        }

        ModelAsyncLoadQueue::TerminalBatch batch = m_Queue.DetachTerminal(maxLoadsPerFrame);
        uint32_t processed = 0;
        for (ModelAsyncLoadQueue::RequestPtr& request : batch.Requests)
        {
            ++processed;
            ModelAsyncLoadQueue::CallbackList callbacks = m_Queue.TakeCallbacksAndRelease(request);
            if (!request || request->bCancelled.Load(std::memory_order_acquire))
            {
                continue;
            }

            ModelHandle resolved = ModelHandle::Invalid();
            bool bCurrent = IsRequestCurrent(request);
            if (bCurrent && request->Result.bSuccess)
            {
                TextureResources* pTextures = nullptr;
                MegaGeometryResources* pMegaGeometry = nullptr;
                {
                    Thread::ScopedLock lock(m_AssetMutex);
                    pTextures = m_pTextures;
                    pMegaGeometry = m_pMegaGeometry;
                }
                ModelHandle candidate = ModelStaging::FinalizeModelStaging(
                    request->Result.Staging,
                    ModelLoadResourceContext{*pTextures, *pMegaGeometry},
                    "main_render",
                    request->RequestId);

                if (candidate.IsValid() && IsRequestCurrent(request))
                {
                    ModelCachePublishResult published = m_Cache.PublishOrAcquire(
                        request->Result.CacheKey,
                        candidate,
                        static_cast<uint32_t>(callbacks.size()));
                    resolved = published.ResolvedHandle;
                    ReleaseHandles(std::move(published.HandlesToRelease));
                }
                else if (candidate.IsValid())
                {
                    if (pMegaGeometry != nullptr)
                    {
                        pMegaGeometry->ReleaseModelUnmanaged(candidate);
                    }
                }
            }

            if (!request->bCancelled.Load(std::memory_order_acquire))
            {
                for (ModelAsyncLoadQueue::Callback& callback : callbacks)
                {
                    if (callback.IsBound())
                    {
                        ModelAsyncLoadQueue::CallbackContextGuard callbackContext;
                        callback(resolved);
                    }
                }
            }
        }
        NORVES_LOG_INFO(
            "AssetLoadProfile",
            "stage=model_async_flush role=main_render processed=%u success=1",
            static_cast<unsigned int>(processed));
        return processed;
    }

    void ModelAssetRuntime::CancelModelLoad(uint32_t requestId)
    {
        m_Queue.Cancel(requestId);
    }

    bool ModelAssetRuntime::CancelPendingModelLoadsAndWait()
    {
        if (ModelAsyncLoadQueue::IsInCallbackContext())
        {
            return false;
        }

        m_Queue.CloseCancelAllAndWait();

        Thread::ScopedLock lock(m_AssetMutex);
        if (m_bBound && !m_bClosing && m_bAcceptingRequests)
        {
            m_Queue.Reopen();
        }
        return true;
    }

    uint32_t ModelAssetRuntime::GetPendingAsyncModelLoadCount() const
    {
        return m_Queue.GetPendingCount();
    }

    ModelCacheReleaseResult ModelAssetRuntime::ReleaseManagedModel(ModelHandle handle)
    {
        return m_Cache.ReleaseLease(handle);
    }

    void ModelAssetRuntime::CloseAndDrain()
    {
        Delegate<void> closeHook;
        {
            Thread::ScopedLock lock(m_AssetMutex);
            m_bClosing = true;
            m_bTerminallyQuiesced = true;
            m_bAcceptingRequests = false;
            closeHook = std::move(m_AdmissionCloseHookForTesting);
            m_AdmissionCloseHookForTesting.Clear();
        }

        closeHook.InvokeIfBound();
        m_Queue.CloseCancelAllAndWait();
    }

    void ModelAssetRuntime::ReopenAfterClear()
    {
        Thread::ScopedLock lock(m_AssetMutex);
        if (!m_bTerminallyQuiesced && m_bBound && m_pTextures != nullptr && m_pMegaGeometry != nullptr)
        {
            m_bClosing = false;
            m_bAcceptingRequests = true;
            m_Queue.Reopen();
        }
    }

    void ModelAssetRuntime::CloseForResourceClear()
    {
        MegaGeometryResources* pMegaGeometry = nullptr;
        {
            Thread::ScopedLock lock(m_AssetMutex);
            m_bClosing = true;
            m_bAcceptingRequests = false;
            pMegaGeometry = m_pMegaGeometry;
        }

        m_Queue.CloseCancelAllAndWait();
        ModelCacheHandleBatch drained = m_Cache.DrainAll();
        if (pMegaGeometry != nullptr)
        {
            for (ModelHandle handle : drained.HandlesToRelease)
            {
                if (handle.IsValid())
                {
                    pMegaGeometry->ReleaseModelUnmanaged(handle);
                }
            }
        }
    }

    void ModelAssetRuntime::AdvanceGenerationForTesting()
    {
        ModelCacheHandleBatch retired;
        {
            Thread::ScopedLock lock(m_AssetMutex);
            ++m_Generation;
            retired = m_Cache.RetireCurrent();
        }
        ReleaseHandles(std::move(retired));
    }

    bool ModelAssetRuntime::SeedCurrentModelForTesting(
        const Container::String& logicalPath,
        ModelHandle handle)
    {
        CookedModelLoadPlan plan;
        if (!handle.IsValid() || !TryBuildPlan(logicalPath, plan))
        {
            return false;
        }
        ModelCachePublishResult published = m_Cache.PublishOrAcquire(plan.CacheKey, handle, 0);
        ReleaseHandles(std::move(published.HandlesToRelease));
        return published.ResolvedHandle == handle;
    }

    void ModelAssetRuntime::ReleaseHandles(ModelCacheHandleBatch batch)
    {
        ReleaseHandles(std::move(batch.HandlesToRelease));
    }

    void ModelAssetRuntime::ReleaseHandles(
        Container::VariableArray<ModelHandle> handles)
    {
        MegaGeometryResources* pMegaGeometry = nullptr;
        {
            Thread::ScopedLock lock(m_AssetMutex);
            pMegaGeometry = m_pMegaGeometry;
        }
        if (pMegaGeometry == nullptr)
        {
            return;
        }
        for (ModelHandle handle : handles)
        {
            if (handle.IsValid())
            {
                pMegaGeometry->ReleaseModelUnmanaged(handle);
            }
        }
    }
} // namespace NorvesLib::Core::Rendering
