#pragma once

// ---------------------------------------------------------------------------
// 荒野（wasteland）の崖の面に貼るスキャンの塊の配置表
//
// 近くの崖の面は、写真測量でスキャンした岩の塊（Poly Haven の rock_face_01・02 など）を Blender で地形の上に置いて作る。
// 置いた結果（素材・位置・向き・大きさ）は配置表（Scripts/Terrain/CliffScanPlacements.json。書くのは
// Scripts/Terrain/BlenderCliffScan.py）に書き出し、荒野のシーンが起動のたびに読んで、塊ごとに仮想化ジオメトリ
// （MegaGeometryComponent）の Entity を置く。配置を変えても地形の焼き直しは要らず、要るのは配置表と素材のクックだけ。
//
// 配置表の形（version 1）:
//   assets:     [{ id, mesh, albedo, normal, orm }]  素材の id とクック済みのメッシュ・テクスチャの論理パス
//   placements: [{ name, asset, position, rotation_xyzw, scale }]
//     position      エンジンのワールドの位置[m]（+X 東・+Y 上・+Z 南）
//     rotation_xyzw Entity::SetRotation へそのまま渡す四元数。Entity の行ベクトルのワールド行列は、ローカルの向きを
//                   この四元数の標準の（列ベクトルの）回転の逆で写す（Y 軸まわりの角 a の四元数は、ローカルの (x, z) を
//                   (x cos a − z sin a, z cos a + x sin a) へ写す。WastelandLayout.cpp の建物と同じ）
//     scale         一様な拡大率
// ---------------------------------------------------------------------------

#include "Core/Public/Container/String.h"
#include "Core/Public/Container/VariableArray.h"
#include "Core/Public/Math/Quaternion.h"
#include "Core/Public/Math/Vector3.h"

#include <cstdint>

namespace Game::GameModes::WastelandCliffScan
{

    /// 配置表の場所（リポジトリの直下からの相対パス）
    inline constexpr const char *kTableRelativePath = "Scripts/Terrain/CliffScanPlacements.json";

    /// 配置表に書ける素材の数の上限（読み込みの結果がシーンの組み立ての中の固定の枠を指すので、枠の数を決めておく）
    inline constexpr uint32_t kMaxAssets = 8;

    /// 素材 1 つ（クック済みのメッシュと、アルベド・法線・ORM のテクスチャの論理パス）
    struct AssetSpec
    {
        NorvesLib::Core::Container::String Id;
        NorvesLib::Core::Container::String MeshPath;
        NorvesLib::Core::Container::String AlbedoPath;
        NorvesLib::Core::Container::String NormalPath;
        NorvesLib::Core::Container::String OrmPath;
    };

    /// 塊 1 つの配置
    struct Placement
    {
        NorvesLib::Core::Container::String Name;
        /// AssetSpec の番号
        uint32_t AssetIndex = 0;
        NorvesLib::Math::Vector3 Position;
        /// Entity::SetRotation へ渡す四元数（正規化済み）
        NorvesLib::Math::Quaternion Rotation;
        float Scale = 1.0f;
    };

    /// 配置表の中身
    struct Table
    {
        NorvesLib::Core::Container::VariableArray<AssetSpec> Assets;
        NorvesLib::Core::Container::VariableArray<Placement> Placements;
    };

    /// 配置表の絶対パス（開発ビルドではリポジトリの Assets の隣を基準にする）
    NorvesLib::Core::Container::String TablePath();

    /// 配置表を読む。ファイルが無い・形が違うときは理由を outError に入れて false（そのときは塊を置かない）
    bool LoadTable(Table &outTable, NorvesLib::Core::Container::String &outError);

} // namespace Game::GameModes::WastelandCliffScan
