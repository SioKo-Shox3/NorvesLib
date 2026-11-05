#include "EncounterSmokeDriver.h"

#include "Engine/Engine.h"
#include "GameMode/GameModeContext.h"
#include "Gameplay/IGameplayModule.h"
#include "Gameplay/World/EncounterService.h"
#include "Gameplay/World/InteractionService.h"
#include "Gameplay/World/ZoneSensorComponent.h"
#include "Logging/LogMacros.h"
#include "Object/Entity.h"
#include "Object/World.h"

#include <cmath>

namespace Game::GameModes
{
    using namespace NorvesLib::Core;
    using namespace NorvesLib::Core::GameMode;
    namespace Gameplay = NorvesLib::Modules::Gameplay;
    namespace W = NorvesLib::Modules::Gameplay::World;
    namespace M = NorvesLib::Math;

    namespace
    {
        enum class EStep : uint8_t
        {
            ApproachBed,
            LeaveBed,
            EnterZone,
            LeaveZone,
            ReenterZone,
            ClearZone
        };

        enum class EMarker : uint8_t
        {
            Show,
            Hide,
            Enter,
            Engaged,
            Exit,
            Reset,
            Cleared
        };

        constexpr EMarker ExpectedMarkers[] = {
            EMarker::Show, EMarker::Hide, EMarker::Enter, EMarker::Engaged,
            EMarker::Exit, EMarker::Reset, EMarker::Enter, EMarker::Engaged, EMarker::Cleared};
        constexpr EStep ExpectedSteps[] = {
            EStep::ApproachBed, EStep::LeaveBed, EStep::EnterZone, EStep::EnterZone,
            EStep::LeaveZone, EStep::LeaveZone, EStep::ReenterZone, EStep::ReenterZone, EStep::ClearZone};
        constexpr uint32_t MarkerCount = 9;
        constexpr float MoveSpeed = 3.0f;
        constexpr double StageTimeoutSeconds = 10.0;

        const char* GetStepName(EStep step)
        {
            switch (step)
            {
            case EStep::ApproachBed: return "approach_bed";
            case EStep::LeaveBed: return "leave_bed";
            case EStep::EnterZone: return "enter_zone";
            case EStep::LeaveZone: return "leave_zone";
            case EStep::ReenterZone: return "reenter_zone";
            case EStep::ClearZone: return "clear_zone";
            }
            return "unknown_stage";
        }

        const char* GetMarkerName(EMarker marker)
        {
            switch (marker)
            {
            case EMarker::Show: return "show";
            case EMarker::Hide: return "hide";
            case EMarker::Enter: return "enter";
            case EMarker::Engaged: return "engaged";
            case EMarker::Exit: return "exit";
            case EMarker::Reset: return "reset";
            case EMarker::Cleared: return "cleared";
            }
            return "unknown_marker";
        }

        bool MoveTo(Entity& dog, const M::Vector3& target, float worldDeltaSeconds)
        {
            const auto position = dog.GetPosition();
            const auto offset = target - position;
            const float distance = std::sqrt(offset.x * offset.x + offset.y * offset.y + offset.z * offset.z);
            const float travel = MoveSpeed * worldDeltaSeconds;
            if (distance <= travel)
            {
                dog.SetPosition(target);
                return true;
            }
            if (travel > 0.0f)
            {
                dog.SetPosition(position + offset * (travel / distance));
            }
            return false;
        }
    } // namespace

    struct EncounterSmokeDriver::State
    {
        Engine::Engine* Engine = nullptr;
        Gameplay::IGameplayModule* Module = nullptr;
        EntityHandle Dog;
        uint64_t BedId = 0;
        EStep Step = EStep::ApproachBed;
        uint32_t NextMarker = 0;
        double StageSeconds = 0.0;
        bool bListening = false;
        bool bEngaged = false;
        bool bClearRequested = false;
        bool bFinished = false;
        int32_t ExitCode = 0;
        Container::String LastPromptAction;
        Delegate<void, const W::EncounterZoneEvent&> EnterCallback;
        Delegate<void, const W::EncounterZoneEvent&> ExitCallback;
        Delegate<void, const W::EncounterZoneEvent&> ResetCallback;
        Delegate<void, const W::EncounterZoneEvent&> ClearedCallback;
        Delegate<void, const W::InteractionPrompt&, const W::InteractionPrompt&> PromptCallback;

        void Fail(const char* reason)
        {
            if (bFinished)
            {
                return;
            }
            bFinished = true;
            ExitCode = 1;
            NORVES_LOG_INFO("CombatSlice", "ENCOUNTER_SMOKE stage=failure reason=%s", reason);
            Engine->RequestExit(1);
        }

        void Unexpected(EMarker marker)
        {
            const Container::String reason = Container::String("unexpected_") + GetMarkerName(marker);
            Fail(reason.c_str());
        }

        void Accept(EMarker marker)
        {
            if (NextMarker >= MarkerCount || ExpectedMarkers[NextMarker] != marker || ExpectedSteps[NextMarker] != Step)
            {
                Unexpected(marker);
                return;
            }
            ++NextMarker;
        }

        void SampleEngaged()
        {
            const bool bNowEngaged = Module->GetEncounterService().IsSensorEngaged(Dog);
            if (bNowEngaged && !bEngaged)
            {
                NORVES_LOG_INFO("CombatSlice", "ENCOUNTER_SMOKE stage=engaged zone=%s", CombatSliceLayout::ZoneId);
                Accept(EMarker::Engaged);
            }
            bEngaged = bNowEngaged;
        }

        void OnZone(EMarker marker, const W::EncounterZoneEvent& event)
        {
            if (!bListening || bFinished)
            {
                return;
            }
            // 配送された順で記録し、段に合わない通知も記録の直後に失敗させる。
            NORVES_LOG_INFO("CombatSlice", "ENCOUNTER_SMOKE stage=%s zone=%s", GetMarkerName(marker), event.ZoneId.c_str());
            if (event.ZoneId != CombatSliceLayout::ZoneId)
            {
                Unexpected(marker);
                return;
            }
            Accept(marker);
            if (!bFinished)
            {
                // 遭遇サービスは通知前に engaged を確定するため、入場と同じ評価で立ち上がりを拾える。
                SampleEngaged();
            }
        }

        void OnPrompt(const W::InteractionPrompt& prompt)
        {
            if (!bListening || bFinished)
            {
                return;
            }
            const bool bShow = prompt.IsValid();
            if (bShow)
            {
                LastPromptAction = prompt.ActionId;
            }
            NORVES_LOG_INFO("CombatSlice", "ENCOUNTER_SMOKE prompt=%s action=%s",
                            bShow ? "show" : "hide", LastPromptAction.c_str());
            const auto marker = bShow ? EMarker::Show : EMarker::Hide;
            if (LastPromptAction != CombatSliceLayout::BedActionId || (bShow && prompt.ObjectId != BedId))
            {
                Unexpected(marker);
                return;
            }
            Accept(marker);
        }

        void Advance(EStep next)
        {
            Step = next;
            StageSeconds = 0.0;
        }
    };

    EncounterSmokeDriver::EncounterSmokeDriver() = default;

    EncounterSmokeDriver::~EncounterSmokeDriver()
    {
        Unsubscribe();
    }

    bool EncounterSmokeDriver::Begin(GameModeContext& ctx, const CombatSliceSceneIds& ids,
                                     const CombatSliceOptions& options)
    {
        (void)options;
        End(ctx);
        m_bFinished = false;
        m_ExitCode = 0;
        m_State = Container::MakeShared<State>();
        auto& state = *m_State;
        state.Engine = &ctx.EngineRef;
        state.Module = Gameplay::FindGameplayModule(Module::GetModuleRegistry());
        auto* dog = ctx.WorldRef.FindEntityByObjectId(ids.Dog);
        auto* sensor = dog ? dog->GetComponent<W::ZoneSensorComponent>() : nullptr;
        if (!state.Module || !dog || dog->IsPendingDestroy() || !sensor || sensor->IsPendingDestroy() ||
            !dog->GetEntityHandle().IsValid())
        {
            state.Fail("begin_sensor");
            return false;
        }
        state.Dog = dog->GetEntityHandle();
        state.BedId = ids.Bed;
        dog->SetPosition({-3.0f, 0.0f, -7.0f});
        sensor->MarkTeleported();

        const Container::TWeakPtr<State> weak = m_State;
        state.EnterCallback = [weak](const W::EncounterZoneEvent& event)
        {
            if (const auto locked = weak.lock())
            {
                locked->OnZone(EMarker::Enter, event);
            }
        };
        state.ExitCallback = [weak](const W::EncounterZoneEvent& event)
        {
            if (const auto locked = weak.lock())
            {
                locked->OnZone(EMarker::Exit, event);
            }
        };
        state.ResetCallback = [weak](const W::EncounterZoneEvent& event)
        {
            if (const auto locked = weak.lock())
            {
                locked->OnZone(EMarker::Reset, event);
            }
        };
        state.ClearedCallback = [weak](const W::EncounterZoneEvent& event)
        {
            if (const auto locked = weak.lock())
            {
                locked->OnZone(EMarker::Cleared, event);
            }
        };
        state.PromptCallback = [weak](const W::InteractionPrompt&, const W::InteractionPrompt& prompt)
        {
            if (const auto locked = weak.lock())
            {
                locked->OnPrompt(prompt);
            }
        };

        auto& encounter = state.Module->GetEncounterService();
        encounter.OnZoneEntered.Add(state.EnterCallback);
        encounter.OnZoneExited.Add(state.ExitCallback);
        encounter.OnZoneReset.Add(state.ResetCallback);
        encounter.OnZoneCleared.Add(state.ClearedCallback);
        state.Module->GetInteractionService().OnPromptChanged.Add(state.PromptCallback);
        state.bListening = true;
        NORVES_LOG_INFO("CombatSlice", "ENCOUNTER_SMOKE stage=ready");
        return true;
    }

    void EncounterSmokeDriver::Tick(GameModeContext& ctx, float deltaTime)
    {
        (void)deltaTime;
        if (!m_State || m_State->bFinished)
        {
            return;
        }
        auto& state = *m_State;
        auto* module = Gameplay::FindGameplayModule(Module::GetModuleRegistry());
        auto* dog = ctx.WorldRef.ResolveEntityHandle(state.Dog);
        auto* sensor = dog ? dog->GetComponent<W::ZoneSensorComponent>() : nullptr;
        if (!module || module != state.Module || !dog || dog->IsPendingDestroy() ||
            !sensor || sensor->IsPendingDestroy())
        {
            state.Fail("lost_sensor");
            return;
        }

        const float worldDeltaSeconds = ctx.EngineRef.GetTimeSystem().GetFrameTimes().World;
        if (!std::isfinite(worldDeltaSeconds) || worldDeltaSeconds < 0.0f)
        {
            state.Fail("invalid_world_dt");
            return;
        }
        // 通知を伴わない立ち上がりも検出する。移動前なので直前の LateTick の結果を読む。
        state.SampleEngaged();
        if (state.bFinished)
        {
            return;
        }
        state.StageSeconds += worldDeltaSeconds;
        if (state.StageSeconds >= StageTimeoutSeconds)
        {
            state.Fail(GetStepName(state.Step));
            return;
        }

        // 通知は移動中にも届く。到着と通知の両方がそろうまで次の段へ進まない。
        switch (state.Step)
        {
        case EStep::ApproachBed:
            if (MoveTo(*dog, {-3.0f, 0.0f, -5.0f}, worldDeltaSeconds) && state.NextMarker == 1)
            {
                state.Advance(EStep::LeaveBed);
            }
            break;
        case EStep::LeaveBed:
            if (MoveTo(*dog, {-3.0f, 0.0f, -7.0f}, worldDeltaSeconds) && state.NextMarker == 2)
            {
                state.Advance(EStep::EnterZone);
            }
            break;
        case EStep::EnterZone:
            if (MoveTo(*dog, {0.0f, 0.0f, -2.0f}, worldDeltaSeconds) && state.NextMarker == 4)
            {
                state.Advance(EStep::LeaveZone);
            }
            break;
        case EStep::LeaveZone:
            if (MoveTo(*dog, {0.0f, 0.0f, -8.0f}, worldDeltaSeconds) && state.NextMarker == 6)
            {
                state.Advance(EStep::ReenterZone);
            }
            break;
        case EStep::ReenterZone:
            if (MoveTo(*dog, {0.0f, 0.0f, -2.0f}, worldDeltaSeconds) && state.NextMarker == 8)
            {
                state.Advance(EStep::ClearZone);
            }
            break;
        case EStep::ClearZone:
            if (!state.bClearRequested)
            {
                state.bClearRequested = true;
                state.Module->GetEncounterService().MarkCleared(CombatSliceLayout::ZoneId);
            }
            else if (state.NextMarker == MarkerCount)
            {
                state.bFinished = true;
                state.ExitCode = 0;
                NORVES_LOG_INFO("CombatSlice", "ENCOUNTER_SMOKE stage=complete exit_code=0");
                ctx.EngineRef.RequestExit(0);
            }
            break;
        }
    }

    bool EncounterSmokeDriver::IsFinished() const
    {
        return m_State ? m_State->bFinished : m_bFinished;
    }

    int32_t EncounterSmokeDriver::GetExitCode() const
    {
        return m_State ? m_State->ExitCode : m_ExitCode;
    }

    void EncounterSmokeDriver::End(GameModeContext& ctx)
    {
        (void)ctx;
        Unsubscribe();
    }

    void EncounterSmokeDriver::Unsubscribe()
    {
        if (!m_State)
        {
            return;
        }
        auto& state = *m_State;
        state.bListening = false;
        auto* module = Gameplay::FindGameplayModule(Module::GetModuleRegistry());
        if (module && module == state.Module)
        {
            auto& encounter = module->GetEncounterService();
            encounter.OnZoneEntered.Remove(state.EnterCallback);
            encounter.OnZoneExited.Remove(state.ExitCallback);
            encounter.OnZoneReset.Remove(state.ResetCallback);
            encounter.OnZoneCleared.Remove(state.ClearedCallback);
            module->GetInteractionService().OnPromptChanged.Remove(state.PromptCallback);
        }
        m_bFinished = state.bFinished;
        m_ExitCode = state.ExitCode;
        m_State.reset();
    }
} // namespace Game::GameModes
