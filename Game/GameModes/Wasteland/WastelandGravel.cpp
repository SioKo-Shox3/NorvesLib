#include "GameModes/Wasteland/WastelandGravel.h"

#include "Core/Public/Logging/LogMacros.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Container;
using namespace NorvesLib::Core::Rendering;

namespace Game::GameModes::WastelandGravel
{
    namespace
    {
        constexpr float kPi = 3.14159265358979f;

        /// 大きさの段の値
        struct TierDesc
        {
            const char *Name;
            /// 候補の升目[m]（升目ごとに 1 つの候補を升目の中のでたらめな位置に置き、密度で間引く）
            float CandidateSpacing;
            /// 区画の一辺の升目の数
            int32_t CandidatesPerCell;
            /// 石の長い辺の範囲[m]
            float SizeRange[2];
            /// 距離で間引く範囲[m]（小さい石ほど手前で消える）
            float FadeStart;
            float FadeEnd;
            /// 地表の分類ごとの被覆の係数（礫・崖錐・細粒・沢底の流路・露岩）
            float GravelCover;
            float TalusCover;
            float FineCover;
            float ChannelCover;
            float RockCover;
            /// 角張った石・丸い石のメッシュの細分の段
            uint32_t AngularSubdivisions;
            uint32_t RoundedSubdivisions;
        };

        // 小礫は 2〜6 cm（手本の ground_01 の手前の礫の地面を覆う粒）、石は 5〜14 cm、大きな石は 14〜40 cm（ground_02 の沢底の石）。
        // 画面の縦 1080 画素・縦の画角 60° で 1 画素は距離 d[m] で約 0.00107 × d[m]。小さい石から先に消すので、どの段も消える所で
        // 石は 2〜6 画素ある（小礫 2 cm は 5 m 先で 4 画素、6 cm は 12 m 先で 5 画素。石 5 cm は 10 m 先、14 cm は 30 m 先で 4 画素。
        // 大きな石 14 cm は 24 m 先、40 cm は 60 m 先で 6 画素）。
        // 小礫は手本の足元の被覆（礫が地面の 3〜4 割を覆う）に寄せて密に置き、数 cm の粒は面の数を抑えた正二十面体で作る。
        // 大きな石は沢底の流路に寄せる（ground_02 の沢底は大きな石が連なり、礫の地面には時折しか無い）
        constexpr TierDesc kTiers[kTierCount] = {
            {"pebble", 0.055f, 76, {0.02f, 0.06f}, 5.0f, 12.0f, 1.0f, 0.8f, 0.85f, 0.6f, 0.2f, 0, 0},
            {"stone", 0.18f, 44, {0.05f, 0.14f}, 10.0f, 30.0f, 1.0f, 0.9f, 0.5f, 0.8f, 0.2f, 1, 1},
            {"cobble", 0.75f, 21, {0.14f, 0.4f}, 22.0f, 60.0f, 0.07f, 0.3f, 0.02f, 0.9f, 0.03f, 2, 2},
        };

        // 置かない傾斜[度]（崖と急な崖錐の面）
        constexpr float kMaxSlopeDegrees = 38.0f;
        // 区画の箱を石の底の高さの範囲から広げる量[m]（横は石の半径、上は最も高い石）
        constexpr float kCellBoundsSideMargin = 0.3f;
        constexpr float kCellBoundsTopMargin = 0.4f;
        constexpr float kCellBoundsBottomMargin = 0.2f;
        // 石の高さに対する埋める割合（角張った破片は半ば埋まり、丸い石は浅く載る）
        constexpr float kAngularSink = 0.3f;
        constexpr float kRoundedSink = 0.22f;
        // 消えるしきい値を大きさに寄せる割合（残りは乱数。大きさがそろった石が同じ距離で一斉に消えない）
        constexpr float kFadeBySize = 0.75f;

        /// 位置から決まる乱数（0〜1）
        float Hash2(int32_t x, int32_t z, uint32_t salt)
        {
            uint32_t h = static_cast<uint32_t>(x) * 0x8DA6B343u ^ static_cast<uint32_t>(z) * 0xD8163841u ^ salt * 0xCB1AB31Fu;
            h ^= h >> 13u;
            h *= 0x5BD1E995u;
            h ^= h >> 15u;
            return static_cast<float>(h >> 8) * (1.0f / 16777216.0f);
        }

        /// 滑らかな値ノイズ（0〜1。scale[m] の斑）
        float ValueNoise(float x, float z, float scale, uint32_t salt)
        {
            const float fx = x / scale;
            const float fz = z / scale;
            const int32_t ix = static_cast<int32_t>(std::floor(fx));
            const int32_t iz = static_cast<int32_t>(std::floor(fz));
            const float tx = fx - static_cast<float>(ix);
            const float tz = fz - static_cast<float>(iz);
            const float ux = tx * tx * (3.0f - 2.0f * tx);
            const float uz = tz * tz * (3.0f - 2.0f * tz);
            const float a = Hash2(ix, iz, salt);
            const float b = Hash2(ix + 1, iz, salt);
            const float c = Hash2(ix, iz + 1, salt);
            const float d = Hash2(ix + 1, iz + 1, salt);
            return (a + (b - a) * ux) + ((c + (d - c) * ux) - (a + (b - a) * ux)) * uz;
        }

        float SmoothStep(float edge0, float edge1, float value)
        {
            const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }

        /// 線形のアルベドを、輝度 Y・R ÷ G・G ÷ B から作る
        void SetLinearColor(float (&destination)[3], float luminance, float redOverGreen, float greenOverBlue)
        {
            const float green =
                luminance / (0.2126f * redOverGreen + 0.7152f + 0.0722f / greenOverBlue);
            destination[0] = green * redOverGreen;
            destination[1] = green;
            destination[2] = green / greenOverBlue;
        }

        struct Float3
        {
            float X = 0.0f;
            float Y = 0.0f;
            float Z = 0.0f;
        };

        Float3 Normalize(const Float3 &v)
        {
            const float length = std::sqrt(v.X * v.X + v.Y * v.Y + v.Z * v.Z);
            return length > 1.0e-12f ? Float3{v.X / length, v.Y / length, v.Z / length} : Float3{0.0f, 1.0f, 0.0f};
        }

        Float3 Cross(const Float3 &a, const Float3 &b)
        {
            return {a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X};
        }

        float Dot(const Float3 &a, const Float3 &b)
        {
            return a.X * b.X + a.Y * b.Y + a.Z * b.Z;
        }

        /// 単位球の正二十面体を subdivisions 段だけ細分した頂点と三角形
        void BuildIcosphere(uint32_t subdivisions, VariableArray<Float3> &outPositions, VariableArray<uint32_t> &outIndices)
        {
            const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;
            outPositions = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t},
                            {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
            for (Float3 &position : outPositions)
            {
                position = Normalize(position);
            }
            outIndices = {0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11, 1, 5, 9, 5, 11, 4, 11, 10, 2, 10, 7, 6, 7, 1, 8,
                          3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9, 4, 9, 5, 2, 4, 11, 6, 2, 10, 8, 6, 7, 9, 8, 1};
            for (uint32_t level = 0; level < subdivisions; ++level)
            {
                VariableArray<uint32_t> next;
                next.reserve(outIndices.size() * 4);
                // 辺の中点（両端の番号の組 → 頂点）。細分の段は小さいので線形の探索で足りる
                VariableArray<uint64_t> edgeKeys;
                VariableArray<uint32_t> edgeVertices;
                auto midpoint = [&](uint32_t a, uint32_t b) -> uint32_t
                {
                    const uint64_t key = (static_cast<uint64_t>(std::min(a, b)) << 32u) | std::max(a, b);
                    for (size_t index = 0; index < edgeKeys.size(); ++index)
                    {
                        if (edgeKeys[index] == key)
                        {
                            return edgeVertices[index];
                        }
                    }
                    const Float3 &pa = outPositions[a];
                    const Float3 &pb = outPositions[b];
                    outPositions.push_back(Normalize({pa.X + pb.X, pa.Y + pb.Y, pa.Z + pb.Z}));
                    const uint32_t vertex = static_cast<uint32_t>(outPositions.size() - 1);
                    edgeKeys.push_back(key);
                    edgeVertices.push_back(vertex);
                    return vertex;
                };
                for (size_t triangle = 0; triangle + 2 < outIndices.size(); triangle += 3)
                {
                    const uint32_t a = outIndices[triangle];
                    const uint32_t b = outIndices[triangle + 1];
                    const uint32_t c = outIndices[triangle + 2];
                    const uint32_t ab = midpoint(a, b);
                    const uint32_t bc = midpoint(b, c);
                    const uint32_t ca = midpoint(c, a);
                    next.insert(next.end(), {a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca});
                }
                outIndices = std::move(next);
            }
        }

        /// 石のメッシュ。角張った石は、いくつかの平面で切った多面体（節理と層理で割れた砂岩の破片）を面ごとの法線で描く。
        /// 丸い石は、低い周波数の凹凸を持つ歪んだ楕円体を滑らかな法線で描く。底を y = 0、水平の長い辺を 1 m にそろえる
        TSharedPtr<FoliageMeshData> BuildStoneMesh(bool bAngular, uint32_t subdivisions, uint32_t seed)
        {
            VariableArray<Float3> positions;
            VariableArray<uint32_t> indices;
            BuildIcosphere(subdivisions, positions, indices);

            auto random = [&](uint32_t salt) { return Hash2(static_cast<int32_t>(seed), static_cast<int32_t>(salt), 977u); };
            // 楕円体の軸（横 1・奥 0.6〜0.9・高さ 0.4〜0.65。礫は平たく寝る）
            const float axisZ = (bAngular ? 0.6f : 0.7f) + 0.25f * random(1);
            const float axisY = (bAngular ? 0.4f : 0.45f) + 0.22f * random(2);
            // 切る平面（角張った石だけ）と、丸い石の凹凸の向き
            constexpr uint32_t kPlaneCount = 9;
            Float3 planeNormals[kPlaneCount];
            float planeOffsets[kPlaneCount];
            for (uint32_t plane = 0; plane < kPlaneCount; ++plane)
            {
                const float u = random(10 + plane * 3);
                const float v = random(11 + plane * 3);
                const float y = 1.0f - 2.0f * u;
                const float ring = std::sqrt(std::max(0.0f, 1.0f - y * y));
                planeNormals[plane] = {ring * std::cos(2.0f * kPi * v), y, ring * std::sin(2.0f * kPi * v)};
                planeOffsets[plane] = 0.55f + 0.3f * random(12 + plane * 3);
            }
            for (Float3 &position : positions)
            {
                float radius = 1.0f;
                if (bAngular)
                {
                    for (uint32_t plane = 0; plane < kPlaneCount; ++plane)
                    {
                        const float facing = Dot(position, planeNormals[plane]);
                        if (facing > 1.0e-3f)
                        {
                            radius = std::min(radius, planeOffsets[plane] / facing);
                        }
                    }
                }
                else
                {
                    for (uint32_t plane = 0; plane < 3; ++plane)
                    {
                        radius += 0.06f * std::cos(2.3f * Dot(position, planeNormals[plane]) * kPi + 6.0f * random(40 + plane));
                    }
                }
                position = {position.X * radius, position.Y * radius * axisY, position.Z * radius * axisZ};
            }
            // 底を y = 0、水平の中心を原点、水平の長い辺を 1 にそろえる
            Float3 minimum{1.0e9f, 1.0e9f, 1.0e9f};
            Float3 maximum{-1.0e9f, -1.0e9f, -1.0e9f};
            for (const Float3 &position : positions)
            {
                minimum = {std::min(minimum.X, position.X), std::min(minimum.Y, position.Y), std::min(minimum.Z, position.Z)};
                maximum = {std::max(maximum.X, position.X), std::max(maximum.Y, position.Y), std::max(maximum.Z, position.Z)};
            }
            const float extent = std::max(maximum.X - minimum.X, maximum.Z - minimum.Z);
            const float normalize = extent > 1.0e-6f ? 1.0f / extent : 1.0f;
            const Float3 center{(minimum.X + maximum.X) * 0.5f, minimum.Y, (minimum.Z + maximum.Z) * 0.5f};
            for (Float3 &position : positions)
            {
                position = {(position.X - center.X) * normalize, (position.Y - center.Y) * normalize,
                            (position.Z - center.Z) * normalize};
            }
            const float height = (maximum.Y - minimum.Y) * normalize;

            auto mesh = MakeShared<FoliageMeshData>();
            mesh->Height = height;
            // 遮蔽: 地面に接する底ほど暗い（接地の陰）
            auto occlusion = [&](float y) { return 0.45f + 0.55f * SmoothStep(0.0f, 0.6f * height, y); };
            if (bAngular)
            {
                // 面ごとの法線（割れた面の稜を立てる）。面ごとに色の混ぜ具合を変え、割れ口の明るさの違いを出す
                mesh->Vertices.reserve(indices.size());
                mesh->Indices.reserve(indices.size());
                for (size_t triangle = 0; triangle + 2 < indices.size(); triangle += 3)
                {
                    const Float3 &a = positions[indices[triangle]];
                    const Float3 &b = positions[indices[triangle + 1]];
                    const Float3 &c = positions[indices[triangle + 2]];
                    Float3 normal = Normalize(Cross({b.X - a.X, b.Y - a.Y, b.Z - a.Z}, {c.X - a.X, c.Y - a.Y, c.Z - a.Z}));
                    const Float3 centroid{(a.X + b.X + c.X) / 3.0f, (a.Y + b.Y + c.Y) / 3.0f, (a.Z + b.Z + c.Z) / 3.0f};
                    // 外向きへそろえる（重心から面の中心への向き）
                    if (Dot(normal, {centroid.X, centroid.Y - 0.5f * height, centroid.Z}) < 0.0f)
                    {
                        normal = {-normal.X, -normal.Y, -normal.Z};
                    }
                    const float tint = 0.35f + 0.65f * random(1000 + static_cast<uint32_t>(triangle));
                    for (const Float3 *corner : {&a, &b, &c})
                    {
                        FoliageVertex vertex;
                        vertex.Position[0] = corner->X;
                        vertex.Position[1] = corner->Y;
                        vertex.Position[2] = corner->Z;
                        vertex.Normal[0] = normal.X;
                        vertex.Normal[1] = normal.Y;
                        vertex.Normal[2] = normal.Z;
                        vertex.Flex = 0.0f;
                        vertex.Tint = tint;
                        vertex.Occlusion = occlusion(corner->Y);
                        vertex.Phase = 0.0f;
                        mesh->Indices.push_back(static_cast<uint32_t>(mesh->Vertices.size()));
                        mesh->Vertices.push_back(vertex);
                    }
                }
            }
            else
            {
                // 滑らかな法線（面の法線を頂点へ足し合わせる）
                VariableArray<Float3> normals(positions.size(), Float3{0.0f, 0.0f, 0.0f});
                for (size_t triangle = 0; triangle + 2 < indices.size(); triangle += 3)
                {
                    const Float3 &a = positions[indices[triangle]];
                    const Float3 &b = positions[indices[triangle + 1]];
                    const Float3 &c = positions[indices[triangle + 2]];
                    const Float3 face = Cross({b.X - a.X, b.Y - a.Y, b.Z - a.Z}, {c.X - a.X, c.Y - a.Y, c.Z - a.Z});
                    for (uint32_t corner = 0; corner < 3; ++corner)
                    {
                        Float3 &normal = normals[indices[triangle + corner]];
                        normal = {normal.X + face.X, normal.Y + face.Y, normal.Z + face.Z};
                    }
                }
                mesh->Vertices.reserve(positions.size());
                for (size_t index = 0; index < positions.size(); ++index)
                {
                    const Float3 &position = positions[index];
                    Float3 normal = Normalize(normals[index]);
                    // 正二十面体の巻きの向きで内向きになったものは外へ返す
                    if (Dot(normal, {position.X, position.Y - 0.5f * height, position.Z}) < 0.0f)
                    {
                        normal = {-normal.X, -normal.Y, -normal.Z};
                    }
                    FoliageVertex vertex;
                    vertex.Position[0] = position.X;
                    vertex.Position[1] = position.Y;
                    vertex.Position[2] = position.Z;
                    vertex.Normal[0] = normal.X;
                    vertex.Normal[1] = normal.Y;
                    vertex.Normal[2] = normal.Z;
                    vertex.Flex = 0.0f;
                    // 頂点ごとの斑（風化の色むら）
                    vertex.Tint = 0.5f + 0.5f * random(5000 + static_cast<uint32_t>(index));
                    vertex.Occlusion = occlusion(position.Y);
                    vertex.Phase = 0.0f;
                    mesh->Vertices.push_back(vertex);
                }
                mesh->Indices = indices;
            }
            return mesh;
        }

        /// 段 tier・種類 kind（0: 赤い角張った石、1: 淡い丸い石）の束のひな形
        FoliageBatch MakeBatch(uint32_t tier, uint32_t kind)
        {
            const TierDesc &desc = kTiers[tier];
            const bool bAngular = kind == 0;
            FoliageBatch batch;
            batch.Mesh = BuildStoneMesh(bAngular, bAngular ? desc.AngularSubdivisions : desc.RoundedSubdivisions,
                                        31u + tier * 7u + kind * 3u);
            if (bAngular)
            {
                // 赤い砂岩の破片（手本 ground_01 の礫と、崖錐の落石の較正 Y 0.17・R/G 2.1・G/B 1.5 に合わせる）。
                // 割れ口（Tint 1）は明るい赤、風化した面（Tint 0）は暗い赤褐色
                SetLinearColor(batch.StemColor, 0.085f, 1.9f, 1.45f);
                SetLinearColor(batch.TipColorA, 0.17f, 2.1f, 1.5f);
                SetLinearColor(batch.TipColorB, 0.14f, 1.75f, 1.4f);
            }
            else
            {
                // 淡い灰桃色の丸い石（手本 ground_02 の沢底の石）
                SetLinearColor(batch.StemColor, 0.11f, 1.35f, 1.15f);
                SetLinearColor(batch.TipColorA, 0.2f, 1.3f, 1.12f);
                SetLinearColor(batch.TipColorB, 0.16f, 1.55f, 1.25f);
            }
            batch.Roughness = bAngular ? 0.86f : 0.78f;
            batch.WindStrength = 0.0f;
            batch.WindSwayFrequency = 0.0f;
            batch.MinPixelWidth = 0.0f;
            batch.MaxWidenRatio = 1.0f;
            batch.FadeStartDistance = desc.FadeStart;
            batch.FadeEndDistance = desc.FadeEnd;
            static const char *const kNames[kTierCount][kKindsPerTier] = {
                {"gravel_pebble_red", "gravel_pebble_pale"},
                {"gravel_stone_red", "gravel_stone_pale"},
                {"gravel_cobble_red", "gravel_cobble_pale"},
            };
            batch.DebugName = kNames[tier][kind];
            return batch;
        }

        float CellSize(uint32_t tier)
        {
            return kTiers[tier].CandidateSpacing * static_cast<float>(kTiers[tier].CandidatesPerCell);
        }

        /// 区画 (ax, az) の四角と区画 (bx, bz) の四角の間の水平の最短の距離[m]（隣り合う区画は 0）
        float DistanceBetweenCells(int32_t ax, int32_t az, int32_t bx, int32_t bz, float cellSize)
        {
            const float dx = static_cast<float>(std::max(std::abs(ax - bx) - 1, 0)) * cellSize;
            const float dz = static_cast<float>(std::max(std::abs(az - bz) - 1, 0)) * cellSize;
            return std::sqrt(dx * dx + dz * dz);
        }
    } // namespace

    void GravelScatter::Initialize(TSharedPtr<const Terrain::TerrainHeightField> field,
                                   TSharedPtr<const Terrain::TerrainSurfaceMask> mask)
    {
        Reset();
        if (!field || !mask)
        {
            LOG_WARNING("WASTELAND_GRAVEL_DISABLED 高さ場か地表の分類の地図が無いため、石と礫を置きません");
            return;
        }
        m_pField = std::move(field);
        m_pMask = std::move(mask);
        for (uint32_t tier = 0; tier < kTierCount; ++tier)
        {
            for (uint32_t kind = 0; kind < kKindsPerTier; ++kind)
            {
                m_Templates[tier][kind] = MakeBatch(tier, kind);
                const FoliageBatch &batch = m_Templates[tier][kind];
                LOG_INFO_F("WASTELAND_GRAVEL_KIND id=%s vertices=%zu triangles=%zu height=%.2f size=[%.2f,%.2f] fade=[%.1f,%.1f] "
                           "cell=%.1f",
                           batch.DebugName, batch.Mesh->Vertices.size(), batch.Mesh->Indices.size() / 3u,
                           static_cast<double>(batch.Mesh->Height), static_cast<double>(kTiers[tier].SizeRange[0]),
                           static_cast<double>(kTiers[tier].SizeRange[1]), static_cast<double>(batch.FadeStartDistance),
                           static_cast<double>(batch.FadeEndDistance), static_cast<double>(CellSize(tier)));
            }
        }
    }

    void GravelScatter::Reset()
    {
        m_pField.reset();
        m_pMask.reset();
        for (auto &tierTemplates : m_Templates)
        {
            for (FoliageBatch &batch : tierTemplates)
            {
                batch = FoliageBatch{};
            }
        }
        for (TierState &tier : m_Tiers)
        {
            tier = TierState{};
        }
        m_ExclusionCircles.clear();
        m_ExclusionRects.clear();
        m_ViewClearings.clear();
    }

    void GravelScatter::SetExclusions(VariableArray<WastelandFoliage::ExclusionCircle> circles,
                                      VariableArray<WastelandFoliage::ExclusionRect> rects,
                                      VariableArray<WastelandFoliage::ExclusionCircle> viewClearings)
    {
        m_ExclusionCircles = std::move(circles);
        m_ExclusionRects = std::move(rects);
        m_ViewClearings = std::move(viewClearings);
        for (TierState &tier : m_Tiers)
        {
            tier.Cells.clear();
            tier.bHasCenter = false;
        }
    }

    bool GravelScatter::IsExcluded(float x, float z, bool bLarge) const
    {
        for (const WastelandFoliage::ExclusionCircle &circle : m_ExclusionCircles)
        {
            const float dx = x - circle.X;
            const float dz = z - circle.Z;
            if (dx * dx + dz * dz < circle.Radius * circle.Radius)
            {
                return true;
            }
        }
        for (const WastelandFoliage::ExclusionRect &rect : m_ExclusionRects)
        {
            const float dx = x - rect.CenterX;
            const float dz = z - rect.CenterZ;
            const float along = dx * rect.AlongX + dz * rect.AlongZ;
            const float lateral = dx * rect.AlongZ - dz * rect.AlongX;
            if (std::fabs(along) < rect.HalfLength && std::fabs(lateral) < rect.HalfWidth)
            {
                return true;
            }
        }
        if (bLarge)
        {
            for (const WastelandFoliage::ExclusionCircle &circle : m_ViewClearings)
            {
                const float dx = x - circle.X;
                const float dz = z - circle.Z;
                if (dx * dx + dz * dz < circle.Radius * circle.Radius)
                {
                    return true;
                }
            }
        }
        return false;
    }

    void GravelScatter::BuildCell(uint32_t tier, Cell &cell) const
    {
        const TierDesc &desc = kTiers[tier];
        const Terrain::TerrainHeightField &field = *m_pField;
        const float minSlopeCos = std::cos(kMaxSlopeDegrees * kPi / 180.0f);
        const uint32_t tierSalt = 100u + tier * 64u;
        cell.MinRootY = 0.0f;
        cell.MaxRootY = 0.0f;
        bool bHasRoot = false;
        for (int32_t j = 0; j < desc.CandidatesPerCell; ++j)
        {
            for (int32_t i = 0; i < desc.CandidatesPerCell; ++i)
            {
                // 世界の升目の番号（区画の作り方やカメラの動きに関わらず、同じ升目は同じ石になる）
                const int32_t gx = cell.X * desc.CandidatesPerCell + i;
                const int32_t gz = cell.Z * desc.CandidatesPerCell + j;
                const float x = (static_cast<float>(gx) + Hash2(gx, gz, tierSalt + 1u)) * desc.CandidateSpacing;
                const float z = (static_cast<float>(gz) + Hash2(gx, gz, tierSalt + 2u)) * desc.CandidateSpacing;
                if (!field.Contains(x, z) || IsExcluded(x, z, tier == kTierCount - 1))
                {
                    continue;
                }
                Terrain::TerrainSurfaceWeights weights;
                if (!m_pMask->Sample(x, z, weights))
                {
                    continue;
                }
                // 沢底の流路（砂の重みが吹きだまりの上限 0.6 を超えて 1 へ近づく所）。吹きだまりの砂には置かない
                const float channel = SmoothStep(0.45f, 0.7f, weights.Sand);
                const float cover = desc.GravelCover * weights.Gravel + desc.TalusCover * weights.Talus +
                                    desc.FineCover * weights.Fine + desc.ChannelCover * channel +
                                    desc.RockCover * weights.Rock;
                // 群れの斑: 数 m の濃淡と 1〜2 m の塊（礫は流れと風で寄せられ、均等には散らない）
                const float clump = 0.6f * ValueNoise(x, z, 7.0f, tierSalt + 3u) + 0.4f * ValueNoise(x, z, 1.8f, tierSalt + 4u);
                const float acceptance = std::clamp(cover * (0.3f + SmoothStep(0.3f, 0.7f, clump)), 0.0f, 1.0f);
                if (Hash2(gx, gz, tierSalt + 5u) >= acceptance)
                {
                    continue;
                }
                const Terrain::TerrainNormal normal = field.SampleNormal(x, z, 0.5f);
                if (!(normal.Y >= minSlopeCos))
                {
                    continue;
                }
                // 種類: 沢底は淡い丸い石が半分、ほかは赤い角張った石が多い
                const float paleShare = 0.1f + 0.4f * channel;
                const uint32_t kind = Hash2(gx, gz, tierSalt + 6u) < paleShare ? 1u : 0u;
                const FoliageBatch &batch = m_Templates[tier][kind];

                // 大きさ: 小さい石を多く（u^2）。横と高さの倍率を少しずつ変える
                const float sizeU = Hash2(gx, gz, tierSalt + 7u);
                const float sizeT = sizeU * sizeU;
                const float size = desc.SizeRange[0] + (desc.SizeRange[1] - desc.SizeRange[0]) * sizeT;
                const float heightScale = size * (0.75f + 0.5f * Hash2(gx, gz, tierSalt + 8u));
                const float stoneHeight = batch.Mesh->Height * heightScale;
                // 埋める深さ: 高さの割合と、斜面で下り側が浮かない分（半径 × 傾きの正接）
                const float slopeTangent = std::sqrt(std::max(0.0f, 1.0f - normal.Y * normal.Y)) / std::max(normal.Y, 0.2f);
                const float sink = stoneHeight * (kind == 0 ? kAngularSink : kRoundedSink) + 0.5f * size * slopeTangent;

                FoliageInstance instance;
                instance.Position[0] = x;
                instance.Position[1] = field.SampleHeight(x, z) - sink;
                instance.Position[2] = z;
                instance.Yaw = Hash2(gx, gz, tierSalt + 9u) * 2.0f * kPi;
                instance.WidthScale = size;
                instance.HeightScale = heightScale;
                instance.ColorVariation = Hash2(gx, gz, tierSalt + 10u);
                instance.WindPhase = Hash2(gx, gz, tierSalt + 11u);
                // 大きい石ほどしきい値を小さくし、遠くまで残す（小さい石から消える）
                instance.FadeThreshold =
                    std::clamp(1.0f - (kFadeBySize * sizeT + (1.0f - kFadeBySize) * Hash2(gx, gz, tierSalt + 12u)), 0.0f, 1.0f);
                cell.Instances[kind].push_back(instance);
                cell.MinRootY = bHasRoot ? std::min(cell.MinRootY, instance.Position[1]) : instance.Position[1];
                cell.MaxRootY = bHasRoot ? std::max(cell.MaxRootY, instance.Position[1]) : instance.Position[1];
                bHasRoot = true;
            }
        }
        // 描画は区画の先頭から、しきい値が残す割合を下回る個数だけを描くので、しきい値の小さい順に並べる
        for (VariableArray<FoliageInstance> &instances : cell.Instances)
        {
            std::sort(instances.begin(), instances.end(),
                      [](const FoliageInstance &a, const FoliageInstance &b) { return a.FadeThreshold < b.FadeThreshold; });
        }
    }

    bool GravelScatter::Update(float cameraX, float cameraZ, VariableArray<FoliageBatch> &outBatches)
    {
        if (!IsInitialized() || !std::isfinite(cameraX) || !std::isfinite(cameraZ))
        {
            return false;
        }
        bool bChanged = false;
        uint32_t created = 0;
        size_t dropped = 0;
        for (uint32_t tierIndex = 0; tierIndex < kTierCount; ++tierIndex)
        {
            TierState &tier = m_Tiers[tierIndex];
            const float cellSize = CellSize(tierIndex);
            const int32_t centerX = static_cast<int32_t>(std::floor(cameraX / cellSize));
            const int32_t centerZ = static_cast<int32_t>(std::floor(cameraZ / cellSize));
            if (tier.bHasCenter && centerX == tier.CenterX && centerZ == tier.CenterZ)
            {
                continue;
            }
            bChanged = true;
            tier.bHasCenter = true;
            tier.CenterX = centerX;
            tier.CenterZ = centerZ;
            // 作る範囲はカメラのいる区画の四角から測る（作り直すのは区画を跨いだときだけなので、区画の中のどこへ動いても
            // 消える距離の内側を作り終えている）。1 区画ぶん離れるまで捨てない
            const float generateRadius = kTiers[tierIndex].FadeEnd;
            const float keepRadius = generateRadius + cellSize;
            const size_t before = tier.Cells.size();
            tier.Cells.erase(std::remove_if(tier.Cells.begin(), tier.Cells.end(),
                                            [&](const Cell &cell) {
                                                return DistanceBetweenCells(centerX, centerZ, cell.X, cell.Z, cellSize) >
                                                       keepRadius;
                                            }),
                             tier.Cells.end());
            dropped += before - tier.Cells.size();
            const int32_t reach = static_cast<int32_t>(std::ceil(generateRadius / cellSize)) + 1;
            for (int32_t cellZ = centerZ - reach; cellZ <= centerZ + reach; ++cellZ)
            {
                for (int32_t cellX = centerX - reach; cellX <= centerX + reach; ++cellX)
                {
                    if (DistanceBetweenCells(centerX, centerZ, cellX, cellZ, cellSize) > generateRadius)
                    {
                        continue;
                    }
                    const bool bExists = std::any_of(tier.Cells.begin(), tier.Cells.end(), [&](const Cell &cell)
                                                     { return cell.X == cellX && cell.Z == cellZ; });
                    if (bExists)
                    {
                        continue;
                    }
                    Cell cell;
                    cell.X = cellX;
                    cell.Z = cellZ;
                    BuildCell(tierIndex, cell);
                    tier.Cells.push_back(std::move(cell));
                    ++created;
                }
            }
        }
        if (!bChanged)
        {
            return false;
        }
        RebuildBatches(outBatches);

        size_t stones[kTierCount] = {};
        for (uint32_t tierIndex = 0; tierIndex < kTierCount; ++tierIndex)
        {
            for (const Cell &cell : m_Tiers[tierIndex].Cells)
            {
                for (const VariableArray<FoliageInstance> &instances : cell.Instances)
                {
                    stones[tierIndex] += instances.size();
                }
            }
        }
        // カメラの足元の地表の分類（置く密度の元。撮影の視点ごとに、どの地表に立っているかを確かめる）
        Terrain::TerrainSurfaceWeights here;
        m_pMask->Sample(cameraX, cameraZ, here);
        LOG_INFO_F("WASTELAND_GRAVEL_SURFACE camera=(%.1f,%.1f) rock=%.2f talus=%.2f fine=%.2f gravel=%.2f sand=%.2f",
                   static_cast<double>(cameraX), static_cast<double>(cameraZ), static_cast<double>(here.Rock),
                   static_cast<double>(here.Talus), static_cast<double>(here.Fine), static_cast<double>(here.Gravel),
                   static_cast<double>(here.Sand));
        LOG_INFO_F("WASTELAND_GRAVEL camera=(%.1f,%.1f) cells=%zu/%zu/%zu stones=%zu/%zu/%zu created=%u dropped=%zu batches=%zu",
                   static_cast<double>(cameraX), static_cast<double>(cameraZ), m_Tiers[0].Cells.size(),
                   m_Tiers[1].Cells.size(), m_Tiers[2].Cells.size(), stones[0], stones[1], stones[2], created, dropped,
                   outBatches.size());
        return true;
    }

    void GravelScatter::RebuildBatches(VariableArray<FoliageBatch> &outBatches) const
    {
        outBatches.clear();
        for (uint32_t tierIndex = 0; tierIndex < kTierCount; ++tierIndex)
        {
            const float cellSize = CellSize(tierIndex);
            const TierState &tier = m_Tiers[tierIndex];
            for (uint32_t kind = 0; kind < kKindsPerTier; ++kind)
            {
                const FoliageBatch &batchTemplate = m_Templates[tierIndex][kind];
                if (!batchTemplate.Mesh)
                {
                    continue;
                }
                auto instances = MakeShared<VariableArray<FoliageInstance>>();
                auto cells = MakeShared<VariableArray<FoliageCell>>();
                size_t total = 0;
                for (const Cell &cell : tier.Cells)
                {
                    total += cell.Instances[kind].size();
                }
                instances->reserve(total);
                for (const Cell &cell : tier.Cells)
                {
                    const VariableArray<FoliageInstance> &source = cell.Instances[kind];
                    if (source.empty())
                    {
                        continue;
                    }
                    FoliageCell range;
                    range.BoundsMin[0] = static_cast<float>(cell.X) * cellSize - kCellBoundsSideMargin;
                    range.BoundsMin[1] = cell.MinRootY - kCellBoundsBottomMargin;
                    range.BoundsMin[2] = static_cast<float>(cell.Z) * cellSize - kCellBoundsSideMargin;
                    range.BoundsMax[0] = static_cast<float>(cell.X + 1) * cellSize + kCellBoundsSideMargin;
                    range.BoundsMax[1] = cell.MaxRootY + kCellBoundsTopMargin;
                    range.BoundsMax[2] = static_cast<float>(cell.Z + 1) * cellSize + kCellBoundsSideMargin;
                    range.FirstInstance = static_cast<uint32_t>(instances->size());
                    range.InstanceCount = static_cast<uint32_t>(source.size());
                    instances->insert(instances->end(), source.begin(), source.end());
                    cells->push_back(range);
                }
                if (instances->empty())
                {
                    continue;
                }
                FoliageBatch batch = batchTemplate;
                batch.Instances = TSharedPtr<const VariableArray<FoliageInstance>>(std::move(instances));
                batch.Cells = TSharedPtr<const VariableArray<FoliageCell>>(std::move(cells));
                outBatches.push_back(std::move(batch));
            }
        }
    }

} // namespace Game::GameModes::WastelandGravel
