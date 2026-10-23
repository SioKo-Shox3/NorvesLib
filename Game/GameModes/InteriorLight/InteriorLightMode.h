#pragma once

// ---------------------------------------------------------------------------
// interior-light（室内の光）
//
// 12 × 8 × 5 m の部屋（樫の板張りの床・温かい白の漆喰の壁と天井・北の端の赤いレンガの壁・濃い色の木の梁）の西の長い壁に
// 大きな窓の開口を 3 つ開け、方位 240°・高さ 15° の低い夕日を差し込ませる。部屋の大きさ・窓・太陽・家具の並びは
// Docs/RenderingValidation/InteriorLightReference.md の「写真から決めた部屋」の値で、座標は部屋の床の南西の角を原点に、
// x を窓の壁に沿って北へ、z を窓の壁から部屋の内側（東）へ、y を上へ取る（m）。窓の外（z < 0）には地面と空が見える。
// 生成した家具（SCN-GEN-FURNITURE-1・2 の 7 種。Assets/AssetSets/InteriorLightModels.json が焼く）は仮想化ジオメトリで置く。
// 部屋の材質のテクスチャは Scripts/FetchPolyHavenTextures.ps1 -Set interior で落とし、アルベドは
// Scripts/MakeInteriorLightAlbedo.py が書く単位のアルベドを張る（Assets/AssetSets/InteriorLightTextures.json が焼く）。
// 焼いていないテクスチャ・メッシュは読まず、INTERIOR_LIGHT_ASSET_MISSING を出して無地のまま描く・置かない。
// 視点は SceneCatalog.h の kInteriorLightViews（overview・window・corner）。
// ---------------------------------------------------------------------------

#include "Core/Public/Asset/AssetSystem.h"
#include "Core/Public/Asset/CookedMeshFormat.h"
#include "Core/Public/Container/Containers.h"
#include "Core/Public/Container/PointerTypes.h"
#include "Core/Public/Container/String.h"
#include "Core/Public/Delegate/Delegate.h"
#include "Core/Public/GameMode/GameModeContext.h"
#include "Core/Public/GameMode/IGameMode.h"
#include "Core/Public/GameMode/TGameMode.h"
#include "Core/Public/Rendering/MaterialTypes.h"
#include "Core/Public/Rendering/MegaGeometry/GeometryPageSource.h"
#include "Pause/PauseMenu.h"
#include "Scenes/FreeCameraRig.h"
#include "Scenes/SceneViewSwitch.h"

#include <cstdint>

namespace Game::GameModes
{

    /// 読み込み中の部屋の材質 1 つ。テクスチャがそろったら TargetMaterial を CreateData で更新する
    struct InteriorPendingMaterial
    {
        NorvesLib::Core::Rendering::MaterialHandle TargetMaterial;
        NorvesLib::Core::Rendering::MaterialCreateData CreateData;
        uint32_t PendingTextureCount = 0;
        bool bCancelled = false;
    };

    /// 家具 1 種の読み込み（クック済みのメッシュと材質のテクスチャ 3 枚）。テクスチャがそろったら Tick が MegaMesh を作って置く
    struct InteriorFurnitureLoad
    {
        /// kFurnitureKinds の番号
        uint32_t KindIndex = 0;
        NorvesLib::Core::Container::TSharedPtr<NorvesLib::Core::Asset::CookedMeshData> Mesh;
        NorvesLib::Core::Container::TSharedPtr<NorvesLib::Core::Rendering::MegaGeometry::IGeometryPageSource> PageSource;
        NorvesLib::Core::Rendering::MaterialCreateData Material;
        /// メッシュの縦の範囲（拡大前のローカル）。床に載せる高さを決める
        float MinY = 0.0f;
        uint32_t PendingTextureCount = 0;
        bool bCancelled = false;
        /// 置き終えた（置けなかったときも立てる）
        bool bDone = false;
    };

    struct InteriorLightData
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
        /// 読み込み中・読み込み済みの部屋の材質。Leave で読み込みの結果を捨てる印を立てる
        NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Container::TSharedPtr<InteriorPendingMaterial>>
            m_PendingMaterials;
        /// テクスチャがそろっていない部屋の材質の数
        uint32_t m_PendingMaterialCount = 0;
        /// 家具の読み込み（種類ごと）
        NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Container::TSharedPtr<InteriorFurnitureLoad>>
            m_FurnitureLoads;
        /// 置いた家具の数
        uint32_t m_PlacedFurniture = 0;
        /// 組み立ての完了を一度だけ記録した
        bool m_bAssemblyLogged = false;
    };

    class InteriorLightRoutine
    {
    public:
        NorvesLib::Core::GameMode::GameModeEnterResult Enter(NorvesLib::Core::GameMode::GameModeContext &ctx,
                                                             InteriorLightData &data);
        void Tick(NorvesLib::Core::GameMode::GameModeContext &ctx, InteriorLightData &data, float deltaTime);
        void Leave(NorvesLib::Core::GameMode::GameModeContext &ctx, InteriorLightData &data,
                   NorvesLib::Core::GameMode::GameModeExitReason reason);

        static constexpr const char *DebugName = "InteriorLight";
    };

    using InteriorLightMode = NorvesLib::Core::GameMode::TGameMode<InteriorLightRoutine, InteriorLightData>;

} // namespace Game::GameModes
