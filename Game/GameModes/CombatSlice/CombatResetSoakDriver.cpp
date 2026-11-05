#include "CombatResetSoakDriver.h"

// StopOwner は停止時間 0 秒でも最短 2/60 秒のフェードを設ける。
// Enter 時の voice は記録だけにし、8 フレーム後の ready で比較する。

#include "Container/StringBuilder.h"
#include "Engine/Engine.h"
#include "Engine/NorvesEngine.h"
#include "GameMode/GameModeContext.h"
#include "GameMode/GameModeScope.h"
#include "Gameplay/IGameplayModule.h"
#include "Gameplay/Progression/ProgressionState.h"
#include "Logging/LogMacros.h"
#include "Module/ModuleRegistry.h"
#include "Save/SaveService.h"

#if defined(NORVES_GAME_AUDIO)
#include "Audio/IAudioModule.h"
#endif

namespace Game::GameModes
{
    using namespace NorvesLib::Core;
    using namespace NorvesLib::Core::GameMode;
    namespace Gameplay = NorvesLib::Modules::Gameplay;
    namespace L = CombatSliceLayout;

    namespace
    {
        constexpr uint32_t ReadyFrames = 8;
        constexpr uint32_t TimeoutFrames = 900;

        void LogCounts(const char* stage, uint32_t index, const CombatResetSoakCounts& counts, const char* extra)
        {
            NORVES_LOG_INFO("CombatSlice",
                            "GAME_RESET stage=%s index=%u roots=%llu textures=%llu meshes=%llu models=%llu pending=%llu voices=%llu%s",
                            stage, index, static_cast<unsigned long long>(counts.RootEntities),
                            static_cast<unsigned long long>(counts.Textures),
                            static_cast<unsigned long long>(counts.Meshes),
                            static_cast<unsigned long long>(counts.Models),
                            static_cast<unsigned long long>(counts.PendingAsyncLoads),
                            static_cast<unsigned long long>(counts.Voices), extra);
        }

        void AppendDifference(Container::AnsiStringBuilder& differences, const char* name,
                              uint64_t baseline, uint64_t current)
        {
            if (baseline != current)
            {
                differences.AppendFormat(" %s=%llu/%llu", name,
                                         static_cast<unsigned long long>(baseline),
                                         static_cast<unsigned long long>(current));
            }
        }
    } // namespace

    CombatResetSoakCounts CombatResetSoakDriver::CaptureCounts(GameModeContext& ctx)
    {
        const auto counts = ctx.ScopeRef.CaptureCounts();
        uint64_t voices = 0;
#if defined(NORVES_GAME_AUDIO)
        if (auto* audio = NorvesLib::Modules::Audio::FindAudioModule(Module::GetModuleRegistry()))
        {
            voices = audio->GetAudioService().GetDiagnostics().ActiveVoiceCount;
        }
#endif
        return {counts.RootEntities, counts.Textures, counts.Meshes, counts.Models, counts.PendingAsyncLoads, voices};
    }

    void CombatResetSoakDriver::Fail(GameModeContext& ctx, const char* reason)
    {
        if (m_Phase == Phase::Finished)
        {
            return;
        }
        m_Phase = Phase::Finished;
        NORVES_LOG_INFO("CombatSlice", "GAME_RESET stage=failure reason=%s index=%u", reason, m_Index);
        ctx.EngineRef.RequestExit(1);
    }

    bool CombatResetSoakDriver::CheckCounts(GameModeContext& ctx, const char* stage,
                                             const CombatResetSoakCounts& baseline,
                                             const CombatResetSoakCounts& current, bool bCompareVoices)
    {
        Container::AnsiStringBuilder differences;
        AppendDifference(differences, "roots", baseline.RootEntities, current.RootEntities);
        AppendDifference(differences, "textures", baseline.Textures, current.Textures);
        AppendDifference(differences, "meshes", baseline.Meshes, current.Meshes);
        AppendDifference(differences, "models", baseline.Models, current.Models);
        AppendDifference(differences, "pending", baseline.PendingAsyncLoads, current.PendingAsyncLoads);
        if (bCompareVoices)
        {
            AppendDifference(differences, "voices", baseline.Voices, current.Voices);
        }
        if (differences.Length() == 0)
        {
            return true;
        }

        m_Phase = Phase::Finished;
        NORVES_LOG_INFO("CombatSlice", "GAME_RESET stage=failure reason=count_mismatch stage=%s index=%u%s",
                        stage, m_Index, differences.ToString().c_str());
        ctx.EngineRef.RequestExit(1);
        return false;
    }

    bool CombatResetSoakDriver::Begin(GameModeContext& ctx, CombatSliceResetState& resetState,
                                      const CombatResetSoakCounts& enterCounts)
    {
        m_ResetState = &resetState;
        m_Index = resetState.ResetCount;
        m_Target = resetState.SoakResetTarget;
        m_TickCount = 0;
        m_Phase = Phase::Ready;
        m_StartingWrites = NorvesLib::Core::GEngine.GetSaveService().GetCompletedWriteCount();

        if (m_Target < 2)
        {
            Fail(ctx, "soak_count");
            return false;
        }
        if (m_Index > m_Target)
        {
            Fail(ctx, "soak_index");
            return false;
        }

        LogCounts("enter", m_Index, enterCounts, m_Index == 0 ? " sample=cold" : "");
        auto* module = Gameplay::FindGameplayModule(Module::GetModuleRegistry());
        if (!module)
        {
            Fail(ctx, "begin_gameplay");
            return false;
        }
        const bool bDefeated = module->GetProgressionState().IsDefeated(L::EnemyId);
        if (m_Index == 0 && bDefeated)
        {
            Fail(ctx, "progression_preloaded");
            return false;
        }
        if (m_Index > 0 && !bDefeated)
        {
            Fail(ctx, "restore_missing");
            return false;
        }

        if (m_Index == 0)
        {
            resetState.bHasSoakBaselineEnter = false;
            resetState.bHasSoakBaselineReady = false;
        }
        else if (m_Index == 1)
        {
            resetState.SoakBaselineEnter = enterCounts;
            resetState.bHasSoakBaselineEnter = true;
            LogCounts("baseline", m_Index, enterCounts, " phase=enter");
        }
        else
        {
            if (!resetState.bHasSoakBaselineEnter || !resetState.bHasSoakBaselineReady)
            {
                Fail(ctx, "baseline_missing");
                return false;
            }
            if (!CheckCounts(ctx, "enter", resetState.SoakBaselineEnter, enterCounts, false))
            {
                return false;
            }
        }
        return true;
    }

    bool CombatResetSoakDriver::Tick(GameModeContext& ctx)
    {
        if (m_Phase == Phase::Finished)
        {
            return false;
        }
        ++m_TickCount;

        // 最初の Tick は Enter と同じフレームなので、8 フレーム後は 9 回目。
        if (m_Phase == Phase::Ready && m_TickCount > ReadyFrames)
        {
            const auto readyCounts = CaptureCounts(ctx);
            LogCounts("ready", m_Index, readyCounts, m_Index == 0 ? " sample=cold" : "");
            if (m_Index == 1)
            {
                m_ResetState->SoakBaselineReady = readyCounts;
                m_ResetState->bHasSoakBaselineReady = true;
                LogCounts("baseline", m_Index, readyCounts, " phase=ready");
            }
            else if (m_Index >= 2 && !CheckCounts(ctx, "ready", m_ResetState->SoakBaselineReady, readyCounts, true))
            {
                return false;
            }

            if (m_Index == m_Target)
            {
                m_Phase = Phase::Finished;
                NORVES_LOG_INFO("CombatSlice", "GAME_RESET stage=done result=pass resets=%u", m_Target);
                ctx.EngineRef.RequestExit(0);
                return false;
            }
            if (m_Index == 0)
            {
                auto* module = Gameplay::FindGameplayModule(Module::GetModuleRegistry());
                if (!module)
                {
                    Fail(ctx, "lost_gameplay");
                    return false;
                }
                auto& progression = module->GetProgressionState();
                progression.MarkDefeated(L::EnemyId);
                if (!progression.IsDefeated(L::EnemyId))
                {
                    Fail(ctx, "defeat");
                    return false;
                }
                NORVES_LOG_INFO("CombatSlice", "GAME_RESET stage=defeat index=0");
                m_Phase = Phase::Save;
            }
            else
            {
                m_Phase = Phase::Finished;
                return true;
            }
        }
        else if (m_Phase == Phase::Save)
        {
            auto& saveService = NorvesLib::Core::GEngine.GetSaveService();
            const uint64_t writes = saveService.GetCompletedWriteCount();
            if (writes > m_StartingWrites && !saveService.HasPendingRequests() && !saveService.IsWriteInFlight())
            {
                NORVES_LOG_INFO("CombatSlice", "GAME_RESET stage=saved index=0 writes=%llu",
                                static_cast<unsigned long long>(writes));
                m_Phase = Phase::Finished;
                return true;
            }
        }

        if (m_TickCount >= TimeoutFrames)
        {
            const char* phase = m_Phase == Phase::Ready ? "ready" : "save";
            m_Phase = Phase::Finished;
            NORVES_LOG_INFO("CombatSlice", "GAME_RESET stage=failure reason=timeout phase=%s index=%u", phase, m_Index);
            ctx.EngineRef.RequestExit(1);
        }
        return false;
    }

    void CombatResetSoakDriver::End(GameModeContext& ctx)
    {
        (void)ctx;
        m_ResetState = nullptr;
        m_Phase = Phase::Finished;
    }
} // namespace Game::GameModes
