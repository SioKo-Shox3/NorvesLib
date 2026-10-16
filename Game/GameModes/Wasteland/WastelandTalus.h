#pragma once

// ---------------------------------------------------------------------------
// 荒野（wasteland）の崖錐の岩
//
// 崖の下の崖錐に、崖から剥がれ落ちた砂岩の岩（節理で割れた角張った岩塊 boulder_01 と、薄い板状の岩 rock_09）を群れとして置く。
// 手本（Assets/Reference/ValleyOfTheGods/cliff_01.jpg）の崖錐は、崖の根元と崖錐の上の方に大岩が多く、大岩の周りに小片が集まり、
// 群れの間には岩の無い赤い斜面が残る。崖錐の先の地表へは、転がり出た岩がまばらに散る。
//   - 群れの中心: 地表の分類の地図（surface.png）の崖錐の重みと、露岩に接する崖の根元の重みで出やすさを決め、
//     70 m 前後の斑の値ノイズで空白の帯を残す。崖（急な斜面）には置かない。
//   - 群れ: 大岩 1 つ（無いこともある）と、その周りの小片（角張った岩と板）。小片は斜面の下へずれて広がる。
//   - 向き: 岩塊は 6 面のどれかを下にし、斜面の傾きへ寄せて寝かせる。板は平たいまま斜面に沿わせる。
//   - 高さ: 回したメッシュの底の頂点ごとに地形の高さを引き、どの底の頂点も地面より下になる高さから、大きさに応じて埋める。
// 配置は世界の升目の番号と種だけで決まり、どの実行でも同じになる。置く仕組みは散布の Object（MegaGeometryScatterComponent）で、
// 岩の 1 つ 1 つは Object を持たない軽い行になる。影を落とすのは大岩と転がり出た岩のうち大きい順に上限までで、小片と残りは
// 影を落とさない別の散布にする（高さフォグが有効な間は CSM が投影物を 1 つずつ描くので、影を落とす数を上限で抑える）。
// ---------------------------------------------------------------------------

#include "Core/Public/Component/MegaGeometryScatterComponent.h"
#include "Core/Public/Container/VariableArray.h"
#include "Core/Public/Math/Vector3.h"
#include "Core/Public/Rendering/Terrain/TerrainHeightField.h"

#include <cstdint>

namespace Game::GameModes::WastelandTalus
{

    /// 散布のメッシュの表の並び（MegaScatterInstance::MeshIndex）
    enum class TalusMesh : uint32_t
    {
        /// 節理で割れた角張った岩塊（boulder_01）
        Crag,
        /// 薄い板状の岩（rock_09）
        Slab,
        Count
    };

    inline constexpr uint32_t kTalusMeshCount = static_cast<uint32_t>(TalusMesh::Count);

    /// メッシュ 1 つの形（ローカルの範囲と、底の高さを引くための頂点の標本）
    struct TalusMeshShape
    {
        bool bAvailable = false;
        NorvesLib::Math::Vector3 Min;
        NorvesLib::Math::Vector3 Max;
        /// 頂点の間引いた標本（ローカルの座標）
        NorvesLib::Core::Container::VariableArray<NorvesLib::Math::Vector3> Points;
    };

    /// 岩を置かない円（視点の足元・建物の足元）
    struct TalusExclusion
    {
        float X = 0.0f;
        float Z = 0.0f;
        float Radius = 0.0f;
    };

    /// 配置の内訳（ログ用）
    struct TalusStats
    {
        uint32_t CellsVisited = 0;
        uint32_t Clusters = 0;
        uint32_t MainBoulders = 0;
        uint32_t Fragments = 0;
        uint32_t ToeRocks = 0;
        uint32_t Rejected = 0;
        uint32_t PerMesh[kTalusMeshCount] = {};
        float MaxBoulderHeight = 0.0f;
        /// 影を落とす候補（大岩・転がり出た岩）の数と、影を落とす岩のうち最も小さい高さ[m]
        uint32_t CasterCandidates = 0;
        float MinCasterHeight = 0.0f;
        /// メッシュごとの底の高さを引く頂点の数
        uint32_t SupportPoints[kTalusMeshCount] = {};
    };

    /**
     * @brief 地表の分類の地図の範囲（DEM の格子の中）の崖錐に岩を置き、散布の入力を作る
     * @param shapes 読めたメッシュの形（bAvailable が偽の種類は使わない。岩塊が無ければ何も置かない）
     * @param exclusions 岩を置かない円
     * @param outCasters 影を落とす岩（大岩と、崖錐の先へ転がり出た岩のうち、大きい順に上限まで）
     * @param outFragments 影を落とさない岩（大岩の周りの角張った岩と板、影の上限から溢れた大岩）
     */
    void BuildTalusRocks(const NorvesLib::Core::Rendering::Terrain::TerrainHeightField &field,
                         const NorvesLib::Core::Rendering::Terrain::TerrainSurfaceMask &mask,
                         const TalusMeshShape (&shapes)[kTalusMeshCount],
                         const NorvesLib::Core::Container::VariableArray<TalusExclusion> &exclusions,
                         NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Component::MegaScatterInstance> &outCasters,
                         NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Component::MegaScatterInstance> &outFragments,
                         TalusStats &outStats);

} // namespace Game::GameModes::WastelandTalus
