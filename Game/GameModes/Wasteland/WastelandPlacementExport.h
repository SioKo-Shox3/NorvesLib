#pragma once

// 置いた物（崖錐の岩・草と低木・石と礫）の配置の書き出し（--wasteland-export-placement=<path>。開発ビルドだけ）。
// 置き方は 1 m の格子の高さ（TerrainHeightField::SampleHeight）を読むので、焼いた地形の面とのずれを
// Scripts/Terrain/CheckPlacement.py が焼いた面の問い合わせ（AssetCook --sample-cooked-terrain）で測る。その入力を JSON で書く。
//   - 崖錐の岩: 置いた全部（影を落とす岩と小片）。形はメッシュの頂点の間引いた標本（ローカルの座標。WastelandTalus と同じ点）
//   - 草と低木・石と礫: カメラの周りだけ撒く散布で全域の数は数億になるので、地形の全域に kSampleLatticeMeters おきの格子の点を
//     取り、各点を含む区画（段ごと）を描画と同じ規則で作って全部書く（区画の石が kMaxScatterPerCell を超える段は等間隔に間引く）
// 物の行は [形の番号, 位置 xyz, 四元数 xyzw, 拡大率 xyz, 根元の高さ]。ワールドの位置 = 位置 + (四元数の共役の回転)(拡大率 ⊙ ローカル)
// （崖錐の岩の散布と同じ写し方。草と石は Yaw を同じ規約の四元数にする）。根元の高さは置き方が地面と見なした高さで、
// 草と低木は株の根元（SampleHeight − 沈め）、石は地面の線（SampleHeight。石の底はそこから沈める）、岩は位置の y。

#include "Core/Public/Component/MegaGeometryScatterComponent.h"
#include "Core/Public/Container/Containers.h"
#include "GameModes/Wasteland/WastelandFoliage.h"
#include "GameModes/Wasteland/WastelandGravel.h"
#include "GameModes/Wasteland/WastelandTalus.h"

namespace Game::GameModes::WastelandPlacementExport
{
    /// 草と低木・石と礫の区画を取る格子の間隔[m]
    inline constexpr float kSampleLatticeMeters = 192.0f;
    /// 書き出す区画 1 つの物の数の上限（超えた区画は等間隔に間引く）
    inline constexpr size_t kMaxScatterPerCell = 128;

    /// 崖錐の岩の入力（形の名前は WastelandPropExport::MeshNameFromLogicalPath と同じ）
    struct TalusInput
    {
        const WastelandTalus::TalusMeshShape *Shapes = nullptr;
        NorvesLib::Core::Container::String MeshNames[WastelandTalus::kTalusMeshCount];
        NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Component::MegaScatterInstance> Instances;
    };

    /// 配置を path へ書き、WASTELAND_PLACEMENT_EXPORT の行を出す。書けなければ理由を出して false
    bool Write(const NorvesLib::Core::Container::String &path,
               const NorvesLib::Core::Rendering::Terrain::TerrainHeightField &field, const TalusInput &talus,
               WastelandFoliage::FoliageScatter &foliage, const WastelandGravel::GravelScatter &gravel);
} // namespace Game::GameModes::WastelandPlacementExport
