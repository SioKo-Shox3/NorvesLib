#pragma once

// ---------------------------------------------------------------------------
// wasteland（荒野のシーン）
//
// 高さ場（Core/Public/Rendering/Terrain/TerrainHeightField.h）から AssetCook が焼いた地形の区画
// （Assets/AssetSets/WastelandTerrain.json。4×4 区画で 2 km 四方、格子の間隔 1 m）を仮想化ジオメトリで描き、
// 物理空・太陽・高さフォグを掛ける。材質は Poly Haven の砂（sand_01）1 種。
// 視点は SceneCatalog.h の kWastelandViews（overview・ground・ridge）。視点の高さは、その (x,z) の地形の高さに足す。
// 地形のクック済みが無いときは COOKED_MODEL_MISSING を出し、空と太陽だけのシーンとして組み立てを終える。
// ---------------------------------------------------------------------------

#include "Core/Public/Container/PointerTypes.h"
#include "Core/Public/Container/String.h"
#include "Core/Public/Delegate/Delegate.h"
#include "Core/Public/GameMode/GameModeContext.h"
#include "Core/Public/GameMode/IGameMode.h"
#include "Core/Public/GameMode/TGameMode.h"
#include "Pause/PauseMenu.h"
#include "Scenes/FreeCameraRig.h"

#include <cstdint>

namespace NorvesLib::Core::Asset
{
    class AssetSystem;
}

namespace Game::GameModes
{

    /// 地形の区画の読み込みと材質のテクスチャ（WastelandRoutine.cpp が中身を持つ）
    struct WastelandAssembly;

    struct WastelandData
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
        /// クック済みの資産を引く AssetSystem の窓口
        NorvesLib::Core::Delegate<NorvesLib::Core::Container::TSharedPtr<const NorvesLib::Core::Asset::AssetSystem>>
            m_GetAssetSystem;

        Game::Scenes::FreeCameraRig m_Rig;
        uint32_t m_TickCount = 0;

        // ---- 組み立ての状態（Routine が持つ。Leave で空に戻す） ----
        NorvesLib::Core::Container::TSharedPtr<WastelandAssembly> m_pAssembly;
        /// 地形を置き終えた（または置けないと決まった）Tick。0 ならまだ
        uint32_t m_TerrainSettledTick = 0;
    };

    class WastelandRoutine
    {
    public:
        NorvesLib::Core::GameMode::GameModeEnterResult Enter(NorvesLib::Core::GameMode::GameModeContext &ctx,
                                                             WastelandData &data);
        void Tick(NorvesLib::Core::GameMode::GameModeContext &ctx, WastelandData &data, float deltaTime);
        void Leave(NorvesLib::Core::GameMode::GameModeContext &ctx, WastelandData &data,
                   NorvesLib::Core::GameMode::GameModeExitReason reason);

        static constexpr const char *DebugName = "Wasteland";
    };

    using WastelandMode = NorvesLib::Core::GameMode::TGameMode<WastelandRoutine, WastelandData>;

} // namespace Game::GameModes
