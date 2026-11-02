// 崖錐の岩と、崖の生成の 7 範囲の壁の足に置く崩れた塊。
// 塊は接地用の格子の焼いた面を使い、不可の升目は見送る。塊の無い所は焼いた面が高さ場と同じ。
// 崖錐と共通の接地・寄せ直し・縮小・ずらしで据える。

#include "GameModes/Wasteland/WastelandTalus.h"
#include "GameModes/Wasteland/WastelandFoliage.h"

#include "Core/Public/Logging/LogMacros.h"
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
namespace Terrain = NorvesLib::Core::Rendering::Terrain;

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
        // DEM の格子の縁から置かない幅[m]（最も大きい岩〔拡大率 4.5・水平の半径約 5.6 m〕の足元が格子の外へはみ出さない）
        constexpr float kGridEdgeMargin = 6.0f;

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
        constexpr double kBottomBand = 0.05;
        // 検査の支持窓と埋もれの上限に対し、DEM の双線形と焼いた面の三角形の差を見込む[m]
        constexpr float kSupportMargin = 0.03f;
        constexpr float kFloatAllowance = 0.02f;
        constexpr uint32_t kAlignmentSteps = 4;
        constexpr float kShrinkFactor = 0.75f;
        // 元の中心から最大 1 m、内外の輪に 4 点ずつ試す。
        constexpr uint32_t kShiftSteps = 8;
        constexpr float kShiftDistances[2] = {0.5f, 1.0f};
        // 埋める深さ（回した高さに対する割合と、足す深さ[m]）
        constexpr float kCragSinkFraction = 0.12f;
        constexpr float kCragSinkMeters = 0.03f;
        constexpr float kSlabSinkFraction = 0.25f;
        // ---- 影 ----
        // 影を落とす岩の上限（大きい順）。高さフォグが有効な間は CSM が投影物を 1 つずつ（カスケードの数だけ）描き、
        // 1 つあたり Debug の記録で約 0.5 ms かかるので、影が遠くから見える大岩だけに絞る。残りは影を落とさない散布へ回す
        constexpr uint32_t kCasterBudget = 96;

        // ---- 崖の塊 ----
        constexpr float kBlockCellMeters = 12.0f;
        constexpr float kBlockMaxSlopeDegrees = 38.0f;
        constexpr float kWallMinSlopeDegrees = 55.0f;
        constexpr float kBlockChance = 0.55f;
        // 崖錐の系列（群れ・小片・斑）から離した専用の系列。
        constexpr uint32_t kBlockStream = 20000u;

        struct CliffBlockRegion
        {
            const char *Id;
            float MinX;
            float MaxX;
            float MinZ;
            float MaxZ;

            bool Contains(float x, float z) const
            {
                return x >= MinX && x <= MaxX && z >= MinZ && z <= MaxZ;
            }
        };

        // 出典: Scripts/Terrain/ShapeRegions.json の range_groups["cliff-gen"]。
        // Tools/AssetCook/TerrainCliffDetail.cpp と同じ世界座標[m]。表の先頭を重複升目の所有範囲にする。
        constexpr CliffBlockRegion kCliffBlockRegions[kCliffBlockRegionCount] = {
            {"cliff_near_2_rooster_butte", 640.0f, 1160.0f, 560.0f, 1160.0f},
            {"cliff_near_3_setting_hen_butte", 960.0f, 1460.0f, 1140.0f, 1960.0f},
            {"cliff_castle_0_main_butte", -3450.0f, -2100.0f, -4450.0f, -3950.0f},
            {"cliff_castle_1_far_cliffs", -3650.0f, -1900.0f, -6340.0f, -4650.0f},
            {"cliff_castle_2_left_7_9km", -6250.0f, -5150.0f, -8250.0f, -6250.0f},
            {"cliff_castle_3_left_5_7km", -5350.0f, -4250.0f, -7000.0f, -5500.0f},
            {"cliff_castle_4_left_edge_9km", -6875.0f, -6150.0f, -8625.0f, -7875.0f}};

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

        /// 塊の外は DEM の三角形をそのまま描く。その面が検査の支持面の向きを満たすかを調べる。
        bool DemTriangleHasSupport(const TerrainHeightField &field, float x, float z)
        {
            if (!field.IsGrid())
            {
                return true;
            }
            const Terrain::TerrainGridLayout layout = field.GetGridLayout();
            if (layout.Columns < 2u || layout.Rows < 2u || layout.Spacing <= 0.0 || !field.Contains(x, z))
            {
                return false;
            }
            const double u = (static_cast<double>(x) - layout.FirstX) / layout.Spacing;
            const double v = (static_cast<double>(z) - layout.FirstZ) / layout.Spacing;
            const uint32_t column = std::min(static_cast<uint32_t>(u), layout.Columns - 2u);
            const uint32_t row = std::min(static_cast<uint32_t>(v), layout.Rows - 2u);
            const double fu = u - column;
            const double fv = v - row;
            const double h00 = field.GetGridVertexHeight(column, row);
            const double h10 = field.GetGridVertexHeight(column + 1u, row);
            const double h01 = field.GetGridVertexHeight(column, row + 1u);
            const double h11 = field.GetGridVertexHeight(column + 1u, row + 1u);
            double dx = 0.0;
            double dz = 0.0;
            if (Terrain::UsesMainDiagonal(column, row))
            {
                dx = fu >= fv ? h10 - h00 : h11 - h01;
                dz = fu >= fv ? h11 - h10 : h01 - h00;
            }
            else
            {
                dx = fu + fv <= 1.0 ? h10 - h00 : h11 - h01;
                dz = fu + fv <= 1.0 ? h01 - h00 : h11 - h10;
            }
            dx /= layout.Spacing;
            dz /= layout.Spacing;
            // CheckPlacement.py と接地格子の交点探索が使う上向きの面の下限。
            constexpr double kMinSupportNormalY = 0.3;
            return std::isfinite(dx) && std::isfinite(dz) &&
                   1.0 / std::sqrt(1.0 + dx * dx + dz * dz) >= kMinSupportNormalY;
        }

        /// 影を落とす候補（大岩・転がり出た岩）。大きい順に kCasterBudget までを影を落とす散布へ回す
        struct CasterCandidate
        {
            MegaScatterInstance Instance;
            float Height = 0.0f;
        };

        /// 接地の標本と許容区間。崖錐と崖の塊で同じ判定を使う。
        struct RockFitter
        {
            const TerrainHeightField &Field;
            const Terrain::TerrainPlacementGround *Ground;
            const TalusMeshShape (&Shapes)[kTalusMeshCount];
            /// 回した全標本の座標（足元の帯と高さを同じ標本から求める）
            VariableArray<Vector3> RotatedPoints;
            /// 検査と同じ倍精度で回した高さの列。
            VariableArray<double> RotatedBandY;

            /// 検査と同じ全標本の下 5 % を足元とし、浮きと埋もれの両方に収まる高さを求める
            bool FitRock(TalusMesh mesh, const Quaternion &worldRotation, float scale, float x, float z,
                         MegaScatterInstance &outInstance, float &outHeight, bool &unreliable, bool &noBlockUnsupported,
                         bool bCheckNeighborhood)
            {
                unreliable = false;
                noBlockUnsupported = false;
                const TalusMeshShape &shape = Shapes[static_cast<uint32_t>(mesh)];
                const VariableArray<Vector3> &points = shape.Points;
                if (points.empty())
                {
                    return false;
                }
                const Vector3 center((shape.Min.x + shape.Max.x) * 0.5f, (shape.Min.y + shape.Max.y) * 0.5f,
                                     (shape.Min.z + shape.Max.z) * 0.5f);
                const Vector3 centerOffset = worldRotation * Vector3(center.x * scale, center.y * scale, center.z * scale);
                const float originX = x - centerOffset.x;
                const float originZ = z - centerOffset.z;
                // 見送った候補も、配置時と同じ原点で範囲別に数える。
                outInstance.Position.x = originX;
                outInstance.Position.z = originZ;

                RotatedPoints.resize(points.size());
                RotatedBandY.resize(points.size());
                float minY = std::numeric_limits<float>::max();
                float maxY = std::numeric_limits<float>::lowest();
                double minBandY = std::numeric_limits<double>::max();
                double maxBandY = std::numeric_limits<double>::lowest();
                const double ux = worldRotation.x;
                const double uy = worldRotation.y;
                const double uz = worldRotation.z;
                const double w = worldRotation.w;
                for (size_t index = 0; index < points.size(); ++index)
                {
                    const Vector3 &point = points[index];
                    const Vector3 offset = worldRotation * Vector3(point.x * scale, point.y * scale, point.z * scale);
                    RotatedPoints[index] = offset;
                    minY = std::min(minY, offset.y);
                    maxY = std::max(maxY, offset.y);
                    // 足元の帯は検査と同じ四元数の式・倍精度で選ぶ。高さの許しと帯の割合は変えない。
                    const double vx = static_cast<double>(point.x) * scale;
                    const double vy = static_cast<double>(point.y) * scale;
                    const double vz = static_cast<double>(point.z) * scale;
                    const double tx = 2.0 * (uy * vz - uz * vy);
                    const double ty = 2.0 * (uz * vx - ux * vz);
                    const double tz = 2.0 * (ux * vy - uy * vx);
                    const double bandY = vy + w * ty + (uz * tx - ux * tz);
                    RotatedBandY[index] = bandY;
                    minBandY = std::min(minBandY, bandY);
                    maxBandY = std::max(maxBandY, bandY);
                }
                outHeight = maxY - minY;
                // 全標本の回転後の高さの下 kBottomBand を足元にする。
                // 接地格子があるときは倍精度、無いときは float で帯を選ぶ。
                const double bottomLimit = Ground != nullptr
                                               ? minBandY + (maxBandY - minBandY) * kBottomBand
                                               : static_cast<double>(minY + outHeight * static_cast<float>(kBottomBand));
                float lowest = std::numeric_limits<float>::max();
                float highest = std::numeric_limits<float>::lowest();
                for (size_t index = 0; index < RotatedPoints.size(); ++index)
                {
                    const Vector3 &offset = RotatedPoints[index];
                    const double bandY = Ground != nullptr ? RotatedBandY[index] : static_cast<double>(offset.y);
                    if (bandY > bottomLimit)
                    {
                        continue;
                    }
                    const float sampleX = originX + offset.x;
                    const float sampleZ = originZ + offset.z;
                    float groundY = 0.0f;
                    if (Ground != nullptr && Ground->Sample(sampleX, sampleZ, groundY) == Terrain::PlacementGroundSample::NoBlock &&
                        !DemTriangleHasSupport(Field, sampleX, sampleZ))
                    {
                        unreliable = true;
                        noBlockUnsupported = true;
                        return false;
                    }
                    if (!WastelandFoliage::SamplePlacementGround(Field, Ground, sampleX, sampleZ, groundY))
                    {
                        unreliable = true;
                        return false;
                    }
                    if (bCheckNeighborhood && Ground != nullptr)
                    {
                        // 救済時は補間可否の境に残る細い段を避け、足元の周囲半升目も確かめる。
                        constexpr float kProbeOffset = Terrain::PlacementGroundFormat::kSpacing * 0.5f;
                        for (int dz = -1; dz <= 1; ++dz)
                        {
                            for (int dx = -1; dx <= 1; ++dx)
                            {
                                if (dx == 0 && dz == 0)
                                {
                                    continue;
                                }
                                float nearbyY = 0.0f;
                                const float nearbyX = sampleX + static_cast<float>(dx) * kProbeOffset;
                                const float nearbyZ = sampleZ + static_cast<float>(dz) * kProbeOffset;
                                const auto nearby = Ground->Sample(nearbyX, nearbyZ, nearbyY);
                                if (nearby == Terrain::PlacementGroundSample::NoBlock &&
                                    !DemTriangleHasSupport(Field, nearbyX, nearbyZ))
                                {
                                    unreliable = true;
                                    noBlockUnsupported = true;
                                    return false;
                                }
                                if (nearby == Terrain::PlacementGroundSample::Unreliable)
                                {
                                    unreliable = true;
                                    return false;
                                }
                            }
                        }
                    }
                    const float contact = groundY - offset.y;
                    lowest = std::min(lowest, contact);
                    highest = std::max(highest, contact);
                }
                const float limit = std::min(0.5f * outHeight, std::max(0.5f, 0.2f * outHeight)) - kSupportMargin;
                const float lower = highest - limit;
                const float upper = lowest + kFloatAllowance;
                if (lower > upper)
                {
                    return false;
                }
                const float sink = mesh == TalusMesh::Slab ? outHeight * kSlabSinkFraction
                                                           : outHeight * kCragSinkFraction + kCragSinkMeters;
                outInstance.Position = Vector3(originX, std::clamp(lowest - sink, lower, upper), originZ);
                // 描画の行ベクトルの規約に合わせ、ワールドの回転の共役を渡す
                outInstance.Rotation = Conjugate(worldRotation);
                outInstance.Scale = Vector3(scale, scale, scale);
                outInstance.MeshIndex = static_cast<uint32_t>(mesh);
                return true;
            }
        };

        struct RockFitResult
        {
            MegaScatterInstance Instance;
            float Height = 0.0f;
            float CenterX = 0.0f;
            float CenterZ = 0.0f;
            float RejectionX = 0.0f;
            float RejectionZ = 0.0f;
            bool bFits = false;
            bool bRealigned = false;
            bool bShrunk = false;
            bool bShifted = false;
            bool bRecoveredRealigned = false;
            bool bRecoveredShrunk = false;
            bool bUnreliable = false;
            bool bNoBlockUnsupported = false;
        };

        /// 向きを決めた全標本の高さ（板では最長辺）で拡大率を決める。
        float OrientedExtent(const TalusMeshShape &shape, const Quaternion &rotation, bool bLongestEdge, float scale = 1.0f)
        {
            Vector3 low(std::numeric_limits<float>::max());
            Vector3 high(std::numeric_limits<float>::lowest());
            for (const Vector3 &point : shape.Points)
            {
                const Vector3 offset = rotation * Vector3(point.x * scale, point.y * scale, point.z * scale);
                low.x = std::min(low.x, offset.x);
                low.y = std::min(low.y, offset.y);
                low.z = std::min(low.z, offset.z);
                high.x = std::max(high.x, offset.x);
                high.y = std::max(high.y, offset.y);
                high.z = std::max(high.z, offset.z);
            }
            return bLongestEdge ? std::max({high.x - low.x, high.y - low.y, high.z - low.z}) : high.y - low.y;
        }

        /// 寄せ直し、縮小、ずらしを共通化する。scale は崖錐では拡大率、塊では狙う寸法。
        template <typename TContext>
        RockFitResult TryPlaceRock(TContext &context, TalusMesh mesh, float x, float z, const TerrainNormal &normal,
                                  float scale, int32_t cx, int32_t cz, uint32_t stream, bool bBlock = false)
        {
            RockFitResult result;
            const bool bSlab = !bBlock && mesh == TalusMesh::Slab;
            const float initialAlign = bSlab ? kSlabAlign : kCragAlign;
            const float jitter = bSlab ? kSlabTiltJitterDegrees : kCragTiltJitterDegrees;
            const float u0 = Random(cx, cz, stream);
            const float u1 = Random(cx, cz, stream + 1);
            const Quaternion yaw(Vector3(0.0f, 1.0f, 0.0f), Random(cx, cz, stream + 2) * 2.0f * kPi);
            const Quaternion face = bSlab ? Quaternion() : RestFace(Random(cx, cz, stream + 3));
            MegaScatterInstance instance;
            float height = 0.0f;
            bool bRealigned = false;
            bool unreliable = false;
            bool noBlockUnsupported = false;
            bool bEncounteredUnreliable = false;
            const float initialScale = scale;
            // 岩塊・板とも、既存の小片の最小拡大率までに留める。
            float minScale = bSlab ? kSlabScale[0] : kFragmentCragScale[0];
            const auto hasBlockSize = [&](const Quaternion &rotation)
            {
                const float extent = mesh == TalusMesh::Slab
                                         ? OrientedExtent(context.Shapes[static_cast<uint32_t>(mesh)], rotation, true, scale)
                                         : height;
                return std::isfinite(extent) && extent >= 3.0f && extent <= 14.0f;
            };
            const auto fitAt = [&](float fitX, float fitZ, const TerrainNormal &fitNormal, bool bShifted)
            {
                Quaternion rotation;
                scale = initialScale;
                bRealigned = false;
                for (uint32_t step = 0; step <= kAlignmentSteps; ++step)
                {
                    const float t = static_cast<float>(step) / static_cast<float>(kAlignmentSteps);
                    // 最後はばらつきも取り除いて法線に揃える。最初は乱数の回転と積の順序を保つ。
                    const Vector3 up = TiltedUp(fitNormal, initialAlign + (1.0f - initialAlign) * t,
                                               jitter * (1.0f - t), u0, u1);
                    rotation = RotateUpTo(up) * yaw;
                    if (!bSlab)
                    {
                        rotation = rotation * face;
                    }
                    if (bBlock)
                    {
                        // 板は薄さを保つため、縮小の下限も高さではなく最長辺 3 m とする。
                        const float extent = OrientedExtent(context.Shapes[static_cast<uint32_t>(mesh)], rotation,
                                                            mesh == TalusMesh::Slab);
                        if (!std::isfinite(extent) || extent <= 1.0e-5f)
                        {
                            return false;
                        }
                        scale = initialScale / extent;
                        minScale = 3.0f / extent;
                    }
                    if (context.Fitter.FitRock(mesh, rotation, scale, fitX, fitZ, instance, height, unreliable, noBlockUnsupported,
                                               bShifted || bEncounteredUnreliable) &&
                        (!bBlock || hasBlockSize(rotation)))
                    {
                        bRealigned = step > 0;
                        result.CenterX = fitX;
                        result.CenterZ = fitZ;
                        return true;
                    }
                    bEncounteredUnreliable = bEncounteredUnreliable || unreliable;
                }
                // 向きと大きさで足元の帯が変わるため、信頼できない升目でも次の試行を続ける。
                while (scale > minScale)
                {
                    scale = std::max(minScale, scale * kShrinkFactor);
                    bRealigned = true;
                    if (context.Fitter.FitRock(mesh, rotation, scale, fitX, fitZ, instance, height, unreliable, noBlockUnsupported,
                                               bShifted || bEncounteredUnreliable) &&
                        (!bBlock || hasBlockSize(rotation)))
                    {
                        result.CenterX = fitX;
                        result.CenterZ = fitZ;
                        return true;
                    }
                    bEncounteredUnreliable = bEncounteredUnreliable || unreliable;
                }
                return false;
            };
            bool bFits = fitAt(x, z, normal, false);
            if (bFits && bEncounteredUnreliable)
            {
                if (bBlock ? height < initialScale : scale < initialScale)
                {
                    result.bRecoveredShrunk = true;
                }
                else
                {
                    result.bRecoveredRealigned = true;
                }
            }
            // 見送りの集計は、ずらす前の最終試行と同じ配置原点に残す。
            const float rejectionX = instance.Position.x;
            const float rejectionZ = instance.Position.z;
            if (!bFits)
            {
                // 系列 +4 は各岩の未使用枠。群れの生成は配置結果を参照せず、予定の中心と大きさを使う。
                const float shiftAngle = Random(cx, cz, stream + 4) * 2.0f * kPi;
                for (uint32_t step = 0; step < kShiftSteps; ++step)
                {
                    const float angle = shiftAngle + static_cast<float>(step % 4) * (0.5f * kPi);
                    const float distance = kShiftDistances[step / 4];
                    const float shiftedX = x + distance * std::cos(angle);
                    const float shiftedZ = z + distance * std::sin(angle);
                    TerrainNormal shiftedNormal;
                    if (context.CanPlace(shiftedX, shiftedZ, shiftedNormal) && fitAt(shiftedX, shiftedZ, shiftedNormal, true))
                    {
                        bFits = true;
                        result.bShifted = true;
                        break;
                    }
                }
            }
            result.Instance = instance;
            result.Height = height;
            result.RejectionX = rejectionX;
            result.RejectionZ = rejectionZ;
            result.bFits = bFits;
            result.bRealigned = bRealigned;
            result.bShrunk = bBlock ? height < initialScale : scale < initialScale;
            result.bUnreliable = unreliable;
            result.bNoBlockUnsupported = noBlockUnsupported;
            return result;
        }

        struct Builder
        {
            const TerrainHeightField &Field;
            const TerrainSurfaceMask &Mask;
            const NorvesLib::Core::Rendering::Terrain::TerrainPlacementGround *Ground;
            const TalusMeshShape (&Shapes)[kTalusMeshCount];
            const VariableArray<TalusExclusion> &Exclusions;
            /// 影を落とさない岩（小片と、影の上限から溢れた候補）
            VariableArray<MegaScatterInstance> &Fragments;
            TalusStats &Stats;
            float MinSlopeCosine = std::cos(kMaxSlopeDegrees * kDegToRad);
            /// 影を落とす候補
            VariableArray<CasterCandidate> Candidates;
            RockFitter Fitter{Field, Ground, Shapes};

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

            /// (x,z) に置けるか（格子の縁から kGridEdgeMargin の内・崖でない・除外の外）。置けるなら法線を返す
            bool CanPlace(float x, float z, TerrainNormal &outNormal) const
            {
                if (!Field.Contains(x - kGridEdgeMargin, z - kGridEdgeMargin) ||
                    !Field.Contains(x + kGridEdgeMargin, z + kGridEdgeMargin) || IsExcluded(x, z))
                {
                    return false;
                }
                outNormal = Field.SampleNormal(x, z, 1.0f);
                return outNormal.Y >= MinSlopeCosine;
            }

            /// 共通の据え方の結果だけを、崖錐の岩の統計と散布へ反映する。
            float AddRock(TalusMesh mesh, float x, float z, const TerrainNormal &normal, float scale, int32_t cx,
                          int32_t cz, uint32_t stream, bool bCaster)
            {
                const RockFitResult result = TryPlaceRock(*this, mesh, x, z, normal, scale, cx, cz, stream);
                Stats.RecoveredShrunk += result.bRecoveredShrunk ? 1u : 0u;
                Stats.RecoveredRealigned += result.bRecoveredRealigned ? 1u : 0u;
                Stats.Shifted += result.bShifted ? 1u : 0u;
                if (!result.bFits)
                {
                    ++Stats.Unplaced;
                    Stats.GroundRejections.push_back({result.RejectionX, result.RejectionZ,
                                                      static_cast<uint32_t>(mesh), result.bUnreliable});
                    if (result.bUnreliable)
                    {
                        ++Stats.Unreliable[static_cast<uint32_t>(mesh)];
                        if (result.bNoBlockUnsupported)
                        {
                            ++Stats.NoBlockUnsupported[static_cast<uint32_t>(mesh)];
                        }
                    }
                    return 0.0f;
                }
                if (result.bRealigned)
                {
                    ++Stats.Realigned;
                }
                if (result.bShrunk)
                {
                    ++Stats.Shrunk;
                }
                if (bCaster)
                {
                    Candidates.push_back({result.Instance, result.Height});
                }
                else
                {
                    Fragments.push_back(result.Instance);
                }
                ++Stats.PerMesh[static_cast<uint32_t>(mesh)];
                return result.Height;
            }

            /// 岩塊を 1 つ置く（6 面のどれか・斜面へ寄せる・向きは乱数）
            float AddCrag(float x, float z, const TerrainNormal &normal, float scale, int32_t cx, int32_t cz, uint32_t stream,
                          bool bCaster)
            {
                return AddRock(TalusMesh::Crag, x, z, normal, scale, cx, cz, stream, bCaster);
            }

            /// 板を 1 つ置く（平たいまま斜面に沿わせる）。収まらず置けなければ偽を返す
            bool AddSlab(float x, float z, const TerrainNormal &normal, float scale, int32_t cx, int32_t cz, uint32_t stream)
            {
                return AddRock(TalusMesh::Slab, x, z, normal, scale, cx, cz, stream, false) > 0.0f;
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
                    if (height > 0.0f)
                    {
                        ++Stats.MainBoulders;
                    }
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
                    bool bPlaced = false;
                    if (Shapes[static_cast<uint32_t>(TalusMesh::Slab)].bAvailable &&
                        Random(cx, cz, stream + 3) < kSlabFraction)
                    {
                        bPlaced = AddSlab(fx, fz, fragmentNormal, Lerp(kSlabScale, Random(cx, cz, stream + 4)), cx, cz,
                                          stream + 5);
                    }
                    else
                    {
                        const float u = Random(cx, cz, stream + 4);
                        bPlaced = AddCrag(fx, fz, fragmentNormal, Lerp(kFragmentCragScale, u * u), cx, cz, stream + 5,
                                          false) > 0.0f;
                    }
                    if (bPlaced)
                    {
                        ++Stats.Fragments;
                    }
                }
            }
        };

        /// 中心と ±1 m の 5 点の焼いた面から中心差分で斜面を求める。不可の升目は見送り、塊の無い所は高さ場を使う。
        bool SampleBlockGround(const TerrainHeightField &field, const Terrain::TerrainPlacementGround &ground,
                               float x, float z, float &height, TerrainNormal &normal)
        {
            float left = 0.0f;
            float right = 0.0f;
            float back = 0.0f;
            float front = 0.0f;
            if (!ground.SampleGround(field, x, z, height) ||
                !ground.SampleGround(field, x - 1.0f, z, left) ||
                !ground.SampleGround(field, x + 1.0f, z, right) ||
                !ground.SampleGround(field, x, z - 1.0f, back) ||
                !ground.SampleGround(field, x, z + 1.0f, front) ||
                !std::isfinite(height) || !std::isfinite(left) || !std::isfinite(right) ||
                !std::isfinite(back) || !std::isfinite(front))
            {
                return false;
            }
            const float dx = (right - left) * 0.5f;
            const float dz = (front - back) * 0.5f;
            const float length = std::sqrt(dx * dx + 1.0f + dz * dz);
            normal = {-dx / length, 1.0f / length, -dz / length};
            return true;
        }

        /// 緩い斜面の上向き 4〜20 m に、3 m 以上高い急な壁がある帯。
        bool IsBlockFoot(const TerrainHeightField &field, const Terrain::TerrainPlacementGround &ground,
                         float x, float z, TerrainNormal &normal, bool &noGround)
        {
            noGround = false;
            float height = 0.0f;
            if (!SampleBlockGround(field, ground, x, z, height, normal))
            {
                noGround = true;
                return false;
            }
            if (normal.Y < std::cos(kBlockMaxSlopeDegrees * kDegToRad))
            {
                return false;
            }
            const float length = std::sqrt(normal.X * normal.X + normal.Z * normal.Z);
            if (length <= 1.0e-5f)
            {
                return false;
            }
            for (uint32_t distance = 4; distance <= 20; distance += 2)
            {
                const float wallX = x - normal.X / length * static_cast<float>(distance);
                const float wallZ = z - normal.Z / length * static_cast<float>(distance);
                float wallHeight = 0.0f;
                TerrainNormal wallNormal;
                if (!SampleBlockGround(field, ground, wallX, wallZ, wallHeight, wallNormal))
                {
                    noGround = true;
                    continue;
                }
                if (wallNormal.Y <= std::cos(kWallMinSlopeDegrees * kDegToRad) && wallHeight >= height + 3.0f)
                {
                    noGround = false;
                    return true;
                }
            }
            return false;
        }

        /// 塊の接地とずらし先の制約。崖錐の散布・統計は持たない。
        struct CliffBlockBuilder
        {
            const TerrainHeightField &Field;
            const Terrain::TerrainPlacementGround *Ground;
            const TalusMeshShape (&Shapes)[kTalusMeshCount];
            const VariableArray<TalusExclusion> &Exclusions;
            const CliffBlockRegion &Region;
            RockFitter Fitter{Field, Ground, Shapes};

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

            bool CanPlace(float x, float z, TerrainNormal &normal) const
            {
                bool noGround = false;
                return Region.Contains(x, z) && !IsExcluded(x, z) && IsBlockFoot(Field, *Ground, x, z, normal, noGround);
            }
        };

        struct PlacedCliffBlock
        {
            int32_t CellX;
            int32_t CellZ;
            float X;
            float Z;
            float Height;
            int32_t Next;
        };

    } // namespace

    const char *GetCliffBlockRegionId(uint32_t index)
    {
        return index < kCliffBlockRegionCount ? kCliffBlockRegions[index].Id : "";
    }

    void BuildCliffBlocks(const TerrainHeightField &field, const Terrain::TerrainPlacementGround *ground,
                          const TalusMeshShape (&shapes)[kTalusMeshCount], const VariableArray<TalusExclusion> &exclusions,
                          VariableArray<MegaScatterInstance> &outBlocks, CliffBlockStats &outStats)
    {
        outBlocks.clear();
        outStats = CliffBlockStats{};
        const TalusMeshShape &crag = shapes[static_cast<uint32_t>(TalusMesh::Crag)];
        if (!crag.bAvailable || crag.Points.empty())
        {
            return;
        }
        VariableArray<PlacedCliffBlock> placed;
        // 世界の升目をハッシュで引き、近隣 ±3 升目だけを調べる。範囲をまたぐ隣の塊とも間隔を保つ。
        constexpr uint32_t kNeighborBuckets = 4096u;
        int32_t heads[kNeighborBuckets];
        for (int32_t &head : heads)
        {
            head = -1;
        }
        const auto bucket = [](int32_t x, int32_t z)
        {
            return Hash(static_cast<uint32_t>(x) ^ Hash(static_cast<uint32_t>(z))) % kNeighborBuckets;
        };
        for (uint32_t regionIndex = 0; regionIndex < kCliffBlockRegionCount; ++regionIndex)
        {
            const CliffBlockRegion &region = kCliffBlockRegions[regionIndex];
            CliffBlockBuilder builder{field, ground, shapes, exclusions, region};
            const int32_t firstX = static_cast<int32_t>(std::ceil(region.MinX / kBlockCellMeters - 0.5f));
            const int32_t lastX = static_cast<int32_t>(std::floor(region.MaxX / kBlockCellMeters - 0.5f));
            const int32_t firstZ = static_cast<int32_t>(std::ceil(region.MinZ / kBlockCellMeters - 0.5f));
            const int32_t lastZ = static_cast<int32_t>(std::floor(region.MaxZ / kBlockCellMeters - 0.5f));
            for (int32_t cz = firstZ; cz <= lastZ; ++cz)
            {
                for (int32_t cx = firstX; cx <= lastX; ++cx)
                {
                    const float centerX = (static_cast<float>(cx) + 0.5f) * kBlockCellMeters;
                    const float centerZ = (static_cast<float>(cz) + 0.5f) * kBlockCellMeters;
                    bool bOwned = true;
                    for (uint32_t previous = 0; previous < regionIndex; ++previous)
                    {
                        if (kCliffBlockRegions[previous].Contains(centerX, centerZ))
                        {
                            bOwned = false;
                            break;
                        }
                    }
                    if (!bOwned)
                    {
                        continue;
                    }
                    ++outStats.CellsVisited;
                    if (ground == nullptr)
                    {
                        ++outStats.NoGround;
                        continue;
                    }
                    const float x = (static_cast<float>(cx) + Random(cx, cz, kBlockStream)) * kBlockCellMeters;
                    const float z = (static_cast<float>(cz) + Random(cx, cz, kBlockStream + 1)) * kBlockCellMeters;
                    if (!region.Contains(x, z))
                    {
                        continue;
                    }
                    TerrainNormal normal;
                    bool noGround = false;
                    if (!IsBlockFoot(field, *ground, x, z, normal, noGround))
                    {
                        outStats.NoGround += noGround ? 1u : 0u;
                        continue;
                    }
                    ++outStats.FootCells;
                    const float coarse = ValueNoise(x / kPatchWavelengths[0], z / kPatchWavelengths[0], kBlockStream + 100);
                    const float fine = ValueNoise(x / kPatchWavelengths[1], z / kPatchWavelengths[1], kBlockStream + 101);
                    const float patch = Smooth(kPatchRange, 0.7f * coarse + 0.3f * fine);
                    if (Random(cx, cz, kBlockStream + 2) >= kBlockChance * patch || builder.IsExcluded(x, z))
                    {
                        continue;
                    }
                    const uint32_t slabIndex = static_cast<uint32_t>(TalusMesh::Slab);
                    const TalusMesh mesh = shapes[slabIndex].bAvailable && !shapes[slabIndex].Points.empty() &&
                                                   Random(cx, cz, kBlockStream + 3) < 0.2f
                                               ? TalusMesh::Slab : TalusMesh::Crag;
                    const float u = Random(cx, cz, kBlockStream + 4);
                    const RockFitResult fit = TryPlaceRock(builder, mesh, x, z, normal, 3.0f + 11.0f * u * u,
                                                          cx, cz, kBlockStream + 16, true);
                    if (!fit.bFits)
                    {
                        ++outStats.Unplaced;
                        outStats.GroundRejections.push_back({fit.RejectionX, fit.RejectionZ,
                                                            static_cast<uint32_t>(mesh), fit.bUnreliable});
                        continue;
                    }
                    const int32_t cellX = static_cast<int32_t>(std::floor(fit.CenterX / kBlockCellMeters));
                    const int32_t cellZ = static_cast<int32_t>(std::floor(fit.CenterZ / kBlockCellMeters));
                    bool bTooClose = false;
                    // 最大の間隔は 0.6 × (14 + 14) = 16.8 m。ずらした後の中心の升目で近傍を引く。
                    for (int32_t dz = -3; dz <= 3 && !bTooClose; ++dz)
                    {
                        for (int32_t dx = -3; dx <= 3 && !bTooClose; ++dx)
                        {
                            for (int32_t index = heads[bucket(cellX + dx, cellZ + dz)]; index >= 0;
                                 index = placed[static_cast<size_t>(index)].Next)
                            {
                                const PlacedCliffBlock &other = placed[static_cast<size_t>(index)];
                                if (other.CellX != cellX + dx || other.CellZ != cellZ + dz)
                                {
                                    continue;
                                }
                                const float separation = 0.6f * (fit.Height + other.Height);
                                const float distanceX = fit.CenterX - other.X;
                                const float distanceZ = fit.CenterZ - other.Z;
                                if (distanceX * distanceX + distanceZ * distanceZ < separation * separation)
                                {
                                    bTooClose = true;
                                    break;
                                }
                            }
                        }
                    }
                    if (bTooClose)
                    {
                        continue;
                    }
                    const uint32_t key = bucket(cellX, cellZ);
                    placed.push_back({cellX, cellZ, fit.CenterX, fit.CenterZ, fit.Height, heads[key]});
                    heads[key] = static_cast<int32_t>(placed.size() - 1);
                    outBlocks.push_back(fit.Instance);
                    ++outStats.Placed;
                    ++outStats.PerRegion[regionIndex];
                    ++outStats.PerMesh[static_cast<uint32_t>(mesh)];
                    outStats.MinHeight = outStats.Placed == 1 ? fit.Height : std::min(outStats.MinHeight, fit.Height);
                    outStats.MaxHeight = std::max(outStats.MaxHeight, fit.Height);
                }
            }
        }
    }

    void BuildTalusRocks(const TerrainHeightField &field, const TerrainSurfaceMask &mask,
                         const NorvesLib::Core::Rendering::Terrain::TerrainPlacementGround *ground,
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
        Builder builder{field, mask, ground, shapes, exclusions, outFragments, outStats};
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
                        if (builder.AddCrag(x, z, normal, Lerp(kToeScale, u * u), cx, cz, 12, true) > 0.0f)
                        {
                            ++outStats.ToeRocks;
                        }
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
        outStats.SupportPoints[0] = static_cast<uint32_t>(shapes[0].Points.size());
        outStats.SupportPoints[1] = static_cast<uint32_t>(shapes[1].Points.size());
        LOG_INFO("WASTELAND_TALUS_FIT realigned=%u shrunk=%u shifted=%u recovered_realigned=%u recovered_shrunk=%u "
                 "unplaced=%u unreliable=%u/%u no_block_unsupported=%u/%u",
                 outStats.Realigned, outStats.Shrunk, outStats.Shifted, outStats.RecoveredRealigned, outStats.RecoveredShrunk,
                 outStats.Unplaced, outStats.Unreliable[0], outStats.Unreliable[1],
                 outStats.NoBlockUnsupported[0], outStats.NoBlockUnsupported[1]);
    }

} // namespace Game::GameModes::WastelandTalus
