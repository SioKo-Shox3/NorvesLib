#pragma once

#include "Resource/ModelAsyncLoadQueue.h"
#include "Resource/ModelHandleCache.h"

#include "Asset/AssetSystem.h"
#include "Container/PointerTypes.h"
#include "Thread/Atomic.h"
#include "Thread/Mutex.h"

namespace NorvesLib::Core::Rendering
{
    class MegaGeometryResources;
    class TextureResources;

    class ModelAssetRuntime final
    {
    public:
        ModelAssetRuntime() = default;
        ~ModelAssetRuntime();
        ModelAssetRuntime(const ModelAssetRuntime&) = delete;
        ModelAssetRuntime& operator=(const ModelAssetRuntime&) = delete;

        void Bind(TextureResources* pTextures,
                  MegaGeometryResources* pMegaGeometry);
        void Unbind();
        [[nodiscard]] bool IsBound() const;
        [[nodiscard]] bool SetAssetSystem(Container::TSharedPtr<const Asset::AssetSystem> assetSystem);
        uint32_t LoadModelAsync(const Container::String& logicalPath,
                                Resource::ModelAsyncLoadQueue::Callback callback);
        uint32_t FlushCompletedModelLoads(uint32_t maxLoadsPerFrame);
        void CancelModelLoad(uint32_t requestId);
        [[nodiscard]] bool CancelPendingModelLoadsAndWait();
        [[nodiscard]] uint32_t GetPendingAsyncModelLoadCount() const;
        [[nodiscard]] Resource::ModelCacheReleaseResult ReleaseManagedModel(ModelHandle handle);

        [[nodiscard]] bool CloseAndDrain();
        void ReopenAfterClear();

        void AdvanceGenerationForTesting();
        [[nodiscard]] bool SeedCurrentModelForTesting(const Container::String& logicalPath,
                                                      ModelHandle handle);

    private:
        [[nodiscard]] bool TryBuildPlan(const Container::String& logicalPath,
                                        Resource::CookedModelLoadPlan& outPlan) const;
        [[nodiscard]] bool IsRequestCurrent(const Resource::ModelAsyncLoadQueue::RequestPtr& request) const;
        void ReleaseHandles(Resource::ModelCacheHandleBatch batch);
        void ReleaseHandles(Container::VariableArray<ModelHandle> handles);

        mutable Thread::Mutex m_AssetMutex;
        Container::TSharedPtr<const Asset::AssetSystem> m_AssetSystem;
        TextureResources* m_pTextures = nullptr;
        MegaGeometryResources* m_pMegaGeometry = nullptr;
        uint64_t m_Generation = 0;
        bool m_bBound = false;
        bool m_bAcceptingRequests = false;
        bool m_bClosing = false;
        Resource::ModelAsyncLoadQueue m_Queue;
        Resource::ModelHandleCache m_Cache;
        Thread::Atomic<uint32_t> m_NextCacheHitRequestId{0x80000000u};
    };
} // namespace NorvesLib::Core::Rendering
