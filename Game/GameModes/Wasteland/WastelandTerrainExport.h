#pragma once

// 荒野の地形のメッシュの書き出し（正解の絵との比較用。開発ビルドの起動引数 --wasteland-export-terrain=<フォルダ> でだけ使う）。
// 置いた地形の区画（近景の DEM の区画と遠景の段。崖の細部はクッカーが区画に焼き込んでいる）ごとに、クック済みメッシュの
// 最も細かい段（LOD 0 のクラスタ。エンジンが --wasteland-finest-lod で描く段）を、エンジンのワールド座標（m、+X 東・+Y 上・
// +Z 南。原点と標高の基準は Assets/Terrain/ValleyOfTheGods/height.json の origin と height_offset_m）のまま、
// PLY（binary_little_endian。頂点の位置と法線、三角形）で <区画の名前>.ply に書く。間引かず、溶接もしない（描くのと同じ三角形）。
// Blender の台本 Scripts/Terrain/BlenderReference.py が読み、座標の変換はそちらの 1 か所で行う。
// 全区画を書くので、範囲は各視点の視錐台・影の届く 4 km・空の遮蔽の届く範囲を含む。
// 全区画を置き終えたら、区画の一覧（ファイル・頂点と三角形の数・境界の箱）を manifest.json に書く。フォルダは呼ぶ側が作っておく。

#include "Core/Public/Container/Containers.h"

#include <cstdint>

namespace NorvesLib::Core::Asset
{
    struct CookedMeshData;
}

namespace Game::GameModes::WastelandTerrainExport
{
    /// 書き出した区画 1 つの記録（manifest.json の 1 行）
    struct TileRecord
    {
        /// 区画の名前（論理パスの末尾から拡張子を除いたもの。WastelandDem_3_4・WastelandHorizon_castle_0_0 など）
        NorvesLib::Core::Container::String Name;
        /// クック済みメッシュの論理パス
        NorvesLib::Core::Container::String LogicalPath;
        uint64_t VertexCount = 0;
        uint64_t TriangleCount = 0;
        /// LOD 0 のクラスタの数と、クック済みメッシュの段の数
        uint32_t ClusterCount = 0;
        uint32_t LodLevelCount = 0;
        /// LOD 0 のクラスタの誤差の最大（最も細かい段なので 0 のはず）
        float MaxLodError = 0.0f;
        /// 書いた頂点のワールド座標の境界の箱[m]
        float BoundsMin[3] = {};
        float BoundsMax[3] = {};
    };

    /// 書き出しの状態（地形の組み立てが持つ）。Directory が空なら何もしない
    struct State
    {
        NorvesLib::Core::Container::String Directory;
        NorvesLib::Core::Container::VariableArray<TileRecord> Tiles;
        uint32_t FailedTiles = 0;

        [[nodiscard]] bool IsEnabled() const
        {
            return !Directory.empty();
        }
    };

    /// 区画 1 つの LOD 0 を <Directory>/<名前>.ply に書き、記録を Tiles へ足す。書けなければ FailedTiles を数えて false
    bool ExportTile(State &state, const NorvesLib::Core::Container::String &logicalPath,
                    const NorvesLib::Core::Asset::CookedMeshData &mesh);

    /// 全区画を書き終えたところで <Directory>/manifest.json を書き、WASTELAND_TERRAIN_EXPORT の行をログへ出す
    bool WriteManifest(const State &state);
} // namespace Game::GameModes::WastelandTerrainExport
