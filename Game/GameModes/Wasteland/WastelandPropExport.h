#pragma once

// 荒野の小物の形と配置の書き出し（小さな遮蔽の比べ。開発ビルドの --wasteland-gray-material で撮影の保存先があるときだけ使う）。
// 崖錐の岩塊・スキャンの崖の塊・配置表の小物について、クック済みメッシュの最も細かい段（LOD 0 のクラスタ）をメッシュの
// ローカルの座標のまま PLY（binary_little_endian。頂点の位置と法線、三角形。地形の書き出し WastelandTerrainExport と同じ並び）で
// <保存先>/occlusion_props.<名前>.ply に、形の一覧を occlusion_props.manifest.json に、置いた配置（形の名前・位置・四元数・
// 拡大率・影を落とすか）を occlusion_props.placements.csv に書く。間引かず、溶接もしない（描くのと同じ三角形）。
// ワールドの位置 = 位置 + (四元数の共役の回転)(拡大率 ⊙ ローカルの位置)（Entity と散布の行ベクトルのワールド行列と同じ写し方）。
// 読むのは Scripts/Terrain/BlenderOcclusionReference.py（座標の変換はそちらで行う）。
// 地形の書き出しのファイルは Cycles の参照の入力の記録（ReferenceCheck.py の terrain_mesh）に入るので、小物はこのファイルに分ける。

#include "Core/Public/Container/Containers.h"

#include <cstdint>

namespace NorvesLib::Core::Asset
{
    struct CookedMeshData;
}

namespace Game::GameModes::WastelandPropExport
{
    /// 書き出した形 1 つの記録（manifest の 1 行）
    struct MeshRecord
    {
        /// 形の名前（論理パスの末尾から拡張子を除いたもの）
        NorvesLib::Core::Container::String Name;
        NorvesLib::Core::Container::String LogicalPath;
        uint64_t VertexCount = 0;
        uint64_t TriangleCount = 0;
        uint32_t ClusterCount = 0;
        uint32_t LodLevelCount = 0;
        float MaxLodError = 0.0f;
        /// 書いた頂点のローカルの座標の境界の箱[m]
        float BoundsMin[3] = {};
        float BoundsMax[3] = {};
    };

    /// 小物 1 つの配置（placements.csv の 1 行）
    struct PlacementRecord
    {
        /// 形の名前（MeshRecord::Name）
        NorvesLib::Core::Container::String Mesh;
        float Position[3] = {};
        /// 四元数 (x, y, z, w)
        float Rotation[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        float Scale[3] = {1.0f, 1.0f, 1.0f};
        bool bCastShadow = false;
    };

    /// 書き出しの状態（地形の組み立てが持つ）。Directory が空なら何もしない
    struct State
    {
        NorvesLib::Core::Container::String Directory;
        NorvesLib::Core::Container::VariableArray<MeshRecord> Meshes;
        NorvesLib::Core::Container::VariableArray<PlacementRecord> Placements;
        uint32_t FailedMeshes = 0;

        [[nodiscard]] bool IsEnabled() const
        {
            return !Directory.empty();
        }
    };

    /// 論理パスから形の名前を作る
    NorvesLib::Core::Container::String MeshNameFromLogicalPath(const NorvesLib::Core::Container::String &logicalPath);

    /// 形 1 つの LOD 0 を <Directory>/occlusion_props.<名前>.ply に書き、記録を Meshes へ足す（同じ名前は 1 回だけ書く）。
    /// 書けなければ FailedMeshes を数えて false
    bool ExportMesh(State &state, const NorvesLib::Core::Container::String &logicalPath,
                    const NorvesLib::Core::Asset::CookedMeshData &mesh);

    /// 形の一覧（occlusion_props.manifest.json）と配置（occlusion_props.placements.csv）を書き、WASTELAND_PROP_EXPORT の行を出す
    bool WriteRecords(const State &state);
} // namespace Game::GameModes::WastelandPropExport
