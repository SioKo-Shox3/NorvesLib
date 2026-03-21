#pragma once

#include "Container/Containers.h"
#include <cstdint>

namespace NorvesLib::Core::Rendering::MegaGeometry
{
    using namespace NorvesLib::Core::Container;

    /**
     * @brief メッシュ簡略化の結果
     */
    struct SimplificationResult
    {
        VariableArray<uint8_t> VertexData; // 簡略化後の頂点データ
        VariableArray<uint32_t> IndexData; // 簡略化後のインデックスデータ
        uint32_t VertexCount = 0;
        uint32_t IndexCount = 0;
        float MaxError = 0.0f; // 簡略化による最大誤差
    };

    /**
     * @brief QEMベースのメッシュ簡略化
     *
     * Quadric Error Metrics (Garland & Heckbert 1997) による
     * エッジ折り畳みベースのメッシュ簡略化を行います。
     *
     * 頂点の位置(float3)が各頂点データの先頭にあることを前提とします。
     * 法線(float3), テクスチャ座標(float2)がある場合も属性補間されます。
     */
    class MeshSimplifier
    {
    public:
        /**
         * @brief メッシュを簡略化
         * @param vertexData 頂点データ（位置が先頭、後続に法線+UV等を想定）
         * @param vertexCount 頂点数
         * @param vertexStride 頂点ストライド（バイト）
         * @param indexData インデックスデータ
         * @param indexCount インデックス数（三角形数 × 3）
         * @param targetTriangleCount 目標三角形数
         * @param maxError 許容最大誤差（0=制限なし）
         * @return 簡略化結果
         */
        static SimplificationResult Simplify(
            const void *vertexData,
            uint32_t vertexCount,
            uint32_t vertexStride,
            const uint32_t *indexData,
            uint32_t indexCount,
            uint32_t targetTriangleCount,
            float maxError = 0.0f);
    };

} // namespace NorvesLib::Core::Rendering::MegaGeometry
