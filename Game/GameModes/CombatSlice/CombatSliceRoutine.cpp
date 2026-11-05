#include "CombatSliceMode.h"

#include "GameModes/GameModeIds.h"
#include "Save/ProgressionSaveBridge.h"
#include "Asset/AssetFileReader.h"
#include "Engine/Engine.h"
#include "Engine/NorvesEngine.h"
#include "GameMode/GameModeContext.h"
#include "GameMode/GameModeScope.h"
#include "GameMode/IGameModeController.h"
#include "Gameplay/IGameplayModule.h"
#include "Gameplay/Progression/ProgressionState.h"
#include "Logging/LogMacros.h"
#include "Save/SaveService.h"

#if defined(NORVES_GAME_AUDIO)
#include "Audio/AudioGameModeOwner.h"
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
        const char* FailurePrefix(CombatSliceSmoke smoke)
        {
            switch (smoke)
            {
            case CombatSliceSmoke::Encounter:
                return "ENCOUNTER_SMOKE";
            case CombatSliceSmoke::Enemy:
                return "ENEMY_SMOKE";
            case CombatSliceSmoke::ResetSoak:
                return "GAME_RESET";
            default:
                return "COMBAT_SMOKE";
            }
        }
    } // namespace

    void CombatSliceRoutine::Stop(GameModeContext& ctx, CombatSliceData& data)
    {
        // ドライバが借りる Component は ScopeRef の片付けまで存続させる。
        if (data.BenchDriver)
        {
            data.BenchDriver->End(ctx);
            data.BenchDriver.reset();
        }
        if (data.CombatDriver)
        {
            data.CombatDriver->End(ctx);
            data.CombatDriver.reset();
        }
        if (data.EncounterDriver)
        {
            data.EncounterDriver->End(ctx);
            data.EncounterDriver.reset();
        }
        if (data.EnemyDriver)
        {
            data.EnemyDriver->End(ctx);
            data.EnemyDriver.reset();
        }
        if (data.Reactions)
        {
            data.Reactions->Unbind(ctx);
            data.Reactions.reset();
        }
        if (data.LateState)
        {
            data.LateState->Data = nullptr;
            data.LateState.reset();
        }
        if (auto slot = data.LateSlot.lock())
        {
            slot->Reset();
        }

        auto* module = Gameplay::FindGameplayModule(Module::GetModuleRegistry());
        if (module && module == data.GameplayModule)
        {
            if (data.bResponseSaved)
            {
                module->SetCombatResponseSettings(data.SavedResponse);
            }
            if (data.bRunSeedSaved)
            {
                module->SetRunSeed(data.SavedRunSeed);
            }
        }
        data.bResponseSaved = false;
        data.bRunSeedSaved = false;
        data.GameplayModule = nullptr;

        ctx.ScopeRef.Cleanup();
        ReleaseCombatSliceActors(ctx, data.Actors);
        ReleaseCombatSliceEnvironment(ctx, data.Environment);
        data.Ids = {};
    }

    GameModeEnterResult CombatSliceRoutine::Enter(GameModeContext& ctx, CombatSliceData& data)
    {
        bool bSucceeded = false;
        data.bExitReported = false;
        struct Guard
        {
            GameModeContext& Context;
            CombatSliceData& Data;
            bool& bSucceeded;

            ~Guard()
            {
                if (!bSucceeded)
                {
                    if (!Data.bExitReported)
                    {
                        NORVES_LOG_INFO("CombatSlice", "%s stage=failure reason=setup", FailurePrefix(Data.Options.Smoke));
                        Context.EngineRef.RequestExit(1);
                        Data.bExitReported = true;
                    }
                    CombatSliceRoutine::Stop(Context, Data);
                }
            }
        } guard{ctx, data, bSucceeded};

#if defined(NORVES_GAME_AUDIO)
        ctx.ScopeRef.AddCleanup(GameModeCleanupPhase::BeforeEntities, []
        {
            if (auto* audio = NorvesLib::Modules::Audio::FindAudioModule(Module::GetModuleRegistry()))
            {
                // 後始末の直後に voice の数を検査するため即時停止を要求する（現実装では最短フェードに補正）。
                audio->GetSoundSystem().StopOwner(
                    NorvesLib::Modules::Audio::GetGameModeOwnerKey(CombatSlice),
                    NorvesLib::Modules::Audio::SoundFade{0.0f});
            }
        });
        ctx.ScopeRef.AddLeakCounter("voices", []() -> uint64_t
        {
            if (auto* audio = NorvesLib::Modules::Audio::FindAudioModule(Module::GetModuleRegistry()))
            {
                return audio->GetAudioService().GetDiagnostics().ActiveVoiceCount;
            }
            return 0;
        });
#endif

        auto* module = Gameplay::FindGameplayModule(Module::GetModuleRegistry());
        if (!module)
        {
            return GameModeEnterResult::Failed;
        }
        data.GameplayModule = module;

        Asset::AssetReadRequest request;
        request.InputPath = CombatSliceLayout::ProgressionPath;
        Asset::AssetFileReader reader;
        const auto definition = reader.Read(request);
        if (!definition.Succeeded())
        {
            return GameModeEnterResult::Failed;
        }
        Container::String text;
        if (definition.Blob.GetSize() != 0)
        {
            text.append(reinterpret_cast<const char*>(definition.Blob.GetData()), definition.Blob.GetSize());
        }
        if (!module->GetProgressionState().LoadDefinitionFromJson(text))
        {
            return GameModeEnterResult::Failed;
        }

        // 初回 Enter では、前の起動の自動保存による既存スモークの progression_preloaded を避けるため読まない。
        if (data.ResetState && data.ResetState->bRestoreFromLastSave)
        {
            data.ResetState->bRestoreFromLastSave = false;
            if (!Game::Save::ClearProgression(module->GetProgressionState()))
            {
                return GameModeEnterResult::Failed;
            }
            const auto result = NorvesLib::Core::GEngine.GetSaveService().Load(0);
            NORVES_LOG_INFO("CombatSlice", "GAME_RESET stage=restore load=%u reset=%u defeated=%d",
                            static_cast<unsigned>(result.Status), data.ResetState->ResetCount,
                            module->GetProgressionState().IsDefeated(L::EnemyId) ? 1 : 0);
        }

        data.SavedRunSeed = module->GetRunSeed();
        data.bRunSeedSaved = true;
        module->SetRunSeed(0x5EED);
        if (!data.Options.CapturePath.empty() && !data.Options.bBench)
        {
            data.SavedResponse = module->GetCombatResponseSettings();
            data.bResponseSaved = true;
            auto response = data.SavedResponse;
            response.bDebugDraw = true;
            module->SetCombatResponseSettings(response);
        }

        if (!BuildCombatSliceEnvironment(ctx, data.Ids, data.Environment) ||
            !BuildCombatSliceActors(ctx, data.Ids, data.Actors))
        {
            return GameModeEnterResult::Failed;
        }
        data.Reactions = Container::MakeUnique<CombatSliceReactions>();
        if (!data.Reactions->Bind(ctx, data.Ids))
        {
            return GameModeEnterResult::Failed;
        }

        data.LateState = Container::MakeShared<CombatSliceLateState>();
        data.LateState->Data = &data;
        const Container::TWeakPtr<CombatSliceLateState> weak = data.LateState;
        auto* engine = &ctx.EngineRef;
        data.LateState->Callback = [weak, engine](float lateDt)
        {
            if (const auto state = weak.lock(); state && state->Data && state->Data->Reactions)
            {
                state->Data->Reactions->TickDecalsAndSubmit(lateDt, engine->GetRenderWorld());
            }
        };

        bool bDriverStarted = false;
        switch (data.Options.Smoke)
        {
        case CombatSliceSmoke::Combat:
            if (data.Options.bBench)
            {
                data.BenchDriver = Container::MakeUnique<CombatBenchDriver>();
                bDriverStarted = data.BenchDriver->Begin(ctx, data.Ids, data.Options);
            }
            else
            {
                data.CombatDriver = Container::MakeUnique<CombatSmokeDriver>();
                bDriverStarted = data.CombatDriver->Begin(ctx, data.Ids, data.Options);
            }
            break;
        case CombatSliceSmoke::Encounter:
            data.EncounterDriver = Container::MakeUnique<EncounterSmokeDriver>();
            bDriverStarted = data.EncounterDriver->Begin(ctx, data.Ids, data.Options);
            break;
        case CombatSliceSmoke::Enemy:
            data.EnemyDriver = Container::MakeUnique<EnemySmokeDriver>();
            bDriverStarted = data.EnemyDriver->Begin(ctx, data.Ids, data.Options);
            break;
        default:
            return GameModeEnterResult::Failed;
        }
        if (!bDriverStarted)
        {
            // Begin が終了を要求済みなら、その失敗のマーカーを重ねない。
            data.bExitReported = ctx.EngineRef.IsExitRequested();
            return GameModeEnterResult::Failed;
        }

        data.bLastDefeated = module->GetProgressionState().IsDefeated(L::EnemyId);
        bSucceeded = true;
        return GameModeEnterResult::Succeeded;
    }

    void CombatSliceRoutine::Tick(GameModeContext& ctx, CombatSliceData& data, float deltaTime)
    {
        if (data.Reactions)
        {
            data.Reactions->ApplyPendingDamage(ctx);
            data.Reactions->TickPresenter(deltaTime);
        }
        if (data.BenchDriver)
        {
            data.BenchDriver->Tick(ctx, deltaTime);
        }
        else if (data.CombatDriver)
        {
            data.CombatDriver->Tick(ctx, deltaTime);
        }
        else if (data.EncounterDriver)
        {
            data.EncounterDriver->Tick(ctx, deltaTime);
        }
        else if (data.EnemyDriver)
        {
            data.EnemyDriver->Tick(ctx, deltaTime);
        }
        if (data.GameplayModule)
        {
            const bool bDefeated = data.GameplayModule->GetProgressionState().IsDefeated(L::EnemyId);
            if (!data.bLastDefeated && bDefeated)
            {
                NorvesLib::Core::GEngine.GetSaveService().RequestAutosave("defeat");
            }
            data.bLastDefeated = bDefeated;
        }
        if (auto slot = data.LateSlot.lock())
        {
            slot->Arm(data.LateState);
        }
    }

    bool CombatSliceRoutine::RequestResetFromLastSave(GameModeContext& ctx, CombatSliceData& data, const char* reason)
    {
        if (!data.ResetState)
        {
            NORVES_LOG_WARNING("CombatSlice", "共有のリセット状態がないため、最後のセーブからリセットできません。");
            return false;
        }

        auto& saveService = NorvesLib::Core::GEngine.GetSaveService();
        if (saveService.HasPendingRequests())
        {
            // 死亡からのリセットは読み込みの切れ目なので、保存の完了を待ってよい。
            saveService.FlushAndWait();
        }
        data.ResetState->bRestoreFromLastSave = true;
        ++data.ResetState->ResetCount;
        NORVES_LOG_INFO("CombatSlice", "GAME_RESET stage=death reason=%s reset=%u",
                        reason ? reason : "unknown", data.ResetState->ResetCount);
        ctx.ControllerRef.RequestReset(CombatSlice);
        return true;
    }

    void CombatSliceRoutine::Leave(GameModeContext& ctx, CombatSliceData& data, GameModeExitReason)
    {
        Stop(ctx, data);
    }
} // namespace Game::GameModes
