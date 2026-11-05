// シ者の場面を選び、共通のカメラと場面ごとの状態を所有する専用モード。
#pragma once

#include "Core/Public/Container/PointerTypes.h"
#include "Core/Public/Container/String.h"
#include "Core/Public/GameMode/IGameMode.h"
#include "Core/Public/GameMode/TGameMode.h"
#include "GameModes/ShishaShowcase/ShishaShowcaseCamera.h"
#include "GameModes/ShishaShowcase/ShishaShowcaseScenes.h"

#include <cstdint>

namespace Game::GameModes
{
    struct ShishaShowcaseStageState;

    struct ShishaShowcaseData
    {
        /// Handler が起動引数から入れる場面の名前。
        NorvesLib::Core::Container::String m_SceneName = TEXT("base");
        float m_OrbitDegreesPerSecond = 0.0f;
        float m_StandinScale = 1.0f;
        /// 場面固有の所有状態。共通の土台とは別に保持できる。
        NorvesLib::Core::Container::TSharedPtr<void> m_SceneState;
        NorvesLib::Core::Container::TSharedPtr<ShishaShowcaseStageState> m_StageState;
        ShishaShowcaseCamera m_Camera;
        const ShishaShowcaseSceneEntry* m_pScene = nullptr;
        uint64_t m_TickCount = 0;
        double m_ElapsedSeconds = 0.0;
        bool m_bReady = false;
        /// 場面が ready の印へ足す空白区切りのキーと値。各トークンの内部に空白を入れない。
        char m_ReadyKeyValues[512]{};
    };

    class ShishaShowcaseRoutine
    {
    public:
        NorvesLib::Core::GameMode::GameModeEnterResult Enter(
            NorvesLib::Core::GameMode::GameModeContext& ctx, ShishaShowcaseData& data);
        void Tick(NorvesLib::Core::GameMode::GameModeContext& ctx, ShishaShowcaseData& data, float deltaTime);
        void Leave(NorvesLib::Core::GameMode::GameModeContext& ctx, ShishaShowcaseData& data,
                   NorvesLib::Core::GameMode::GameModeExitReason reason);

        static constexpr const char* DebugName = "ShishaShowcase";
    };

    using ShishaShowcaseMode = NorvesLib::Core::GameMode::TGameMode<ShishaShowcaseRoutine, ShishaShowcaseData>;
} // namespace Game::GameModes
