#include "Rendering/MegaGeometry/MeshSimplifier.h"
#include "Logging/LogMacros.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <limits>

namespace NorvesLib::Core::Rendering::MegaGeometry
{
    // ========================================
    // QEM 内部実装
    // ========================================

    namespace
    {
        /**
         * @brief 4x4対称行列（二次誤差行列 Q）
         *
         * 対称なので10要素のみ保持
         * | a[0]  a[1]  a[2]  a[3]  |
         * | a[1]  a[4]  a[5]  a[6]  |
         * | a[2]  a[5]  a[7]  a[8]  |
         * | a[3]  a[6]  a[8]  a[9]  |
         */
        struct SymmetricMatrix
        {
            double m[10];

            SymmetricMatrix()
            {
                for (int i = 0; i < 10; ++i)
                {
                    m[i] = 0.0;
                }
            }

            SymmetricMatrix(double a, double b, double c, double d)
            {
                // 面 ax+by+cz+d=0 から Q = pp^T を構築
                m[0] = a * a;
                m[1] = a * b;
                m[2] = a * c;
                m[3] = a * d;
                m[4] = b * b;
                m[5] = b * c;
                m[6] = b * d;
                m[7] = c * c;
                m[8] = c * d;
                m[9] = d * d;
            }

            SymmetricMatrix operator+(const SymmetricMatrix &other) const
            {
                SymmetricMatrix result;
                for (int i = 0; i < 10; ++i)
                {
                    result.m[i] = m[i] + other.m[i];
                }
                return result;
            }

            SymmetricMatrix &operator+=(const SymmetricMatrix &other)
            {
                for (int i = 0; i < 10; ++i)
                {
                    m[i] += other.m[i];
                }
                return *this;
            }

            /**
             * @brief v^T * Q * v を計算（頂点の誤差）
             */
            double Evaluate(double x, double y, double z) const
            {
                return m[0] * x * x + 2.0 * m[1] * x * y + 2.0 * m[2] * x * z + 2.0 * m[3] * x + m[4] * y * y + 2.0 * m[5] * y * z + 2.0 * m[6] * y + m[7] * z * z + 2.0 * m[8] * z + m[9];
            }

            /**
             * @brief 最適折り畳み点を求める（3x3部分行列の逆行列）
             * @return 逆行列が存在すればtrue
             */
            bool OptimalPosition(double &ox, double &oy, double &oz) const
            {
                // 3x3部分: [m0 m1 m2; m1 m4 m5; m2 m5 m7]
                // rhs: [-m3, -m6, -m8]
                double det =
                    m[0] * (m[4] * m[7] - m[5] * m[5]) -
                    m[1] * (m[1] * m[7] - m[5] * m[2]) +
                    m[2] * (m[1] * m[5] - m[4] * m[2]);

                if (std::abs(det) < 1e-15)
                {
                    return false;
                }

                double invDet = 1.0 / det;
                ox = -invDet * ((m[4] * m[7] - m[5] * m[5]) * m[3] +
                                (m[2] * m[5] - m[1] * m[7]) * m[6] +
                                (m[1] * m[5] - m[2] * m[4]) * m[8]);
                oy = -invDet * ((m[5] * m[2] - m[1] * m[7]) * m[3] +
                                (m[0] * m[7] - m[2] * m[2]) * m[6] +
                                (m[1] * m[2] - m[0] * m[5]) * m[8]);
                oz = -invDet * ((m[1] * m[5] - m[4] * m[2]) * m[3] +
                                (m[1] * m[2] - m[0] * m[5]) * m[6] +
                                (m[0] * m[4] - m[1] * m[1]) * m[8]);

                return true;
            }
        };

        struct QEMVertex
        {
            double x, y, z;
            SymmetricMatrix q;
            bool bDeleted = false;
            uint32_t originalIndex = 0;
        };

        struct QEMTriangle
        {
            uint32_t v[3];
            bool bDeleted = false;
        };

        struct EdgeCollapse
        {
            uint32_t v0, v1;         // エッジ端点
            double cost;             // 折り畳みコスト
            double optX, optY, optZ; // 最適折り畳み位置
        };

        inline void GetVertexPos(const void *data, uint32_t stride, uint32_t index,
                                 float &x, float &y, float &z)
        {
            const auto *base = static_cast<const uint8_t *>(data);
            const auto *pos = reinterpret_cast<const float *>(base + static_cast<size_t>(index) * stride);
            x = pos[0];
            y = pos[1];
            z = pos[2];
        }

        /**
         * @brief エッジ折り畳みコストを計算
         */
        EdgeCollapse ComputeEdgeCost(const QEMVertex &v0, const QEMVertex &v1,
                                     uint32_t i0, uint32_t i1)
        {
            EdgeCollapse ec;
            ec.v0 = i0;
            ec.v1 = i1;

            SymmetricMatrix qSum = v0.q + v1.q;

            // 最適位置を求める
            if (qSum.OptimalPosition(ec.optX, ec.optY, ec.optZ))
            {
                ec.cost = qSum.Evaluate(ec.optX, ec.optY, ec.optZ);
            }
            else
            {
                // 退化の場合: v0, v1, 中点のうち最小コストを選択
                double c0 = qSum.Evaluate(v0.x, v0.y, v0.z);
                double c1 = qSum.Evaluate(v1.x, v1.y, v1.z);
                double mx = (v0.x + v1.x) * 0.5, my = (v0.y + v1.y) * 0.5, mz = (v0.z + v1.z) * 0.5;
                double cm = qSum.Evaluate(mx, my, mz);

                if (c0 <= c1 && c0 <= cm)
                {
                    ec.optX = v0.x;
                    ec.optY = v0.y;
                    ec.optZ = v0.z;
                    ec.cost = c0;
                }
                else if (c1 <= cm)
                {
                    ec.optX = v1.x;
                    ec.optY = v1.y;
                    ec.optZ = v1.z;
                    ec.cost = c1;
                }
                else
                {
                    ec.optX = mx;
                    ec.optY = my;
                    ec.optZ = mz;
                    ec.cost = cm;
                }
            }

            if (ec.cost < 0.0)
            {
                ec.cost = 0.0;
            }
            return ec;
        }

    } // anonymous namespace

    // ========================================
    // Simplify
    // ========================================

    SimplificationResult MeshSimplifier::Simplify(
        const void *vertexData,
        uint32_t vertexCount,
        uint32_t vertexStride,
        const uint32_t *indexData,
        uint32_t indexCount,
        uint32_t targetTriangleCount,
        float maxError)
    {
        SimplificationResult result;

        if (!vertexData || !indexData || indexCount < 3 || vertexCount < 3)
        {
            return result;
        }

        uint32_t triangleCount = indexCount / 3;
        if (triangleCount <= targetTriangleCount)
        {
            // 既に目標以下、そのままコピー
            result.VertexData.resize(static_cast<size_t>(vertexCount) * vertexStride);
            std::memcpy(result.VertexData.data(), vertexData, result.VertexData.size());
            result.IndexData.assign(indexData, indexData + indexCount);
            result.VertexCount = vertexCount;
            result.IndexCount = indexCount;
            result.MaxError = 0.0f;
            return result;
        }

        // ========================================
        // 1. 内部構造体に変換
        // ========================================

        VariableArray<QEMVertex> vertices(vertexCount);
        for (uint32_t i = 0; i < vertexCount; ++i)
        {
            float x, y, z;
            GetVertexPos(vertexData, vertexStride, i, x, y, z);
            vertices[i].x = x;
            vertices[i].y = y;
            vertices[i].z = z;
            vertices[i].originalIndex = i;
        }

        VariableArray<QEMTriangle> triangles(triangleCount);
        for (uint32_t i = 0; i < triangleCount; ++i)
        {
            triangles[i].v[0] = indexData[i * 3 + 0];
            triangles[i].v[1] = indexData[i * 3 + 1];
            triangles[i].v[2] = indexData[i * 3 + 2];
        }

        // ========================================
        // 2. 各頂点の二次誤差行列を初期化
        // ========================================

        for (const auto &tri : triangles)
        {
            const auto &v0 = vertices[tri.v[0]];
            const auto &v1 = vertices[tri.v[1]];
            const auto &v2 = vertices[tri.v[2]];

            // 面の法線
            double e1x = v1.x - v0.x, e1y = v1.y - v0.y, e1z = v1.z - v0.z;
            double e2x = v2.x - v0.x, e2y = v2.y - v0.y, e2z = v2.z - v0.z;
            double nx = e2y * e1z - e2z * e1y;
            double ny = e2z * e1x - e2x * e1z;
            double nz = e2x * e1y - e2y * e1x;
            double len = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (len > 1e-12)
            {
                nx /= len;
                ny /= len;
                nz /= len;
            }
            double d = -(nx * v0.x + ny * v0.y + nz * v0.z);

            SymmetricMatrix q(nx, ny, nz, d);
            vertices[tri.v[0]].q += q;
            vertices[tri.v[1]].q += q;
            vertices[tri.v[2]].q += q;
        }

        // ========================================
        // 3. 反復エッジ折り畳み
        // ========================================

        uint32_t remainingTriangles = triangleCount;
        double globalMaxError = 0.0;

        // リマッピングテーブル: vertices[i]が別の頂点に統合された場合
        VariableArray<uint32_t> vertexRemap(vertexCount);
        for (uint32_t i = 0; i < vertexCount; ++i)
        {
            vertexRemap[i] = i;
        }

        // リマップをたどって最終的な頂点を取得
        auto Resolve = [&](uint32_t idx) -> uint32_t
        {
            while (vertexRemap[idx] != idx)
            {
                idx = vertexRemap[idx];
            }
            return idx;
        };

        while (remainingTriangles > targetTriangleCount)
        {
            // 全三角形のエッジから最小コストの折り畳みを見つける
            EdgeCollapse bestCollapse;
            bestCollapse.cost = std::numeric_limits<double>::max();
            bool bFound = false;

            for (uint32_t t = 0; t < static_cast<uint32_t>(triangles.size()); ++t)
            {
                if (triangles[t].bDeleted)
                {
                    continue;
                }

                // 頂点インデックスを解決
                for (int e = 0; e < 3; ++e)
                {
                    triangles[t].v[e] = Resolve(triangles[t].v[e]);
                }

                // 退化三角形チェック
                if (triangles[t].v[0] == triangles[t].v[1] ||
                    triangles[t].v[1] == triangles[t].v[2] ||
                    triangles[t].v[0] == triangles[t].v[2])
                {
                    triangles[t].bDeleted = true;
                    --remainingTriangles;
                    continue;
                }

                for (int e = 0; e < 3; ++e)
                {
                    uint32_t i0 = triangles[t].v[e];
                    uint32_t i1 = triangles[t].v[(e + 1) % 3];
                    if (i0 > i1)
                    {
                        continue; // 各エッジを一方向のみ処理
                    }

                    EdgeCollapse ec = ComputeEdgeCost(vertices[i0], vertices[i1], i0, i1);
                    if (ec.cost < bestCollapse.cost)
                    {
                        bestCollapse = ec;
                        bFound = true;
                    }
                }
            }

            if (!bFound)
            {
                break;
            }

            // 最大誤差チェック
            if (maxError > 0.0f && bestCollapse.cost > static_cast<double>(maxError) * static_cast<double>(maxError))
            {
                break;
            }

            if (bestCollapse.cost > globalMaxError)
            {
                globalMaxError = bestCollapse.cost;
            }

            // v1 → v0 に折り畳み
            uint32_t keepVert = bestCollapse.v0;
            uint32_t removeVert = bestCollapse.v1;

            // 保持する頂点の位置と二次誤差行列を更新
            vertices[keepVert].x = bestCollapse.optX;
            vertices[keepVert].y = bestCollapse.optY;
            vertices[keepVert].z = bestCollapse.optZ;
            vertices[keepVert].q = vertices[keepVert].q + vertices[removeVert].q;

            // リマップ
            vertexRemap[removeVert] = keepVert;
            vertices[removeVert].bDeleted = true;

            // 退化三角形の除去
            for (auto &tri : triangles)
            {
                if (tri.bDeleted)
                {
                    continue;
                }

                // リマップ適用
                for (int e = 0; e < 3; ++e)
                {
                    tri.v[e] = Resolve(tri.v[e]);
                }

                if (tri.v[0] == tri.v[1] || tri.v[1] == tri.v[2] || tri.v[0] == tri.v[2])
                {
                    tri.bDeleted = true;
                    --remainingTriangles;
                }
            }
        }

        // ========================================
        // 4. コンパクト化: 結果の頂点・インデックスを出力
        // ========================================

        // 使用中の頂点をコンパクト化
        VariableArray<uint32_t> newVertexIndex(vertexCount, UINT32_MAX);
        uint32_t newVertCount = 0;

        for (const auto &tri : triangles)
        {
            if (tri.bDeleted)
            {
                continue;
            }
            for (int e = 0; e < 3; ++e)
            {
                uint32_t v = tri.v[e];
                if (newVertexIndex[v] == UINT32_MAX)
                {
                    newVertexIndex[v] = newVertCount++;
                }
            }
        }

        // 頂点データを構築（属性補間: QEMで移動した位置を使用、その他属性は元の頂点から）
        result.VertexData.resize(static_cast<size_t>(newVertCount) * vertexStride);
        const auto *srcBytes = static_cast<const uint8_t *>(vertexData);

        for (uint32_t i = 0; i < vertexCount; ++i)
        {
            if (newVertexIndex[i] == UINT32_MAX)
            {
                continue;
            }

            // 元の頂点データをコピー（法線・UV等の属性を保持）
            uint32_t srcIdx = vertices[i].originalIndex;
            auto *dst = result.VertexData.data() + static_cast<size_t>(newVertexIndex[i]) * vertexStride;
            std::memcpy(dst, srcBytes + static_cast<size_t>(srcIdx) * vertexStride, vertexStride);

            // 位置をQEMの最適位置で上書き
            auto *dstPos = reinterpret_cast<float *>(dst);
            dstPos[0] = static_cast<float>(vertices[i].x);
            dstPos[1] = static_cast<float>(vertices[i].y);
            dstPos[2] = static_cast<float>(vertices[i].z);
        }

        // インデックスデータを構築
        for (const auto &tri : triangles)
        {
            if (tri.bDeleted)
            {
                continue;
            }
            result.IndexData.push_back(newVertexIndex[tri.v[0]]);
            result.IndexData.push_back(newVertexIndex[tri.v[1]]);
            result.IndexData.push_back(newVertexIndex[tri.v[2]]);
        }

        result.VertexCount = newVertCount;
        result.IndexCount = static_cast<uint32_t>(result.IndexData.size());
        result.MaxError = static_cast<float>(std::sqrt(globalMaxError));

        NORVES_LOG_DEBUG("MeshSimplifier",
                         "簡略化完了: %u → %u 三角形 (頂点: %u → %u, 最大誤差: %.6f)",
                         triangleCount, result.IndexCount / 3,
                         vertexCount, result.VertexCount, result.MaxError);

        return result;
    }

} // namespace NorvesLib::Core::Rendering::MegaGeometry
