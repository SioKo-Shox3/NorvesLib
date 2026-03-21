#pragma once

#include "Rendering/MegaGeometry/MegaGeometryTypes.h"
#include "Container/Containers.h"
#include <cstdint>

namespace NorvesLib::Core::Rendering::MegaGeometry
{
    using namespace NorvesLib::Core::Container;

    /**
     * @brief LOD階層の構築設定
     */
    struct LODBuildSettings
    {
        float SimplificationRatio = 0.5f;  // 各LODでの三角形削減比率
        uint32_t MaxLODLevels = 8;         // 最大LODレベル数
        uint32_t MinTrianglesForLOD = 128; // LOD構築終了の三角形数閾値
    };

    /**
     * @brief LOD階層ビルダー
     *
     * MeshClusterizer, ClusterGroupBuilder, MeshSimplifier を組み合わせて
     * 再帰的にLOD DAGを構築します。
     *
     * 構築フロー:
     * 1. LOD 0: 入力メッシュをクラスタ化
     * 2. クラスタをグループ化（ClusterGroupBuilder）
     * 3. 各グループを簡略化（MeshSimplifier）
     * 4. 簡略化結果を再クラスタ化 → LOD N+1
     * 5. 親子関係を設定（ParentStart/ParentCount）
     * 6. 三角形数がMinTrianglesForLOD以下になるまで繰り返し
     */
    class LODHierarchyBuilder
    {
    public:
        /**
         * @brief LOD階層を構築
         *
         * @param vertexData 頂点データ（位置が先頭にあることを前提）
         * @param vertexCount 頂点数
         * @param vertexStride 頂点ストライド（バイト）
         * @param indexData インデックスデータ
         * @param indexCount インデックス数
         * @param settings LOD構築設定
         * @return LOD階層データ
         */
        static LODHierarchy Build(
            const void *vertexData,
            uint32_t vertexCount,
            uint32_t vertexStride,
            const uint32_t *indexData,
            uint32_t indexCount,
            const LODBuildSettings &settings = LODBuildSettings{});
    };

} // namespace NorvesLib::Core::Rendering::MegaGeometry
