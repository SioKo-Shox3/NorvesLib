#pragma once

// ---------------------------------------------------------------------------
// sandbox（確認用シーン。開発ビルドだけランチャーに出る）
//
// 物理空と、平らな地面（8 m ごとに繰り返す格子の模様）だけのシーン。自由飛行カメラ（Scenes/FreeCameraRig.h）の
// 動きを撮影で確かめるのに使う。格子は 1 m の升目で、x が 8 m の倍数の升目は赤、z が 8 m の倍数の升目は青、
// 両方が重なる升目は黄にして、動いた向きと距離を画像から読めるようにしてある。
// 視点は SceneCatalog.h の kSandboxViews（overview・low・top）。--camera-script=<file> で入力の台本を流せる。
// 地面を囲む一周のカメラパス（kSandboxPath）を持ち、P キー（ゲームパッドは Y）で再生・停止する。
// ---------------------------------------------------------------------------

#include "Core/Public/Container/String.h"
#include "Core/Public/GameMode/GameModeContext.h"
#include "Core/Public/GameMode/IGameMode.h"
#include "Core/Public/GameMode/TGameMode.h"
#include "Pause/PauseMenu.h"
#include "Scenes/FreeCameraRig.h"
#include "Scenes/SceneViewSwitch.h"

namespace Game::GameModes
{

    struct SandboxData
    {
        /// 起動時の視点の名前（--scene-view。空ならシーンの既定の視点）
        NorvesLib::Core::Container::String m_ViewName;
        /// 入力の台本のパス（--camera-script。空なら流さない）
        NorvesLib::Core::Container::String m_CameraScriptPath;
        /// 起動時にカメラパスの途中へ置く時刻[秒]（--camera-path-time）。負なら置かない
        float m_CameraPathTime = -1.0f;
        /// 組み立てが済んだところからカメラパスを再生する（--camera-path-play）
        bool m_bCameraPathPlay = false;
        /// ポーズメニュー（非所有）。開いている間はカメラを動かさない。無いなら nullptr
        const Game::Pause::PauseMenu *m_pPauseMenu = nullptr;
        /// 実行中の視点の切り替えの依頼（非所有。--capture-views の撮影が使う）。無いなら nullptr
        Game::Scenes::SceneViewSwitch *m_pViewSwitch = nullptr;

        Game::Scenes::FreeCameraRig m_Rig;
        uint32_t m_TickCount = 0;
    };

    class SandboxRoutine
    {
    public:
        NorvesLib::Core::GameMode::GameModeEnterResult Enter(NorvesLib::Core::GameMode::GameModeContext &ctx,
                                                             SandboxData &data);
        void Tick(NorvesLib::Core::GameMode::GameModeContext &ctx, SandboxData &data, float deltaTime);
        void Leave(NorvesLib::Core::GameMode::GameModeContext &ctx, SandboxData &data,
                   NorvesLib::Core::GameMode::GameModeExitReason reason);

        static constexpr const char *DebugName = "Sandbox";
    };

    using SandboxMode = NorvesLib::Core::GameMode::TGameMode<SandboxRoutine, SandboxData>;

} // namespace Game::GameModes
