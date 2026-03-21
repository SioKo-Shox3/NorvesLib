#pragma once

#include "Rendering/MegaGeometry/MegaGeometryTypes.h"
#include "Container/Containers.h"
#include <cstdint>

namespace NorvesLib::Core::Rendering::MegaGeometry
{
    using namespace NorvesLib::Core::Container;

    /**
     * @brief クラスタグループビルダー
     *
     * 空間的に近いクラスタをグループ化し、LOD階層構築の
     * 簡略化単位となるクラスタグループを生成します。
     *
     * アルゴリズム:
     * 1. クラスタのバウンディングスフィア中心を用いた空間近接性判定
     * 2. 貪欲法で4〜8クラスタをグループ化
     * 3. グループごとのバウンディングスフィアを計算
     */
    class ClusterGroupBuilder
    {
    public:
        /** @brief 1グループあたりの最小クラスタ数 */
        static constexpr uint32_t MIN_CLUSTERS_PER_GROUP = 4;

        /** @brief 1グループあたりの最大クラスタ数 */
        static constexpr uint32_t MAX_CLUSTERS_PER_GROUP = 8;

        /**
         * @brief クラスタをグループ化
         *
         * 同一LODレベルのクラスタを空間近接性に基づいてグループ化します。
         *
         * @param clusters 入力クラスタ配列
         * @param clusterStartIndex クラスタ配列中の処理開始インデックス
         * @param clusterCount 処理するクラスタ数
         * @return クラスタグループ配列
         */
        static VariableArray<ClusterGroup> BuildGroups(
            const VariableArray<MeshCluster> &clusters,
            uint32_t clusterStartIndex,
            uint32_t clusterCount);
    };

} // namespace NorvesLib::Core::Rendering::MegaGeometry
