#pragma once
// cooked骨格資産のCPU非同期runtime。製品loopやGPUを自動駆動しない。
#include "Animation/SkeletalAssetResource.h"
#include "Animation/RigSplitAssetRequest.h"
#include "Asset/AssetResolveResult.h"
#include "Asset/CookedSkeletalFormat.h"
#include "Container/PointerTypes.h"
#include "Container/StringView.h"
#include "Delegate/Delegate.h"
#include "Thread/Thread.h"
namespace NorvesLib::Thread
{
    class JobSystem;
}
namespace NorvesLib::Core::Asset
{
    class AssetSystem;
}
namespace NorvesLib::Core
{
    class ResourceRegistry;
    namespace ResourceIO::Detail
    {
        struct SkeletalRuntimeTestAccess;
    }
    enum class SkeletalRuntimeStatus : uint8_t
    {
        Success,
        Accepted,
        Unchanged,
        NotBound,
        AlreadyBound,
        WrongOwner,
        Closed,
        EmptyCallback,
        InvalidPath,
        LimitExceeded,
        IdExhausted,
        RegistrySessionChanged,
        PreparationException,
        SubmitRejected,
        SubmitException,
        CacheRejected,
        Busy,
        GenerationExhausted,
        Reentrant,
        NotClosed,
        Deferred,
        Drained
    };
    enum class SkeletalRuntimeFailure : uint8_t
    {
        None,
        InvalidRequest,
        ResolveRejected,
        FormatRejected,
        ParseRejected,
        MetadataMismatch,
        InvalidCpu,
        WrongOwner,
        RegistryNotReady,
        ResourceCreateFailed,
        ResourceLoadFailed,
        AssemblyInjectedFailure,
        AssemblyException,
        WorkerException,
        OwnerException,
        PublicationInvalidRequest,
        PublicationSessionChanged,
        PublicationInvalidCandidate,
        PublicationKeyCollision,
        PublicationIdCollision,
        PublicationInvalidCache,
        PublicationBudget,
        PublicationInjectedFailure,
        PublicationException,
        BindingRejected
    };
    struct SkeletalAssetCompletion
    {
        uint64_t RequestId = 0;
        SkeletalRuntimeFailure Failure = SkeletalRuntimeFailure::InvalidRequest;
        Container::TSharedPtr<SkeletalAssetResource> Asset;
        NorvesLib::Core::Asset::AssetResolveStatus ResolveStatus =
            NorvesLib::Core::Asset::AssetResolveStatus::InvalidRequest;
        NorvesLib::Core::Asset::CookedSkeletalParseStatus ParseStatus =
            NorvesLib::Core::Asset::CookedSkeletalParseStatus::InvalidBlob;
        bool bCacheHit = false;
        Container::TSharedPtr<const RigSplitAssetDiagnostics> SplitDiagnostics;
    };
    struct SkeletalRuntimeLimits
    {
        size_t MaxPendingGroups = 1024, MaxSubscribersPerGroup = 1024, MaxPathBytes = 2048, MaxKeyBytes = 4096;
        size_t MaxBundleClips = 1024;
        uint64_t MaxRegistrySlots = 65536, MaxRegistryMapEntries = 262144, MaxRegistryBuckets = 1048576;
        size_t MaxSplitKeyBytes = 256 * 1024;
    };
    struct SkeletalAdmissionResult
    {
        SkeletalRuntimeStatus Status = SkeletalRuntimeStatus::NotBound;
        uint64_t RequestId = 0;
    };
    struct SkeletalFlushResult
    {
        SkeletalRuntimeStatus Status = SkeletalRuntimeStatus::NotBound;
        uint64_t Groups = 0, Callbacks = 0, CallbackExceptions = 0;
    };
    class SkeletalAssetRuntime final
    {
      public:
        using Callback = Delegate<void, const SkeletalAssetCompletion&>;
        SkeletalAssetRuntime();
        ~SkeletalAssetRuntime();
        SkeletalAssetRuntime(const SkeletalAssetRuntime&) = delete;
        SkeletalAssetRuntime& operator=(const SkeletalAssetRuntime&) = delete;
        // Bindは一度だけ。借用依存の寿命とlifecycle排他は外側Drainまでcallerが保証する。
        [[nodiscard]] SkeletalRuntimeStatus Bind(ResourceRegistry& registry, Thread::JobSystem& jobs,
                                                 Container::TSharedPtr<const Asset::AssetSystem> snapshot,
                                                 Thread::Thread::ThreadId explicitOwner,
                                                 const SkeletalRuntimeLimits& limits = {});
        // Bind/Load/Flush/SetSnapshotはownerのみ。拒否ID0にはcallbackを約束しない。
        [[nodiscard]] SkeletalAdmissionResult LoadAsync(Container::AnsiStringView path, Callback callback);
        [[nodiscard]] SkeletalAdmissionResult LoadRigSplitAsync(const Skeletal::RigSplitRequest&, Callback callback);
        [[nodiscard]] SkeletalFlushResult FlushCompleted(uint32_t maxLoads = 0);
        [[nodiscard]] SkeletalRuntimeStatus SetSnapshot(Container::TSharedPtr<const Asset::AssetSystem> snapshot);
        // group全体の未予約callbackを抑止する。予約済み呼出終了までtarget寿命が必要。
        [[nodiscard]] bool Cancel(uint64_t requestId);
        void Close();
        // closedのみ。自身のcallback/active操作内はDeferred。TaskWaitとは異なるhandoff境界。
        [[nodiscard]] SkeletalRuntimeStatus Drain();
        [[nodiscard]] size_t GetPendingCount() const;

      private:
        struct State;
        Container::TSharedPtr<State> m_State;
        friend struct ResourceIO::Detail::SkeletalRuntimeTestAccess;
    };
} // namespace NorvesLib::Core
