#pragma once

// ---------------------------------------------------------------------------
// 荒野（wasteland）の小物と建物の配置
//
// Poly Haven の岩・崖と、生成した建物 6 種を、地形と同じ高さ場（TerrainHeightField）の関数で置く。
// 配置は seed と番号だけで決まり、どの実行でも同じになる。
//   - 前哨地: 盆地の中央を北北東へ抜ける道の両側に建物を並べ、表を道へ向ける。道の北の端に石のアーチを門として渡す。
//   - メサ・稜線の足元: 原点から放射状に地形をたどって最初の急な崖を探し、その根元に崖の岩を表を盆地へ向けて据え、
//     脇に大きな岩を添える。崖の岩の背面（スキャンの開いた側）はメサの側へ埋まる。
//   - 盆地: 道と建物の周りを空けて、岩を散らす。
//   - 崖の視点の手前: 崖の足元の落石の素材（角張った岩塊・板状の岩）を、見比べるために 1 つずつ置く（手続きの地形のときだけ）。
// 位置の y は、メッシュの足元の範囲で引いた地形の最も低い高さから、種類ごとの深さだけ埋めた高さ。
// 以上は手続きの地形の配置。DEM の地形（荒野の既定）では、前哨地の建物だけを造成の範囲（WastelandTerrain::kOutpostPads）の
// 中心に置く（高さ場は造成の後の高さを返すので、建物は均した平らな足元に据わる）。
// 夜の集落（night-village）は、同じ DEM の前哨地の候補 B（造成しない自然の平らな谷床）に、東西の道沿いの建物と、
// 街灯・松明の柱・焚き火・井戸・樽・木箱・荷車を置く（BuildNightVillagePlacements）。
// ---------------------------------------------------------------------------

#include "Core/Public/Container/VariableArray.h"
#include "Core/Public/Rendering/Terrain/TerrainHeightField.h"

#include <cstdint>

namespace Game::GameModes::WastelandLayout
{

    /// 置く資産の種類（GetPropKindSpec の並び）
    enum class PropKind : uint32_t
    {
        Boulder02,
        Boulder03,
        Boulder04,
        Boulder05,
        Cliff01,
        Cliff02,
        AdobeRuin,
        Watchtower,
        StoneArch,
        Windmill,
        WaterTower,
        StoneShed,
        /// 崖の足元の落石: 節理で割れた角張った岩塊（Poly Haven の boulder_01）、薄い板状の岩（rock_09）
        TalusCrag,
        TalusSlab,
        /// 夜の集落の小物（生成。Assets/AssetSets/NightVillageModels.json が焼く）
        StreetLamp,
        TorchPost,
        CampfireRing,
        Well,
        Barrel,
        Crate,
        Cart,
        Count
    };

    inline constexpr uint32_t kPropKindCount = static_cast<uint32_t>(PropKind::Count);

    /// 資産 1 種類のクック済みの論理パス
    struct PropKindSpec
    {
        const char *Id;
        const char *MeshPath;
        const char *AlbedoPath;
        const char *NormalPath;
        const char *OrmPath;
        /// 生成した建物・小物（表がローカルの +Z）。偽なら Poly Haven のスキャン
        bool bBuilding;
    };

    const PropKindSpec &GetPropKindSpec(PropKind kind);

    /// 読み込んだメッシュのローカルの範囲[m]と表の向き
    struct PropMeshShape
    {
        bool bAvailable = false;
        float MinX = 0.0f;
        float MaxX = 0.0f;
        float MinY = 0.0f;
        float MaxY = 0.0f;
        float MinZ = 0.0f;
        float MaxZ = 0.0f;
        /// 表の向き（ローカルの XZ で +Z から測った角[rad]）。背面の無いスキャンは開いた側の逆、建物は 0
        float FrontYaw = 0.0f;
    };

    /// 置く 1 つ。Entity の位置・Y 軸まわりの回転[rad]・一様な拡大率
    struct PropPlacement
    {
        PropKind Kind = PropKind::Boulder02;
        float X = 0.0f;
        float Y = 0.0f;
        float Z = 0.0f;
        float Yaw = 0.0f;
        float Scale = 1.0f;
    };

    /// 配置の内訳（ログ用）
    struct LayoutStats
    {
        uint32_t Buildings = 0;
        uint32_t FootRays = 0;
        uint32_t FootHits = 0;
        uint32_t Cliffs = 0;
        uint32_t FootBoulders = 0;
        uint32_t BasinBoulders = 0;
        uint32_t CliffViewSamples = 0;
        /// 夜の集落の小物（街灯・焚き火・樽など）
        uint32_t VillageProps = 0;
    };

    /// 前哨地の道の、岩・草・石を置かない範囲（中心・道の向き（XZ の単位ベクトル）・長さと幅の半分[m]）
    struct RoadClearArea
    {
        float CenterX = 0.0f;
        float CenterZ = 0.0f;
        float AlongX = 0.0f;
        float AlongZ = 1.0f;
        float HalfLength = 0.0f;
        float HalfWidth = 0.0f;
    };

    /// DEM の地形の前哨地の道の、置かない範囲（造成の道の箱と同じ長さ、幅は轍の外の縁まで）
    RoadClearArea GetRoadClearArea();

    /// 崖の足元の落石の種類か（手続きの地形の配置を外しても読み、DEM の地形の崖錐へ散布で置く。WastelandTalus.h）
    bool IsTalusKind(PropKind kind);

    /// 夜の集落だけで置く小物の種類か
    bool IsNightVillagePropKind(PropKind kind);

    /// DEM の地形で読む種類か（崖の足元の落石と前哨地の建物）
    bool IsUsedOnDem(PropKind kind);

    /// 夜の集落で読む種類か（崖の足元の落石と、集落に置く建物・小物）
    bool IsUsedInNightVillage(PropKind kind);

    /// 夜の集落の道と広場の中心[m]（DEM の前哨地の候補 B。Game/Scenes/SceneCatalog.h の kNightVillageViews と同じ座標）
    inline constexpr float kNightVillageCenterX = 1049.5f;
    inline constexpr float kNightVillageCenterZ = -449.5f;

    /// 夜の集落の、草・石を置かない範囲（東西の道と、焚き火の広場）の数
    inline constexpr uint32_t kNightVillageClearAreaCount = 2;

    /// 夜の集落の、草・石を置かない範囲（道・広場）
    void GetNightVillageClearAreas(RoadClearArea (&outAreas)[kNightVillageClearAreaCount]);

    /// 夜の集落の建物と小物の配置を決める（読めた種類だけ。崖錐の岩は WastelandTalus.h が別に置く）
    void BuildNightVillagePlacements(const NorvesLib::Core::Rendering::Terrain::TerrainHeightField &field,
                                     const PropMeshShape (&shapes)[kPropKindCount],
                                     NorvesLib::Core::Container::VariableArray<PropPlacement> &outPlacements,
                                     LayoutStats &outStats);

    /// 読めた種類（shapes[kind].bAvailable）だけを使って配置を決める。bProceduralLayout が偽なら、手続きの地形
    /// （中央の盆地とメサの輪）に合わせた前哨地・足元・盆地の配置と崖の視点の手前の見本の落石を外し、DEM の地形の前哨地の
    /// 建物だけを置く
    void BuildPlacements(const NorvesLib::Core::Rendering::Terrain::TerrainHeightField &field,
                         const PropMeshShape (&shapes)[kPropKindCount], bool bProceduralLayout,
                         NorvesLib::Core::Container::VariableArray<PropPlacement> &outPlacements, LayoutStats &outStats);

} // namespace Game::GameModes::WastelandLayout
