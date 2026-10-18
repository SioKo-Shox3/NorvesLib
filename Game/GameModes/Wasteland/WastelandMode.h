#pragma once

// ---------------------------------------------------------------------------
// wasteland（荒野のシーン）
//
// Valley of the Gods（ユタ州）の 1 m DEM を切り出した格子（Assets/Terrain/ValleyOfTheGods/height.json。4.7 × 4.6 km、
// 東が +X・北が -Z）から AssetCook が焼いた地形の区画（Assets/AssetSets/WastelandTerrain.json の WastelandDem_<x>_<z>。
// 10 × 10 区画）を仮想化ジオメトリで描き、物理空・太陽・高さフォグ・空気遠近・雲の層を掛ける。区画は 1 つ 37 MB 前後あるので、
// 材質のテクスチャが届いてから Tick ごとに数区画ずつ読んで置き、置いたら解析したメッシュを手放す。
// 材質は Poly Haven の砂（sand_01）・乾いた土（dry_ground_rocks）・岩肌（rock_face）の 3 層を、傾斜・高さ・低周波ノイズと凹凸で混ぜる
// （MegaGeometry::MegaTerrainBlend）。高さ場は同じ格子を TerrainHeightField::LoadGrid で 1 回だけ読み、シーンの間だけ持つ。
// 岩・崖・建物の配置（WastelandLayout.h）は手続きの地形に合わせたものなので、前哨地を置き直す（SCN-WL-OUTPOST）まで置かない。
// 崖の下の崖錐には、剥がれ落ちた岩の群れを散布の Object 1 つで置く（WastelandTalus.h）。
// 近景の礫の地面と沢底には、カメラの周りだけ石と礫を置き、遠くは地形の材質の礫の層に任せる（WastelandGravel.h）。
// 光は夕方寄りの低い西日で、カメラのトーンマップ・グレーディングを夕景に合わせる。
// 視点は SceneCatalog.h の kWastelandViews（overview・ground・ridge・cliff・outpost）。視点の高さは、その (x,z) の地形の高さに足す。
// 撮影の視点の切り替え（Scenes/SceneViewSwitch.h）では、地形を読み直さずにカメラ・画角・太陽だけを置き直す。
// 地形の格子・区画のクック済みが無いときは COOKED_MODEL_MISSING を出し、空と太陽だけのシーンとして組み立てを終える。
// ---------------------------------------------------------------------------

#include "Core/Public/Container/PointerTypes.h"
#include "Core/Public/Container/String.h"
#include "Core/Public/Delegate/Delegate.h"
#include "Core/Public/GameMode/GameModeContext.h"
#include "Core/Public/GameMode/IGameMode.h"
#include "Core/Public/GameMode/TGameMode.h"
#include "Core/Public/Rendering/FoliageTypes.h"
#include "GameModes/Wasteland/WastelandCommonLight.h"
#include "GameModes/Wasteland/WastelandFoliage.h"
#include "GameModes/Wasteland/WastelandGravel.h"
#include "Pause/PauseMenu.h"
#include "Scenes/FreeCameraRig.h"
#include "Scenes/SceneViewSwitch.h"

#include <cstdint>

namespace NorvesLib::Core::Asset
{
    class AssetSystem;
}

namespace NorvesLib::Core::Rendering::Terrain
{
    class TerrainHeightField;
    class TerrainSurfaceMask;
}

namespace Game::GameModes
{
    /// 比較のために荒野から外すもの（--wasteland-hide=<名前を + でつないだもの、または all>）。地形だけの撮影などに使う
    namespace WastelandHide
    {
        enum Flag : uint32_t
        {
            /// 雲の層
            Clouds = 1u << 0u,
            /// 草・低木
            Foliage = 1u << 1u,
            /// 崖錐の岩
            Rocks = 1u << 2u,
            /// 近景の石と礫
            Gravel = 1u << 3u,
            /// 前哨地の建物
            Buildings = 1u << 4u,
            /// 空気遠近
            Aerial = 1u << 5u,
            /// 高さフォグ
            Fog = 1u << 6u,
            All = (1u << 7u) - 1u,
        };

        /// "clouds+foliage" のような名前の並びか "all" を読む。知らない名前・空の名前があれば outError に入れて false
        bool Parse(const NorvesLib::Core::Container::String &value, uint32_t &outMask,
                   NorvesLib::Core::Container::String &outError);
    } // namespace WastelandHide

    /// 遠景のぼけの切り分けの撮影で替えるアンチエイリアシング（--wasteland-aa。開発ビルドだけ）
    enum class WastelandAntiAliasing : uint8_t
    {
        /// 既定（FXAA）
        Default,
        Fxaa,
        TemporalAA,
        /// FXAA のパスを外す（起動引数の解析がポストのパスを止め、カメラは FXAA の指定のまま）
        None,
    };

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
        /// 実行中の視点の切り替えの依頼（非所有。--capture-views の撮影が使う）。無いなら nullptr
        Game::Scenes::SceneViewSwitch *m_pViewSwitch = nullptr;
        /// クック済みの資産を引く AssetSystem の窓口
        NorvesLib::Core::Delegate<NorvesLib::Core::Container::TSharedPtr<const NorvesLib::Core::Asset::AssetSystem>>
            m_GetAssetSystem;

        Game::Scenes::FreeCameraRig m_Rig;
        uint32_t m_TickCount = 0;
        /// 手本の構図の視点で替える前のカメラの縦の画角[度]（視点を置き直すときに戻す）
        float m_DefaultFieldOfView = 0.0f;

        // ---- 組み立ての状態（Routine が持つ。Leave で空に戻す） ----
        NorvesLib::Core::Container::TSharedPtr<WastelandAssembly> m_pAssembly;
        /// 地形の高さ場（DEM の格子。Enter で読み、Leave で手放す。読めないときは空で、視点の地面の高さを 0 とする）
        NorvesLib::Core::Container::TSharedPtr<const NorvesLib::Core::Rendering::Terrain::TerrainHeightField> m_pHeightField;
        /// 地表の分類の地図（草・低木と崖錐の岩を撒く所を決める。Enter で読み、Leave で手放す。読めないときは空）
        NorvesLib::Core::Container::TSharedPtr<const NorvesLib::Core::Rendering::Terrain::TerrainSurfaceMask> m_pSurfaceMask;
        /// 地形を置き終えた（または置けないと決まった）Tick。0 ならまだ
        uint32_t m_TerrainSettledTick = 0;
        /// 草・低木の束と風（カメラの周りの区画が変わるたびに m_FoliageScatter が作り直し、RenderWorld へ渡す）
        NorvesLib::Core::Rendering::FoliageScene m_Foliage;
        /// 草・低木を地形へ撒く区画の持ち主（Enter で高さ場と地表の分類の地図を渡し、Leave で手放す）
        Game::GameModes::WastelandFoliage::FoliageScatter m_FoliageScatter;
        /// 近景の石と礫の束（カメラの周りの区画が変わるたびに m_GravelScatter が作り直し、草・低木の束と並べて渡す）
        NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Rendering::FoliageBatch> m_GravelBatches;
        /// 石と礫を置く区画の持ち主（Enter で高さ場と地表の分類の地図を渡し、Leave で手放す）
        Game::GameModes::WastelandGravel::GravelScatter m_GravelScatter;
        /// 共通の照明と灰色の板（--wasteland-common-light・--common-light-case・--wasteland-gray-plate。開発ビルドだけ）。
        /// Opts は起動引数から入り、残りは Enter が作って Leave で戻す
        Game::GameModes::WastelandCommonLight::State m_CommonLight;
        /// 外すもの（WastelandHide::Flag の組み合わせ。--wasteland-hide。0 なら既定の見た目）
        uint32_t m_HideMask = 0;
        /// 地形を最も細かい段で描く（--wasteland-finest-lod。開発ビルドだけ。閾値は起動引数の解析が描画側へ渡し、ここはログの印だけ）
        bool m_bFinestLod = false;
        /// 地形の区画の LOD 0 を PLY で書き出すフォルダ（--wasteland-export-terrain。開発ビルドだけ。空なら書かない。
        /// WastelandTerrainExport.h）
        NorvesLib::Core::Container::String m_TerrainExportDir;
        /// 遠景のぼけの切り分けの撮影（開発ビルドだけ）: アンチエイリアシング（--wasteland-aa）と内部解像度の倍率
        /// （--wasteland-render-scale。0.5〜1、1 なら替えない。Leave で 1 に戻す）
        WastelandAntiAliasing m_AntiAliasing = WastelandAntiAliasing::Default;
        float m_RenderScale = 1.0f;
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
