#pragma once

#include "Asset/CookedMeshFormat.h"
#include "Container/String.h"
#include "Container/VariableArray.h"

#include <cstddef>
#include <cstdint>

namespace NorvesLib::Tools::AssetCook
{
    // LOD の階層(クラスタの DAG)の焼き込みの結果の概要。ログと受入れの確認に使う。
    struct CookMeshDagStats
    {
        // 溶接と縮退した三角形の除去のあとの、入力の三角形数と頂点数
        uint32_t SourceTriangles = 0;
        uint32_t WeldedVertices = 0;
        uint32_t LODLevelCount = 0;
        uint32_t ClusterCount = 0;
        uint32_t GroupCount = 0;
        uint32_t RootClusterCount = 0;
        // 属性の継ぎ目をまたぐ統合を許して簡略化したグループの数(0 なら、すべて継ぎ目を保ったまま半分にできた)
        uint32_t PermissiveGroupCount = 0;
        // フォールバックの段の三角形数と、その目標(全体の 1/16 か 32K の小さい方。根の段までしか粗くできなければ超える)
        uint32_t FallbackTriangles = 0;
        uint32_t FallbackTargetTriangles = 0;
        // 根が 1 クラスタまで縮んだか(false なら、簡略化が進まなくなった段で打ち切っている)
        bool bReachedSingleRoot = false;
        // 段ごとの三角形数(段 0 から根の段まで)。簡略化がどこで進まなくなったかの確認に使う。
        Core::Container::VariableArray<uint32_t> LevelTriangles;
    };

    struct CookMeshDagResult
    {
        // SerializeCookedMeshV1 へそのまま渡せる中身(材質のテクスチャ参照だけは呼び出し側が埋める)
        Core::Asset::CookedMeshV1WriteInput Output;
        CookMeshDagStats Stats;
    };

    // 三角形リストのメッシュから、NVMESH v1 の LOD の階層を作る。
    //   1. 位置・法線・UV が同じ頂点を溶接し、縮退した三角形を除く。
    //   2. 128 三角形・128 頂点以下のクラスタに分ける(段 0。誤差 0)。
    //   3. 約 4 クラスタずつ隣接でグループにし、グループの境界の頂点を固定して、法線・UV を保つ簡略化で
    //      三角形を半分にし、再びクラスタに分ける。これを根まで繰り返す。
    //      グループの誤差 = メンバの誤差の最大 + 簡略化の誤差(単調)、境界球 = メンバの境界球を包む球。
    //   4. 誤差のしきい値で切ったクラスタの三角形の集まりを、フォールバックの段として持つ。
    // 結果のクラスタ・インデックスは、クラスタごとに自分の頂点の範囲(最大 128 頂点)を持つ。
    [[nodiscard]] bool BakeMeshLodDag(const Core::Asset::CookedMeshVertex* vertices,
                                      size_t vertexCount,
                                      const uint32_t* indices,
                                      size_t indexCount,
                                      CookMeshDagResult& outResult,
                                      Core::Container::AnsiString& error);
}
