#pragma once

#include "CameraLateUpdate.h"
#include "CombatBenchDriver.h"
#include "CombatSliceActors.h"
#include "CombatSliceReactions.h"
#include "CombatSliceScene.h"
#include "CombatSmokeDriver.h"
#include "EncounterSmokeDriver.h"
#include "EnemySmokeDriver.h"
#include "GameMode/TGameMode.h"
#include "Gameplay/Combat/CombatResponseSettings.h"

namespace Game::GameModes
{
    struct CombatSliceData;

    // 遅い更新の予約中だけ Data を借りる。Leave で借用と予約を無効にする。
    struct CombatSliceLateState : Game::CameraLateUpdateBinding
    {
        CombatSliceData* Data = nullptr;
    };

    struct CombatSliceData
    {
        CombatSliceOptions Options;
        NorvesLib::Core::Container::TWeakPtr<Game::CameraLateUpdateSlot> LateSlot;
        CombatSliceSceneIds Ids;
        CombatSliceEnvironment Environment;
        CombatSliceActors Actors;
        NorvesLib::Core::Container::TUniquePtr<CombatSliceReactions> Reactions;
        NorvesLib::Core::Container::TUniquePtr<CombatBenchDriver> BenchDriver;
        NorvesLib::Core::Container::TUniquePtr<CombatSmokeDriver> CombatDriver;
        NorvesLib::Core::Container::TUniquePtr<EncounterSmokeDriver> EncounterDriver;
        NorvesLib::Core::Container::TUniquePtr<EnemySmokeDriver> EnemyDriver;
        NorvesLib::Core::Container::TSharedPtr<CombatSliceLateState> LateState;
        NorvesLib::Modules::Gameplay::IGameplayModule* GameplayModule = nullptr;
        NorvesLib::Modules::Gameplay::Combat::CombatResponseSettings SavedResponse;
        uint64_t SavedRunSeed = 0;
        bool bResponseSaved = false;
        bool bRunSeedSaved = false;
        bool bExitReported = false;
    };

    class CombatSliceRoutine
    {
    public:
        NorvesLib::Core::GameMode::GameModeEnterResult Enter(
            NorvesLib::Core::GameMode::GameModeContext& ctx, CombatSliceData& data);
        void Tick(NorvesLib::Core::GameMode::GameModeContext& ctx, CombatSliceData& data, float deltaTime);
        void Leave(NorvesLib::Core::GameMode::GameModeContext& ctx, CombatSliceData& data,
                   NorvesLib::Core::GameMode::GameModeExitReason reason);
        static constexpr const char* DebugName = "CombatSlice";

    private:
        static void Stop(NorvesLib::Core::GameMode::GameModeContext& ctx, CombatSliceData& data);
    };

    using CombatSliceMode = NorvesLib::Core::GameMode::TGameMode<CombatSliceRoutine, CombatSliceData>;
} // namespace Game::GameModes
