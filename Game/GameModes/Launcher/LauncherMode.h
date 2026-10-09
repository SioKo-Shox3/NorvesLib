#pragma once

// ---------------------------------------------------------------------------
// ランチャーのゲームモード
//
// 引数なしで Game を起動すると入るモード。背景は物理空の夕景（地物なし）で、その上に ImGui の全画面のメニュー
// （LauncherView）を重ね、選んだシーンの GameMode へ RequestChange する。
// ImGui が使えないビルド（NORVES_ENABLE_IMGUI なし）や ImGui モジュールが無い起動では、メニューを出せないので
// 一覧の先頭のシーンへそのまま入る。
// ---------------------------------------------------------------------------

#include "Core/Public/GameMode/GameModeContext.h"
#include "Core/Public/GameMode/GameModeParams.h"
#include "Core/Public/GameMode/IGameMode.h"
#include "Core/Public/GameMode/TGameMode.h"
#include "GameModes/Launcher/LauncherMenu.h"
#include "Pause/PauseMenu.h"

#if defined(NORVES_ENABLE_IMGUI)
#include "GameModes/Launcher/LauncherView.h"
#endif

namespace NorvesLib::Core
{
    class Entity;
}
namespace NorvesLib::Core::Component
{
    class CameraComponent;
    class SpringArmComponent;
} // namespace NorvesLib::Core::Component

namespace Game::GameModes
{

    struct LauncherData
    {
        /// 選んだシーンの GameMode へ渡す引数
        NorvesLib::Core::GameMode::GameModeParams m_SceneParams;
        LauncherMenu m_Menu;
        /// ポーズメニュー（非所有。GameApplicationHandler が持つ）。ランチャーの間は無効にする。無いなら nullptr
        Game::Pause::PauseMenu *m_pPauseMenu = nullptr;

        NorvesLib::Core::Entity *m_pCameraPivot = nullptr;
        NorvesLib::Core::Entity *m_pCameraObject = nullptr;
        NorvesLib::Core::Component::SpringArmComponent *m_pSpringArm = nullptr;
        NorvesLib::Core::Component::CameraComponent *m_pCamera = nullptr;

        /// Enter 前の RTGI の設定（Leave で戻す）
        bool m_bPreviousRTGI = true;
        bool m_bMenuRegistered = false;
        bool m_bViewRegistered = false;
        /// メニューを出せない（ImGui なし・ImGui モジュールなし）ので、一覧の先頭のシーンへそのまま入る
        bool m_bFallbackToFirstScene = false;
        /// シーンへの切り替えや終了を要求済みか（二重に要求しない）
        bool m_bLeaving = false;
        uint32_t m_TickCount = 0;

#if defined(NORVES_ENABLE_IMGUI)
        LauncherView m_View{&m_Menu};
#endif
    };

    class LauncherRoutine
    {
    public:
        NorvesLib::Core::GameMode::GameModeEnterResult Enter(NorvesLib::Core::GameMode::GameModeContext &ctx,
                                                             LauncherData &data);
        void Tick(NorvesLib::Core::GameMode::GameModeContext &ctx, LauncherData &data, float deltaTime);
        void Leave(NorvesLib::Core::GameMode::GameModeContext &ctx, LauncherData &data,
                   NorvesLib::Core::GameMode::GameModeExitReason reason);

        static constexpr const char *DebugName = "Launcher";
    };

    using LauncherMode = NorvesLib::Core::GameMode::TGameMode<LauncherRoutine, LauncherData>;

} // namespace Game::GameModes
