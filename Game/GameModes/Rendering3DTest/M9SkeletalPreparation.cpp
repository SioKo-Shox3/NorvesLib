#include "GameModes/Rendering3DTest/M9SkeletalPreparation.h"
namespace Game::GameModes
{
    namespace Core = NorvesLib::Core;
    namespace C = Core::Container;
    using Status = Core::SkeletalRuntimeStatus;
    struct M9SkeletalPreparation::State
    {
        Core::SkeletalAssetRuntime* pRuntime = nullptr;
        uint64_t RequestId = 0;
        C::String ClipName;
        M9SkeletalEvent Event;
        bool bAlive = true, bRequestPending = true, bEventPending = false;
    };
    M9SkeletalPreparation::~M9SkeletalPreparation()
    {
        Cancel();
    }
    Core::SkeletalAdmissionResult M9SkeletalPreparation::Start(Core::SkeletalAssetRuntime& runtime,
                                                               C::AnsiStringView path, C::StringView clipName)
    {
        if (!CanPrepare())
        {
            return {Status::Busy, 0};
        }
        C::TSharedPtr<State> candidate;
        try
        {
            candidate = C::MakeShared<State>();
            candidate->ClipName = C::String(clipName);
        }
        catch (...)
        {
            return {Status::PreparationException, 0};
        }
        Cancel();
        m_State = candidate;
        candidate->pRuntime = &runtime;
        const C::TWeakPtr<State> weak = candidate;
        Core::SkeletalAdmissionResult result;
        try
        {
            result = runtime.LoadAsync(
                path,
                [weak](const Core::SkeletalAssetCompletion& completion)
                {
                    auto state = weak.lock();
                    if (!state || !state->bAlive || !state->bRequestPending || state->RequestId != completion.RequestId)
                    {
                        return;
                    }
                    state->bRequestPending = false;
                    state->bEventPending = true;
                    state->Event.Admission = Status::Accepted;
                    state->Event.Failure = completion.Failure;
                    if (completion.Failure == Core::SkeletalRuntimeFailure::None && completion.Asset)
                    {
                        auto clip = completion.Asset->GetClip(C::StringView(state->ClipName));
                        if (clip && clip->IsLoaded() && clip->IsValid())
                        {
                            state->Event.Asset = completion.Asset;
                            state->Event.Clip = std::move(clip);
                            state->Event.bReady = true;
                        }
                        else
                        {
                            state->Event.bClipSelectionFailed = true;
                        }
                    }
                });
        }
        catch (...)
        {
            result = {Status::PreparationException, 0};
        }
        candidate->RequestId = result.RequestId;
        if (result.Status != Status::Accepted)
        {
            candidate->bRequestPending = false;
            candidate->bEventPending = true;
            candidate->Event.Admission = result.Status;
        }
        return result;
    }
    void M9SkeletalPreparation::Cancel()
    {
        auto state = std::move(m_State);
        if (!state)
        {
            return;
        }
        state->bAlive = false;
        state->bRequestPending = false;
        state->bEventPending = false;
        state->Event = {};
        auto* runtime = state->pRuntime;
        state->pRuntime = nullptr;
        const auto id = state->RequestId;
        state->RequestId = 0;
        if (runtime && id != 0)
        {
            (void)runtime->Cancel(id);
        }
    }
    bool M9SkeletalPreparation::TakeEvent(M9SkeletalEvent& out)
    {
        if (!m_State || !m_State->bAlive || !m_State->bEventPending)
        {
            return false;
        }
        out = std::move(m_State->Event);
        m_State->Event = {};
        m_State->bEventPending = false;
        return true;
    }
    bool M9SkeletalPreparation::HasPendingWork() const
    {
        return m_State && m_State->bAlive && (m_State->bRequestPending || m_State->bEventPending);
    }
    bool M9SkeletalPreparation::CanPrepare() const
    {
        return !IsActive() || HasPendingWork();
    }
    bool M9SkeletalPreparation::CanEnter() const
    {
        return HasPendingWork();
    }
    bool M9SkeletalPreparation::IsActive() const
    {
        return m_State && m_State->bAlive;
    }
    uint64_t M9SkeletalPreparation::GetRequestId() const
    {
        return m_State ? m_State->RequestId : 0;
    }
} // namespace Game::GameModes
