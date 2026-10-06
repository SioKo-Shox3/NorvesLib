#pragma once
// BetweenIndexes/DrainWaitingはState、InsidePublicationCommitはState+Registryを保持し再入禁止。
// runtime単体反証の限定入口。hook設定変更は全呼出との排他を試験側が保つ。
#include "Animation/SkeletalAssetRuntime.h"
#include "Thread/Task.h"
namespace NorvesLib::Core::ResourceIO::Detail
{
    enum class SkeletalRuntimePoint : uint8_t
    {
        Admission,
        BeforeHandler,
        AfterHandler,
        BetweenIndexes,
        BeforeSubmit,
        WorkerBeforeLoad,
        WorkerAfterLoad,
        BeforeAssembly,
        AfterAssembly,
        BeforeCommit,
        BeforeCallback
    };
    enum class SkeletalRuntimeObservation : uint8_t
    {
        AfterSubmit,
        BeforeHandoffAck,
        AfterDetach,
        InsidePublicationCommit,
        AfterCommit,
        CallbackReserved,
        DrainWaiting
    };
    struct SkeletalRuntimeHooks
    {
        void (*Fault)(SkeletalRuntimePoint, uint64_t, void*) = nullptr;
        void (*Observe)(SkeletalRuntimeObservation, uint64_t, void*) noexcept = nullptr;
        void* Context = nullptr;
    };
    struct SkeletalRuntimeCounts
    {
        uint64_t WorkerCalls = 0, AssemblyCalls = 0, CommitCalls = 0;
        size_t Ready = 0, Handoffs = 0, Admissions = 0, Accepted = 0;
        uint64_t Session = 0, Domain = 0, Generation = 0;
    };
    struct SkeletalRuntimeTestAccess
    {
        static void SetHooks(SkeletalAssetRuntime&, const SkeletalRuntimeHooks&);
        static SkeletalRuntimeCounts Counts(const SkeletalAssetRuntime&);
        static Container::TWeakPtr<void> WeakState(const SkeletalAssetRuntime&);
        static Container::TWeakPtr<void> WeakRequest(const SkeletalAssetRuntime&, uint64_t);
        static Container::TWeakPtr<SkeletalAssetResource> WeakPrepared(const SkeletalAssetRuntime&);
        static Thread::TaskPtr PeekTask(const SkeletalAssetRuntime&, uint64_t);
        // TryStateLockは自分がmutexを保持していないthreadからだけ呼ぶ。
        static bool TryStateLock(SkeletalAssetRuntime&);
        static void AlterStoredKey(SkeletalAssetRuntime&, uint64_t, const Container::String&);
        static bool WaitReady(SkeletalAssetRuntime&, size_t count);
        static void SetNextId(SkeletalAssetRuntime&, uint64_t);
        static void SetGeneration(SkeletalAssetRuntime&, uint64_t);
    };
} // namespace NorvesLib::Core::ResourceIO::Detail
