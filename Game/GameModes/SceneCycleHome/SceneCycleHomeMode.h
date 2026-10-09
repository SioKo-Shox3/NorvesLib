#pragma once

#include "Core/Public/GameMode/IGameMode.h"
#include "Core/Public/GameMode/TGameMode.h"

namespace Game::GameModes
{

    /// 何も置かないモードのデータ（持つものは無い）
    struct SceneCycleHomeData
    {
    };

    /**
     * @brief 何も置かない「ホーム」のモードのロジック
     *
     * --scene-cycle-smoke がシーンと行き来する相手。ランチャーができるまでは、
     * シーンを出た後に資源が残らないことを確かめるための空のモードとして使う。
     */
    class SceneCycleHomeRoutine
    {
    public:
        NorvesLib::Core::GameMode::GameModeEnterResult Enter(NorvesLib::Core::GameMode::GameModeContext &, SceneCycleHomeData &)
        {
            return NorvesLib::Core::GameMode::GameModeEnterResult::Succeeded;
        }

        void Tick(NorvesLib::Core::GameMode::GameModeContext &, SceneCycleHomeData &, float)
        {
        }

        void Leave(NorvesLib::Core::GameMode::GameModeContext &, SceneCycleHomeData &,
                   NorvesLib::Core::GameMode::GameModeExitReason)
        {
        }

        static constexpr const char *DebugName = "SceneCycleHome";
    };

    using SceneCycleHomeMode = NorvesLib::Core::GameMode::TGameMode<SceneCycleHomeRoutine, SceneCycleHomeData>;

} // namespace Game::GameModes
