#include "GameModes/Wasteland/WastelandTalus.h"

#include "Core/Public/Math/Quaternion.h"

#include <algorithm>
#include <cmath>
#include <limits>

using namespace NorvesLib::Core::Container;
using NorvesLib::Core::Component::MegaScatterInstance;
using NorvesLib::Core::Rendering::Terrain::TerrainHeightField;
using NorvesLib::Core::Rendering::Terrain::TerrainNormal;
using NorvesLib::Core::Rendering::Terrain::TerrainSurfaceMask;
using NorvesLib::Core::Rendering::Terrain::TerrainSurfaceWeights;
using NorvesLib::Math::Quaternion;
using NorvesLib::Math::Vector3;

namespace Game::GameModes::WastelandTalus
{

    namespace
    {
        constexpr float kPi = 3.14159265358979f;
        constexpr float kDegToRad = kPi / 180.0f;

        // ---- 群れの中心 ----
        // 群れの中心を探す升目の一辺[m]。升目ごとに 1 点をずらして引き、出やすさの確率で群れを置く
        constexpr float kClusterCellMeters = 3.5f;
        // 崖錐の重みから出やすさへ移す範囲（重みがこの間で 0 → 1）
        constexpr float kTalusWeightRange[2] = {0.08f, 0.45f};
        // 崖の根元（露岩に接する崖錐）: 露岩と崖錐の重みがともにある所は剥離片が多い
        constexpr float kBaseRockRange[2] = {0.04f, 0.3f};
        constexpr float kBaseTalusRange[2] = {0.08f, 0.35f};
        constexpr float kBaseBoost = 0.8f;
        // 斑（空白の帯）: 2 つの波長の値ノイズの和を、この範囲で 0 → 1 にする
        constexpr float kPatchWavelengths[2] = {70.0f, 23.0f};
        constexpr float kPatchRange[2] = {0.36f, 0.66f};
        // 崖の根元では斑の空白を浅くし、剥離片が連なる帯にする（斑の値の下限 = 係数 × 根元の重み）
        constexpr float kBaseBeltFloor = 0.75f;
        // 出やすさ 1 の升目に群れを置く確率
        constexpr float kClusterChance = 0.95f;
        // 崖錐の先（重みが薄い縁）に転がり出た岩を 1 つ置く確率（升目あたり）
        constexpr float kToeTalusInner[2] = {0.02f, 0.1f};
        constexpr float kToeTalusOuter[2] = {0.14f, 0.32f};
        constexpr float kToeChance = 0.05f;
        // 岩を置かない傾斜（崖）
        constexpr float kMaxSlopeDegrees = 42.0f;

        // ---- 群れ ----
        // 大岩の無い群れ（小片だけ）の割合
        constexpr float kNoMainChance = 0.1f;
        // 大岩の拡大率（boulder_01 は 1.27 × 1.00 × 1.83 m なので、拡大率がおよそ高さ[m]）。u^2 で引き、小さめを多くする。
        // 崖の根元では大きくする
        constexpr float kMainScale[2] = {1.2f, 3.6f};
        constexpr float kMainScaleBaseBoost = 0.6f;
        constexpr float kMainScaleMax = 4.5f;
        // 小片の数: 基本 + 乱数 × (幅 + 大岩の拡大率 × 係数)
        constexpr uint32_t kFragmentBase = 5;
        constexpr float kFragmentSpread = 4.0f;
        constexpr float kFragmentPerMainScale = 5.0f;
        constexpr uint32_t kFragmentsWithoutMain[2] = {6, 12};
        // 小片が散らばる半径[m] = 基本 + 大岩の拡大率 × 係数。斜面の下へ半径 × kDownslopeShift まで寄る
        constexpr float kFragmentRadiusBase = 1.0f;
        constexpr float kFragmentRadiusPerMainScale = 1.8f;
        constexpr float kFragmentRadiusWithoutMain = 3.0f;
        constexpr float kDownslopeShift = 0.8f;
        // 小片のうち板の割合と、拡大率（rock_09 は長さ 0.14 m なので 4〜12 倍で 0.6〜1.7 m）・角張った岩の拡大率（高さ 0.35〜1.4 m）
        constexpr float kSlabFraction = 0.4f;
        constexpr float kSlabScale[2] = {4.0f, 12.0f};
        constexpr float kFragmentCragScale[2] = {0.35f, 1.4f};
        // 崖錐の先に転がり出た岩の拡大率（高さ 0.5〜1.8 m）
        constexpr float kToeScale[2] = {0.5f, 1.8f};

        // ---- 向きと高さ ----
        // 斜面の傾きへ寄せる割合（岩塊は半ば、板はほぼ沿わせる）と、その上に足す傾きのばらつき[度]
        constexpr float kCragAlign = 0.6f;
        constexpr float kSlabAlign = 0.9f;
        constexpr float kCragTiltJitterDegrees = 10.0f;
        constexpr float kSlabTiltJitterDegrees = 12.0f;
        // 底の頂点とみなす、回した高さの下からの割合
        constexpr float kBottomBand = 0.2f;
        // 埋める深さ（回した高さに対する割合と、足す深さ[m]）
        constexpr float kCragSinkFraction = 0.12f;
        constexpr float kCragSinkMeters = 0.03f;
        constexpr float kSlabSinkFraction = 0.25f;
        // 底の高さを引く頂点: メッシュの標本から、この数の向きそれぞれで最も遠い頂点（凸包の頂点の近似）だけを残す。
        // 地面に触れるのは凸包の頂点なので、岩の数が多くても 1 つあたりの高さの標本を数十に抑えられる
        constexpr uint32_t kSupportDirections = 162;

        // ---- 影 ----
        // 影を落とす岩の上限（大きい順）。高さフォグが有効な間は CSM が投影物を 1 つずつ（カスケードの数だけ）描き、
        // 1 つあたり Debug の記録で約 0.5 ms かかるので、影が遠くから見える大岩だけに絞る。残りは影を落とさない散布へ回す
        constexpr uint32_t kCasterBudget = 96;

        uint32_t Hash(uint32_t value)
        {
            value ^= value >> 16;
            value *= 0x7feb352du;
            value ^= value >> 15;
            value *= 0x846ca68bu;
            value ^= value >> 16;
            return value;
        }

        /// 升目 (cx, cz) の系列 stream の [0,1) の乱数
        float Random(int32_t cx, int32_t cz, uint32_t stream)
        {
            const uint32_t key = Hash(static_cast<uint32_t>(cx) * 0x8da6b343u ^ Hash(static_cast<uint32_t>(cz) * 0xd8163841u) ^
                                      (stream * 0x9e3779b9u + 0x27d4eb2fu));
            return static_cast<float>(key & 0xFFFFFFu) / 16777216.0f;
        }

        float Smooth(const float (&range)[2], float value)
        {
            const float t = std::clamp((value - range[0]) / (range[1] - range[0]), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }

        float Lerp(const float (&range)[2], float t)
        {
            return range[0] + (range[1] - range[0]) * t;
        }

        /// 格子点の乱数を滑らかにつないだ値ノイズ（0〜1）
        float ValueNoise(float x, float z, uint32_t stream)
        {
            const float fx = std::floor(x);
            const float fz = std::floor(z);
            const int32_t ix = static_cast<int32_t>(fx);
            const int32_t iz = static_cast<int32_t>(fz);
            float tx = x - fx;
            float tz = z - fz;
            tx = tx * tx * (3.0f - 2.0f * tx);
            tz = tz * tz * (3.0f - 2.0f * tz);
            const float a = Random(ix, iz, stream);
            const float b = Random(ix + 1, iz, stream);
            const float c = Random(ix, iz + 1, stream);
            const float d = Random(ix + 1, iz + 1, stream);
            return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * tz;
        }

        /// 空白の帯を残す斑（0〜1）
        float Patch(float x, float z)
        {
            const float coarse = ValueNoise(x / kPatchWavelengths[0], z / kPatchWavelengths[0], 101);
            const float fine = ValueNoise(x / kPatchWavelengths[1], z / kPatchWavelengths[1], 102);
            return Smooth(kPatchRange, 0.7f * coarse + 0.3f * fine);
        }

        Quaternion Conjugate(const Quaternion &q)
        {
            return Quaternion(-q.x, -q.y, -q.z, q.w);
        }

        /// 上 (0,1,0) を単位ベクトル n へ回す回転
        Quaternion RotateUpTo(const Vector3 &n)
        {
            const float cosine = std::clamp(n.y, -1.0f, 1.0f);
            const float axisLength = std::sqrt(n.z * n.z + n.x * n.x);
            if (axisLength < 1.0e-5f)
            {
                return Quaternion();
            }
            return Quaternion(Vector3(n.z / axisLength, 0.0f, -n.x / axisLength), std::acos(cosine));
        }

        /// 地形の法線へ割合 align だけ寄せ、水平の向き（乱数 u0）へ jitterDegrees × u1 だけ傾けた上の向き
        Vector3 TiltedUp(const TerrainNormal &normal, float align, float jitterDegrees, float u0, float u1)
        {
            Vector3 up(normal.X * align, 1.0f - align + normal.Y * align, normal.Z * align);
            up = up.Normalized();
            const float angle = u0 * 2.0f * kPi;
            const float tilt = std::tan(jitterDegrees * kDegToRad * u1);
            return Vector3(up.x + tilt * std::cos(angle), up.y, up.z + tilt * std::sin(angle)).Normalized();
        }

        /// 岩塊を 6 面のどれかを下にして置く回転
        Quaternion RestFace(float u)
        {
            const uint32_t face = std::min(5u, static_cast<uint32_t>(u * 6.0f));
            const Vector3 axisX(1.0f, 0.0f, 0.0f);
            const Vector3 axisZ(0.0f, 0.0f, 1.0f);
            switch (face)
            {
            case 1:
                return Quaternion(axisX, 0.5f * kPi);
            case 2:
                return Quaternion(axisX, -0.5f * kPi);
            case 3:
                return Quaternion(axisX, kPi);
            case 4:
                return Quaternion(axisZ, 0.5f * kPi);
            case 5:
                return Quaternion(axisZ, -0.5f * kPi);
            default:
                return Quaternion();
            }
        }

        /// 頂点の標本から、kSupportDirections の向き（球面の黄金角の螺旋）それぞれで最も遠い頂点を重複なく選ぶ
        void SelectSupportPoints(const VariableArray<Vector3> &points, VariableArray<Vector3> &outSupport)
        {
            outSupport.clear();
            if (points.empty())
            {
                return;
            }
            VariableArray<uint8_t> chosen(points.size(), 0);
            const float goldenAngle = kPi * (3.0f - std::sqrt(5.0f));
            for (uint32_t direction = 0; direction < kSupportDirections; ++direction)
            {
                const float y = 1.0f - 2.0f * (static_cast<float>(direction) + 0.5f) / static_cast<float>(kSupportDirections);
                const float ring = std::sqrt(std::max(0.0f, 1.0f - y * y));
                const float angle = goldenAngle * static_cast<float>(direction);
                const Vector3 axis(ring * std::cos(angle), y, ring * std::sin(angle));
                size_t best = 0;
                float bestDot = std::numeric_limits<float>::lowest();
                for (size_t index = 0; index < points.size(); ++index)
                {
                    const Vector3 &point = points[index];
                    const float dot = point.x * axis.x + point.y * axis.y + point.z * axis.z;
                    if (dot > bestDot)
                    {
                        bestDot = dot;
                        best = index;
                    }
                }
                if (chosen[best] == 0)
                {
                    chosen[best] = 1;
                    outSupport.push_back(points[best]);
                }
            }
        }

        /// 影を落とす候補（大岩・転がり出た岩）。大きい順に kCasterBudget までを影を落とす散布へ回す
        struct CasterCandidate
        {
            MegaScatterInstance Instance;
            float Height = 0.0f;
        };

        struct Builder
        {
            const TerrainHeightField &Field;
            const TerrainSurfaceMask &Mask;
            const TalusMeshShape (&Shapes)[kTalusMeshCount];
            const VariableArray<TalusExclusion> &Exclusions;
            /// 影を落とさない岩（小片と、影の上限から溢れた候補）
            VariableArray<MegaScatterInstance> &Fragments;
            TalusStats &Stats;
            float MinSlopeCosine = std::cos(kMaxSlopeDegrees * kDegToRad);
            /// 影を落とす候補
            VariableArray<CasterCandidate> Candidates;
            /// メッシュごとの底の高さを引く頂点（SelectSupportPoints が選んだもの）
            VariableArray<Vector3> Support[kTalusMeshCount];
            /// 回した標本の y（AddRock の作業用）
            VariableArray<float> RotatedY;

            bool IsExcluded(float x, float z) const
            {
                for (const TalusExclusion &exclusion : Exclusions)
                {
                    const float dx = x - exclusion.X;
                    const float dz = z - exclusion.Z;
                    if (dx * dx + dz * dz < exclusion.Radius * exclusion.Radius)
                    {
                        return true;
                    }
                }
                return false;
            }

            /// (x,z) に置けるか（格子の中・崖でない・除外の外）。置けるなら法線を返す
            bool CanPlace(float x, float z, TerrainNormal &outNormal) const
            {
                if (!Field.Contains(x, z) || IsExcluded(x, z))
                {
                    return false;
                }
                outNormal = Field.SampleNormal(x, z, 1.0f);
                return outNormal.Y >= MinSlopeCosine;
            }

            /// メッシュ mesh を、ワールドの回転 worldRotation（Quaternion の * の向き）・拡大率 scale で、範囲の中心が (x,z) に
            /// 来るように置き、底の頂点がどれも地面より下になる高さから埋める。置いた岩の回した高さ[m]を返す
            float AddRock(TalusMesh mesh, const Quaternion &worldRotation, float scale, float x, float z, bool bCaster)
            {
                const TalusMeshShape &shape = Shapes[static_cast<uint32_t>(mesh)];
                const VariableArray<Vector3> &points = Support[static_cast<uint32_t>(mesh)];
                const Vector3 center((shape.Min.x + shape.Max.x) * 0.5f, (shape.Min.y + shape.Max.y) * 0.5f,
                                     (shape.Min.z + shape.Max.z) * 0.5f);
                const Vector3 centerOffset = worldRotation * Vector3(center.x * scale, center.y * scale, center.z * scale);
                const float originX = x - centerOffset.x;
                const float originZ = z - centerOffset.z;

                RotatedY.resize(points.size());
                float minY = std::numeric_limits<float>::max();
                float maxY = std::numeric_limits<float>::lowest();
                for (size_t index = 0; index < points.size(); ++index)
                {
                    const Vector3 &point = points[index];
                    const float y = (worldRotation * Vector3(point.x * scale, point.y * scale, point.z * scale)).y;
                    RotatedY[index] = y;
                    minY = std::min(minY, y);
                    maxY = std::max(maxY, y);
                }
                const float height = maxY - minY;
                const float bottomLimit = minY + height * kBottomBand;
                float lowest = std::numeric_limits<float>::max();
                for (size_t index = 0; index < points.size(); ++index)
                {
                    if (RotatedY[index] > bottomLimit)
                    {
                        continue;
                    }
                    const Vector3 &point = points[index];
                    const Vector3 offset = worldRotation * Vector3(point.x * scale, point.y * scale, point.z * scale);
                    lowest = std::min(lowest, Field.SampleHeight(originX + offset.x, originZ + offset.z) - offset.y);
                }
                const float sink = mesh == TalusMesh::Slab ? height * kSlabSinkFraction
                                                           : height * kCragSinkFraction + kCragSinkMeters;

                MegaScatterInstance instance;
                instance.Position = Vector3(originX, lowest - sink, originZ);
                // 描画の変換は行ベクトルの規約で、四元数の回転の逆を掛ける（WastelandLayout.cpp の RotateXZ と同じ向き）ので、
                // ワールドで worldRotation になるよう共役を渡す
                instance.Rotation = Conjugate(worldRotation);
                instance.Scale = Vector3(scale, scale, scale);
                instance.MeshIndex = static_cast<uint32_t>(mesh);
                if (bCaster)
                {
                    Candidates.push_back({instance, height});
                }
                else
                {
                    Fragments.push_back(instance);
                }
                ++Stats.PerMesh[static_cast<uint32_t>(mesh)];
                return height;
            }

            /// 岩塊を 1 つ置く（6 面のどれか・斜面へ寄せる・向きは乱数）
            float AddCrag(float x, float z, const TerrainNormal &normal, float scale, int32_t cx, int32_t cz, uint32_t stream,
                          bool bCaster)
            {
                const Vector3 up = TiltedUp(normal, kCragAlign, kCragTiltJitterDegrees, Random(cx, cz, stream),
                                            Random(cx, cz, stream + 1));
                const Quaternion yaw(Vector3(0.0f, 1.0f, 0.0f), Random(cx, cz, stream + 2) * 2.0f * kPi);
                const Quaternion rotation = RotateUpTo(up) * yaw * RestFace(Random(cx, cz, stream + 3));
                return AddRock(TalusMesh::Crag, rotation, scale, x, z, bCaster);
            }

            /// 板を 1 つ置く（平たいまま斜面へ沿わせる）
            void AddSlab(float x, float z, const TerrainNormal &normal, float scale, int32_t cx, int32_t cz, uint32_t stream)
            {
                const Vector3 up = TiltedUp(normal, kSlabAlign, kSlabTiltJitterDegrees, Random(cx, cz, stream),
                                            Random(cx, cz, stream + 1));
                const Quaternion yaw(Vector3(0.0f, 1.0f, 0.0f), Random(cx, cz, stream + 2) * 2.0f * kPi);
                AddRock(TalusMesh::Slab, RotateUpTo(up) * yaw, scale, x, z, false);
            }

            /// 升目 (cx, cz) の中心 (x,z) に群れを置く
            void AddCluster(float x, float z, const TerrainNormal &normal, float baseWeight, int32_t cx, int32_t cz)
            {
                ++Stats.Clusters;
                const bool bMain = Random(cx, cz, 10) >= kNoMainChance;
                float mainScale = 0.0f;
                if (bMain)
                {
                    const float u = Random(cx, cz, 11);
                    mainScale = std::min(kMainScaleMax, Lerp(kMainScale, u * u) * (1.0f + kMainScaleBaseBoost * baseWeight));
                    const float height = AddCrag(x, z, normal, mainScale, cx, cz, 12, true);
                    Stats.MaxBoulderHeight = std::max(Stats.MaxBoulderHeight, height);
                    ++Stats.MainBoulders;
                }
                const uint32_t fragments =
                    bMain ? kFragmentBase + static_cast<uint32_t>(Random(cx, cz, 20) *
                                                                  (kFragmentSpread + kFragmentPerMainScale * mainScale))
                          : kFragmentsWithoutMain[0] +
                                static_cast<uint32_t>(Random(cx, cz, 20) *
                                                      static_cast<float>(kFragmentsWithoutMain[1] - kFragmentsWithoutMain[0] + 1));
                const float radius =
                    bMain ? kFragmentRadiusBase + kFragmentRadiusPerMainScale * mainScale : kFragmentRadiusWithoutMain;
                // 大岩に埋もれない最小の距離（大岩の長い辺の半分ほど）
                const float keepOut = bMain ? 0.6f * mainScale : 0.0f;
                // 斜面の下の水平の向き（法線の水平成分の向き）
                float downX = normal.X;
                float downZ = normal.Z;
                const float downLength = std::sqrt(downX * downX + downZ * downZ);
                if (downLength > 1.0e-4f)
                {
                    downX /= downLength;
                    downZ /= downLength;
                }
                else
                {
                    downX = downZ = 0.0f;
                }
                for (uint32_t index = 0; index < fragments; ++index)
                {
                    const uint32_t stream = 100 + index * 16;
                    const float angle = Random(cx, cz, stream) * 2.0f * kPi;
                    float distance = radius * std::sqrt(Random(cx, cz, stream + 1));
                    distance = std::max(distance, keepOut);
                    const float shift = radius * kDownslopeShift * Random(cx, cz, stream + 2);
                    const float fx = x + distance * std::cos(angle) + shift * downX;
                    const float fz = z + distance * std::sin(angle) + shift * downZ;
                    TerrainNormal fragmentNormal;
                    if (!CanPlace(fx, fz, fragmentNormal))
                    {
                        ++Stats.Rejected;
                        continue;
                    }
                    if (Shapes[static_cast<uint32_t>(TalusMesh::Slab)].bAvailable &&
                        Random(cx, cz, stream + 3) < kSlabFraction)
                    {
                        AddSlab(fx, fz, fragmentNormal, Lerp(kSlabScale, Random(cx, cz, stream + 4)), cx, cz, stream + 5);
                    }
                    else
                    {
                        const float u = Random(cx, cz, stream + 4);
                        AddCrag(fx, fz, fragmentNormal, Lerp(kFragmentCragScale, u * u), cx, cz, stream + 5, false);
                    }
                    ++Stats.Fragments;
                }
            }
        };
    } // namespace

    void BuildTalusRocks(const TerrainHeightField &field, const TerrainSurfaceMask &mask,
                         const TalusMeshShape (&shapes)[kTalusMeshCount], const VariableArray<TalusExclusion> &exclusions,
                         VariableArray<MegaScatterInstance> &outCasters, VariableArray<MegaScatterInstance> &outFragments,
                         TalusStats &outStats)
    {
        outCasters.clear();
        outFragments.clear();
        outStats = TalusStats{};
        const TalusMeshShape &crag = shapes[static_cast<uint32_t>(TalusMesh::Crag)];
        if (!crag.bAvailable || crag.Points.empty())
        {
            return;
        }
        Builder builder{field, mask, shapes, exclusions, outFragments, outStats};
        for (uint32_t meshIndex = 0; meshIndex < kTalusMeshCount; ++meshIndex)
        {
            SelectSupportPoints(shapes[meshIndex].Points, builder.Support[meshIndex]);
        }
        const auto &layout = mask.GetLayout();
        const int32_t cellsX = static_cast<int32_t>(std::floor((layout.MaxX - layout.MinX) / kClusterCellMeters));
        const int32_t cellsZ = static_cast<int32_t>(std::floor((layout.MaxZ - layout.MinZ) / kClusterCellMeters));
        // 升目の番号は世界の原点からの升目にする（地図の範囲が変わっても、同じ升目は同じ乱数）
        const int32_t firstX = static_cast<int32_t>(std::floor(layout.MinX / kClusterCellMeters));
        const int32_t firstZ = static_cast<int32_t>(std::floor(layout.MinZ / kClusterCellMeters));
        for (int32_t iz = 0; iz < cellsZ; ++iz)
        {
            for (int32_t ix = 0; ix < cellsX; ++ix)
            {
                const int32_t cx = firstX + ix;
                const int32_t cz = firstZ + iz;
                ++outStats.CellsVisited;
                const float x = (static_cast<float>(cx) + Random(cx, cz, 0)) * kClusterCellMeters;
                const float z = (static_cast<float>(cz) + Random(cx, cz, 1)) * kClusterCellMeters;
                TerrainSurfaceWeights weights;
                if (!mask.Sample(x, z, weights))
                {
                    continue;
                }
                const float baseWeight = Smooth(kBaseRockRange, weights.Rock) * Smooth(kBaseTalusRange, weights.Talus);
                const float density = std::min(1.0f, Smooth(kTalusWeightRange, weights.Talus) + kBaseBoost * baseWeight);
                const float toe = Smooth(kToeTalusInner, weights.Talus) * (1.0f - Smooth(kToeTalusOuter, weights.Talus));
                if (density <= 0.0f && toe <= 0.0f)
                {
                    continue;
                }
                const float pick = Random(cx, cz, 2);
                const float patch = std::max(Patch(x, z), kBaseBeltFloor * baseWeight);
                const float clusterChance = density * patch * kClusterChance;
                if (pick < clusterChance)
                {
                    TerrainNormal normal;
                    if (builder.CanPlace(x, z, normal))
                    {
                        builder.AddCluster(x, z, normal, baseWeight, cx, cz);
                    }
                }
                else if (pick < clusterChance + toe * kToeChance)
                {
                    TerrainNormal normal;
                    if (builder.CanPlace(x, z, normal))
                    {
                        const float u = Random(cx, cz, 3);
                        builder.AddCrag(x, z, normal, Lerp(kToeScale, u * u), cx, cz, 12, true);
                        ++outStats.ToeRocks;
                    }
                }
            }
        }

        // 影を落とす候補を大きい順に並べ、上限までを影を落とす散布へ、残りを影を落とさない散布へ回す
        VariableArray<CasterCandidate> &candidates = builder.Candidates;
        std::sort(candidates.begin(), candidates.end(),
                  [](const CasterCandidate &a, const CasterCandidate &b) { return a.Height > b.Height; });
        outStats.CasterCandidates = static_cast<uint32_t>(candidates.size());
        for (size_t index = 0; index < candidates.size(); ++index)
        {
            if (index < kCasterBudget)
            {
                outCasters.push_back(candidates[index].Instance);
                outStats.MinCasterHeight = candidates[index].Height;
            }
            else
            {
                outFragments.push_back(candidates[index].Instance);
            }
        }
        outStats.SupportPoints[0] = static_cast<uint32_t>(builder.Support[0].size());
        outStats.SupportPoints[1] = static_cast<uint32_t>(builder.Support[1].size());
    }

} // namespace Game::GameModes::WastelandTalus
