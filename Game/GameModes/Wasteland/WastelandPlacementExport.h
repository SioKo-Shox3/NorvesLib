#pragma once

// 置いた物（崖錐の岩・草と低木・石と礫）の配置の書き出し（--wasteland-export-placement=<path>。開発ビルドだけ）。
// 置き方は焼いた面の接地用格子を読み、読めないときは高さ場に戻る。焼いた地形の面とのずれを
// Scripts/Terrain/CheckPlacement.py が焼いた面の問い合わせ（AssetCook --sample-cooked-terrain）で測る。その入力を書く。
//   - 崖錐の岩: 置いた全部（影を落とす岩と小片）。形はメッシュの頂点の間引いた標本（ローカルの座標。WastelandTalus と同じ点）
//   - 草と低木・大きな石（cobble）: DEM の格子に掛かる全部の区画を描画と同じ規則で作って全部書く
//   - 石（stone）・小礫（pebble）: 全域で数億個になるので標本にする。kSampleLatticeMeters おきの格子の点のうち、
//     形の範囲（Scripts/Terrain/ShapeRegions.json）の中の点と、kSampleCoarseStride 個おきの点（範囲の外の粗い格子）を取り、
//     各点を含む区画を間引かずに丸ごと書く
// path の JSON には格子・標本の取り方・形の表を書き、物の行は隣のバイナリ（<path から .json を除いた名前>.objects.bin。
// float32 のリトルエンディアン、1 行 kObjectColumns 個）に書く。
// 物の行は [形の番号, 位置 xyz, 四元数 xyzw, 拡大率 xyz, 根元の高さ]。ワールドの位置 = 位置 + (四元数の共役の回転)(拡大率 ⊙ ローカル)
// （崖錐の岩の散布と同じ写し方。草と石は Yaw を同じ規約の四元数にする）。根元の高さは置き方が地面と見なした高さで、
// 草と低木は株の根元、石は地面の線（石の底はそこから沈める）、岩は位置の y。

#include "Core/Public/Component/MegaGeometryScatterComponent.h"
#include "Core/Public/Container/Containers.h"
#include "GameModes/Wasteland/WastelandFoliage.h"
#include "GameModes/Wasteland/WastelandGravel.h"
#include "GameModes/Wasteland/WastelandTalus.h"

namespace Game::GameModes::WastelandPlacementExport
{
    /// 石と小礫の標本の区画を取る格子の間隔[m]（形の範囲の中）
    inline constexpr float kSampleLatticeMeters = 48.0f;
    /// 形の範囲の外では格子の点をこの数おきに取る（48 m × 4 = 192 m おき）
    inline constexpr int32_t kSampleCoarseStride = 4;
    /// 物の行の数の個数
    inline constexpr size_t kObjectColumns = 12;

    /// 崖錐の岩の入力（形の名前は WastelandPropExport::MeshNameFromLogicalPath と同じ）
    struct TalusInput
    {
        const WastelandTalus::TalusMeshShape *Shapes = nullptr;
        NorvesLib::Core::Container::String MeshNames[WastelandTalus::kTalusMeshCount];
        NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Component::MegaScatterInstance> Instances;
        NorvesLib::Core::Container::VariableArray<WastelandTalus::TalusGroundRejection> GroundRejections;
    };

    /// 配置を path へ書き、WASTELAND_PLACEMENT_EXPORT の行を出す。書けなければ理由を出して false
    bool Write(const NorvesLib::Core::Container::String &path,
               const NorvesLib::Core::Rendering::Terrain::TerrainHeightField &field,
               const NorvesLib::Core::Rendering::Terrain::TerrainPlacementGround *ground, const TalusInput &talus,
               WastelandFoliage::FoliageScatter &foliage, const WastelandGravel::GravelScatter &gravel);
} // namespace Game::GameModes::WastelandPlacementExport
