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
//   - 崖の視点の手前: 崖の足元の落石の素材（角張った岩塊・板状の岩塊）を、見比べるために 1 つずつ置く。
// 位置の y は、メッシュの足元の範囲で引いた地形の最も低い高さから、種類ごとの深さだけ埋めた高さ。
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
        /// 崖の足元の落石: 節理の穴の多いごつごつした岩塊（Poly Haven の boulder_01）、平らな面で割れた角張った岩塊（rock_07）、
        /// 薄い板状の岩（rock_09）
        TalusCrag,
        TalusBlock,
        TalusSlab,
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
        /// 生成した建物（表がローカルの +Z）。偽なら Poly Haven のスキャン
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
    };

    /// 前哨地の道の、岩・草を置かない範囲（中心・道の向き（XZ の単位ベクトル）・長さと幅の半分[m]）
    struct RoadClearArea
    {
        float CenterX = 0.0f;
        float CenterZ = 0.0f;
        float AlongX = 0.0f;
        float AlongZ = 1.0f;
        float HalfLength = 0.0f;
        float HalfWidth = 0.0f;
    };

    RoadClearArea GetRoadClearArea();

    /// 崖の足元の落石の種類か（手続きの地形の配置を外しても、DEM の地形の上に置く）
    bool IsTalusKind(PropKind kind);

    /// 読めた種類（shapes[kind].bAvailable）だけを使って配置を決める。bProceduralLayout が偽なら、手続きの地形
    /// （中央の盆地とメサの輪）に合わせた前哨地・足元・盆地の配置を外し、崖の視点の手前の見本の落石だけを置く
    void BuildPlacements(const NorvesLib::Core::Rendering::Terrain::TerrainHeightField &field,
                         const PropMeshShape (&shapes)[kPropKindCount], bool bProceduralLayout,
                         NorvesLib::Core::Container::VariableArray<PropPlacement> &outPlacements, LayoutStats &outStats);

} // namespace Game::GameModes::WastelandLayout
