#pragma once

#include "Rendering/MegaGeometry/MegaGeometryTypes.h"
#include "Rendering/MegaGeometry/MeshClusterizer.h"
#include "Rendering/ProceduralMeshGenerator.h"
#include "Container/Containers.h"

#include <cmath>
#include <cstdint>

namespace NorvesLib::Core::Rendering::MegaGeometry
{
    /**
     * @brief 手続きで作るMegaGeometryの球の設定
     *
     * 緯度経度の格子（UV球）を、LOD0の分割数から段ごとに縦横半分ずつ粗くして作る。
     * 分割数は 2^(LODLevelCount-1) で割り切れる必要がある。
     */
    struct ProceduralMegaSphereSettings
    {
        float Radius = 1.0f;
        /** @brief LOD0の経度方向の分割数 */
        uint32_t Segments = 1280;
        /** @brief LOD0の緯度方向（極から極）の分割数 */
        uint32_t Rings = 640;
        uint32_t LODLevelCount = 5;
        /** @brief 1クラスタの辺のセル数（8なら8×8セル＝最大128三角形） */
        uint32_t PatchCells = 8;
        /** @brief 経度方向（1周）のテクスチャの繰り返し回数。整数なら継ぎ目で模様がつながる */
        float TexCoordRepeatU = 3.0f;
        /** @brief 緯度方向（極から極）のテクスチャの繰り返し回数 */
        float TexCoordRepeatV = 1.5f;
    };

    /**
     * @brief 手続きで作るMegaGeometryの球の結果
     *
     * 全段の頂点とインデックスを1本ずつに統合し、インデックスは全て統合頂点の通し番号
     * （クラスタの頂点の基点は0）。各段のクラスタは統合インデックスの中で連続している。
     */
    struct ProceduralMegaSphereData
    {
        Container::VariableArray<Mesh3DVertex> Vertices;
        Container::VariableArray<uint32_t> Indices;
        Container::VariableArray<MeshCluster> Clusters;
        /** @brief 段ごとの三角形数 */
        Container::VariableArray<uint32_t> LevelTriangleCounts;
        /** @brief 段ごとのクラスタ数 */
        Container::VariableArray<uint32_t> LevelClusterCounts;
        /** @brief 段ごとのLODの誤差（LOD0の面からのずれの上限。LOD0は0） */
        Container::VariableArray<float> LevelErrors;
        BoundingSphere Bounds;
    };

    /**
     * @brief クラスタとLODの階層を持つ高ポリの球を作る
     *
     * どの段も閉じた球のメッシュで、段kのクラスタ（格子のパッチ）の親は、同じ範囲を覆う段k+1の
     * パッチ1つ。LODの誤差は、平面の三角形と球面とのずれの上限（三角形の外接円の角半径 ρ で
     * R(1−cos ρ)）に、LOD0自身のずれを足したもの。メッシュ全体で共通のLOD球（MegaMeshCreateInfo::
     * LODBounds）と組み合わせると、全クラスタが同じ段を選ぶため段の境目に割れ目ができない。
     * 三角形の巻き方と法線・UVの向きは ProceduralMeshGenerator::GenerateUVSphere と同じ。
     *
     * @return 設定が不正（分割数が段数で割り切れない等）なら false
     */
    inline bool BuildProceduralMegaSphere(const ProceduralMegaSphereSettings &settings,
                                          ProceduralMegaSphereData &outData)
    {
        outData = ProceduralMegaSphereData{};

        const uint32_t levelCount = settings.LODLevelCount;
        const uint32_t patchCells = settings.PatchCells;
        if (!(settings.Radius > 0.0f) || levelCount == 0u || levelCount > 16u || patchCells == 0u ||
            2u * patchCells * patchCells > MAX_TRIANGLES_PER_CLUSTER)
        {
            return false;
        }
        const uint32_t coarsestDivisor = 1u << (levelCount - 1u);
        if (settings.Segments % coarsestDivisor != 0u || settings.Rings % coarsestDivisor != 0u ||
            settings.Segments / coarsestDivisor < 3u || settings.Rings / coarsestDivisor < 2u)
        {
            return false;
        }

        constexpr double kPi = 3.14159265358979323846;
        const float radius = settings.Radius;

        // 段ごとの頂点数・三角形数を先に数え、配列を一度に確保する。
        // 頂点の並び: 行0（北極）は列ごとに1つ（S個）、行1〜R-1 は S+1 個（経度0と1周の継ぎ目を重ねる）、
        // 行R（南極）は S 個。極の頂点を列ごとに分けるのは、UVを列の中央に置くため。
        uint64_t totalVertices = 0;
        uint64_t totalTriangles = 0;
        uint64_t totalClusters = 0;
        for (uint32_t level = 0; level < levelCount; ++level)
        {
            const uint64_t segments = settings.Segments >> level;
            const uint64_t rings = settings.Rings >> level;
            totalVertices += 2u * segments + (rings - 1u) * (segments + 1u);
            totalTriangles += 2u * segments * rings - 2u * segments;
            totalClusters += ((segments + patchCells - 1u) / patchCells) * ((rings + patchCells - 1u) / patchCells);
        }
        if (totalVertices > 0xFFFFFFFFull || totalTriangles * 3u > 0xFFFFFFFFull)
        {
            return false;
        }

        outData.Vertices.resize(static_cast<size_t>(totalVertices));
        outData.Indices.resize(static_cast<size_t>(totalTriangles * 3u));
        outData.Clusters.reserve(static_cast<size_t>(totalClusters));
        outData.LevelTriangleCounts.resize(levelCount);
        outData.LevelClusterCounts.resize(levelCount);
        outData.LevelErrors.resize(levelCount);

        Mesh3DVertex *vertices = outData.Vertices.data();
        uint32_t *indices = outData.Indices.data();
        uint32_t vertexBase = 0;
        uint32_t indexCursor = 0;

        // LOD0の三角形と球面とのずれ（段k≥1の誤差に足して、LOD0からのずれの上限にする）
        auto computeLevelDeviation = [&](uint32_t level) -> float
        {
            const double segmentAngle = 2.0 * kPi / static_cast<double>(settings.Segments >> level);
            const double ringAngle = kPi / static_cast<double>(settings.Rings >> level);
            // 赤道のセルが最も大きい。直角に近い三角形の外接円の角半径は斜辺の半分以下。
            const double circumAngle = 0.5 * std::sqrt(segmentAngle * segmentAngle + ringAngle * ringAngle);
            return static_cast<float>(static_cast<double>(radius) * (1.0 - std::cos(circumAngle)));
        };
        const float lod0Deviation = computeLevelDeviation(0u);

        Container::VariableArray<float> sinTheta;
        Container::VariableArray<float> cosTheta;
        Container::VariableArray<uint32_t> levelClusterStart(levelCount + 1u, 0u);

        for (uint32_t level = 0; level < levelCount; ++level)
        {
            const uint32_t segments = settings.Segments >> level;
            const uint32_t rings = settings.Rings >> level;
            const uint32_t rowStride = segments + 1u;

            // 行 r・列 c の頂点の通し番号
            auto vertexIndex = [&](uint32_t row, uint32_t column) -> uint32_t
            {
                if (row == 0u)
                {
                    return vertexBase + column;
                }
                if (row == rings)
                {
                    return vertexBase + segments + (rings - 1u) * rowStride + column;
                }
                return vertexBase + segments + (row - 1u) * rowStride + column;
            };

            // --- 頂点 ---
            sinTheta.resize(rowStride);
            cosTheta.resize(rowStride);
            for (uint32_t column = 0; column <= segments; ++column)
            {
                const double theta = 2.0 * kPi * static_cast<double>(column) / static_cast<double>(segments);
                sinTheta[column] = static_cast<float>(std::sin(theta));
                cosTheta[column] = static_cast<float>(std::cos(theta));
            }
            // 継ぎ目の位置を経度0とぴったり重ねる
            sinTheta[segments] = sinTheta[0];
            cosTheta[segments] = cosTheta[0];

            for (uint32_t column = 0; column < segments; ++column)
            {
                const float u = (static_cast<float>(column) + 0.5f) / static_cast<float>(segments) *
                                settings.TexCoordRepeatU;
                Mesh3DVertex &north = vertices[vertexIndex(0u, column)];
                north.Position[0] = 0.0f;
                north.Position[1] = radius;
                north.Position[2] = 0.0f;
                north.Normal[0] = 0.0f;
                north.Normal[1] = 1.0f;
                north.Normal[2] = 0.0f;
                north.TexCoord[0] = u;
                north.TexCoord[1] = 0.0f;

                Mesh3DVertex &south = vertices[vertexIndex(rings, column)];
                south.Position[0] = 0.0f;
                south.Position[1] = -radius;
                south.Position[2] = 0.0f;
                south.Normal[0] = 0.0f;
                south.Normal[1] = -1.0f;
                south.Normal[2] = 0.0f;
                south.TexCoord[0] = u;
                south.TexCoord[1] = settings.TexCoordRepeatV;
            }
            for (uint32_t row = 1; row < rings; ++row)
            {
                const double phi = kPi * static_cast<double>(row) / static_cast<double>(rings);
                const float sinPhi = static_cast<float>(std::sin(phi));
                const float cosPhi = static_cast<float>(std::cos(phi));
                const float v = static_cast<float>(row) / static_cast<float>(rings) * settings.TexCoordRepeatV;
                Mesh3DVertex *rowVertices = vertices + vertexIndex(row, 0u);
                for (uint32_t column = 0; column <= segments; ++column)
                {
                    const float nx = sinPhi * cosTheta[column];
                    const float ny = cosPhi;
                    const float nz = sinPhi * sinTheta[column];
                    Mesh3DVertex &vertex = rowVertices[column];
                    vertex.Position[0] = radius * nx;
                    vertex.Position[1] = radius * ny;
                    vertex.Position[2] = radius * nz;
                    vertex.Normal[0] = nx;
                    vertex.Normal[1] = ny;
                    vertex.Normal[2] = nz;
                    vertex.TexCoord[0] = static_cast<float>(column) / static_cast<float>(segments) *
                                         settings.TexCoordRepeatU;
                    vertex.TexCoord[1] = v;
                }
            }

            // --- クラスタ（パッチ）ごとのインデックス ---
            const uint32_t patchColumns = (segments + patchCells - 1u) / patchCells;
            const uint32_t patchRows = (rings + patchCells - 1u) / patchCells;
            levelClusterStart[level] = static_cast<uint32_t>(outData.Clusters.size());
            const float levelError = level == 0u ? 0.0f : computeLevelDeviation(level) + lod0Deviation;
            outData.LevelErrors[level] = levelError;

            const uint32_t levelIndexStart = indexCursor;
            for (uint32_t patchRow = 0; patchRow < patchRows; ++patchRow)
            {
                const uint32_t rowBegin = patchRow * patchCells;
                const uint32_t rowEnd = rowBegin + patchCells < rings ? rowBegin + patchCells : rings;
                for (uint32_t patchColumn = 0; patchColumn < patchColumns; ++patchColumn)
                {
                    const uint32_t columnBegin = patchColumn * patchCells;
                    const uint32_t columnEnd =
                        columnBegin + patchCells < segments ? columnBegin + patchCells : segments;

                    MeshCluster cluster;
                    cluster.IndexOffset = indexCursor;
                    cluster.VertexOffset = 0;
                    cluster.LODLevel = level;
                    cluster.LODError = levelError;
                    cluster.MaterialIndex = 0;

                    for (uint32_t row = rowBegin; row < rowEnd; ++row)
                    {
                        for (uint32_t column = columnBegin; column < columnEnd; ++column)
                        {
                            const uint32_t i0 = vertexIndex(row, column);
                            const uint32_t i2 = vertexIndex(row + 1u, column);
                            if (row == 0u)
                            {
                                // 北極の列: 極の頂点から次の行の2頂点へ（GenerateUVSphere の北極キャップと同じ向き）
                                indices[indexCursor++] = i0;
                                indices[indexCursor++] = i2;
                                indices[indexCursor++] = vertexIndex(1u, column + 1u);
                                continue;
                            }
                            const uint32_t i1 = vertexIndex(row, column + 1u);
                            if (row + 1u == rings)
                            {
                                // 南極の列: 上の行の2頂点と極の頂点
                                indices[indexCursor++] = i0;
                                indices[indexCursor++] = i2;
                                indices[indexCursor++] = i1;
                                continue;
                            }
                            const uint32_t i3 = vertexIndex(row + 1u, column + 1u);
                            indices[indexCursor++] = i0;
                            indices[indexCursor++] = i2;
                            indices[indexCursor++] = i1;
                            indices[indexCursor++] = i1;
                            indices[indexCursor++] = i2;
                            indices[indexCursor++] = i3;
                        }
                    }

                    cluster.IndexCount = indexCursor - cluster.IndexOffset;
                    MeshClusterizer::ComputeBoundingSphere(vertices, sizeof(Mesh3DVertex), indices, cluster);
                    MeshClusterizer::ComputeNormalCone(vertices, sizeof(Mesh3DVertex), indices, cluster);
                    outData.Clusters.push_back(cluster);
                }
            }

            outData.LevelTriangleCounts[level] = (indexCursor - levelIndexStart) / 3u;
            outData.LevelClusterCounts[level] = patchRows * patchColumns;
            vertexBase += 2u * segments + (rings - 1u) * rowStride;
        }
        levelClusterStart[levelCount] = static_cast<uint32_t>(outData.Clusters.size());

        // 親のリンク: 段kのパッチ(行pr, 列pc)は、段k+1のパッチ(pr/2, pc/2)と同じ範囲に含まれる。
        for (uint32_t level = 0; level + 1u < levelCount; ++level)
        {
            const uint32_t segments = settings.Segments >> level;
            const uint32_t patchColumns = (segments + patchCells - 1u) / patchCells;
            const uint32_t parentPatchColumns = ((segments >> 1u) + patchCells - 1u) / patchCells;
            for (uint32_t clusterIndex = levelClusterStart[level]; clusterIndex < levelClusterStart[level + 1u];
                 ++clusterIndex)
            {
                const uint32_t local = clusterIndex - levelClusterStart[level];
                const uint32_t patchRow = local / patchColumns;
                const uint32_t patchColumn = local % patchColumns;
                MeshCluster &cluster = outData.Clusters[clusterIndex];
                cluster.ParentStart =
                    levelClusterStart[level + 1u] + (patchRow / 2u) * parentPatchColumns + patchColumn / 2u;
                cluster.ParentCount = 1u;
            }
        }

        outData.Bounds.CenterX = 0.0f;
        outData.Bounds.CenterY = 0.0f;
        outData.Bounds.CenterZ = 0.0f;
        outData.Bounds.Radius = radius;
        return indexCursor == outData.Indices.size() && vertexBase == outData.Vertices.size();
    }

} // namespace NorvesLib::Core::Rendering::MegaGeometry
