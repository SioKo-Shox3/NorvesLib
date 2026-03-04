#pragma once

#include "Container/Containers.h"
#include <cmath>
#include <cstdint>

namespace NorvesLib::Core::Rendering
{

    /**
     * @brief 3Dメッシュ用頂点データ（Position + Normal + TexCoord）
     */
    struct Mesh3DVertex
    {
        float Position[3];
        float Normal[3];
        float TexCoord[2];
    };

    /**
     * @brief プロシージャルメッシュ生成ユーティリティ
     */
    class ProceduralMeshGenerator
    {
    public:
        /**
         * @brief UV球体のメッシュデータを生成
         * @param radius 球体の半径
         * @param sliceCount 水平方向の分割数（経度）
         * @param stackCount 垂直方向の分割数（緯度）
         * @param outVertices 出力頂点データ
         * @param outIndices 出力インデックスデータ
         */
        static void GenerateUVSphere(
            float radius,
            uint32_t sliceCount,
            uint32_t stackCount,
            Container::VariableArray<Mesh3DVertex> &outVertices,
            Container::VariableArray<uint32_t> &outIndices)
        {
            outVertices.clear();
            outIndices.clear();

            constexpr float PI = 3.14159265358979323846f;

            // 北極の頂点
            Mesh3DVertex topVertex;
            topVertex.Position[0] = 0.0f;
            topVertex.Position[1] = radius;
            topVertex.Position[2] = 0.0f;
            topVertex.Normal[0] = 0.0f;
            topVertex.Normal[1] = 1.0f;
            topVertex.Normal[2] = 0.0f;
            topVertex.TexCoord[0] = 0.5f;
            topVertex.TexCoord[1] = 0.0f;
            outVertices.push_back(topVertex);

            // 中間の頂点を生成（北極と南極の間）
            for (uint32_t stack = 1; stack < stackCount; ++stack)
            {
                float phi = PI * static_cast<float>(stack) / static_cast<float>(stackCount);
                float sinPhi = std::sin(phi);
                float cosPhi = std::cos(phi);

                for (uint32_t slice = 0; slice <= sliceCount; ++slice)
                {
                    float theta = 2.0f * PI * static_cast<float>(slice) / static_cast<float>(sliceCount);
                    float sinTheta = std::sin(theta);
                    float cosTheta = std::cos(theta);

                    Mesh3DVertex vertex;
                    // 位置（Y-up座標系）
                    vertex.Position[0] = radius * sinPhi * cosTheta;
                    vertex.Position[1] = radius * cosPhi;
                    vertex.Position[2] = radius * sinPhi * sinTheta;

                    // 法線（球体なので位置を正規化したもの）
                    float invRadius = 1.0f / radius;
                    vertex.Normal[0] = vertex.Position[0] * invRadius;
                    vertex.Normal[1] = vertex.Position[1] * invRadius;
                    vertex.Normal[2] = vertex.Position[2] * invRadius;

                    // UV座標（球面マッピング）
                    vertex.TexCoord[0] = static_cast<float>(slice) / static_cast<float>(sliceCount);
                    vertex.TexCoord[1] = static_cast<float>(stack) / static_cast<float>(stackCount);

                    outVertices.push_back(vertex);
                }
            }

            // 南極の頂点
            Mesh3DVertex bottomVertex;
            bottomVertex.Position[0] = 0.0f;
            bottomVertex.Position[1] = -radius;
            bottomVertex.Position[2] = 0.0f;
            bottomVertex.Normal[0] = 0.0f;
            bottomVertex.Normal[1] = -1.0f;
            bottomVertex.Normal[2] = 0.0f;
            bottomVertex.TexCoord[0] = 0.5f;
            bottomVertex.TexCoord[1] = 1.0f;
            outVertices.push_back(bottomVertex);

            // === インデックス生成 ===

            // 北極キャップ（最初の頂点 → 最初のリング）
            for (uint32_t slice = 0; slice < sliceCount; ++slice)
            {
                outIndices.push_back(0);
                outIndices.push_back(slice + 1);
                outIndices.push_back(slice + 2);
            }

            // 中間のリング
            uint32_t ringVertexCount = sliceCount + 1;
            for (uint32_t stack = 0; stack < stackCount - 2; ++stack)
            {
                for (uint32_t slice = 0; slice < sliceCount; ++slice)
                {
                    uint32_t baseIndex = 1 + stack * ringVertexCount;

                    uint32_t i0 = baseIndex + slice;
                    uint32_t i1 = baseIndex + slice + 1;
                    uint32_t i2 = baseIndex + ringVertexCount + slice;
                    uint32_t i3 = baseIndex + ringVertexCount + slice + 1;

                    // 上三角形
                    outIndices.push_back(i0);
                    outIndices.push_back(i2);
                    outIndices.push_back(i1);

                    // 下三角形
                    outIndices.push_back(i1);
                    outIndices.push_back(i2);
                    outIndices.push_back(i3);
                }
            }

            // 南極キャップ（最後のリング → 最後の頂点）
            uint32_t southPoleIndex = static_cast<uint32_t>(outVertices.size()) - 1;
            uint32_t lastRingBaseIndex = southPoleIndex - ringVertexCount;
            for (uint32_t slice = 0; slice < sliceCount; ++slice)
            {
                outIndices.push_back(southPoleIndex);
                outIndices.push_back(lastRingBaseIndex + slice + 1);
                outIndices.push_back(lastRingBaseIndex + slice);
            }
        }

        /**
         * @brief 平面メッシュを生成（XZ平面、Y-up）
         * @param width X方向の幅
         * @param depth Z方向の奥行き
         * @param subdivisionsX X方向の分割数
         * @param subdivisionsZ Z方向の分割数
         * @param outVertices 出力頂点データ
         * @param outIndices 出力インデックスデータ
         */
        static void GeneratePlane(
            float width,
            float depth,
            uint32_t subdivisionsX,
            uint32_t subdivisionsZ,
            Container::VariableArray<Mesh3DVertex> &outVertices,
            Container::VariableArray<uint32_t> &outIndices)
        {
            outVertices.clear();
            outIndices.clear();

            float halfW = width * 0.5f;
            float halfD = depth * 0.5f;
            float stepX = width / static_cast<float>(subdivisionsX);
            float stepZ = depth / static_cast<float>(subdivisionsZ);

            // 頂点生成
            for (uint32_t iz = 0; iz <= subdivisionsZ; ++iz)
            {
                for (uint32_t ix = 0; ix <= subdivisionsX; ++ix)
                {
                    Mesh3DVertex vertex;
                    vertex.Position[0] = -halfW + static_cast<float>(ix) * stepX;
                    vertex.Position[1] = 0.0f;
                    vertex.Position[2] = -halfD + static_cast<float>(iz) * stepZ;
                    vertex.Normal[0] = 0.0f;
                    vertex.Normal[1] = 1.0f;
                    vertex.Normal[2] = 0.0f;

                    // UV座標（0～1にマッピング）
                    vertex.TexCoord[0] = static_cast<float>(ix) / static_cast<float>(subdivisionsX);
                    vertex.TexCoord[1] = static_cast<float>(iz) / static_cast<float>(subdivisionsZ);

                    outVertices.push_back(vertex);
                }
            }

            // インデックス生成
            uint32_t rowVerts = subdivisionsX + 1;
            for (uint32_t iz = 0; iz < subdivisionsZ; ++iz)
            {
                for (uint32_t ix = 0; ix < subdivisionsX; ++ix)
                {
                    uint32_t i0 = iz * rowVerts + ix;
                    uint32_t i1 = i0 + 1;
                    uint32_t i2 = i0 + rowVerts;
                    uint32_t i3 = i2 + 1;

                    // CW winding (上から見て時計回り、FrontFace::Clockwiseに合わせる)
                    outIndices.push_back(i0);
                    outIndices.push_back(i1);
                    outIndices.push_back(i2);

                    outIndices.push_back(i1);
                    outIndices.push_back(i3);
                    outIndices.push_back(i2);
                }
            }
        }
    };

} // namespace NorvesLib::Core::Rendering
