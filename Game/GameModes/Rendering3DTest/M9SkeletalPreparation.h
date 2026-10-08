#pragma once
// M9のowner側一回event。Taskやloader状態をroutineからpollしない。
#include "Core/Public/Animation/SkeletalAssetRuntime.h"
namespace Game::GameModes
{
    struct M9SkeletalEvent
    {
        NorvesLib::Core::Container::TSharedPtr<NorvesLib::Core::SkeletalAssetResource> Asset;
        NorvesLib::Core::Container::TSharedPtr<NorvesLib::Core::AnimationClipResource> Clip;
        NorvesLib::Core::SkeletalRuntimeFailure Failure = NorvesLib::Core::SkeletalRuntimeFailure::InvalidRequest;
        NorvesLib::Core::SkeletalRuntimeStatus Admission = NorvesLib::Core::SkeletalRuntimeStatus::NotBound;
        bool bReady = false, bClipSelectionFailed = false;
    };
    class M9SkeletalPreparation final
    {
      public:
        M9SkeletalPreparation() = default;
        ~M9SkeletalPreparation();
        M9SkeletalPreparation(const M9SkeletalPreparation&) = delete;
        M9SkeletalPreparation& operator=(const M9SkeletalPreparation&) = delete;
        // owner限定。借用runtimeはCancelまたはこのobjectの破棄まで生存する。
        [[nodiscard]] NorvesLib::Core::SkeletalAdmissionResult Start(NorvesLib::Core::SkeletalAssetRuntime& runtime,
                                                                     NorvesLib::Core::Container::AnsiStringView path,
                                                                     NorvesLib::Core::Container::StringView clipName);
        void Cancel();
        [[nodiscard]] bool TakeEvent(M9SkeletalEvent& out);
        [[nodiscard]] bool HasPendingWork() const;
        // 消費済みactivationはLeaveのCancelまで再Prepareしない。
        [[nodiscard]] bool CanPrepare() const;
        // 未配送要求または未消費eventのあるfresh activationだけEnterできる。
        [[nodiscard]] bool CanEnter() const;
        [[nodiscard]] bool IsActive() const;
        [[nodiscard]] uint64_t GetRequestId() const;

      private:
        struct State;
        NorvesLib::Core::Container::TSharedPtr<State> m_State;
    };
} // namespace Game::GameModes
