#pragma once

// ---------------------------------------------------------------------------
// 崖の細部の焼き直し（DEM の区画のうち、急な斜面と崖の升目だけを細かい間隔で焼く）
//
// 1 m の DEM では崖が丸く崩れ、垂直な面・段・割れ目が出ない。手本の視点に写る崖の範囲（近景の Battleship Rock・
// Franklin Butte・Rooster Butte・Setting Hen Butte と、遠景の castle の段の far_01 の主役のビュートと奥の崖・画面の左の奥の崖）の
// 急な升目を、面に沿って範囲ごとの間隔（近景 0.25 m・遠景 0.5〜1 m）に細分し、面の法線の向きに地層の凹凸を押し出す:
//   - 硬い層の張り出しと軟らかい層のくぼみ（層の境はシェーダーの地層 Assets/Shaders/Common/TerrainStrata.glsl と同じ関数・同じ
//     基準面で決める。張り出しの形もシェーダーが法線を倒す d(y) そのもの）
//   - 硬い層の面を垂直に立てる段（層の中で外への押し出しを高さとともに増やし、層の上面で引っ込めて棚にする）
//   - 縦の割れ目（水平面の Voronoi の境。垂直の面では縦の溝になる）と、割れ目で区切られた岩の塊の出入り
//   - 崩れた縁（段の張り出しの強さを横方向のノイズで変える）と、細かな凹凸
// 細分しない升目は今までと同じ頂点と三角形のまま。細分した升目と接する升目は、境の辺の点を共有する扇で埋めて
// 継ぎ目を作らない。区画の境の頂点は全体の格子の番号だけから決まるので、隣の区画とビット単位で同じになる。
// 遠景の段は元のメッシュのうち、細分する升目とその周りの升目だけを張り直し、ほかの三角形と頂点は元のまま使う。
// 配置の高さ（岩・建物）は今までの高さ場のまま（TerrainHeightField）で、ここで焼いた形は使わない。
// ---------------------------------------------------------------------------

#include "Container/String.h"
#include "Container/VariableArray.h"
#include "Rendering/Terrain/TerrainHeightField.h"

#include <cstdint>

namespace NorvesLib::Tools::AssetCook
{
    /// 崖の細部の焼き直しの版。形の関数や値を変えたら上げる（クックの元のハッシュに入る）
    inline constexpr uint32_t kTerrainCliffDetailVersion = 5u;

    /// 焼いた頂点（位置・法線・UV。メッシュの頂点の記録と同じ並び）
    struct TerrainCliffVertex
    {
        float Position[3] = {};
        float Normal[3] = {};
        float TexCoord[2] = {};
    };

    /// 区画を焼いた結果の数
    struct TerrainCliffTileStats
    {
        uint32_t RefinedCells = 0;
        uint32_t TransitionCells = 0;
        uint32_t DisplacedVertices = 0;
    };

    /// 遠景の段（Scripts/Terrain/HorizonDem.py の castle の段）の格子。高さは行ごとに Layout.Columns × Layout.Rows 個
    struct TerrainCliffHorizonGrid
    {
        Core::Rendering::Terrain::TerrainGridLayout Layout;
        const float *Heights = nullptr;
    };

    /// 格子の区画（最初の升目 firstColumn・firstRow から cellsX × cellsZ 升目）が、崖の細部を焼く範囲に掛かるか
    bool TerrainCliffTileIntersects(const Core::Rendering::Terrain::TerrainGridLayout &layout, uint32_t firstColumn,
                                    uint32_t cellsX, uint32_t firstRow, uint32_t cellsZ);

    /// 格子の区画を、崖の細部を足して焼く。UV はワールドの XZ を textureTileMeters で割った値。
    /// 頂点の位置・法線が有限でなければ false を返し、outError に理由を書く
    bool BuildTerrainCliffTile(const Core::Rendering::Terrain::TerrainHeightField &field, uint32_t firstColumn,
                               uint32_t cellsX, uint32_t firstRow, uint32_t cellsZ, float textureTileMeters,
                               Core::Container::VariableArray<TerrainCliffVertex> &outVertices,
                               Core::Container::VariableArray<uint32_t> &outIndices, TerrainCliffTileStats &outStats,
                               Core::Container::AnsiString &outError);

    /// 遠景の段の区画が、崖の細部を焼く範囲に掛かるか
    bool TerrainCliffHorizonTileIntersects(const TerrainCliffHorizonGrid &grid, uint32_t firstColumn, uint32_t cellsX,
                                           uint32_t firstRow, uint32_t cellsZ);

    /// 遠景の段の区画のメッシュに、崖の細部を足す。inOutVertices・inOutIndices は区画の元のメッシュ（格子の頂点を行ごとに
    /// (cellsX + 1) × (cellsZ + 1) 個並べたもの）で、細分する升目とその周りの升目だけを張り直したメッシュに置き換える。
    /// 張り直さない升目の三角形と頂点（位置・法線）は元のまま
    bool BuildTerrainCliffHorizonTile(const TerrainCliffHorizonGrid &grid, uint32_t firstColumn, uint32_t cellsX,
                                      uint32_t firstRow, uint32_t cellsZ, float textureTileMeters,
                                      Core::Container::VariableArray<TerrainCliffVertex> &inOutVertices,
                                      Core::Container::VariableArray<uint32_t> &inOutIndices,
                                      TerrainCliffTileStats &outStats, Core::Container::AnsiString &outError);
} // namespace NorvesLib::Tools::AssetCook
