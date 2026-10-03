#include "Rendering/MegaGeometry/MeshClusterizer.h"
#include "Logging/LogMacros.h"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace NorvesLib::Core::Rendering::MegaGeometry
{

    // ========================================
    // ヘルパー関数
    // ========================================

    namespace
    {
        /**
         * @brief 頂点位置を取得
         */
        inline void GetVertexPosition(const void *vertexData, uint32_t vertexStride,
                                      uint32_t vertexIndex, float &x, float &y, float &z)
        {
            const auto *base = static_cast<const uint8_t *>(vertexData);
            const auto *pos = reinterpret_cast<const float *>(base + static_cast<size_t>(vertexIndex) * vertexStride);
            x = pos[0];
            y = pos[1];
            z = pos[2];
        }

        /**
         * @brief 三角形の法線を計算（正規化なし）
         */
        inline void ComputeTriangleNormal(const void *vertexData, uint32_t vertexStride,
                                          const uint32_t *indices, uint32_t triIndex,
                                          float &nx, float &ny, float &nz)
        {
            uint32_t i0 = indices[triIndex * 3 + 0];
            uint32_t i1 = indices[triIndex * 3 + 1];
            uint32_t i2 = indices[triIndex * 3 + 2];

            float x0, y0, z0, x1, y1, z1, x2, y2, z2;
            GetVertexPosition(vertexData, vertexStride, i0, x0, y0, z0);
            GetVertexPosition(vertexData, vertexStride, i1, x1, y1, z1);
            GetVertexPosition(vertexData, vertexStride, i2, x2, y2, z2);

            // edge vectors
            float e1x = x1 - x0, e1y = y1 - y0, e1z = z1 - z0;
            float e2x = x2 - x0, e2y = y2 - y0, e2z = z2 - z0;

            // FrontFace::Clockwiseに合わせ、CW windingで外向き法線になる向きを採用する
            nx = e2y * e1z - e2z * e1y;
            ny = e2z * e1x - e2x * e1z;
            nz = e2x * e1y - e2y * e1x;
        }

        /**
         * @brief エッジキーを生成（頂点インデックスペアの正規化）
         */
        inline uint64_t MakeEdgeKey(uint32_t v0, uint32_t v1)
        {
            if (v0 > v1)
            {
                uint32_t tmp = v0;
                v0 = v1;
                v1 = tmp;
            }
            return (static_cast<uint64_t>(v0) << 32) | static_cast<uint64_t>(v1);
        }

        /**
         * @brief 位置の比較に使う float のビット列（-0 は +0 にそろえる）
         */
        inline uint32_t PositionComponentBits(float value)
        {
            if (value == 0.0f)
            {
                value = 0.0f;
            }
            uint32_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            return bits;
        }

        /**
         * @brief 各頂点を、同じ位置の頂点のうち最小の番号（代表）へ写す
         *
         * UVの継ぎ目や法線の分かれ目で複製された頂点は位置が完全に一致するので、
         * ビット列が一致する位置を同じ頂点として扱う。
         */
        VariableArray<uint32_t> BuildPositionRepresentatives(const void *vertexPositions, uint32_t vertexStride,
                                                             uint32_t vertexCount)
        {
            struct PositionKey
            {
                uint32_t X;
                uint32_t Y;
                uint32_t Z;
                uint32_t Vertex;
            };

            VariableArray<PositionKey> keys(vertexCount);
            for (uint32_t vertex = 0; vertex < vertexCount; ++vertex)
            {
                float x, y, z;
                GetVertexPosition(vertexPositions, vertexStride, vertex, x, y, z);
                keys[vertex] = {PositionComponentBits(x), PositionComponentBits(y), PositionComponentBits(z), vertex};
            }

            std::sort(keys.begin(), keys.end(), [](const PositionKey &a, const PositionKey &b)
                      {
                          if (a.X != b.X)
                              return a.X < b.X;
                          if (a.Y != b.Y)
                              return a.Y < b.Y;
                          if (a.Z != b.Z)
                              return a.Z < b.Z;
                          return a.Vertex < b.Vertex; });

            VariableArray<uint32_t> representatives(vertexCount);
            uint32_t runRepresentative = 0;
            for (uint32_t i = 0; i < vertexCount; ++i)
            {
                const PositionKey &key = keys[i];
                if (i == 0 || key.X != keys[i - 1].X || key.Y != keys[i - 1].Y || key.Z != keys[i - 1].Z)
                {
                    // 同じ位置の並びの先頭が最小の番号になる
                    runRepresentative = key.Vertex;
                }
                representatives[key.Vertex] = runRepresentative;
            }
            return representatives;
        }

        /**
         * @brief 三角形の重心を一様格子に登録し、未割り当ての中で指定点に最も近い三角形を探す
         *
         * 隣接でつながらない部品（離れた板・窓枠など）をクラスタへまとめるときに使う。
         */
        class TriangleCentroidGrid
        {
        public:
            static constexpr uint32_t NotFound = 0xFFFFFFFFu;

            void Build(const VariableArray<float> &centroids, uint32_t triangleCount)
            {
                float minPos[3] = {3.402823466e+38f, 3.402823466e+38f, 3.402823466e+38f};
                float maxPos[3] = {-3.402823466e+38f, -3.402823466e+38f, -3.402823466e+38f};
                for (uint32_t tri = 0; tri < triangleCount; ++tri)
                {
                    for (uint32_t axis = 0; axis < 3; ++axis)
                    {
                        const float value = centroids[tri * 3 + axis];
                        if (std::isfinite(value))
                        {
                            minPos[axis] = std::min(minPos[axis], value);
                            maxPos[axis] = std::max(maxPos[axis], value);
                        }
                    }
                }

                float maxExtent = 0.0f;
                float extent[3] = {0.0f, 0.0f, 0.0f};
                for (uint32_t axis = 0; axis < 3; ++axis)
                {
                    if (minPos[axis] > maxPos[axis])
                    {
                        minPos[axis] = 0.0f;
                        maxPos[axis] = 0.0f;
                    }
                    m_Min[axis] = minPos[axis];
                    extent[axis] = maxPos[axis] - minPos[axis];
                    maxExtent = std::max(maxExtent, extent[axis]);
                }
                if (!std::isfinite(maxExtent))
                {
                    // 範囲が float で表せないほど広いときは1セルで全探索する
                    extent[0] = extent[1] = extent[2] = 0.0f;
                    maxExtent = 0.0f;
                }

                // 1セルに三角形が平均4つ程度入る大きさにする。平らなメッシュで体積が0にならないよう各辺に下限を置く
                m_CellSize = 1.0f;
                if (maxExtent > 0.0f && std::isfinite(maxExtent))
                {
                    const float minEdge = maxExtent * 1.0e-3f;
                    const double volume = static_cast<double>(std::max(extent[0], minEdge)) *
                                          static_cast<double>(std::max(extent[1], minEdge)) *
                                          static_cast<double>(std::max(extent[2], minEdge));
                    const double targetCells = std::max(1.0, static_cast<double>(triangleCount) / 4.0);
                    m_CellSize = static_cast<float>(std::cbrt(volume / targetCells));
                    m_CellSize = std::max(m_CellSize, maxExtent / 1024.0f);
                }

                // セル数が三角形数に比べて多すぎるときはセルを大きくする
                const uint64_t maxCellCount = std::max<uint64_t>(64u, static_cast<uint64_t>(triangleCount) * 8u);
                uint64_t cellCount = 1;
                for (;;)
                {
                    cellCount = 1;
                    for (uint32_t axis = 0; axis < 3; ++axis)
                    {
                        m_Dim[axis] = static_cast<uint32_t>(extent[axis] / m_CellSize) + 1u;
                        cellCount *= m_Dim[axis];
                    }
                    if (cellCount <= maxCellCount)
                    {
                        break;
                    }
                    m_CellSize *= 1.25f;
                }

                m_CellStart.assign(static_cast<size_t>(cellCount) + 1u, 0u);
                m_CellRemaining.assign(static_cast<size_t>(cellCount), 0u);
                m_TriangleCell.resize(triangleCount);
                for (uint32_t tri = 0; tri < triangleCount; ++tri)
                {
                    const uint32_t cell = CellIndex(CellCoord(centroids, tri, 0), CellCoord(centroids, tri, 1),
                                                    CellCoord(centroids, tri, 2));
                    m_TriangleCell[tri] = cell;
                    ++m_CellStart[cell + 1u];
                    ++m_CellRemaining[cell];
                }
                for (size_t cell = 0; cell < static_cast<size_t>(cellCount); ++cell)
                {
                    m_CellStart[cell + 1u] += m_CellStart[cell];
                }

                VariableArray<uint32_t> cursor(m_CellStart.begin(), m_CellStart.end() - 1);
                m_CellTriangles.resize(triangleCount);
                for (uint32_t tri = 0; tri < triangleCount; ++tri)
                {
                    m_CellTriangles[cursor[m_TriangleCell[tri]]++] = tri;
                }
                m_Remaining = triangleCount;
            }

            void MarkAssigned(uint32_t tri)
            {
                --m_CellRemaining[m_TriangleCell[tri]];
                --m_Remaining;
            }

            uint32_t FindNearestUnassigned(const VariableArray<float> &centroids, const VariableArray<bool> &assigned,
                                           float x, float y, float z) const
            {
                if (m_Remaining == 0u)
                {
                    return NotFound;
                }

                const float query[3] = {x, y, z};
                int32_t center[3];
                for (uint32_t axis = 0; axis < 3; ++axis)
                {
                    center[axis] = static_cast<int32_t>(CoordFromValue(query[axis], axis));
                }

                const int32_t maxRing = static_cast<int32_t>(std::max({m_Dim[0], m_Dim[1], m_Dim[2]}));
                uint32_t best = NotFound;
                float bestDistSq = 3.402823466e+38f;
                for (int32_t ring = 0; ring <= maxRing; ++ring)
                {
                    // 半径 ring のセルの殻（チェビシェフ距離がちょうど ring）だけを走査する
                    for (int32_t dz = -ring; dz <= ring; ++dz)
                    {
                        const int32_t cz = center[2] + dz;
                        if (cz < 0 || cz >= static_cast<int32_t>(m_Dim[2]))
                        {
                            continue;
                        }
                        for (int32_t dy = -ring; dy <= ring; ++dy)
                        {
                            const int32_t cy = center[1] + dy;
                            if (cy < 0 || cy >= static_cast<int32_t>(m_Dim[1]))
                            {
                                continue;
                            }
                            const bool onFace = (dz == -ring || dz == ring || dy == -ring || dy == ring);
                            const int32_t step = (onFace || ring == 0) ? 1 : 2 * ring;
                            for (int32_t dx = -ring; dx <= ring; dx += step)
                            {
                                const int32_t cx = center[0] + dx;
                                if (cx < 0 || cx >= static_cast<int32_t>(m_Dim[0]))
                                {
                                    continue;
                                }
                                const uint32_t cell = CellIndex(static_cast<uint32_t>(cx), static_cast<uint32_t>(cy),
                                                                static_cast<uint32_t>(cz));
                                if (m_CellRemaining[cell] == 0u)
                                {
                                    continue;
                                }
                                for (uint32_t i = m_CellStart[cell]; i < m_CellStart[cell + 1u]; ++i)
                                {
                                    const uint32_t tri = m_CellTriangles[i];
                                    if (assigned[tri])
                                    {
                                        continue;
                                    }
                                    const float ex = centroids[tri * 3 + 0] - x;
                                    const float ey = centroids[tri * 3 + 1] - y;
                                    const float ez = centroids[tri * 3 + 2] - z;
                                    const float distSq = ex * ex + ey * ey + ez * ez;
                                    // 非有限の重心も最後には拾えるよう、未発見なら距離に関係なく採る
                                    if (best == NotFound || distSq < bestDistSq)
                                    {
                                        best = tri;
                                        bestDistSq = distSq;
                                    }
                                }
                            }
                        }
                    }

                    // 次の殻のセルは問い合わせ点から少なくとも ring セル分離れている
                    const float ringDistance = static_cast<float>(ring) * m_CellSize;
                    if (best != NotFound && bestDistSq <= ringDistance * ringDistance)
                    {
                        break;
                    }
                }
                return best;
            }

        private:
            uint32_t CoordFromValue(float value, uint32_t axis) const
            {
                const float scaled = (value - m_Min[axis]) / m_CellSize;
                if (!(scaled > 0.0f))
                {
                    return 0u;
                }
                if (scaled >= static_cast<float>(m_Dim[axis] - 1u))
                {
                    return m_Dim[axis] - 1u;
                }
                return static_cast<uint32_t>(scaled);
            }

            uint32_t CellCoord(const VariableArray<float> &centroids, uint32_t tri, uint32_t axis) const
            {
                return CoordFromValue(centroids[tri * 3 + axis], axis);
            }

            uint32_t CellIndex(uint32_t cx, uint32_t cy, uint32_t cz) const
            {
                return (cz * m_Dim[1] + cy) * m_Dim[0] + cx;
            }

            float m_Min[3] = {0.0f, 0.0f, 0.0f};
            float m_CellSize = 1.0f;
            uint32_t m_Dim[3] = {1u, 1u, 1u};
            uint32_t m_Remaining = 0u;
            VariableArray<uint32_t> m_CellStart;
            VariableArray<uint32_t> m_CellRemaining;
            VariableArray<uint32_t> m_CellTriangles;
            VariableArray<uint32_t> m_TriangleCell;
        };
    } // anonymous namespace

    // ========================================
    // Clusterize
    // ========================================

    void MeshClusterizer::Clusterize(
        const void *vertexPositions,
        uint32_t vertexCount,
        uint32_t vertexStride,
        const uint32_t *indexData,
        uint32_t indexCount,
        VariableArray<MeshCluster> &outClusters,
        VariableArray<uint32_t> &outIndices)
    {
        outClusters.clear();
        outIndices.clear();

        if (!vertexPositions || !indexData || indexCount < 3)
        {
            return;
        }

        uint32_t triangleCount = indexCount / 3;

        // 三角形が最大クラスタサイズ以下なら、単一クラスタとして出力
        if (triangleCount <= MAX_TRIANGLES_PER_CLUSTER)
        {
            // インデックスをそのままコピー
            outIndices.assign(indexData, indexData + indexCount);

            MeshCluster cluster;
            cluster.IndexOffset = 0;
            cluster.IndexCount = indexCount;
            cluster.VertexOffset = 0;
            cluster.VertexCount = vertexCount;
            cluster.LODLevel = 0;
            cluster.LODError = 0.0f;
            cluster.MaterialIndex = 0;

            ComputeBoundingSphere(vertexPositions, vertexStride, outIndices.data(), cluster);
            ComputeNormalCone(vertexPositions, vertexStride, outIndices.data(), cluster);

            outClusters.push_back(cluster);
            return;
        }

        // 隣接グラフを構築
        TriangleAdjacency adjacency = BuildAdjacencyGraph(vertexPositions, vertexStride, indexData, triangleCount, vertexCount);

        // 隣接が尽きたときに近い三角形を探すための重心と格子
        VariableArray<float> centroids(static_cast<size_t>(triangleCount) * 3u);
        for (uint32_t tri = 0; tri < triangleCount; ++tri)
        {
            ComputeTriangleCentroid(vertexPositions, vertexStride, indexData, tri,
                                    centroids[tri * 3 + 0], centroids[tri * 3 + 1], centroids[tri * 3 + 2]);
        }
        TriangleCentroidGrid centroidGrid;
        centroidGrid.Build(centroids, triangleCount);

        // 貪欲法でクラスタを形成
        VariableArray<bool> assigned(triangleCount, false);

        // 一時的なクラスタ三角形リスト
        VariableArray<VariableArray<uint32_t>> clusterTriangleLists;

        for (uint32_t startTri = 0; startTri < triangleCount; ++startTri)
        {
            if (assigned[startTri])
            {
                continue;
            }

            // 新しいクラスタを開始
            VariableArray<uint32_t> clusterTriangles;
            clusterTriangles.reserve(MAX_TRIANGLES_PER_CLUSTER);
            double sumX = 0.0, sumY = 0.0, sumZ = 0.0;

            auto addTriangle = [&](uint32_t tri)
            {
                assigned[tri] = true;
                centroidGrid.MarkAssigned(tri);
                clusterTriangles.push_back(tri);
                const float cx = centroids[tri * 3 + 0];
                const float cy = centroids[tri * 3 + 1];
                const float cz = centroids[tri * 3 + 2];
                if (std::isfinite(cx) && std::isfinite(cy) && std::isfinite(cz))
                {
                    sumX += cx;
                    sumY += cy;
                    sumZ += cz;
                }
            };

            // BFS/貪欲成長でクラスタを拡大（frontier は clusterTriangles の未展開の部分）
            addTriangle(startTri);
            size_t frontierHead = 0;

            while (clusterTriangles.size() < MAX_TRIANGLES_PER_CLUSTER)
            {
                if (frontierHead >= clusterTriangles.size())
                {
                    // 隣接でつながる三角形が尽きたら、クラスタの重心の平均に最も近い未割り当ての三角形から続ける
                    const double invCount = 1.0 / static_cast<double>(clusterTriangles.size());
                    const uint32_t nearestTri = centroidGrid.FindNearestUnassigned(
                        centroids, assigned,
                        static_cast<float>(sumX * invCount),
                        static_cast<float>(sumY * invCount),
                        static_cast<float>(sumZ * invCount));
                    if (nearestTri == TriangleCentroidGrid::NotFound)
                    {
                        break;
                    }
                    addTriangle(nearestTri);
                    continue;
                }

                const uint32_t currentTri = clusterTriangles[frontierHead++];

                for (uint32_t neighborTri : adjacency.Neighbors[currentTri])
                {
                    if (clusterTriangles.size() >= MAX_TRIANGLES_PER_CLUSTER)
                    {
                        break;
                    }

                    if (!assigned[neighborTri])
                    {
                        addTriangle(neighborTri);
                    }
                }
            }

            clusterTriangleLists.push_back(std::move(clusterTriangles));
        }

        // 並べ替えたインデックス配列を構築し、各クラスタのオフセットを設定
        outIndices.reserve(indexCount);

        for (auto &clusterTriangles : clusterTriangleLists)
        {
            MeshCluster cluster;
            cluster.IndexOffset = static_cast<uint32_t>(outIndices.size());
            cluster.IndexCount = static_cast<uint32_t>(clusterTriangles.size()) * 3;
            cluster.VertexOffset = 0;
            cluster.VertexCount = 0;
            cluster.LODLevel = 0;
            cluster.LODError = 0.0f;
            cluster.MaterialIndex = 0;

            // このクラスタの三角形インデックスをoutIndicesに追加
            for (uint32_t triIdx : clusterTriangles)
            {
                outIndices.push_back(indexData[triIdx * 3 + 0]);
                outIndices.push_back(indexData[triIdx * 3 + 1]);
                outIndices.push_back(indexData[triIdx * 3 + 2]);
            }

            // バウンディングと法線コーンを計算
            ComputeBoundingSphere(vertexPositions, vertexStride, outIndices.data(), cluster);
            ComputeNormalCone(vertexPositions, vertexStride, outIndices.data(), cluster);

            outClusters.push_back(cluster);
        }

        NORVES_LOG_INFO("MeshClusterizer", "メッシュを%uクラスタに分割（三角形数: %u）",
                        static_cast<uint32_t>(outClusters.size()), triangleCount);
    }

    // ========================================
    // 隣接グラフ構築
    // ========================================

    MeshClusterizer::TriangleAdjacency MeshClusterizer::BuildAdjacencyGraph(
        const void *vertexPositions,
        uint32_t vertexStride,
        const uint32_t *indexData,
        uint32_t triangleCount,
        uint32_t vertexCount)
    {
        TriangleAdjacency result;
        result.Neighbors.resize(triangleCount);

        // 同じ位置の頂点を代表の番号へまとめ、UVの継ぎ目などで複製された頂点の両側をつなぐ。
        // 範囲外の番号は代表と衝突しないよう（代表は vertexCount 未満）そのまま使う
        const VariableArray<uint32_t> representatives =
            BuildPositionRepresentatives(vertexPositions, vertexStride, vertexCount);
        auto representativeOf = [&](uint32_t vertex)
        {
            return vertex < vertexCount ? representatives[vertex] : vertex;
        };

        // エッジ→三角形のマッピングを構築
        // key: エッジキー(代表v0,代表v1), value: このエッジを共有する三角形インデックスのリスト
        UnorderedMap<uint64_t, VariableArray<uint32_t>> edgeToTriangles;
        edgeToTriangles.reserve(static_cast<size_t>(triangleCount) * 2u);

        for (uint32_t triIdx = 0; triIdx < triangleCount; ++triIdx)
        {
            const uint32_t corners[3] = {
                representativeOf(indexData[triIdx * 3 + 0]),
                representativeOf(indexData[triIdx * 3 + 1]),
                representativeOf(indexData[triIdx * 3 + 2])};

            // 3辺分のエッジキーを登録（同じ位置へ潰れた辺は隣接に使わない）
            for (uint32_t edge = 0; edge < 3; ++edge)
            {
                const uint32_t a = corners[edge];
                const uint32_t b = corners[(edge + 1) % 3];
                if (a != b)
                {
                    edgeToTriangles[MakeEdgeKey(a, b)].push_back(triIdx);
                }
            }
        }

        // エッジを共有する三角形同士を隣接として登録
        for (const auto &[edgeKey, triangles] : edgeToTriangles)
        {
            for (size_t i = 0; i < triangles.size(); ++i)
            {
                for (size_t j = i + 1; j < triangles.size(); ++j)
                {
                    uint32_t triA = triangles[i];
                    uint32_t triB = triangles[j];

                    // 重複チェック
                    auto &neighborsA = result.Neighbors[triA];
                    if (std::find(neighborsA.begin(), neighborsA.end(), triB) == neighborsA.end())
                    {
                        neighborsA.push_back(triB);
                    }

                    auto &neighborsB = result.Neighbors[triB];
                    if (std::find(neighborsB.begin(), neighborsB.end(), triA) == neighborsB.end())
                    {
                        neighborsB.push_back(triA);
                    }
                }
            }
        }

        return result;
    }

    // ========================================
    // 三角形の重心計算
    // ========================================

    void MeshClusterizer::ComputeTriangleCentroid(
        const void *vertexPositions,
        uint32_t vertexStride,
        const uint32_t *indexData,
        uint32_t triangleIndex,
        float &outX, float &outY, float &outZ)
    {
        float x0, y0, z0, x1, y1, z1, x2, y2, z2;
        GetVertexPosition(vertexPositions, vertexStride, indexData[triangleIndex * 3 + 0], x0, y0, z0);
        GetVertexPosition(vertexPositions, vertexStride, indexData[triangleIndex * 3 + 1], x1, y1, z1);
        GetVertexPosition(vertexPositions, vertexStride, indexData[triangleIndex * 3 + 2], x2, y2, z2);

        outX = (x0 + x1 + x2) / 3.0f;
        outY = (y0 + y1 + y2) / 3.0f;
        outZ = (z0 + z1 + z2) / 3.0f;
    }

    // ========================================
    // バウンディングスフィア計算
    // ========================================

    void MeshClusterizer::ComputeBoundingSphere(
        const void *vertexPositions,
        uint32_t vertexStride,
        const uint32_t *indexData,
        MeshCluster &cluster)
    {
        if (cluster.IndexCount == 0)
        {
            cluster.Bounds = {};
            return;
        }

        // まずAABBを計算
        float minX = 3.402823466e+38f, minY = minX, minZ = minX;
        float maxX = -3.402823466e+38f, maxY = maxX, maxZ = maxX;

        for (uint32_t i = 0; i < cluster.IndexCount; ++i)
        {
            uint32_t idx = indexData[cluster.IndexOffset + i];
            float x, y, z;
            GetVertexPosition(vertexPositions, vertexStride, idx, x, y, z);

            if (x < minX)
                minX = x;
            if (y < minY)
                minY = y;
            if (z < minZ)
                minZ = z;
            if (x > maxX)
                maxX = x;
            if (y > maxY)
                maxY = y;
            if (z > maxZ)
                maxZ = z;
        }

        // AABBの中心をスフィアの中心とする
        cluster.Bounds.CenterX = (minX + maxX) * 0.5f;
        cluster.Bounds.CenterY = (minY + maxY) * 0.5f;
        cluster.Bounds.CenterZ = (minZ + maxZ) * 0.5f;

        // 最大距離を半径とする
        float maxDistSq = 0.0f;
        for (uint32_t i = 0; i < cluster.IndexCount; ++i)
        {
            uint32_t idx = indexData[cluster.IndexOffset + i];
            float x, y, z;
            GetVertexPosition(vertexPositions, vertexStride, idx, x, y, z);

            float dx = x - cluster.Bounds.CenterX;
            float dy = y - cluster.Bounds.CenterY;
            float dz = z - cluster.Bounds.CenterZ;
            float distSq = dx * dx + dy * dy + dz * dz;
            if (distSq > maxDistSq)
            {
                maxDistSq = distSq;
            }
        }

        cluster.Bounds.Radius = std::sqrt(maxDistSq);
    }

    // ========================================
    // 法線コーン計算
    // ========================================

    void MeshClusterizer::ComputeNormalCone(
        const void *vertexPositions,
        uint32_t vertexStride,
        const uint32_t *indexData,
        MeshCluster &cluster)
    {
        if (cluster.IndexCount < 3)
        {
            cluster.ConeAxisX = 0.0f;
            cluster.ConeAxisY = 1.0f;
            cluster.ConeAxisZ = 0.0f;
            cluster.ConeCutoff = -1.0f;
            return;
        }

        uint32_t triangleCount = cluster.IndexCount / 3;

        // 全三角形の法線を計算して平均法線を取得
        float avgNx = 0.0f, avgNy = 0.0f, avgNz = 0.0f;

        VariableArray<float> normals(triangleCount * 3);

        for (uint32_t t = 0; t < triangleCount; ++t)
        {
            float nx, ny, nz;
            uint32_t tempIndices[3] =
                {
                    indexData[cluster.IndexOffset + t * 3 + 0],
                    indexData[cluster.IndexOffset + t * 3 + 1],
                    indexData[cluster.IndexOffset + t * 3 + 2]};

            // 直接法線計算（インライン展開）
            float x0, y0, z0, x1, y1, z1, x2, y2, z2;
            GetVertexPosition(vertexPositions, vertexStride, tempIndices[0], x0, y0, z0);
            GetVertexPosition(vertexPositions, vertexStride, tempIndices[1], x1, y1, z1);
            GetVertexPosition(vertexPositions, vertexStride, tempIndices[2], x2, y2, z2);

            float e1x = x1 - x0, e1y = y1 - y0, e1z = z1 - z0;
            float e2x = x2 - x0, e2y = y2 - y0, e2z = z2 - z0;

            nx = e2y * e1z - e2z * e1y;
            ny = e2z * e1x - e2x * e1z;
            nz = e2x * e1y - e2y * e1x;

            // 正規化
            float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (len > 1e-8f)
            {
                nx /= len;
                ny /= len;
                nz /= len;
            }

            normals[t * 3 + 0] = nx;
            normals[t * 3 + 1] = ny;
            normals[t * 3 + 2] = nz;

            avgNx += nx;
            avgNy += ny;
            avgNz += nz;
        }

        // 平均法線を正規化 → コーン軸
        float avgLen = std::sqrt(avgNx * avgNx + avgNy * avgNy + avgNz * avgNz);
        if (avgLen < 1e-8f)
        {
            // 法線がキャンセルしあった場合 → カリング不可
            cluster.ConeAxisX = 0.0f;
            cluster.ConeAxisY = 1.0f;
            cluster.ConeAxisZ = 0.0f;
            cluster.ConeCutoff = -1.0f;
            return;
        }

        cluster.ConeAxisX = avgNx / avgLen;
        cluster.ConeAxisY = avgNy / avgLen;
        cluster.ConeAxisZ = avgNz / avgLen;

        // 全法線との最小ドット積を計算 → コーンのカットオフ
        float minDot = 1.0f;
        for (uint32_t t = 0; t < triangleCount; ++t)
        {
            float dot = normals[t * 3 + 0] * cluster.ConeAxisX +
                        normals[t * 3 + 1] * cluster.ConeAxisY +
                        normals[t * 3 + 2] * cluster.ConeAxisZ;
            if (dot < minDot)
            {
                minDot = dot;
            }
        }

        cluster.ConeCutoff = minDot;
    }

} // namespace NorvesLib::Core::Rendering::MegaGeometry
