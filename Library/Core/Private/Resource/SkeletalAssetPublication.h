#pragma once
// 統合骨格bundleの有限なRegistry公開境界。queue/製品loop/GPUは駆動しない。
#include "Resource/SkeletalAssetLoader.h"
#include "Object/ResourceRegistry.h"
#include "Resource/RigSplitPublicationIdentity.h"
namespace NorvesLib::Core::ResourceIO
{
    class SkeletalBundlePublisherAccess;
    // Registry session内で一度だけ発行するcache名前空間。ResourceIdを消費しない。
    struct SkeletalCacheDomain
    {
        uint64_t Session = 0;
        uint64_t Ordinal = 0;
    };
    struct SkeletalPublicationLimits
    {
        size_t MaxBundleClips = 1024;
        size_t MaxKeyBytes = 4096;
        uint64_t MaxSlots = 65536;
        uint64_t MaxMapEntries = 262144;
        uint64_t MaxCopiedBuckets = 1048576;
    };
    enum class SkeletalPublicationStatus : uint8_t
    {
        Success,
        CacheMiss,
        InvalidRequest,
        WrongOwnerThread,
        RegistryNotReady,
        SessionChanged,
        InvalidCandidate,
        KeyCollision,
        IdCollision,
        InvalidCachedAsset,
        BudgetExceeded,
        AssemblyFailed,
        InjectedFailure,
        Exception
    };
    struct SkeletalPublicationReport
    {
        SkeletalPublicationStatus Status = SkeletalPublicationStatus::InvalidRequest;
        SkeletalAssetLoadReport Assembly;
        Skeletal::RigSplitReport SplitAssembly;
        uint64_t Session = 0;
        uint64_t CopiedSlots = 0, CopiedIdEntries = 0, CopiedPathEntries = 0, CopiedFreeIndices = 0, CopiedBuckets = 0;
        uint32_t PublishedResources = 0;
        bool bCacheHit = false;
    };
    // Registryの借用寿命はcallerが保証する。Shutdown/Initializeを跨ぐpreparedは拒否する。
    class SkeletalPreparedPublication
    {
      public:
        SkeletalPreparedPublication();
        ~SkeletalPreparedPublication();
        SkeletalPreparedPublication(SkeletalPreparedPublication&&) noexcept;
        SkeletalPreparedPublication& operator=(SkeletalPreparedPublication&&) noexcept;
        SkeletalPreparedPublication(const SkeletalPreparedPublication&) = delete;
        SkeletalPreparedPublication& operator=(const SkeletalPreparedPublication&) = delete;
        [[nodiscard]] bool IsValid() const noexcept;

      private:
        struct State;
        Container::TUniquePtr<State> m_State;
        friend class SkeletalBundlePublisherAccess;
    };
    struct SkeletalPublishedAsset
    {
        Container::TSharedPtr<SkeletalAssetResource> Asset;
        ResourceHandle<SkeletalAssetResource> AggregateHandle;
        ResourceHandle<SkinnedMeshResource> MeshHandle;
        ResourceHandle<SkeletonResource> SkeletonHandle;
        Container::VariableArray<ResourceHandle<AnimationClipResource>> ClipHandles;
    };
    // 同じRegistry sessionでP1を呼ぶ。成功だけpreparedを置換し、まだ登録しない。
    [[nodiscard]] bool PrepareSkeletalPublication(const CookedSkeletalCpuAsset& cpu,
                                                  const SkeletalAssetCreateContext& context,
                                                  SkeletalPreparedPublication& out, SkeletalPublicationReport& report);
    [[nodiscard]] bool PrepareRigSplitPublication(Container::TSharedPtr<const RigSplitPublicationReceipt>,
                                                  const SkeletalAssetCreateContext&, SkeletalPreparedPublication&,
                                                  SkeletalPublicationReport&);
    [[nodiscard]] bool FindPublishedRigSplitAsset(const SkeletalAssetCreateContext&, const RigSplitRequestIdentity&,
                                                  const SkeletalPublicationLimits&, SkeletalPublishedAsset&,
                                                  SkeletalPublicationReport&);
    [[nodiscard]] bool AllocateSkeletalCacheDomain(const SkeletalAssetCreateContext& context, SkeletalCacheDomain& out,
                                                   SkeletalPublicationReport& report);
    [[nodiscard]] bool ValidateSkeletalCacheDomain(const SkeletalAssetCreateContext& context,
                                                   const SkeletalCacheDomain& domain,
                                                   SkeletalPublicationReport& report);
    // cacheKeyは世代/domain識別、logicalPathは元URI。両方を実文字列として検査する。
    [[nodiscard]] bool FindPublishedSkeletalAsset(const SkeletalAssetCreateContext& context,
                                                  const Container::String& cacheKey,
                                                  const Container::String& logicalPath,
                                                  const SkeletalPublicationLimits& limits, SkeletalPublishedAsset& out,
                                                  SkeletalPublicationReport& report);
    // 4型poolをshadowで準備し、mutex内のnoexcept pointer swapだけで一括公開する。
    // cold公開ごとに既存4poolのメタデータをcopyする。高スループット/RSS/時間上限は保証しない。
    [[nodiscard]] bool CommitSkeletalPublication(const SkeletalPreparedPublication& prepared,
                                                 const Container::String& cacheKey,
                                                 const SkeletalPublicationLimits& limits, SkeletalPublishedAsset& out,
                                                 SkeletalPublicationReport& report);
} // namespace NorvesLib::Core::ResourceIO
