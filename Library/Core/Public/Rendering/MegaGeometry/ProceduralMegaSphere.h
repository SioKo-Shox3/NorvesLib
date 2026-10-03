#pragma once

#include "Rendering/MegaGeometry/MegaGeometryTypes.h"
#include "Rendering/MegaGeometry/MeshClusterizer.h"
#include "Rendering/ProceduralMeshGenerator.h"
#include "Container/Containers.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace NorvesLib::Core::Rendering::MegaGeometry
{
    /**
     * @brief 球の変位に使う高さの場（0〜1、1が高い）
     *
     * 元の画像（辺 SourceSize 画素の正方形で、縦横とも継ぎ目なく繰り返す）のミップを、FirstMip 段目から
     * 粗い方へ Levels に持つ。Levels[i] の辺は SourceSize >> (FirstMip + i) 画素で、行が v・列が u。
     */
    struct ProceduralMegaSphereHeightField
    {
        Container::VariableArray<Container::VariableArray<float>> Levels;
        uint32_t SourceSize = 0;
        uint32_t FirstMip = 0;
    };

    /**
     * @brief 16ビットのグレーの高さの画像から、FirstMip 段目以降のミップを箱型の平均で作る
     *
     * 球の頂点の間隔は元の画像の数画素以上あるため、細かい段は持たない（FirstMip 段目を直接平均で作る）。
     *
     * @return 画像が2の累乗の正方形でない、または FirstMip 段目が1画素未満なら false
     */
    inline bool BuildProceduralMegaSphereHeightField(const uint16_t *pixels,
                                                     uint32_t size,
                                                     uint32_t firstMip,
                                                     ProceduralMegaSphereHeightField &outField)
    {
        outField = ProceduralMegaSphereHeightField{};
        if (!pixels || size == 0u || (size & (size - 1u)) != 0u || firstMip >= 31u || (size >> firstMip) == 0u)
        {
            return false;
        }

        const uint32_t firstSize = size >> firstMip;
        const uint32_t block = 1u << firstMip;
        const double scale = 1.0 / (65535.0 * static_cast<double>(block) * static_cast<double>(block));
        outField.SourceSize = size;
        outField.FirstMip = firstMip;

        Container::VariableArray<float> first(static_cast<size_t>(firstSize) * firstSize);
        for (uint32_t y = 0; y < firstSize; ++y)
        {
            for (uint32_t x = 0; x < firstSize; ++x)
            {
                uint64_t sum = 0;
                for (uint32_t by = 0; by < block; ++by)
                {
                    const uint16_t *row = pixels + static_cast<size_t>(y * block + by) * size + x * block;
                    for (uint32_t bx = 0; bx < block; ++bx)
                    {
                        sum += row[bx];
                    }
                }
                first[static_cast<size_t>(y) * firstSize + x] = static_cast<float>(static_cast<double>(sum) * scale);
            }
        }
        outField.Levels.push_back(std::move(first));

        for (uint32_t levelSize = firstSize; levelSize > 1u; levelSize >>= 1u)
        {
            const Container::VariableArray<float> &fine = outField.Levels.back();
            const uint32_t coarseSize = levelSize >> 1u;
            Container::VariableArray<float> coarse(static_cast<size_t>(coarseSize) * coarseSize);
            for (uint32_t y = 0; y < coarseSize; ++y)
            {
                for (uint32_t x = 0; x < coarseSize; ++x)
                {
                    const size_t base = static_cast<size_t>(2u * y) * levelSize + 2u * x;
                    coarse[static_cast<size_t>(y) * coarseSize + x] =
                        0.25f * (fine[base] + fine[base + 1u] + fine[base + levelSize] + fine[base + levelSize + 1u]);
                }
            }
            outField.Levels.push_back(std::move(coarse));
        }
        return true;
    }

    /**
     * @brief 高さの場を、繰り返しの座標(u, v)と元の画像でのミップの段（小数）で3線形に引く
     *
     * 縦横とも繰り返す（u・v の整数部は無視する）。段は持っている範囲に収める。
     */
    inline float SampleProceduralMegaSphereHeightField(const ProceduralMegaSphereHeightField &field,
                                                       double u,
                                                       double v,
                                                       double mip)
    {
        if (field.Levels.empty())
        {
            return 1.0f;
        }
        const double lastLevel = static_cast<double>(field.Levels.size() - 1u);
        const double levelPosition = std::clamp(mip - static_cast<double>(field.FirstMip), 0.0, lastLevel);
        const uint32_t level0 = static_cast<uint32_t>(levelPosition);
        const uint32_t level1 = std::min<uint32_t>(level0 + 1u, static_cast<uint32_t>(field.Levels.size() - 1u));
        const double levelWeight = levelPosition - static_cast<double>(level0);

        auto sampleLevel = [&](uint32_t level) -> double
        {
            const int64_t levelSize = static_cast<int64_t>((field.SourceSize >> field.FirstMip) >> level);
            const Container::VariableArray<float> &texels = field.Levels[level];
            // 画素の中心を (i + 0.5) / size に置く
            const double x = u * static_cast<double>(levelSize) - 0.5;
            const double y = v * static_cast<double>(levelSize) - 0.5;
            const double fx = std::floor(x);
            const double fy = std::floor(y);
            const double tx = x - fx;
            const double ty = y - fy;
            auto wrap = [levelSize](int64_t i) -> size_t
            {
                const int64_t m = i % levelSize;
                return static_cast<size_t>(m < 0 ? m + levelSize : m);
            };
            const size_t x0 = wrap(static_cast<int64_t>(fx));
            const size_t x1 = wrap(static_cast<int64_t>(fx) + 1);
            const size_t y0 = wrap(static_cast<int64_t>(fy)) * static_cast<size_t>(levelSize);
            const size_t y1 = wrap(static_cast<int64_t>(fy) + 1) * static_cast<size_t>(levelSize);
            const double top = texels[y0 + x0] + (texels[y0 + x1] - texels[y0 + x0]) * tx;
            const double bottom = texels[y1 + x0] + (texels[y1 + x1] - texels[y1 + x0]) * tx;
            return top + (bottom - top) * ty;
        };

        const double h0 = sampleLevel(level0);
        if (level1 == level0 || levelWeight <= 0.0)
        {
            return static_cast<float>(h0);
        }
        return static_cast<float>(h0 + (sampleLevel(level1) - h0) * levelWeight);
    }

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

        /**
         * @brief 変位に使う高さの場（nullptr なら変位しない）
         *
         * 各頂点を、そのUVで引いた高さ h により半径方向へ −(1−h)×DisplacementDepth 動かす（h=1 は元の球面に
         * 残り、窪みは内側へ入るので境界球は変わらない）。高さは段ごとの頂点の間隔に合ったミップで引く。
         */
        const ProceduralMegaSphereHeightField *HeightField = nullptr;
        /** @brief 高さ0の点を球面から内側へ動かす距離（m） */
        float DisplacementDepth = 0.0f;
        /**
         * @brief 変位を極へ向けて弱める範囲（sin(極からの角度)＝回転軸からの距離/半径）
         *
         * 極の近くはテクスチャが経度方向へ縮み、変位の傾きが 1/sin で急になって棘になるため、
         * sin が PoleFadeEndSin 以上で全量、PoleFadeStartSin 以下で0にし、間を smoothstep でつなぐ。
         */
        float PoleFadeStartSin = 0.15f;
        float PoleFadeEndSin = 0.5f;
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
        /** @brief 段ごとの変位の誤差（LOD0の変位した面との半径方向のずれの最大。LevelErrors はこれと球面とのずれの大きい方） */
        Container::VariableArray<float> LevelDisplacementErrors;
        /** @brief 変位で最も内側へ動いた量（m。変位しなければ0） */
        float MaxDisplacementDepth = 0.0f;
        /** @brief 作り直した法線が退化・裏返りのため元の球の法線に戻った頂点の数 */
        uint32_t NormalFallbackCount = 0;
        /** @brief 変位したときのLOD0の頂点の間隔（UV単位。MegaMeshMaterial::DisplacementUVSpacing へ渡す。変位しなければ0） */
        float DisplacementUVSpacing = 0.0f;
        /**
         * @brief LOD0の作り直した法線と元の球の法線との角度の最大（度）
         *
         * 変位が全量の帯（sin(極からの角度) ≥ PoleFadeEndSin）と、極へ向けて弱める帯に分けて持つ。
         * 弱める帯の値が全量の帯を大きく超えなければ、極の付近に棘が無い。
         */
        float MaxNormalTiltFullDegrees = 0.0f;
        float MaxNormalTiltPoleFadeDegrees = 0.0f;
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
     * 高さの場があれば各段の頂点を変位させ、法線を変位した格子の隣の頂点の差から作り直す。経度の継ぎ目の
     * 列は経度0の列の位置・法線をそのまま写し、極の頂点は動かさない（極の付近は変位が0なので、極の頂点を
     * 列ごとに分けても位置が揃う）。変位した段のLODの誤差は、球面とのずれと、LOD0の変位した面との半径方向の
     * ずれの最大の大きい方。
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
        const ProceduralMegaSphereHeightField *heightField = settings.HeightField;
        if (heightField &&
            (heightField->Levels.empty() || heightField->SourceSize == 0u ||
             !(settings.DisplacementDepth >= 0.0f) || !(settings.DisplacementDepth < settings.Radius) ||
             !(settings.PoleFadeStartSin >= 0.0f) || !(settings.PoleFadeEndSin > settings.PoleFadeStartSin)))
        {
            return false;
        }

        constexpr double kPi = 3.14159265358979323846;
        const float radius = settings.Radius;

        // 極からの角度の sin に対する変位の重み（極の付近で0、PoleFadeEndSin 以上で1）
        auto poleFadeWeight = [&](double sinPhi) -> double
        {
            const double t = std::clamp((sinPhi - static_cast<double>(settings.PoleFadeStartSin)) /
                                            static_cast<double>(settings.PoleFadeEndSin - settings.PoleFadeStartSin),
                                        0.0,
                                        1.0);
            return t * t * (3.0 - 2.0 * t);
        };

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
        outData.LevelDisplacementErrors.resize(levelCount, 0.0f);

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
        Container::VariableArray<uint32_t> levelVertexBase(levelCount, 0u);

        for (uint32_t level = 0; level < levelCount; ++level)
        {
            const uint32_t segments = settings.Segments >> level;
            const uint32_t rings = settings.Rings >> level;
            const uint32_t rowStride = segments + 1u;
            levelVertexBase[level] = vertexBase;

            // この段の頂点の間隔（元の画像の画素数）に合ったミップで高さを引く
            double heightMip = 0.0;
            if (heightField)
            {
                const double texelsPerColumn = static_cast<double>(heightField->SourceSize) *
                                               static_cast<double>(settings.TexCoordRepeatU) / segments;
                const double texelsPerRow = static_cast<double>(heightField->SourceSize) *
                                            static_cast<double>(settings.TexCoordRepeatV) / rings;
                heightMip = std::log2(std::max(std::max(texelsPerColumn, texelsPerRow), 1.0));
            }

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
                const double fadeWeight = heightField ? poleFadeWeight(std::sin(phi)) : 0.0;
                const double heightV = static_cast<double>(row) / rings * settings.TexCoordRepeatV;
                Mesh3DVertex *rowVertices = vertices + vertexIndex(row, 0u);
                for (uint32_t column = 0; column <= segments; ++column)
                {
                    const float nx = sinPhi * cosTheta[column];
                    const float ny = cosPhi;
                    const float nz = sinPhi * sinTheta[column];
                    Mesh3DVertex &vertex = rowVertices[column];
                    float displacedRadius = radius;
                    if (fadeWeight > 0.0 && column < segments)
                    {
                        const double heightU = static_cast<double>(column) / segments * settings.TexCoordRepeatU;
                        const double height = SampleProceduralMegaSphereHeightField(*heightField, heightU, heightV, heightMip);
                        const double depth = (1.0 - std::clamp(height, 0.0, 1.0)) * settings.DisplacementDepth * fadeWeight;
                        displacedRadius = static_cast<float>(static_cast<double>(radius) - depth);
                        outData.MaxDisplacementDepth = std::max(outData.MaxDisplacementDepth, static_cast<float>(depth));
                    }
                    vertex.Position[0] = displacedRadius * nx;
                    vertex.Position[1] = displacedRadius * ny;
                    vertex.Position[2] = displacedRadius * nz;
                    if (fadeWeight > 0.0 && column == segments)
                    {
                        // 継ぎ目の列は経度0の列の位置を写す（ビット単位で一致して割れ目ができない）
                        vertex.Position[0] = rowVertices[0].Position[0];
                        vertex.Position[1] = rowVertices[0].Position[1];
                        vertex.Position[2] = rowVertices[0].Position[2];
                    }
                    vertex.Normal[0] = nx;
                    vertex.Normal[1] = ny;
                    vertex.Normal[2] = nz;
                    vertex.TexCoord[0] = static_cast<float>(column) / static_cast<float>(segments) *
                                         settings.TexCoordRepeatU;
                    vertex.TexCoord[1] = v;
                }
            }

            // --- 変位した形からの法線 ---
            // 隣の列・行の頂点の差（経度方向は継ぎ目をまたいで回り込み、緯度方向の端は極の頂点）の外積。
            // 継ぎ目の列は経度0の列の法線を写す。退化・裏返りは元の球の法線に戻して数える。
            if (heightField)
            {
                for (uint32_t row = 1; row < rings; ++row)
                {
                    Mesh3DVertex *rowVertices = vertices + vertexIndex(row, 0u);
                    for (uint32_t column = 0; column < segments; ++column)
                    {
                        const uint32_t left = column == 0u ? segments - 1u : column - 1u;
                        const float *pl = vertices[vertexIndex(row, left)].Position;
                        const float *pr = vertices[vertexIndex(row, column + 1u)].Position;
                        const float *pu = vertices[vertexIndex(row - 1u, column)].Position;
                        const float *pd = vertices[vertexIndex(row + 1u, column)].Position;
                        const double alongColumn[3] = {static_cast<double>(pr[0]) - pl[0],
                                                       static_cast<double>(pr[1]) - pl[1],
                                                       static_cast<double>(pr[2]) - pl[2]};
                        const double alongRow[3] = {static_cast<double>(pd[0]) - pu[0],
                                                    static_cast<double>(pd[1]) - pu[1],
                                                    static_cast<double>(pd[2]) - pu[2]};
                        // 経度方向 × 緯度方向（南向き）が外向き
                        double n[3] = {alongColumn[1] * alongRow[2] - alongColumn[2] * alongRow[1],
                                       alongColumn[2] * alongRow[0] - alongColumn[0] * alongRow[2],
                                       alongColumn[0] * alongRow[1] - alongColumn[1] * alongRow[0]};
                        const double length = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                        Mesh3DVertex &vertex = rowVertices[column];
                        const double facing = length > 0.0 ? (n[0] * vertex.Normal[0] + n[1] * vertex.Normal[1] +
                                                              n[2] * vertex.Normal[2]) / length
                                                           : 0.0;
                        if (!(length > 1.0e-12) || !(facing > 0.0))
                        {
                            ++outData.NormalFallbackCount;
                            continue;
                        }
                        if (level == 0u)
                        {
                            const double tiltDegrees = std::acos(std::min(facing, 1.0)) * 180.0 / kPi;
                            const double sinPhi = std::sin(kPi * static_cast<double>(row) / static_cast<double>(rings));
                            float &maxTilt = sinPhi >= static_cast<double>(settings.PoleFadeEndSin)
                                                 ? outData.MaxNormalTiltFullDegrees
                                                 : outData.MaxNormalTiltPoleFadeDegrees;
                            maxTilt = std::max(maxTilt, static_cast<float>(tiltDegrees));
                        }
                        vertex.Normal[0] = static_cast<float>(n[0] / length);
                        vertex.Normal[1] = static_cast<float>(n[1] / length);
                        vertex.Normal[2] = static_cast<float>(n[2] / length);
                    }
                    rowVertices[segments].Normal[0] = rowVertices[0].Normal[0];
                    rowVertices[segments].Normal[1] = rowVertices[0].Normal[1];
                    rowVertices[segments].Normal[2] = rowVertices[0].Normal[2];
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

        // 変位した段のLODの誤差: LOD0の各頂点で、段kの格子の半径方向の変位を双線形に補間した値との差の最大と、
        // 平面の三角形と球面とのずれの大きい方（粗い段で失われる凹凸の高さ。これが無いと近くでも粗い段が選ばれる）。
        if (heightField && levelCount > 1u)
        {
            auto radialOffset = [&](uint32_t level, uint32_t row, uint32_t column) -> double
            {
                const uint32_t segments = settings.Segments >> level;
                const uint32_t rings = settings.Rings >> level;
                if (row == 0u || row >= rings)
                {
                    return 0.0;
                }
                const Mesh3DVertex &vertex =
                    vertices[levelVertexBase[level] + segments + (row - 1u) * (segments + 1u) + column];
                const double length = std::sqrt(static_cast<double>(vertex.Position[0]) * vertex.Position[0] +
                                                static_cast<double>(vertex.Position[1]) * vertex.Position[1] +
                                                static_cast<double>(vertex.Position[2]) * vertex.Position[2]);
                return length - static_cast<double>(radius);
            };

            for (uint32_t level = 1; level < levelCount; ++level)
            {
                const uint32_t step = 1u << level;
                double maxDifference = 0.0;
                for (uint32_t row = 1; row < settings.Rings; ++row)
                {
                    const uint32_t coarseRow = row / step;
                    const double rowWeight = static_cast<double>(row % step) / step;
                    for (uint32_t column = 0; column < settings.Segments; ++column)
                    {
                        const uint32_t coarseColumn = column / step;
                        const double columnWeight = static_cast<double>(column % step) / step;
                        const double top = radialOffset(level, coarseRow, coarseColumn) +
                                           (radialOffset(level, coarseRow, coarseColumn + 1u) -
                                            radialOffset(level, coarseRow, coarseColumn)) * columnWeight;
                        const double bottom = radialOffset(level, coarseRow + 1u, coarseColumn) +
                                              (radialOffset(level, coarseRow + 1u, coarseColumn + 1u) -
                                               radialOffset(level, coarseRow + 1u, coarseColumn)) * columnWeight;
                        const double coarse = top + (bottom - top) * rowWeight;
                        maxDifference = std::max(maxDifference, std::abs(radialOffset(0u, row, column) - coarse));
                    }
                }
                outData.LevelDisplacementErrors[level] = static_cast<float>(maxDifference);
                // 球面からのずれと変位の差は同じ半径方向のずれなので、大きい方をこの段の誤差にする。
                // 粗い段ほど誤差が大きい順を保つ（カリングは親の誤差が子以上であることを前提にする）
                outData.LevelErrors[level] = std::max(std::max(outData.LevelErrors[level], static_cast<float>(maxDifference)),
                                                      outData.LevelErrors[level - 1u]);
            }
            for (MeshCluster &cluster : outData.Clusters)
            {
                cluster.LODError = outData.LevelErrors[cluster.LODLevel];
            }
        }

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

        if (heightField)
        {
            outData.DisplacementUVSpacing =
                std::max(settings.TexCoordRepeatU / static_cast<float>(settings.Segments),
                         settings.TexCoordRepeatV / static_cast<float>(settings.Rings));
        }
        // 変位は内側へだけ動かすので、元の球が境界球のまま
        outData.Bounds.CenterX = 0.0f;
        outData.Bounds.CenterY = 0.0f;
        outData.Bounds.CenterZ = 0.0f;
        outData.Bounds.Radius = radius;
        return indexCursor == outData.Indices.size() && vertexBase == outData.Vertices.size();
    }

    /**
     * @brief 同じ位置に重なるはずの頂点（経度の継ぎ目の列と経度0の列、極の列ごとの頂点）の食い違いを数える
     *
     * 格子の三角形は球を隙間なく覆うので、重なる頂点の位置がビット単位で一致すればメッシュに穴・割れは無い。
     * 位置か法線のどちらかが一致しない頂点を1つと数える（法線の食い違いは陰影の継ぎ目になる）。
     */
    inline uint32_t CountProceduralMegaSphereSeamMismatches(const ProceduralMegaSphereSettings &settings,
                                                            const ProceduralMegaSphereData &data)
    {
        auto differs = [](const Mesh3DVertex &a, const Mesh3DVertex &b)
        {
            for (int i = 0; i < 3; ++i)
            {
                if (a.Position[i] != b.Position[i] || a.Normal[i] != b.Normal[i])
                {
                    return true;
                }
            }
            return false;
        };

        uint32_t mismatches = 0;
        size_t vertexBase = 0;
        for (uint32_t level = 0; level < settings.LODLevelCount; ++level)
        {
            const uint32_t segments = settings.Segments >> level;
            const uint32_t rings = settings.Rings >> level;
            const size_t levelVertexCount = 2u * static_cast<size_t>(segments) +
                                            static_cast<size_t>(rings - 1u) * (segments + 1u);
            if (vertexBase + levelVertexCount > data.Vertices.size())
            {
                return UINT32_MAX;
            }
            const Mesh3DVertex *north = data.Vertices.data() + vertexBase;
            const Mesh3DVertex *south = north + segments + static_cast<size_t>(rings - 1u) * (segments + 1u);
            for (uint32_t column = 1; column < segments; ++column)
            {
                mismatches += differs(north[column], north[0]) ? 1u : 0u;
                mismatches += differs(south[column], south[0]) ? 1u : 0u;
            }
            for (uint32_t row = 1; row < rings; ++row)
            {
                const Mesh3DVertex *rowVertices = north + segments + static_cast<size_t>(row - 1u) * (segments + 1u);
                mismatches += differs(rowVertices[segments], rowVertices[0]) ? 1u : 0u;
            }
            vertexBase += levelVertexCount;
        }
        return mismatches;
    }

} // namespace NorvesLib::Core::Rendering::MegaGeometry
