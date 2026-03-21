#pragma once

#include "Rendering/MegaGeometry/MegaGeometryTypes.h"
#include "Container/Containers.h"
#include <cstdint>

namespace NorvesLib::Core::Rendering::MegaGeometry
{
    using namespace NorvesLib::Core::Container;

    /**
     * @brief メッシュクラスタリング処理
     *
     * 入力メッシュを空間的に近い三角形のクラスタに分割します。
     * 各クラスタは MAX_TRIANGLES_PER_CLUSTER 三角形以下で構成されます。
     *
     * アルゴリズム:
     * 1. 三角形の隣接関係グラフを構築
     * 2. 貪欲法（Greedy Growing）で空間局所性を維持したクラスタを生成
     * 3. 各クラスタのバウンディングスフィアと法線コーンを計算
     */
    class MeshClusterizer
    {
    public:
        /**
         * @brief メッシュをクラスタに分割
         *
         * クラスタ毎にインデックスが連続するように並べ替えた出力インデックス配列を生成し、
         * 各MeshClusterのIndexOffset/IndexCountを出力配列に対応させます。
         *
         * @param vertexPositions 頂点位置データ（float3 × vertexCount）
         * @param vertexCount 頂点数
         * @param vertexStride 頂点ストライド（バイト）。Positionは先頭にあることを前提
         * @param indexData インデックスデータ
         * @param indexCount インデックス数（三角形数 × 3）
         * @param outClusters 出力クラスタ配列
         * @param outIndices クラスタ順に並べ替えたインデックス配列
         */
        static void Clusterize(
            const void* vertexPositions,
            uint32_t vertexCount,
            uint32_t vertexStride,
            const uint32_t* indexData,
            uint32_t indexCount,
            VariableArray<MeshCluster>& outClusters,
            VariableArray<uint32_t>& outIndices);

        /**
         * @brief クラスタの法線コーンを計算
         *
         * クラスタ内の全三角形の法線から、バックフェースカリング用の
         * 法線コーン（軸方向 + カットオフ角度）を計算します。
         *
         * @param vertexPositions 頂点位置データ
         * @param vertexStride 頂点ストライド（バイト）
         * @param indexData インデックスデータ
         * @param cluster 対象クラスタ（結果は直接書き込まれる）
         */
        static void ComputeNormalCone(
            const void* vertexPositions,
            uint32_t vertexStride,
            const uint32_t* indexData,
            MeshCluster& cluster);

        /**
         * @brief クラスタのバウンディングスフィアを計算
         *
         * @param vertexPositions 頂点位置データ
         * @param vertexStride 頂点ストライド（バイト）
         * @param indexData インデックスデータ
         * @param cluster 対象クラスタ（結果は直接書き込まれる）
         */
        static void ComputeBoundingSphere(
            const void* vertexPositions,
            uint32_t vertexStride,
            const uint32_t* indexData,
            MeshCluster& cluster);

    private:
        /**
         * @brief 三角形の隣接情報
         */
        struct TriangleAdjacency
        {
            VariableArray<VariableArray<uint32_t>> Neighbors; // neighbors[triIdx] = {adjacent tri indices}
        };

        /**
         * @brief 三角形の隣接グラフを構築
         */
        static TriangleAdjacency BuildAdjacencyGraph(
            const uint32_t* indexData,
            uint32_t triangleCount,
            uint32_t vertexCount);

        /**
         * @brief 三角形の重心を計算
         */
        static void ComputeTriangleCentroid(
            const void* vertexPositions,
            uint32_t vertexStride,
            const uint32_t* indexData,
            uint32_t triangleIndex,
            float& outX, float& outY, float& outZ);
    };

} // namespace NorvesLib::Core::Rendering::MegaGeometry
