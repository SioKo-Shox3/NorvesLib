#include "Rendering/MegaGeometry/MeshClusterizer.h"
#include "Logging/LogMacros.h"
#include <cmath>
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
        inline void GetVertexPosition(const void* vertexData, uint32_t vertexStride,
                                       uint32_t vertexIndex, float& x, float& y, float& z)
        {
            const auto* base = static_cast<const uint8_t*>(vertexData);
            const auto* pos = reinterpret_cast<const float*>(base + static_cast<size_t>(vertexIndex) * vertexStride);
            x = pos[0];
            y = pos[1];
            z = pos[2];
        }

        /**
         * @brief 三角形の法線を計算（正規化なし）
         */
        inline void ComputeTriangleNormal(const void* vertexData, uint32_t vertexStride,
                                            const uint32_t* indices, uint32_t triIndex,
                                            float& nx, float& ny, float& nz)
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

            // cross product
            nx = e1y * e2z - e1z * e2y;
            ny = e1z * e2x - e1x * e2z;
            nz = e1x * e2y - e1y * e2x;
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
    } // anonymous namespace

    // ========================================
    // Clusterize
    // ========================================

    void MeshClusterizer::Clusterize(
        const void* vertexPositions,
        uint32_t vertexCount,
        uint32_t vertexStride,
        const uint32_t* indexData,
        uint32_t indexCount,
        VariableArray<MeshCluster>& outClusters,
        VariableArray<uint32_t>& outIndices)
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
        TriangleAdjacency adjacency = BuildAdjacencyGraph(indexData, triangleCount, vertexCount);

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

            // BFS/貪欲成長でクラスタを拡大
            VariableArray<uint32_t> frontier;
            frontier.push_back(startTri);
            assigned[startTri] = true;
            clusterTriangles.push_back(startTri);

            while (clusterTriangles.size() < MAX_TRIANGLES_PER_CLUSTER && !frontier.empty())
            {
                uint32_t currentTri = frontier.front();
                frontier.erase(frontier.begin());

                for (uint32_t neighborTri : adjacency.Neighbors[currentTri])
                {
                    if (clusterTriangles.size() >= MAX_TRIANGLES_PER_CLUSTER)
                    {
                        break;
                    }

                    if (!assigned[neighborTri])
                    {
                        assigned[neighborTri] = true;
                        clusterTriangles.push_back(neighborTri);
                        frontier.push_back(neighborTri);
                    }
                }
            }

            clusterTriangleLists.push_back(std::move(clusterTriangles));
        }

        // 並べ替えたインデックス配列を構築し、各クラスタのオフセットを設定
        outIndices.reserve(indexCount);

        for (auto& clusterTriangles : clusterTriangleLists)
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
        const uint32_t* indexData,
        uint32_t triangleCount,
        uint32_t vertexCount)
    {
        TriangleAdjacency result;
        result.Neighbors.resize(triangleCount);

        // エッジ→三角形のマッピングを構築
        // key: エッジキー(v0,v1), value: このエッジを共有する三角形インデックスのリスト
        UnorderedMap<uint64_t, VariableArray<uint32_t>> edgeToTriangles;

        for (uint32_t triIdx = 0; triIdx < triangleCount; ++triIdx)
        {
            uint32_t i0 = indexData[triIdx * 3 + 0];
            uint32_t i1 = indexData[triIdx * 3 + 1];
            uint32_t i2 = indexData[triIdx * 3 + 2];

            // 3辺分のエッジキーを登録
            uint64_t edge0 = MakeEdgeKey(i0, i1);
            uint64_t edge1 = MakeEdgeKey(i1, i2);
            uint64_t edge2 = MakeEdgeKey(i2, i0);

            edgeToTriangles[edge0].push_back(triIdx);
            edgeToTriangles[edge1].push_back(triIdx);
            edgeToTriangles[edge2].push_back(triIdx);
        }

        // エッジを共有する三角形同士を隣接として登録
        for (const auto& [edgeKey, triangles] : edgeToTriangles)
        {
            for (size_t i = 0; i < triangles.size(); ++i)
            {
                for (size_t j = i + 1; j < triangles.size(); ++j)
                {
                    uint32_t triA = triangles[i];
                    uint32_t triB = triangles[j];

                    // 重複チェック
                    auto& neighborsA = result.Neighbors[triA];
                    if (std::find(neighborsA.begin(), neighborsA.end(), triB) == neighborsA.end())
                    {
                        neighborsA.push_back(triB);
                    }

                    auto& neighborsB = result.Neighbors[triB];
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
        const void* vertexPositions,
        uint32_t vertexStride,
        const uint32_t* indexData,
        uint32_t triangleIndex,
        float& outX, float& outY, float& outZ)
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
        const void* vertexPositions,
        uint32_t vertexStride,
        const uint32_t* indexData,
        MeshCluster& cluster)
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

            if (x < minX) minX = x;
            if (y < minY) minY = y;
            if (z < minZ) minZ = z;
            if (x > maxX) maxX = x;
            if (y > maxY) maxY = y;
            if (z > maxZ) maxZ = z;
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
        const void* vertexPositions,
        uint32_t vertexStride,
        const uint32_t* indexData,
        MeshCluster& cluster)
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
                indexData[cluster.IndexOffset + t * 3 + 2]
            };

            // 直接法線計算（インライン展開）
            float x0, y0, z0, x1, y1, z1, x2, y2, z2;
            GetVertexPosition(vertexPositions, vertexStride, tempIndices[0], x0, y0, z0);
            GetVertexPosition(vertexPositions, vertexStride, tempIndices[1], x1, y1, z1);
            GetVertexPosition(vertexPositions, vertexStride, tempIndices[2], x2, y2, z2);

            float e1x = x1 - x0, e1y = y1 - y0, e1z = z1 - z0;
            float e2x = x2 - x0, e2y = y2 - y0, e2z = z2 - z0;

            nx = e1y * e2z - e1z * e2y;
            ny = e1z * e2x - e1x * e2z;
            nz = e1x * e2y - e1y * e2x;

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
