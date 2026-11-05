// 草原のクック済み地形を TerrainComponent で配置し、谷・尾根・低い視点の見え方を確かめる開発用シーン。
// 視点は SceneCatalog.h の kGrasslandViews（near・valley・low）。自由飛行と撮影時の視点の切り替えに対応する。
// 地形は Assets/TerrainSpecs/Grassland/GrasslandTerrain.json と Scripts/Terrain/GenerateGrasslandHeight.py から
// クックした Assets/Terrain/Grassland/Grassland.nvtrn と Tiles/GrasslandTile_<x>_<z>.generated を使う。
#pragma once

#include "Core/Public/Container/String.h"
#include "Core/Public/GameMode/GameModeContext.h"
#include "Core/Public/GameMode/IGameMode.h"
#include "Core/Public/GameMode/TGameMode.h"
#include "Core/Public/Scatter/ScatterSystem.h"
#include "Pause/PauseMenu.h"
#include "Scenes/FreeCameraRig.h"
#include "Scenes/SceneViewSwitch.h"

namespace NorvesLib::Core::Component
{
    class TerrainComponent;
}

namespace Game::GameModes
{

    struct GrasslandData
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
        const Game::Pause::PauseMenu* m_pPauseMenu = nullptr;
        /// 実行中の視点の切り替えの依頼（非所有。--capture-views の撮影が使う）。無いなら nullptr
        Game::Scenes::SceneViewSwitch* m_pViewSwitch = nullptr;

        Game::Scenes::FreeCameraRig m_Rig;
        /// 地形の配置完了を確かめる非所有の参照。実体はスコープで追跡した Entity が所有する
        NorvesLib::Core::Component::TerrainComponent* m_pTerrain = nullptr;
        /// 散布と、地形の配置を待つ開発用の合成本数。
        NorvesLib::Core::Scatter::ScatterHandle m_Scatter = NorvesLib::Core::Scatter::kInvalidScatterHandle;
        uint32_t m_ScatterStressCount = 0;
        bool m_bScatterPending = false;
        uint32_t m_TickCount = 0;
    };

    class GrasslandRoutine
    {
    public:
        NorvesLib::Core::GameMode::GameModeEnterResult Enter(NorvesLib::Core::GameMode::GameModeContext& ctx,
                                                          GrasslandData& data);
        void Tick(NorvesLib::Core::GameMode::GameModeContext& ctx, GrasslandData& data, float deltaTime);
        void Leave(NorvesLib::Core::GameMode::GameModeContext& ctx, GrasslandData& data,
                   NorvesLib::Core::GameMode::GameModeExitReason reason);

        static constexpr const char* DebugName = "Grassland";
    };

    using GrasslandMode = NorvesLib::Core::GameMode::TGameMode<GrasslandRoutine, GrasslandData>;

} // namespace Game::GameModes
