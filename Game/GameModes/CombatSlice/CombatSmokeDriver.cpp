#include "CombatSmokeDriver.h"

#include "CombatSliceCapture.h"
#include "CombatSliceSwing.h"
#include "Container/StringBuilder.h"
#include "Engine/Engine.h"
#include "GameMode/GameModeContext.h"
#include "Gameplay/Combat/HitboxComponent.h"
#include "Gameplay/Combat/HurtboxSetComponent.h"
#include "Gameplay/IGameplayModule.h"
#include "Gameplay/World/ZoneSensorComponent.h"
#include "Logging/LogMacros.h"
#include "Object/Entity.h"
#include "Object/World.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <system_error>

namespace Game::GameModes
{
    using namespace NorvesLib::Core;
    using namespace NorvesLib::Core::GameMode;
    namespace Combat = NorvesLib::Modules::Gameplay::Combat;
    namespace Gameplay = NorvesLib::Modules::Gameplay;
    namespace W = NorvesLib::Modules::Gameplay::World;

    namespace
    {
        constexpr uint32_t PreparationFrames = 10;
        constexpr uint32_t TimeoutFrames = 600;

        bool CreateCaptureParent(const Container::String& path)
        {
            try
            {
                Container::VariableArray<char8_t> utf8(path.size());
                for (size_t index = 0; index < path.size(); ++index)
                {
                    utf8[index] = static_cast<char8_t>(path[index]);
                }
                const auto parent = std::filesystem::path(utf8.begin(), utf8.end()).parent_path();
                if (!parent.empty())
                {
                    std::error_code error;
                    std::filesystem::create_directories(parent, error);
                    return !error;
                }
                return true;
            }
            catch (const std::filesystem::filesystem_error&)
            {
                return false;
            }
        }
    } // namespace

    struct CombatSmokeDriver::State
    {
        Engine::Engine* Engine = nullptr;
        World* WorldRef = nullptr;
        Gameplay::IGameplayModule* Module = nullptr;
        CombatSliceSceneIds Ids;
        CombatSliceSwing Swing;
        CombatSliceCapture Capture;
        Container::String CapturePath;
        Container::VariableArray<Container::String> HitKeys;
        Delegate<void, const Combat::CombatHitEvent&> HitCallback;
        uint32_t Frames = 0;
        int32_t DtInv = 0;
        float FixedWorldDt = 0.0f;
        bool bReady = false;
        bool bListening = false;
        bool bProfileMismatch = false;
        bool bCaptureStarted = false;
        bool bCaptureSaved = false;
        bool bFinished = false;
        int32_t ExitCode = 0;

        void Fail(const char* reason)
        {
            if (bFinished)
            {
                return;
            }
            bFinished = true;
            ExitCode = 1;
            NORVES_LOG_INFO("CombatSlice", "COMBAT_SMOKE stage=failure reason=%s", reason);
            Engine->RequestExit(1);
        }

        void OnHit(const Combat::CombatHitEvent& event)
        {
            if (!bListening || bFinished || event.AttackerActorId != Ids.Dog)
            {
                return;
            }
            auto* sword = WorldRef->FindEntityByObjectId(Ids.Sword);
            auto* hitbox = sword ? sword->GetComponent<Combat::HitboxComponent>() : nullptr;
            if (!hitbox || hitbox->IsPendingDestroy() || !hitbox->GetProfile() ||
                hitbox->GetProfile()->ProfileId.GetHash() != event.ProfileId)
            {
                // 配送中には終了処理をせず、次の Tick へ不一致を渡す。
                bProfileMismatch = true;
                return;
            }
            auto attack = hitbox->GetProfile()->ProfileId.ToString();
            if (attack.empty())
            {
                attack = "combat_slice_sword";
            }
            NORVES_LOG_INFO("CombatSlice", "COMBAT_SMOKE stage=hit index=%zu dt_inv=%d part=%u attack=%s attack_instance=%llu point=%.3f,%.3f,%.3f",
                            HitKeys.size(), DtInv, event.PartId, attack.c_str(),
                            static_cast<unsigned long long>(event.AttackInstanceId),
                            event.Point.x, event.Point.y, event.Point.z);
            Container::StringBuilder key;
            key.AppendFormat("%u:%s", event.PartId, attack.c_str());
            HitKeys.push_back(key.ToString());
        }

        void Summarize()
        {
            auto keys = HitKeys;
            std::sort(keys.begin(), keys.end());
            const auto uniqueEnd = std::unique(keys.begin(), keys.end());
            Container::StringBuilder set;
            for (auto key = keys.begin(); key != uniqueEnd; ++key)
            {
                if (key != keys.begin())
                {
                    set.Append(';');
                }
                set.Append(*key);
            }
            const auto text = set.ToString();
            NORVES_LOG_INFO("CombatSlice", "COMBAT_SMOKE stage=summary dt_inv=%d hits=%zu set=%s",
                            DtInv, HitKeys.size(), text.c_str());
        }
    };

    CombatSmokeDriver::CombatSmokeDriver() = default;

    CombatSmokeDriver::~CombatSmokeDriver()
    {
        Unsubscribe();
    }

    bool CombatSmokeDriver::Begin(GameModeContext& ctx, const CombatSliceSceneIds& ids,
                                 const CombatSliceOptions& options)
    {
        End(ctx);
        m_bFinished = false;
        m_ExitCode = 0;
        m_State = Container::MakeShared<State>();
        auto& state = *m_State;
        state.Engine = &ctx.EngineRef;
        state.WorldRef = &ctx.WorldRef;
        state.Ids = ids;
        state.CapturePath = options.CapturePath;
        state.Module = Gameplay::FindGameplayModule(Module::GetModuleRegistry());
        auto* dog = ctx.WorldRef.FindEntityByObjectId(ids.Dog);
        auto* sword = ctx.WorldRef.FindEntityByObjectId(ids.Sword);
        auto* shisha = ctx.WorldRef.FindEntityByObjectId(ids.Shisha);
        auto* hitbox = sword ? sword->GetComponent<Combat::HitboxComponent>() : nullptr;
        auto* hurtbox = shisha ? shisha->GetComponent<Combat::HurtboxSetComponent>() : nullptr;
        if (!state.Module || !dog || dog->IsPendingDestroy() || !sword || sword->IsPendingDestroy() ||
            !shisha || shisha->IsPendingDestroy() || !hitbox || hitbox->IsPendingDestroy() ||
            !hitbox->GetProfile() || !hurtbox || hurtbox->IsPendingDestroy())
        {
            state.Fail("begin_actors");
            return false;
        }
        if (!state.CapturePath.empty() && !CreateCaptureParent(state.CapturePath))
        {
            state.Fail("capture");
            return false;
        }

        dog->SetPosition(CombatSliceLayout::DogStrikePosition);
        if (auto* sensor = dog->GetComponent<W::ZoneSensorComponent>(); sensor && !sensor->IsPendingDestroy())
        {
            sensor->MarkTeleported();
        }
        const Container::TWeakPtr<State> weak = m_State;
        state.HitCallback = [weak](const Combat::CombatHitEvent& event)
        {
            if (const auto locked = weak.lock())
            {
                locked->OnHit(event);
            }
        };
        state.Module->GetOnAnyHit().Add(state.HitCallback);
        state.bListening = true;
        return true;
    }

    void CombatSmokeDriver::Tick(GameModeContext& ctx, float deltaTime)
    {
        (void)deltaTime;
        if (!m_State || m_State->bFinished)
        {
            return;
        }
        auto& state = *m_State;
        if (++state.Frames >= TimeoutFrames)
        {
            state.Fail("timeout");
            return;
        }
        if (state.bProfileMismatch)
        {
            state.Fail("profile");
            return;
        }
        const float worldDt = ctx.EngineRef.GetTimeSystem().GetFrameTimes().World;
        if (!state.bReady)
        {
            if (!std::isfinite(worldDt) || worldDt < 0.0f ||
                (worldDt > 0.0f && state.FixedWorldDt > 0.0f &&
                 std::abs(worldDt - state.FixedWorldDt) > state.FixedWorldDt * 0.01f))
            {
                state.Fail("dt_not_fixed");
                return;
            }
            if (worldDt > 0.0f && state.FixedWorldDt == 0.0f)
            {
                state.FixedWorldDt = worldDt;
            }
            // 最初の十フレームは姿勢と取り付けの評価だけを待つ。
            if (state.Frames <= PreparationFrames)
            {
                return;
            }
            const double inverseDt = state.FixedWorldDt > 0.0f ? 1.0 / state.FixedWorldDt : 0.0;
            if (inverseDt < 1.0 || inverseDt > (std::numeric_limits<int32_t>::max)())
            {
                state.Fail("dt_not_fixed");
                return;
            }
            auto* shisha = ctx.WorldRef.FindEntityByObjectId(state.Ids.Shisha);
            auto* hurtbox = shisha ? shisha->GetComponent<Combat::HurtboxSetComponent>() : nullptr;
            if (!shisha || shisha->IsPendingDestroy() || !hurtbox || hurtbox->IsPendingDestroy())
            {
                state.Fail("lost_hurtbox");
                return;
            }
            state.DtInv = static_cast<int32_t>(std::lround(inverseDt));
            NORVES_LOG_INFO("CombatSlice", "COMBAT_SMOKE stage=ready dt_inv=%d hurtbox_parts=%zu",
                            state.DtInv, hurtbox->GetParts().size());
            state.bReady = true;
            if (!state.Swing.Start())
            {
                state.Fail("swing");
            }
            return;
        }

        // 当たりは前の LateTick で届くため、この要求のフレームの PostTick に火花が放出される。
        if (!state.CapturePath.empty() && !state.HitKeys.empty() && !state.bCaptureStarted)
        {
            state.bCaptureStarted = true;
            if (!state.Capture.Request(ctx, state.CapturePath))
            {
                state.Fail("capture");
                return;
            }
        }
        if (state.bCaptureStarted && !state.bCaptureSaved)
        {
            const auto status = state.Capture.Poll(ctx);
            if (status == CombatSliceCaptureStatus::Failed)
            {
                state.Fail("capture");
                return;
            }
            if (status == CombatSliceCaptureStatus::Saved)
            {
                state.bCaptureSaved = true;
                NORVES_LOG_INFO("CombatSlice", "COMBAT_SMOKE stage=capture path=%s", state.CapturePath.c_str());
            }
        }

        state.Swing.Tick(ctx, state.Ids, worldDt);
        if (state.Swing.IsFailed())
        {
            state.Fail("swing");
            return;
        }
        if (!state.Swing.IsFinished())
        {
            return;
        }
        if (state.HitKeys.empty())
        {
            state.Summarize();
            state.Fail("no_hits");
            return;
        }
        if (!state.CapturePath.empty() && !state.bCaptureSaved)
        {
            return;
        }
        state.Summarize();
        state.bFinished = true;
        state.ExitCode = 0;
        NORVES_LOG_INFO("CombatSlice", "COMBAT_SMOKE stage=complete exit_code=0");
        ctx.EngineRef.RequestExit(0);
    }

    bool CombatSmokeDriver::IsFinished() const
    {
        return m_State ? m_State->bFinished : m_bFinished;
    }

    int32_t CombatSmokeDriver::GetExitCode() const
    {
        return m_State ? m_State->ExitCode : m_ExitCode;
    }

    void CombatSmokeDriver::End(GameModeContext& ctx)
    {
        if (!m_State)
        {
            return;
        }
        const auto state = m_State;
        Unsubscribe();
        state->Swing.Reset(ctx, state->Ids);
    }

    void CombatSmokeDriver::Unsubscribe()
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
            module->GetOnAnyHit().Remove(state.HitCallback);
        }
        m_bFinished = state.bFinished;
        m_ExitCode = state.ExitCode;
        m_State.reset();
    }
} // namespace Game::GameModes
