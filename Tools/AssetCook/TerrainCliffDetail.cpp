#include "TerrainCliffDetail.h"
#include "TerrainCliffCarveBattleship.h"
#include "TerrainCliffShelfBattleship.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace NorvesLib::Tools::AssetCook
{
    namespace
    {
        namespace Terrain = Core::Rendering::Terrain;
        using Core::Container::AnsiString;
        using Core::Container::VariableArray;

        // 升目の一辺あたりの細分の数の範囲（下限は範囲ごとに変えられる。既定の下限）
        constexpr uint32_t kMinSubdivisions = 4u;
        constexpr uint32_t kMaxSubdivisions = 16u;

        // 崖の細部を焼く範囲（エンジンの m）と、面に沿った細分の間隔（m）と、升目の一辺あたりの細分の数の下限。
        // 範囲は手本の視点に写る崖を、視点からの距離で 1 画素が何 m になるかで間隔を決めて選んだ。範囲の内側
        // kRegionFadeMeters で押し出しを 0 から上げる
        struct CliffRegion
        {
            double MinX;
            double MaxX;
            double MinZ;
            double MaxZ;
            double TargetSpacing;
            uint32_t MinSubdivisions = kMinSubdivisions;
        };
        // 近景（1 m の DEM の格子）の範囲
        constexpr CliffRegion kNearRegions[] = {
            // 主役の Battleship Rock と周りの崖錐（Scripts/Terrain/CropDem.py の確認で y 15 m を越える範囲が
            // X −2120〜−721・Z −1649〜−349）
            {-2150.0, -900.0, -1650.0, -420.0, 0.25},
            // Franklin Butte の尖塔の台座（ref_cliff_02 で 2.6 km・1 画素 0.25 m、ref_ground_02 で 1.7 km・0.28 m）
            {-760.0, -240.0, -660.0, -140.0, 0.25},
            // Rooster Butte（ref_far_02 で 1 km・0.5 m、ref_ground_02 で 3.5 km・0.58 m）
            {640.0, 1160.0, 560.0, 1160.0, 0.25},
            // Setting Hen Butte と南の小さな崖（ref_far_02 で 0.2〜0.6 km・0.11〜0.3 m）
            {960.0, 1460.0, 1140.0, 1960.0, 0.25},
        };
        // 遠景の castle の段（2 m の格子。Scripts/Terrain/HorizonDem.py）の範囲。段の外周（mid との縫い目の帯）から
        // 押し出しが 0 になる位置までを 40 m 以上空ける
        constexpr CliffRegion kCastleRegions[] = {
            // ref_far_01 の主役のビュート（頂 X −2372・Z −4189、2.7〜3 km・1 画素 0.56〜0.63 m）と左のメサ（3.4 km・0.72 m）
            {-3450.0, -2100.0, -4450.0, -3950.0, 0.5},
            // ref_far_01 の奥の左右の崖（3.3〜5 km・0.7〜1 m）。右の崖は Z −6260 まで続くので、崖の北の端まで押し出しを
            // 弱めずに届かせる
            {-3650.0, -1900.0, -6340.0, -4650.0, 0.5},
            // ref_far_01 の画面の左の奥の崖（5〜9 km・1.1〜2 m）。範囲は、視点から見える急な点（X −6838〜−4242・
            // Z −8473〜−5583）がまとまる 3 か所だけにする（見えない裏側の崖まで細分すると、ジオメトリのページの常駐が
            // 足りなくなる）。3 か所は隣と 100 m 以上重ね、境の押し出しを弱めない。上の範囲とは重ねない（重なると上の範囲の
            // 崖の細分の間隔が変わる）。1 画素が 1.1〜2 m なので、間隔 2 m で細分の下限を 1 にする（緩い升目は 2 m の
            // 格子の点の押し出しだけ。急な升目だけ面に沿って 2 m ごとに割る）
            // 画面の左端から 380 画素までの崖（7〜9 km）
            {-6250.0, -5150.0, -8250.0, -6250.0, 2.0, 1u},
            // 画面の 30〜570 画素の手前の崖（5〜7 km）
            {-5350.0, -4250.0, -7000.0, -5500.0, 2.0, 1u},
            // 画面の左端の奥の崖（9 km）
            {-6875.0, -6150.0, -8625.0, -7875.0, 2.0, 1u},
        };
        constexpr double kRegionFadeMeters = 40.0;
        // 押し出しの向き（面の法線）を求める高さの差分の間隔（m）。1 m の格子の凹凸ではなく数 m の面の向きにする
        constexpr double kNormalSmoothingMeters = 1.5;
        // 押し出す斜面の傾き（面の法線の水平の長さ = 傾斜の sin）。38° から効き始め 52° で満ちる
        constexpr float kSteepSinLow = 0.616f;
        constexpr float kSteepSinHigh = 0.788f;
        // 焼いた頂点の法線を求める差分の間隔（m）
        constexpr double kVertexNormalStepMeters = 0.1;
        // 区画の周りに印を求める升目の余白
        constexpr int kWindowMargin = 3;

        // 以下はシェーダーの地層（Assets/Shaders/Common/TerrainStrata.glsl）と同じ値。層の境の高さをそろえる
        constexpr float kStrataContactHeight = 105.0f;
        constexpr float kStrataContactUndulation = 4.0f;
        constexpr float kStrataWarpAmplitude = 2.5f;
        constexpr float kStrataWarpWavelength = 420.0f;
        constexpr float kStrataLowerBedMeters = 1.2f;
        constexpr float kStrataUpperSetMeters = 7.0f;
        constexpr float kStrataBoundaryJitter = 0.6f;
        constexpr float kStrataBoundaryLateral = 0.3f;
        constexpr float kStrataLateralWavelength = 90.0f;
        constexpr float kStrataPinchWavelength = 70.0f;
        constexpr float kStrataLowerLedgeMeters = 0.35f;
        constexpr float kStrataUpperLedgeMeters = 0.6f;
        constexpr float kStrataPartingMin = 0.4f;
        constexpr float kStrataPartingMax = 1.5f;

        // 形だけの値（シェーダーには無い）
        // 硬い層の面を立てる段の強さ（1 で面が垂直）と、押し出しの上限（m）
        constexpr float kLowerTerraceStrength = 0.8f;
        constexpr float kLowerTerraceLimit = 0.8f;
        constexpr float kUpperTerraceLimit = 1.6f;
        // 柔らかい層・赤い薄層のくぼみ（m）
        constexpr float kLowerSoftRecess = 0.12f;
        constexpr float kPartingRecess = 0.35f;
        // 層の境で段を移す幅の上限（m）。上の単位は棚の幅が広く、急に引っ込めると面が折り返すので広く取る
        constexpr float kLowerBoundaryBlend = 0.25f;
        constexpr float kUpperBoundaryBlend = 0.8f;
        // 縦の割れ目: 上の単位（セットを貫く）と下の単位（層ごとにずれる）の間隔・溝の幅・深さ（m）
        constexpr float kUpperJointSpacing = 7.0f;
        constexpr float kUpperJointWidth = 0.45f;
        constexpr float kUpperJointDepthMin = 0.4f;
        constexpr float kUpperJointDepthMax = 1.2f;
        constexpr float kUpperBlockOffset = 0.3f;
        constexpr float kLowerJointSpacing = 2.5f;
        constexpr float kLowerJointWidth = 0.2f;
        constexpr float kLowerJointDepthMin = 0.1f;
        constexpr float kLowerJointDepthMax = 0.3f;
        // 地層の棚: 上の単位のセットの底の柔らかい帯を奥へ引き、その上の砂岩を張り出させる段。段の高さ（引っ込みと
        // 張り出しの和、m）の範囲・底の帯の厚さ（m）・段の縁の幅（m）・横方向の強さの揺れの波長（m）
        constexpr float kLedgeStepMin = 1.0f;
        constexpr float kLedgeStepMax = 2.4f;
        constexpr float kLedgeRecessShare = 0.55f;
        constexpr float kLedgeBandMin = 0.6f;
        constexpr float kLedgeBandMax = 1.4f;
        constexpr float kLedgeEdgeMeters = 0.15f;
        constexpr float kLedgeContinuityWavelength = 110.0f;
        // 細かな凹凸の振幅（m）
        constexpr float kRoughnessCoarse = 0.12f;
        constexpr float kRoughnessFine = 0.05f;
        // 押し出しの上限（m）
        constexpr float kMaxDisplacement = 2.5f;

        float Mix(float a, float b, float t)
        {
            return a + (b - a) * t;
        }

        float SmoothStep(float edge0, float edge1, float x)
        {
            if (!(edge1 > edge0))
            {
                return x >= edge1 ? 1.0f : 0.0f;
            }
            const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }

        // シェーダーの TerrainLatticeHash と同じ（0〜1）
        float LatticeHash(int x, int y)
        {
            const uint32_t qx = static_cast<uint32_t>(x) * 1597334673u;
            const uint32_t qy = static_cast<uint32_t>(y) * 3812015801u;
            uint32_t n = (qx ^ qy) * 1597334673u;
            n ^= n >> 15u;
            return static_cast<float>(n >> 8u) * (1.0f / 16777216.0f);
        }

        // シェーダーの TerrainValueNoise と同じ滑らかな値ノイズ（0〜1）
        float ValueNoise(float px, float py)
        {
            const float ox = std::floor(px);
            const float oy = std::floor(py);
            const float fx = px - ox;
            const float fy = py - oy;
            const float ux = fx * fx * (3.0f - 2.0f * fx);
            const float uy = fy * fy * (3.0f - 2.0f * fy);
            const int cx = static_cast<int>(ox);
            const int cy = static_cast<int>(oy);
            const float a = LatticeHash(cx, cy);
            const float b = LatticeHash(cx + 1, cy);
            const float d = LatticeHash(cx, cy + 1);
            const float e = LatticeHash(cx + 1, cy + 1);
            return Mix(Mix(a, b, ux), Mix(d, e, ux), uy);
        }

        // 3 次元の値ノイズ（0〜1）。細かな凹凸に使う（シェーダーには無い）
        float LatticeHash3(int x, int y, int z)
        {
            const uint32_t folded = static_cast<uint32_t>(y) * 2654435761u ^ static_cast<uint32_t>(z) * 2246822519u;
            return LatticeHash(x, static_cast<int>(folded));
        }

        float ValueNoise3(float px, float py, float pz)
        {
            const float ox = std::floor(px);
            const float oy = std::floor(py);
            const float oz = std::floor(pz);
            const float fx = px - ox;
            const float fy = py - oy;
            const float fz = pz - oz;
            const float ux = fx * fx * (3.0f - 2.0f * fx);
            const float uy = fy * fy * (3.0f - 2.0f * fy);
            const float uz = fz * fz * (3.0f - 2.0f * fz);
            const int cx = static_cast<int>(ox);
            const int cy = static_cast<int>(oy);
            const int cz = static_cast<int>(oz);
            float layers[2];
            for (int k = 0; k < 2; ++k)
            {
                const float a = LatticeHash3(cx, cy, cz + k);
                const float b = LatticeHash3(cx + 1, cy, cz + k);
                const float d = LatticeHash3(cx, cy + 1, cz + k);
                const float e = LatticeHash3(cx + 1, cy + 1, cz + k);
                layers[k] = Mix(Mix(a, b, ux), Mix(d, e, ux), uy);
            }
            return Mix(layers[0], layers[1], uz);
        }

        // 1 つの層の範囲（基準面からの m）と番号。シェーダーの TerrainStrataBed と同じ
        struct StrataBed
        {
            float Bottom = 0.0f;
            float Top = 0.0f;
            int Index = 0;
        };

        // シェーダーの TerrainStrataBoundary と同じ（層の平均の厚さを 1 とした単位）
        float StrataBoundary(int index, float x, float z, int seed)
        {
            const float jitter = (LatticeHash(index, seed) - 0.5f) * kStrataBoundaryJitter;
            const float px = x * (1.0f / kStrataLateralWavelength) + static_cast<float>(index) * 7.131f;
            const float pz = z * (1.0f / kStrataLateralWavelength) + static_cast<float>(seed) * 3.173f;
            const float lateral = (ValueNoise(px, pz) - 0.5f) * kStrataBoundaryLateral;
            return static_cast<float>(index) + jitter + lateral;
        }

        // 番号 index の層の範囲
        StrataBed StrataBedByIndex(int index, float x, float z, float origin, float thickness, int seed)
        {
            StrataBed bed;
            bed.Index = index;
            bed.Bottom = origin + StrataBoundary(index, x, z, seed) * thickness;
            bed.Top = origin + StrataBoundary(index + 1, x, z, seed) * thickness;
            return bed;
        }

        // シェーダーの FindTerrainStrataBed と同じ（高さ height を含む層）
        StrataBed FindStrataBed(float height, float x, float z, float origin, float thickness, int seed)
        {
            const float u = (height - origin) / thickness;
            const int i = static_cast<int>(std::floor(u + 0.5f));
            const float b = StrataBoundary(i, x, z, seed);
            StrataBed bed;
            if (u >= b)
            {
                bed.Index = i;
                bed.Bottom = b;
                bed.Top = StrataBoundary(i + 1, x, z, seed);
            }
            else
            {
                bed.Index = i - 1;
                bed.Bottom = StrataBoundary(i - 1, x, z, seed);
                bed.Top = b;
            }
            bed.Bottom = origin + bed.Bottom * thickness;
            bed.Top = origin + bed.Top * thickness;
            return bed;
        }

        // シェーダーの TerrainStrataPresence と同じ（層ごとの横方向の有無 0〜1）
        float StrataPresence(int index, float x, float z, int seed)
        {
            const float px = x * (1.0f / kStrataPinchWavelength) + static_cast<float>(index) * 3.917f;
            const float pz = z * (1.0f / kStrataPinchWavelength) + static_cast<float>(seed) * 11.37f;
            return SmoothStep(0.32f, 0.55f, ValueNoise(px, pz));
        }

        // 押し出しを求める点の、高さ以外の値
        struct DisplacementPoint
        {
            float X = 0.0f;
            float Z = 0.0f;
            // 層の中で高さ 1 m あたりに足す押し出し（面を垂直に立てる量。法線の y ÷ 水平の長さの 2 乗）
            float TerraceSlope = 0.0f;
            // 面の外向きの水平の単位ベクトル
            float FaceX = 0.0f;
            float FaceZ = 0.0f;
            // 垂直に近い面か（0〜1。割れ目を入れる）
            float Cliff = 0.0f;
        };

        // 下の単位の層 bed の、高さ y での押し出し（m）。硬い層は張り出し、面が立ち、柔らかい層はくぼむ
        float LowerBedOffset(const StrataBed &bed, float y, const DisplacementPoint &point)
        {
            const float thickness = bed.Top - bed.Bottom;
            const float hardness = SmoothStep(0.45f, 0.75f, LatticeHash(bed.Index, 101)) *
                                   StrataPresence(bed.Index, point.X, point.Z, 1);
            // 崩れた縁: 段の張り出しと面の立ち方を、横方向に数 m の塊で弱める
            const float integrity =
                Mix(0.3f, 1.0f,
                    SmoothStep(0.3f, 0.6f,
                               ValueNoise(point.X * (1.0f / 3.5f) + static_cast<float>(bed.Index) * 1.713f,
                                          point.Z * (1.0f / 3.5f) - static_cast<float>(bed.Index) * 2.297f)));
            const float edge = std::max(std::min(thickness * 0.3f, 0.3f), 0.15f);
            // シェーダーの TerrainStrataLedgeSlope が法線を倒す張り出し d(y)（その傾きがシェーダーの値）
            const float ledge = kStrataLowerLedgeMeters * hardness *
                                (SmoothStep(bed.Bottom, std::min(bed.Bottom + edge, bed.Top), y) -
                                 SmoothStep(std::max(bed.Top - edge * 2.0f, bed.Bottom), bed.Top, y));
            const float terrace =
                std::clamp(point.TerraceSlope * kLowerTerraceStrength * hardness * ((y - bed.Bottom) - 0.5f * thickness),
                           -kLowerTerraceLimit, kLowerTerraceLimit);
            return (ledge + terrace) * integrity - kLowerSoftRecess * (1.0f - hardness);
        }

        // 上の単位のセット bed の、高さ y での押し出し（m）。セットの底の赤い薄層はくぼみ、その上の砂岩は面が立って張り出す
        float UpperSetOffset(const StrataBed &set, float y, const DisplacementPoint &point)
        {
            const float partingPresence = StrataPresence(set.Index, point.X, point.Z, 2) *
                                          (LatticeHash(set.Index, 307) >= 0.5f ? 1.0f : 0.0f);
            const float parting =
                Mix(kStrataPartingMin, kStrataPartingMax, LatticeHash(set.Index, 311)) * partingPresence;
            const float sandBottom = set.Bottom + parting;
            const float thickness = set.Top - sandBottom;
            const float hardness = SmoothStep(0.35f, 0.9f, LatticeHash(set.Index, 503)) *
                                   Mix(0.2f, 1.0f, StrataPresence(set.Index, point.X, point.Z, 3));
            const float integrity =
                Mix(0.35f, 1.0f,
                    SmoothStep(0.3f, 0.6f,
                               ValueNoise(point.X * (1.0f / 6.0f) + static_cast<float>(set.Index) * 2.131f,
                                          point.Z * (1.0f / 6.0f) - static_cast<float>(set.Index) * 1.377f)));
            const float edge = std::max(std::min(thickness * 0.2f, 1.0f), 0.25f);
            const float ledge = kStrataUpperLedgeMeters * hardness *
                                (SmoothStep(sandBottom, std::min(sandBottom + edge, set.Top), y) -
                                 SmoothStep(std::max(set.Top - edge * 2.0f, sandBottom), set.Top, y));
            const float above = std::max(y, sandBottom);
            const float terrace = std::clamp(point.TerraceSlope * Mix(0.45f, 1.0f, hardness) *
                                                 ((above - sandBottom) - 0.5f * thickness),
                                             -kUpperTerraceLimit, kUpperTerraceLimit);
            const float partingCut =
                -kPartingRecess * partingPresence * (1.0f - SmoothStep(sandBottom - 0.1f, sandBottom + 0.2f, y));
            return (ledge + terrace) * integrity + partingCut;
        }

        // 地層の棚の段の高さ（m）。セットごとの強さに、横方向に長い波長でゆっくり変わる強さを掛ける（数 m の塊で
        // 途切れさせず、棚を水平に続ける）
        float LedgeStepHeight(int index, float x, float z)
        {
            const float continuity =
                Mix(0.55f, 1.0f,
                    SmoothStep(0.25f, 0.6f,
                               ValueNoise(x * (1.0f / kLedgeContinuityWavelength) + static_cast<float>(index) * 1.371f,
                                          z * (1.0f / kLedgeContinuityWavelength) - static_cast<float>(index) * 0.913f)));
            return Mix(kLedgeStepMin, kLedgeStepMax, LatticeHash(index, 709)) * continuity;
        }

        // 上の単位のセット set の、高さ y での棚の押し出し（m）。底の帯は奥へ引き、帯の上の砂岩は張り出す。帯の上の縁
        // （張り出した砂岩の下面）とセットの境（下のセットの砂岩の踏み面）が水平の段になる。境では下のセットの張り出しと
        // 上のセットの引っ込みを縁の幅で移す（境のどちらの側から求めても同じ値）
        float UpperLedgeOffset(const StrataBed &set, float y, float x, float z)
        {
            const auto recess = [&](int index) { return -kLedgeRecessShare * LedgeStepHeight(index, x, z); };
            const auto protrude = [&](int index) { return (1.0f - kLedgeRecessShare) * LedgeStepHeight(index, x, z); };
            const float band = Mix(kLedgeBandMin, kLedgeBandMax, LatticeHash(set.Index, 701));
            const float bandTop = std::min(set.Bottom + band, 0.5f * (set.Bottom + set.Top));
            float value = Mix(recess(set.Index), protrude(set.Index),
                              SmoothStep(bandTop - kLedgeEdgeMeters, bandTop + kLedgeEdgeMeters, y));
            if (y < set.Bottom + kLedgeEdgeMeters)
            {
                value = Mix(protrude(set.Index - 1), value,
                            SmoothStep(set.Bottom - kLedgeEdgeMeters, set.Bottom + kLedgeEdgeMeters, y));
            }
            else if (y > set.Top - kLedgeEdgeMeters)
            {
                value = Mix(value, recess(set.Index + 1),
                            SmoothStep(set.Top - kLedgeEdgeMeters, set.Top + kLedgeEdgeMeters, y));
            }
            return value;
        }

        // 単位の層の押し出しを、層の境で隣の層へ滑らかに移す（境の上下の幅 blend の中で混ぜる。境のどちらの側から
        // 求めても同じ値になる）
        template <typename Profile>
        float BlendedStrataOffset(float y, float origin, float thickness, int seed, float maxBlend,
                                  const DisplacementPoint &point, Profile profile)
        {
            const StrataBed bed = FindStrataBed(y, point.X, point.Z, origin, thickness, seed);
            const float bedThickness = bed.Top - bed.Bottom;
            const float value = profile(bed, y, point);
            if (y < bed.Bottom + maxBlend)
            {
                const StrataBed below = StrataBedByIndex(bed.Index - 1, point.X, point.Z, origin, thickness, seed);
                const float blend = std::min(maxBlend, 0.25f * std::min(bedThickness, below.Top - below.Bottom));
                if (y < bed.Bottom + blend)
                {
                    return Mix(profile(below, y, point), value,
                               SmoothStep(bed.Bottom - blend, bed.Bottom + blend, y));
                }
            }
            if (y > bed.Top - maxBlend)
            {
                const StrataBed above = StrataBedByIndex(bed.Index + 1, point.X, point.Z, origin, thickness, seed);
                const float blend = std::min(maxBlend, 0.25f * std::min(bedThickness, above.Top - above.Bottom));
                if (y > bed.Top - blend)
                {
                    return Mix(value, profile(above, y, point), SmoothStep(bed.Top - blend, bed.Top + blend, y));
                }
            }
            return value;
        }

        // 縦の割れ目: 水平面の Voronoi の境（間隔 spacing m）。溝の形（0〜1）・割れ目ごとの深さの擬似乱数（0〜1）と、
        // 割れ目で区切られた塊の出入り（−0.5〜0.5）
        struct JointSample
        {
            float Groove = 0.0f;
            float Depth = 0.0f;
            float Block = 0.0f;
        };

        JointSample SampleJoints(float x, float z, float spacing, int seed, float width, const DisplacementPoint &point)
        {
            const float px = x / spacing;
            const float pz = z / spacing;
            const int ix = static_cast<int>(std::floor(px));
            const int iz = static_cast<int>(std::floor(pz));
            float best1 = std::numeric_limits<float>::max();
            float best2 = std::numeric_limits<float>::max();
            int cell1[2] = {0, 0};
            int cell2[2] = {0, 0};
            float feature1[2] = {0.0f, 0.0f};
            float feature2[2] = {0.0f, 0.0f};
            for (int oz = -1; oz <= 1; ++oz)
            {
                for (int ox = -1; ox <= 1; ++ox)
                {
                    const int cx = ix + ox;
                    const int cz = iz + oz;
                    const float fx = static_cast<float>(cx) + 0.1f + 0.8f * LatticeHash(cx + seed * 1013, cz);
                    const float fz = static_cast<float>(cz) + 0.1f + 0.8f * LatticeHash(cx, cz + seed * 2029 + 17);
                    const float distance = (fx - px) * (fx - px) + (fz - pz) * (fz - pz);
                    if (distance < best1)
                    {
                        best2 = best1;
                        cell2[0] = cell1[0];
                        cell2[1] = cell1[1];
                        feature2[0] = feature1[0];
                        feature2[1] = feature1[1];
                        best1 = distance;
                        cell1[0] = cx;
                        cell1[1] = cz;
                        feature1[0] = fx;
                        feature1[1] = fz;
                    }
                    else if (distance < best2)
                    {
                        best2 = distance;
                        cell2[0] = cx;
                        cell2[1] = cz;
                        feature2[0] = fx;
                        feature2[1] = fz;
                    }
                }
            }
            // 最も近い 2 点の垂直二等分線（割れ目）までの距離（m）と、割れ目の面の向き
            float normalX = feature2[0] - feature1[0];
            float normalZ = feature2[1] - feature1[1];
            const float normalLength = std::sqrt(normalX * normalX + normalZ * normalZ);
            JointSample result;
            if (normalLength < 1.0e-5f)
            {
                return result;
            }
            normalX /= normalLength;
            normalZ /= normalLength;
            const float midX = 0.5f * (feature1[0] + feature2[0]);
            const float midZ = 0.5f * (feature1[1] + feature2[1]);
            const float edgeDistance = -((px - midX) * normalX + (pz - midZ) * normalZ) * spacing;
            // 割れ目の面が崖の面と平行に近いと、面に沿った広い帯になるので、崖の面を横切る割れ目だけを残す
            const float facing = std::abs(normalX * point.FaceX + normalZ * point.FaceZ);
            const float across = 1.0f - SmoothStep(0.55f, 0.85f, facing);
            const float profile = 1.0f - SmoothStep(0.0f, width, std::max(edgeDistance, 0.0f));
            result.Groove = profile * across;
            result.Depth = LatticeHash(cell1[0] + cell2[0] + seed * 31, cell1[1] + cell2[1]);
            // 塊の出入りは割れ目の上で 0 にして、溝の両側で段を作らない
            result.Block = (LatticeHash(cell1[0] + seed * 53, cell1[1] - seed * 97) - 0.5f) *
                           SmoothStep(0.0f, width * 1.5f, std::max(edgeDistance, 0.0f));
            return result;
        }

        float BattleshipCarveAmount(double x, double z);

        // 地層の棚を掛けない範囲か。Battleship Rock と Franklin Butte の尖塔の台座（kNearRegions[0]・[1]）はスキャンの崖の
        // 担当で、棚を足さない（範囲の縁は押し出しが 0 なので、矩形で切っても面は続く）
        bool IsScanOwnedRegion(float x, float z)
        {
            for (int index = 0; index < 2; ++index)
            {
                const CliffRegion &region = kNearRegions[index];
                if (x >= region.MinX && x <= region.MaxX && z >= region.MinZ && z <= region.MaxZ)
                {
                    return true;
                }
            }
            return false;
        }

        // 面の法線 (nx, ny, nz) の点 (x, y, z) を外へ押し出す量（m）。y は基準面からの高さ
        float CliffDisplacement(float x, float y, float z, float nx, float ny, float nz)
        {
            const float horizontal = std::sqrt(nx * nx + nz * nz);
            if (horizontal < 1.0e-3f)
            {
                return 0.0f;
            }
            DisplacementPoint point;
            point.X = x;
            point.Z = z;
            point.FaceX = nx / horizontal;
            point.FaceZ = nz / horizontal;
            point.TerraceSlope = std::min(ny / std::max(horizontal * horizontal, 1.0e-3f), 1.0f) *
                                 SmoothStep(0.70f, 0.87f, horizontal);
            point.Cliff = SmoothStep(0.75f, 0.92f, horizontal);

            // シェーダーと同じ地層全体のうねりと、単位の境の高さ
            const float warp = (ValueNoise(x * (1.0f / kStrataWarpWavelength) + 3.71f,
                                           z * (1.0f / kStrataWarpWavelength) - 8.13f) *
                                    2.0f -
                                1.0f) *
                               kStrataWarpAmplitude;
            const float strataY = y + warp;
            const float contact =
                kStrataContactHeight +
                (ValueNoise(x * (1.0f / 160.0f) - 19.3f, z * (1.0f / 160.0f) + 27.1f) * 2.0f - 1.0f) *
                    kStrataContactUndulation;
            const float unitBlend = SmoothStep(contact - 0.4f, contact + 0.4f, strataY);

            float displacement = 0.0f;
            int lowerBedIndex = 0;
            if (unitBlend < 1.0f)
            {
                const float lower = BlendedStrataOffset(strataY, contact, kStrataLowerBedMeters, 1, kLowerBoundaryBlend,
                                                        point, LowerBedOffset);
                displacement += lower * (1.0f - unitBlend);
                lowerBedIndex = FindStrataBed(strataY, x, z, contact, kStrataLowerBedMeters, 1).Index;
            }
            if (unitBlend > 0.0f)
            {
                const float upper = BlendedStrataOffset(strataY, contact, kStrataUpperSetMeters, 2, kUpperBoundaryBlend,
                                                        point, UpperSetOffset);
                displacement += upper * unitBlend;
                // 地層の棚（急な面だけ。緩い斜面は上の段の押し出しで棚になる。スキャンの崖の担当の範囲には掛けない）
                if (!IsScanOwnedRegion(x, z))
                {
                    const StrataBed set = FindStrataBed(strataY, x, z, contact, kStrataUpperSetMeters, 2);
                    displacement +=
                        UpperLedgeOffset(set, strataY, x, z) * unitBlend * SmoothStep(0.70f, 0.87f, horizontal);
                }
            }

            // 縦の割れ目（垂直に近い面だけ）。上の単位はセットを貫く大きな割れ目と塊、下の単位は層ごとにずれる細い割れ目。
            // Battleship Rock の刻んだ壁では割れ目が薄い板の櫛に見えるので掛けない
            const float jointScale = 1.0f - BattleshipCarveAmount(x, z);
            if (point.Cliff > 0.0f && jointScale > 0.0f)
            {
                const float presence =
                    jointScale *
                    Mix(0.35f, 1.0f, SmoothStep(0.3f, 0.65f, ValueNoise(x * (1.0f / 30.0f) + 5.3f, z * (1.0f / 30.0f) - 2.1f)));
                if (unitBlend > 0.0f)
                {
                    const JointSample joint = SampleJoints(x, z, kUpperJointSpacing, 1, kUpperJointWidth, point);
                    const float depth = Mix(kUpperJointDepthMin, kUpperJointDepthMax, joint.Depth);
                    displacement += unitBlend * point.Cliff * presence *
                                    (-depth * joint.Groove + kUpperBlockOffset * joint.Block);
                }
                if (unitBlend < 1.0f)
                {
                    const JointSample joint =
                        SampleJoints(x, z, kLowerJointSpacing, 100 + lowerBedIndex, kLowerJointWidth, point);
                    const float depth = Mix(kLowerJointDepthMin, kLowerJointDepthMax, joint.Depth);
                    displacement -= (1.0f - unitBlend) * point.Cliff * presence * depth * joint.Groove;
                }
            }

            // 細かな凹凸（風化した岩の面のうねり）
            displacement += (ValueNoise3(x * (1.0f / 1.6f), strataY * (1.0f / 1.6f), z * (1.0f / 1.6f)) - 0.5f) *
                                (2.0f * kRoughnessCoarse) +
                            (ValueNoise3(x * (1.0f / 0.6f) + 13.1f, strataY * (1.0f / 0.6f), z * (1.0f / 0.6f)) - 0.5f) *
                                (2.0f * kRoughnessFine);
            return std::clamp(displacement, -kMaxDisplacement, kMaxDisplacement);
        }

        // Battleship Rock の刻み（尾根の刻みと崖錐の棚の段）が届く範囲（m。折れ線の周りの外接の箱）
        struct CarveBounds
        {
            double MinX = std::numeric_limits<double>::max();
            double MaxX = std::numeric_limits<double>::lowest();
            double MinZ = std::numeric_limits<double>::max();
            double MaxZ = std::numeric_limits<double>::lowest();
        };

        // 尾根の刻みと棚の段を掛けるか。刻んだ範囲では縦の割れ目と滑らかな面の行き過ぎを止め、層の縞だけを残す
        // （WastelandCliffScan.md の「尾根を地形の焼き込みで段に刻む試し」）
        constexpr bool kBattleshipCarveEnabled = true;
        constexpr bool kBattleshipShelfEnabled = true;
        // 刻んだ範囲の外へ割れ目と滑らかな面を戻す幅（m）と、その割合を引く升目の一辺（m）
        constexpr double kBattleshipCarveFadeMeters = 8.0;
        constexpr double kBattleshipCarveMaskCell = 2.0;
        // 尾根の刻みの割合を 1 にする範囲を、刻みの届く半径（と壁の前の段）よりこの幅（m）だけ外へ広げる。刻みの縁
        // （尾根の点の壁・奥の打ち切り・棚の届く端）が割合の 1 の境に重なると、割合の升目の 2 m の双線形で縁の内外に割合が揺れ、
        // 混ぜ方の境と刻みの段差が同じ所に出るので、縁を割合 1 の内側に置く
        constexpr double kBattleshipCarveMaskMargin = 4.0;
        // 尾根の刻みの中心を列の順に並べた折れ線で、前後に均す刻みの数（中心の前後の揺れが丸い柱の列にならないように）
        constexpr int kBattleshipRidgeSmooth = 4;
        // 隣の刻みの中心がこれより離れていれば（m）、別の尾根として折れ線を切る
        constexpr double kBattleshipRidgeBreak = 8.0;
        // 棚の段: 棚の点の高さから上へこの高さ（m）までの斜面を棚の高さへ削り、平らな踏み面と縦の蹴上げにする。
        // 棚の線からこの水平の距離（m）の内だけ削る
        constexpr float kBattleshipShelfRiser = 2.5f;
        constexpr double kBattleshipShelfReach = 10.0;
        // 折れ線の線分を引く升目の一辺（m）
        constexpr double kCarveIndexCell = 4.0;
        // 上げた壁の前の縁に置く段: 尾根の点から手前へこの奥行き（m）ごとに 1 段下げ、kBattleshipWallSteps 段で元の地面へ
        // 下ろす（壁の面に横に割れた層の段を作る）。段の縁は kBattleshipWallJitter（m）だけ揺らして直線の筋にしない。
        // 段は見上げる視点で上端の角を 10〜15 画素の弧に丸め、揺らしは壁の面の縦の溝になるので、どちらも 0 にして
        // 尾根の点で切り立つ壁にする（WastelandCliffScan.md の「尾根の点の距離を均し、…」）
        constexpr double kBattleshipWallTread = 2.0;
        constexpr int kBattleshipWallSteps = 0;
        constexpr double kBattleshipWallJitter = 0.0;
        // 下げる列は、尾根の点から手前へこの奥行き（m）の内でも、写真の外形の行を通る視線より上の地形を視線まで削る
        // （尾根の点のすぐ手前の高い地面が下げた尾根の上に出て、写真の斜めの壁を丸い弧にしないように）
        constexpr double kBattleshipLowerFront = 12.0;

        // 刻みの折れ線の点。尾根の刻みは目標の高さと向き（1 は上げる、-1 は下げる）、棚の段は棚の高さと効きの割合
        struct CarveNode
        {
            double X;
            double Z;
            float Height;
            float Value;
            uint32_t Group;
        };

        // 折れ線と、升目ごとの、問い合わせの半径の内に掛かる線分（始点の番号）の一覧
        struct CarvePolyline
        {
            VariableArray<CarveNode> Nodes;
            double MinX = 0.0;
            double MinZ = 0.0;
            int Columns = 0;
            int Rows = 0;
            VariableArray<uint32_t> CellStart;
            VariableArray<uint32_t> Segments;
        };

        // 点 (x, z) に最も近い線分と、線分に沿った位置（0〜1）と水平の距離（m）
        struct CarveHit
        {
            uint32_t Segment = 0;
            double U = 0.0;
            double Distance = std::numeric_limits<double>::max();
        };

        double SegmentDistance(const CarveNode &a, const CarveNode &b, double x, double z, double &outU)
        {
            const double ex = b.X - a.X;
            const double ez = b.Z - a.Z;
            const double length = ex * ex + ez * ez;
            outU = length > 0.0 ? std::clamp(((x - a.X) * ex + (z - a.Z) * ez) / length, 0.0, 1.0) : 0.0;
            const double dx = a.X + ex * outU - x;
            const double dz = a.Z + ez * outU - z;
            return std::sqrt(dx * dx + dz * dz);
        }

        // 同じ並びの隣り合う点を線分にし、半径 reach の問い合わせに掛かる線分を升目ごとに引いておく
        void BuildCarveIndex(CarvePolyline &polyline, double reach)
        {
            if (polyline.Nodes.size() < 2)
            {
                return;
            }
            double minX = std::numeric_limits<double>::max();
            double maxX = std::numeric_limits<double>::lowest();
            double minZ = std::numeric_limits<double>::max();
            double maxZ = std::numeric_limits<double>::lowest();
            for (const CarveNode &node : polyline.Nodes)
            {
                minX = std::min(minX, node.X);
                maxX = std::max(maxX, node.X);
                minZ = std::min(minZ, node.Z);
                maxZ = std::max(maxZ, node.Z);
            }
            polyline.MinX = minX - reach;
            polyline.MinZ = minZ - reach;
            polyline.Columns = static_cast<int>(std::ceil((maxX + reach - polyline.MinX) / kCarveIndexCell)) + 1;
            polyline.Rows = static_cast<int>(std::ceil((maxZ + reach - polyline.MinZ) / kCarveIndexCell)) + 1;
            const double cellReach = reach + kCarveIndexCell * 0.7072;
            polyline.CellStart.assign(static_cast<size_t>(polyline.Columns) * polyline.Rows + 1, 0u);
            for (int row = 0; row < polyline.Rows; ++row)
            {
                for (int column = 0; column < polyline.Columns; ++column)
                {
                    const double x = polyline.MinX + (column + 0.5) * kCarveIndexCell;
                    const double z = polyline.MinZ + (row + 0.5) * kCarveIndexCell;
                    for (size_t index = 0; index + 1 < polyline.Nodes.size(); ++index)
                    {
                        const CarveNode &a = polyline.Nodes[index];
                        const CarveNode &b = polyline.Nodes[index + 1];
                        double u = 0.0;
                        if (a.Group == b.Group && SegmentDistance(a, b, x, z, u) <= cellReach)
                        {
                            polyline.Segments.push_back(static_cast<uint32_t>(index));
                        }
                    }
                    polyline.CellStart[static_cast<size_t>(row) * polyline.Columns + column + 1] =
                        static_cast<uint32_t>(polyline.Segments.size());
                }
            }
        }

        // 点 (x, z) から reach の内で最も近い線分。無ければ false
        bool NearestCarveSegment(const CarvePolyline &polyline, double x, double z, double reach, CarveHit &outHit)
        {
            if (polyline.Columns == 0)
            {
                return false;
            }
            const int column = static_cast<int>(std::floor((x - polyline.MinX) / kCarveIndexCell));
            const int row = static_cast<int>(std::floor((z - polyline.MinZ) / kCarveIndexCell));
            if (column < 0 || row < 0 || column >= polyline.Columns || row >= polyline.Rows)
            {
                return false;
            }
            const size_t cell = static_cast<size_t>(row) * polyline.Columns + column;
            outHit = CarveHit{};
            for (uint32_t item = polyline.CellStart[cell]; item < polyline.CellStart[cell + 1]; ++item)
            {
                const uint32_t segment = polyline.Segments[item];
                double u = 0.0;
                const double distance =
                    SegmentDistance(polyline.Nodes[segment], polyline.Nodes[segment + 1], x, z, u);
                if (distance < outHit.Distance)
                {
                    outHit.Segment = segment;
                    outHit.U = u;
                    outHit.Distance = distance;
                }
            }
            return outHit.Distance <= reach;
        }

        // 尾根の刻みの折れ線（TerrainCliffCarveBattleship.h の中心を列の順に並べ、跳びで並びを切り、並びの内で前後 kBattleshipRidgeSmooth 個で均す。
        // 目標の高さと向きは刻みのまま）
        const CarvePolyline &BattleshipRidge()
        {
            static const CarvePolyline ridge = []
            {
                CarvePolyline result;
                if (!kBattleshipCarveEnabled)
                {
                    return result;
                }
                const int count = static_cast<int>(sizeof(kBattleshipCarveStamps) / sizeof(kBattleshipCarveStamps[0]));
                // 隣の中心から kBattleshipRidgeBreak より離れた刻み（奥の別の尾根）で並びを切る
                VariableArray<uint32_t> groups;
                groups.assign(static_cast<size_t>(count), 0u);
                for (int index = 1; index < count; ++index)
                {
                    const double dx = kBattleshipCarveStamps[index].CenterX - kBattleshipCarveStamps[index - 1].CenterX;
                    const double dz = kBattleshipCarveStamps[index].CenterZ - kBattleshipCarveStamps[index - 1].CenterZ;
                    groups[index] = groups[index - 1] + (dx * dx + dz * dz > kBattleshipRidgeBreak * kBattleshipRidgeBreak ? 1u : 0u);
                }
                for (int index = 0; index < count; ++index)
                {
                    double x = 0.0;
                    double z = 0.0;
                    int used = 0;
                    for (int other = std::max(0, index - kBattleshipRidgeSmooth);
                         other <= std::min(count - 1, index + kBattleshipRidgeSmooth); ++other)
                    {
                        if (groups[other] != groups[index])
                        {
                            continue;
                        }
                        x += kBattleshipCarveStamps[other].CenterX;
                        z += kBattleshipCarveStamps[other].CenterZ;
                        ++used;
                    }
                    const TerrainCliffCarveStamp &stamp = kBattleshipCarveStamps[index];
                    result.Nodes.push_back(
                        CarveNode{x / used, z / used, stamp.Height, static_cast<float>(stamp.Direction), groups[index]});
                }
                BuildCarveIndex(result, kBattleshipCarveRadius + kBattleshipCarveFadeMeters);
                return result;
            }();
            return ridge;
        }

        // 尾根の刻みの列の表（視点からの方位の順。方位で挟む隣の列を補間する）
        struct CarveColumn
        {
            double Azimuth;
            double CrestDistance;
            float Height;
            int Direction;
            int Column;
        };

        const VariableArray<CarveColumn> &BattleshipRidgeColumns()
        {
            static const VariableArray<CarveColumn> columns = []
            {
                VariableArray<CarveColumn> result;
                if (!kBattleshipCarveEnabled)
                {
                    return result;
                }
                for (const TerrainCliffCarveStamp &stamp : kBattleshipCarveStamps)
                {
                    const double dx = stamp.CrestX - kBattleshipCarveOriginX;
                    const double dz = stamp.CrestZ - kBattleshipCarveOriginZ;
                    result.push_back(CarveColumn{std::atan2(dz, dx), std::sqrt(dx * dx + dz * dz), stamp.Height, stamp.Direction,
                                                 stamp.Column});
                }
                std::sort(result.begin(), result.end(),
                          [](const CarveColumn &a, const CarveColumn &b) { return a.Azimuth < b.Azimuth; });
                return result;
            }();
            return columns;
        }

        // 崖錐の棚の段の折れ線（TerrainCliffShelfBattleship.h の並びごと）
        const CarvePolyline &BattleshipShelves()
        {
            static const CarvePolyline shelves = []
            {
                CarvePolyline result;
                if (!kBattleshipShelfEnabled)
                {
                    return result;
                }
                for (const TerrainCliffShelfPoint &point : kBattleshipShelfPoints)
                {
                    result.Nodes.push_back(CarveNode{point.X, point.Z, point.Height, point.Weight, point.Group});
                }
                BuildCarveIndex(result, kBattleshipShelfReach + kBattleshipCarveFadeMeters);
                return result;
            }();
            return shelves;
        }

        const CarveBounds &BattleshipCarveBounds()
        {
            static const CarveBounds bounds = []
            {
                CarveBounds result;
                const auto include = [&result](const CarvePolyline &polyline, double reach)
                {
                    for (const CarveNode &node : polyline.Nodes)
                    {
                        result.MinX = std::min(result.MinX, node.X - reach);
                        result.MaxX = std::max(result.MaxX, node.X + reach);
                        result.MinZ = std::min(result.MinZ, node.Z - reach);
                        result.MaxZ = std::max(result.MaxZ, node.Z + reach);
                    }
                };
                include(BattleshipRidge(), kBattleshipCarveRadius + std::max(kBattleshipWallTread * kBattleshipWallSteps, kBattleshipLowerFront) +
                                               kBattleshipWallJitter);
                include(BattleshipShelves(), kBattleshipShelfReach + kBattleshipCarveMaskMargin);
                return result;
            }();
            return bounds;
        }

        // 刻んだ後の高さ。尾根の刻み: 視点からの方位で点を挟む隣の列（列の間隔が kBattleshipCarveColumnStep 以下）があり、
        // 点が尾根の点から奥へ 2 × 半径の帯の内なら、両方の列の刻みの結果（目標の高さまで上げるか下げる）を方位に沿って補間する
        // （列の外へ丸い張り出しを出さない）。上げる列は尾根の点の手前に kBattleshipWallSteps 段の層の段を置き、下げる列は手前
        // kBattleshipLowerFront の内を写真の外形の行を通る視線まで削る。棚の段: 最も近い
        // 棚の線分が kBattleshipShelfReach の内にあり、高さが棚の高さから蹴上げの高さまでの間なら、効きの割合だけ棚の高さへ削る
        float CarveBattleshipHeight(double x, double z, float height)
        {
            const CarveBounds &bounds = BattleshipCarveBounds();
            if (x < bounds.MinX || x > bounds.MaxX || z < bounds.MinZ || z > bounds.MaxZ)
            {
                return height;
            }
            const VariableArray<CarveColumn> &columns = BattleshipRidgeColumns();
            if (columns.size() >= 2)
            {
                const double dx = x - kBattleshipCarveOriginX;
                const double dz = z - kBattleshipCarveOriginZ;
                const double azimuth = std::atan2(dz, dx);
                const double distance = std::sqrt(dx * dx + dz * dz);
                const auto upper = std::upper_bound(columns.begin(), columns.end(), azimuth,
                                                    [](double value, const CarveColumn &column) { return value < column.Azimuth; });
                if (upper != columns.begin() && upper != columns.end())
                {
                    const CarveColumn &a = *(upper - 1);
                    const CarveColumn &b = *upper;
                    if (std::abs(a.Column - b.Column) <= kBattleshipCarveColumnStep && b.Azimuth > a.Azimuth)
                    {
                        const double u = (azimuth - a.Azimuth) / (b.Azimuth - a.Azimuth);
                        const double crest = a.CrestDistance + (b.CrestDistance - a.CrestDistance) * u;
                        const double wallDepth = kBattleshipWallTread * kBattleshipWallSteps;
                        const double jitter =
                            (ValueNoise3(static_cast<float>(x * 0.21), 7.3f, static_cast<float>(z * 0.21)) - 0.5f) * (2.0 * kBattleshipWallJitter);
                        const double front = crest - distance + jitter;
                        if (front <= std::max(wallDepth, kBattleshipLowerFront) && distance <= crest + 2.0 * kBattleshipCarveRadius)
                        {
                            const auto apply = [height, front, distance, wallDepth](const CarveColumn &column)
                            {
                                if (column.Direction > 0)
                                {
                                    if (front > wallDepth)
                                    {
                                        return height;
                                    }
                                    if (front <= 0.0)
                                    {
                                        return std::max(height, column.Height);
                                    }
                                    const int step = static_cast<int>(std::ceil(front / kBattleshipWallTread));
                                    const float rise = std::max(0.0f, column.Height - height);
                                    return std::max(height, column.Height - rise * static_cast<float>(step) /
                                                                                 static_cast<float>(kBattleshipWallSteps + 1));
                                }
                                if (column.Direction < 0 && front <= 0.0)
                                {
                                    return std::min(height, column.Height);
                                }
                                if (column.Direction < 0)
                                {
                                    const double sight = kBattleshipCarveOriginY +
                                                         (column.Height - kBattleshipCarveOriginY) * distance / column.CrestDistance;
                                    return std::min(height, static_cast<float>(sight));
                                }
                                return height;
                            };
                            const float ha = apply(a);
                            const float hb = apply(b);
                            height = ha + (hb - ha) * static_cast<float>(u);
                        }
                    }
                }
            }
            CarveHit hit;
            const CarvePolyline &shelves = BattleshipShelves();
            if (NearestCarveSegment(shelves, x, z, kBattleshipShelfReach, hit))
            {
                const CarveNode &a = shelves.Nodes[hit.Segment];
                const CarveNode &b = shelves.Nodes[hit.Segment + 1];
                const float u = static_cast<float>(hit.U);
                const float shelf = a.Height + (b.Height - a.Height) * u;
                const float weight = a.Value + (b.Value - a.Value) * u;
                const float above = height - shelf;
                if (above >= 0.0f && above <= kBattleshipShelfRiser)
                {
                    height -= above * weight;
                }
            }
            return height;
        }

        // 刻んだ範囲の割合の升目（尾根の刻みの半径と壁の前の段・棚の段の範囲に kBattleshipCarveMaskMargin を足した範囲の内 1、
        // 外へ kBattleshipCarveFadeMeters で 0）
        struct CarveMask
        {
            double MinX = 0.0;
            double MinZ = 0.0;
            int Columns = 0;
            int Rows = 0;
            VariableArray<float> Values;
        };

        const CarveMask &BattleshipCarveMask()
        {
            static const CarveMask mask = []
            {
                CarveMask result;
                const CarveBounds &bounds = BattleshipCarveBounds();
                if (bounds.MinX > bounds.MaxX)
                {
                    return result;
                }
                result.MinX = bounds.MinX - kBattleshipCarveFadeMeters;
                result.MinZ = bounds.MinZ - kBattleshipCarveFadeMeters;
                result.Columns = static_cast<int>(std::ceil((bounds.MaxX + kBattleshipCarveFadeMeters - result.MinX) /
                                                            kBattleshipCarveMaskCell)) + 1;
                result.Rows = static_cast<int>(std::ceil((bounds.MaxZ + kBattleshipCarveFadeMeters - result.MinZ) /
                                                         kBattleshipCarveMaskCell)) + 1;
                result.Values.assign(static_cast<size_t>(result.Columns) * result.Rows, 0.0f);
                const auto amount = [](const CarvePolyline &polyline, double x, double z, double reach)
                {
                    CarveHit hit;
                    if (!NearestCarveSegment(polyline, x, z, reach + kBattleshipCarveFadeMeters, hit))
                    {
                        return 0.0;
                    }
                    return 1.0 - std::clamp((hit.Distance - reach) / kBattleshipCarveFadeMeters, 0.0, 1.0);
                };
                for (int row = 0; row < result.Rows; ++row)
                {
                    for (int column = 0; column < result.Columns; ++column)
                    {
                        const double x = result.MinX + column * kBattleshipCarveMaskCell;
                        const double z = result.MinZ + row * kBattleshipCarveMaskCell;
                        const double value = std::max(amount(BattleshipRidge(), x, z, kBattleshipCarveRadius + kBattleshipWallTread * kBattleshipWallSteps + kBattleshipWallJitter +
                                                                                   kBattleshipCarveMaskMargin),
                                                      amount(BattleshipShelves(), x, z, kBattleshipShelfReach + kBattleshipCarveMaskMargin));
                        result.Values[static_cast<size_t>(row) * result.Columns + column] = static_cast<float>(value);
                    }
                }
                return result;
            }();
            return mask;
        }

        // 点 (x, z) の刻んだ範囲の割合（升目の双線形の補間）
        float BattleshipCarveAmount(double x, double z)
        {
            const CarveMask &mask = BattleshipCarveMask();
            if (mask.Columns < 2 || mask.Rows < 2)
            {
                return 0.0f;
            }
            const double gx = (x - mask.MinX) / kBattleshipCarveMaskCell;
            const double gz = (z - mask.MinZ) / kBattleshipCarveMaskCell;
            if (gx < 0.0 || gz < 0.0 || gx >= mask.Columns - 1 || gz >= mask.Rows - 1)
            {
                return 0.0f;
            }
            const int ix = static_cast<int>(gx);
            const int iz = static_cast<int>(gz);
            const float tx = static_cast<float>(gx - ix);
            const float tz = static_cast<float>(gz - iz);
            const auto value = [&mask](int column, int row)
            { return mask.Values[static_cast<size_t>(row) * mask.Columns + column]; };
            return Mix(Mix(value(ix, iz), value(ix + 1, iz), tx), Mix(value(ix, iz + 1), value(ix + 1, iz + 1), tx), tz);
        }

        // 焼く格子: 近景の高さ場か、遠景の段の高さの並びと、その格子に掛ける範囲。近景は Battleship Rock の尾根の刻みを掛けた高さ
        struct CliffGrid
        {
            Terrain::TerrainGridLayout Layout;
            const Terrain::TerrainHeightField *Field = nullptr;
            const float *Heights = nullptr;
            const CliffRegion *Regions = nullptr;
            size_t RegionCount = 0;

            float Height(uint32_t column, uint32_t row) const
            {
                if (Heights != nullptr)
                {
                    return Heights[static_cast<size_t>(row) * Layout.Columns + column];
                }
                return CarveBattleshipHeight(Layout.FirstX + static_cast<double>(column) * Layout.Spacing,
                                             Layout.FirstZ + static_cast<double>(row) * Layout.Spacing,
                                             Field->GetGridVertexHeight(column, row));
            }

            // 刻む前の高さ場の格子の高さ（遠景の段の格子では格子の高さのまま）
            float RawHeight(uint32_t column, uint32_t row) const
            {
                if (Heights != nullptr)
                {
                    return Heights[static_cast<size_t>(row) * Layout.Columns + column];
                }
                return Field->GetGridVertexHeight(column, row);
            }

            // 近景の点 (x, z) の法線。刻みの届く範囲では刻んだ格子の高さの差分から、外は高さ場の法線のまま
            Terrain::TerrainNormal NearNormal(float x, float z, float sampleSpacing) const
            {
                const CarveBounds &bounds = BattleshipCarveBounds();
                const double reach = static_cast<double>(sampleSpacing) + 2.0 * Layout.Spacing;
                if (x < bounds.MinX - reach || x > bounds.MaxX + reach || z < bounds.MinZ - reach || z > bounds.MaxZ + reach)
                {
                    return Field->SampleNormal(x, z, sampleSpacing);
                }
                const auto sample = [this](double px, double pz)
                {
                    const double gx =
                        std::clamp((px - Layout.FirstX) / Layout.Spacing, 0.0, static_cast<double>(Layout.Columns - 1));
                    const double gz =
                        std::clamp((pz - Layout.FirstZ) / Layout.Spacing, 0.0, static_cast<double>(Layout.Rows - 1));
                    const uint32_t ix = std::min(static_cast<uint32_t>(gx), Layout.Columns - 2);
                    const uint32_t iz = std::min(static_cast<uint32_t>(gz), Layout.Rows - 2);
                    const double tx = gx - ix;
                    const double tz = gz - iz;
                    const double h0 = Height(ix, iz) + (Height(ix + 1, iz) - Height(ix, iz)) * tx;
                    const double h1 = Height(ix, iz + 1) + (Height(ix + 1, iz + 1) - Height(ix, iz + 1)) * tx;
                    return h0 + (h1 - h0) * tz;
                };
                const double e = sampleSpacing > 0.0f ? sampleSpacing : 0.5;
                const double dx = sample(x + e, z) - sample(x - e, z);
                const double dz = sample(x, z + e) - sample(x, z - e);
                const double length = std::sqrt(dx * dx + 4.0 * e * e + dz * dz);
                Terrain::TerrainNormal normal;
                normal.X = static_cast<float>(-dx / length);
                normal.Y = static_cast<float>(2.0 * e / length);
                normal.Z = static_cast<float>(-dz / length);
                return normal;
            }
        };

        CliffGrid MakeNearGrid(const Terrain::TerrainHeightField &field)
        {
            CliffGrid grid;
            grid.Layout = field.GetGridLayout();
            grid.Field = &field;
            grid.Regions = kNearRegions;
            grid.RegionCount = sizeof(kNearRegions) / sizeof(kNearRegions[0]);
            return grid;
        }

        CliffGrid MakeHorizonGrid(const TerrainCliffHorizonGrid &horizon)
        {
            CliffGrid grid;
            grid.Layout = horizon.Layout;
            grid.Heights = horizon.Heights;
            grid.Regions = kCastleRegions;
            grid.RegionCount = sizeof(kCastleRegions) / sizeof(kCastleRegions[0]);
            return grid;
        }

        // 範囲の内側への距離（m。外は負）
        double RegionInside(const CliffRegion &region, double x, double z)
        {
            return std::min(std::min(x - region.MinX, region.MaxX - x), std::min(z - region.MinZ, region.MaxZ - z));
        }

        // 格子の区画（升目の範囲）が、どれかの範囲（窓の余白を足す）に掛かるか
        bool GridTileIntersects(const CliffGrid &grid, uint32_t firstColumn, uint32_t cellsX, uint32_t firstRow,
                                uint32_t cellsZ, int windowMargin)
        {
            const Terrain::TerrainGridLayout &layout = grid.Layout;
            const double margin = static_cast<double>(windowMargin + 1) * layout.Spacing;
            const double minX = layout.FirstX + static_cast<double>(firstColumn) * layout.Spacing;
            const double maxX = layout.FirstX + static_cast<double>(firstColumn + cellsX) * layout.Spacing;
            const double minZ = layout.FirstZ + static_cast<double>(firstRow) * layout.Spacing;
            const double maxZ = layout.FirstZ + static_cast<double>(firstRow + cellsZ) * layout.Spacing;
            for (size_t index = 0; index < grid.RegionCount; ++index)
            {
                const CliffRegion &region = grid.Regions[index];
                if (maxX >= region.MinX - margin && minX <= region.MaxX + margin && maxZ >= region.MinZ - margin &&
                    minZ <= region.MaxZ + margin)
                {
                    return true;
                }
            }
            return false;
        }

        // 区画の周りの升目の印と、点の位置を求める面
        class CliffSurface
        {
        public:
            CliffSurface(const CliffGrid &grid, uint32_t firstColumn, uint32_t cellsX, uint32_t firstRow,
                         uint32_t cellsZ)
                : m_Grid(grid), m_Layout(grid.Layout)
            {
                m_CellColumns = static_cast<int>(m_Layout.Columns) - 1;
                m_CellRows = static_cast<int>(m_Layout.Rows) - 1;
                m_WindowColumn = static_cast<int>(firstColumn) - kWindowMargin;
                m_WindowRow = static_cast<int>(firstRow) - kWindowMargin;
                m_WindowColumns = static_cast<int>(cellsX) + 2 * kWindowMargin;
                m_WindowRows = static_cast<int>(cellsZ) + 2 * kWindowMargin;

                // 急な升目の印（膨らませる分、窓より 1 升目広く求める）
                const int steepColumns = m_WindowColumns + 2;
                const int steepRows = m_WindowRows + 2;
                VariableArray<uint8_t> steep(static_cast<size_t>(steepColumns) * steepRows, 0u);
                for (int row = 0; row < steepRows; ++row)
                {
                    for (int column = 0; column < steepColumns; ++column)
                    {
                        steep[static_cast<size_t>(row) * steepColumns + column] =
                            IsSteepCell(m_WindowColumn - 1 + column, m_WindowRow - 1 + row) ? 1u : 0u;
                    }
                }
                // 細分する升目は、急な升目とその周り 1 升目（押し出しが 0 から上がる帯）
                const size_t windowCells = static_cast<size_t>(m_WindowColumns) * m_WindowRows;
                m_Refined.assign(windowCells, 0u);
                m_SubdivisionX.assign(windowCells, 1u);
                m_SubdivisionZ.assign(windowCells, 1u);
                for (int row = 0; row < m_WindowRows; ++row)
                {
                    for (int column = 0; column < m_WindowColumns; ++column)
                    {
                        const int globalColumn = m_WindowColumn + column;
                        const int globalRow = m_WindowRow + row;
                        if (!IsCellInGrid(globalColumn, globalRow))
                        {
                            continue;
                        }
                        bool bRefined = false;
                        for (int dz = 0; dz <= 2 && !bRefined; ++dz)
                        {
                            for (int dx = 0; dx <= 2 && !bRefined; ++dx)
                            {
                                bRefined = steep[static_cast<size_t>(row + dz) * steepColumns + column + dx] != 0u;
                            }
                        }
                        if (!bRefined)
                        {
                            continue;
                        }
                        const size_t cell = static_cast<size_t>(row) * m_WindowColumns + column;
                        m_Refined[cell] = 1u;
                        // 面に沿った長さを範囲の細分の間隔で割る（升目の 2 つの辺の大きい方の傾き）
                        const double spacing = m_Layout.Spacing;
                        const double slopeX =
                            std::max(std::abs(GridHeight(globalColumn + 1, globalRow) - GridHeight(globalColumn, globalRow)),
                                     std::abs(GridHeight(globalColumn + 1, globalRow + 1) -
                                              GridHeight(globalColumn, globalRow + 1))) /
                            spacing;
                        const double slopeZ =
                            std::max(std::abs(GridHeight(globalColumn, globalRow + 1) - GridHeight(globalColumn, globalRow)),
                                     std::abs(GridHeight(globalColumn + 1, globalRow + 1) -
                                              GridHeight(globalColumn + 1, globalRow))) /
                            spacing;
                        const CliffRegion &region = InnermostRegion(WorldX(static_cast<double>(globalColumn) + 0.5),
                                                                    WorldZ(static_cast<double>(globalRow) + 0.5));
                        m_SubdivisionX[cell] = SubdivisionCount(std::sqrt(1.0 + slopeX * slopeX) * spacing, region);
                        m_SubdivisionZ[cell] = SubdivisionCount(std::sqrt(1.0 + slopeZ * slopeZ) * spacing, region);
                    }
                }
            }

            bool IsRefined(int column, int row) const
            {
                const size_t cell = WindowCell(column, row);
                return cell != kNoCell && m_Refined[cell] != 0u;
            }

            uint32_t SubdivisionX(int column, int row) const
            {
                const size_t cell = WindowCell(column, row);
                return cell == kNoCell ? 1u : m_SubdivisionX[cell];
            }

            uint32_t SubdivisionZ(int column, int row) const
            {
                const size_t cell = WindowCell(column, row);
                return cell == kNoCell ? 1u : m_SubdivisionZ[cell];
            }

            // 格子の行 row の、列 column から column + 1 への辺の細分の数（両側の升目の大きい方）
            uint32_t EdgeCountX(int column, int row) const
            {
                return std::max(SubdivisionX(column, row - 1), SubdivisionX(column, row));
            }

            // 格子の列 column の、行 row から row + 1 への辺の細分の数
            uint32_t EdgeCountZ(int column, int row) const
            {
                return std::max(SubdivisionZ(column - 1, row), SubdivisionZ(column, row));
            }

            // 格子の頂点 (column, row) が細分した升目だけに囲まれているか（押し出してよい頂点か）
            bool IsInteriorPoint(int column, int row) const
            {
                return IsRefined(column - 1, row - 1) && IsRefined(column, row - 1) && IsRefined(column - 1, row) &&
                       IsRefined(column, row);
            }

            /// 格子の頂点の高さ（番号は格子の内側に切り詰める）
            double GridHeight(int column, int row) const
            {
                column = std::clamp(column, 0, static_cast<int>(m_Layout.Columns) - 1);
                row = std::clamp(row, 0, static_cast<int>(m_Layout.Rows) - 1);
                return static_cast<double>(m_Grid.Height(static_cast<uint32_t>(column), static_cast<uint32_t>(row)));
            }

            // 格子の座標 (gx, gz)（列・行の番号の実数）の、格子の高さを Catmull-Rom で補間した滑らかな面の高さ。
            // 格子の頂点では格子の高さそのものになる。Battleship Rock の刻んだ段では Catmull-Rom が段の縁で上へ行き過ぎて
            // 尖るので、刻みの割合で刻んだ高さへ寄せる。割合の全域（0〜1）で同じ 2 つの式を混ぜ、割合の境で式を切り替えない
            double BaseHeight(double gx, double gz) const
            {
                const float carve = BattleshipCarveAmount(WorldX(gx), WorldZ(gz));
                if (carve <= 0.0f)
                {
                    return CatmullRomHeight(gx, gz);
                }
                const double carved = CarvedHeight(gx, gz);
                if (carve >= 1.0f)
                {
                    return carved;
                }
                const double smooth = CatmullRomHeight(gx, gz);
                return smooth + (carved - smooth) * carve;
            }

            // 刻んだ高さ。近景では刻みを細分の点ごとに刻む前の高さの双線形へ掛ける（格子の点だけで刻むと、段の上端の角と
            // 壁の列の境が格子の 1 m の双線形で 2〜3 m の弧に丸まる。格子の点では格子の高さと同じ値になる）。
            // 遠景の段の格子は刻まないので、格子の高さの双線形
            double CarvedHeight(double gx, double gz) const
            {
                if (m_Grid.Heights == nullptr)
                {
                    return static_cast<double>(
                        CarveBattleshipHeight(WorldX(gx), WorldZ(gz), static_cast<float>(RawBilinearHeight(gx, gz))));
                }
                return BilinearHeight(gx, gz);
            }

            double CatmullRomHeight(double gx, double gz) const
            {
                const double fx = std::floor(gx);
                const double fz = std::floor(gz);
                const int ix = static_cast<int>(fx);
                const int iz = static_cast<int>(fz);
                double wx[4];
                double wz[4];
                CatmullRomWeights(gx - fx, wx);
                CatmullRomWeights(gz - fz, wz);
                double height = 0.0;
                for (int j = 0; j < 4; ++j)
                {
                    if (wz[j] == 0.0)
                    {
                        continue;
                    }
                    double row = 0.0;
                    for (int i = 0; i < 4; ++i)
                    {
                        if (wx[i] != 0.0)
                        {
                            row += wx[i] * GridHeight(ix - 1 + i, iz - 1 + j);
                        }
                    }
                    height += wz[j] * row;
                }
                return height;
            }

            // 格子の高さの双線形の補間（押し出しの向きを求めるだけなので、滑らかな面より軽い式で引く）
            double BilinearHeight(double gx, double gz) const
            {
                const double fx = std::floor(gx);
                const double fz = std::floor(gz);
                const int ix = static_cast<int>(fx);
                const int iz = static_cast<int>(fz);
                const double tx = gx - fx;
                const double tz = gz - fz;
                const double h0 = GridHeight(ix, iz) + (GridHeight(ix + 1, iz) - GridHeight(ix, iz)) * tx;
                const double h1 = GridHeight(ix, iz + 1) + (GridHeight(ix + 1, iz + 1) - GridHeight(ix, iz + 1)) * tx;
                return h0 + (h1 - h0) * tz;
            }

            // 刻む前の格子の高さの双線形の補間
            double RawBilinearHeight(double gx, double gz) const
            {
                const int ix = static_cast<int>(std::floor(gx));
                const int iz = static_cast<int>(std::floor(gz));
                const double tx = gx - ix;
                const double tz = gz - iz;
                const auto raw = [this](int column, int row)
                {
                    column = std::clamp(column, 0, static_cast<int>(m_Layout.Columns) - 1);
                    row = std::clamp(row, 0, static_cast<int>(m_Layout.Rows) - 1);
                    return static_cast<double>(m_Grid.RawHeight(static_cast<uint32_t>(column), static_cast<uint32_t>(row)));
                };
                const double h0 = raw(ix, iz) + (raw(ix + 1, iz) - raw(ix, iz)) * tx;
                const double h1 = raw(ix, iz + 1) + (raw(ix + 1, iz + 1) - raw(ix, iz + 1)) * tx;
                return h0 + (h1 - h0) * tz;
            }

            // 数 m の幅で見た面の法線（押し出しの向き）
            void SmoothedNormal(double gx, double gz, float outNormal[3]) const
            {
                const double step = kNormalSmoothingMeters / m_Layout.Spacing;
                const double slopeX =
                    (BilinearHeight(gx + step, gz) - BilinearHeight(gx - step, gz)) / (2.0 * kNormalSmoothingMeters);
                const double slopeZ =
                    (BilinearHeight(gx, gz + step) - BilinearHeight(gx, gz - step)) / (2.0 * kNormalSmoothingMeters);
                const double length = std::sqrt(slopeX * slopeX + 1.0 + slopeZ * slopeZ);
                outNormal[0] = static_cast<float>(-slopeX / length);
                outNormal[1] = static_cast<float>(1.0 / length);
                outNormal[2] = static_cast<float>(-slopeZ / length);
            }

            // 範囲の内側への距離で 0〜1（外は 0。範囲が重なれば大きい方）
            double RegionFade(double x, double z) const
            {
                double inside = -std::numeric_limits<double>::max();
                for (size_t index = 0; index < m_Grid.RegionCount; ++index)
                {
                    inside = std::max(inside, RegionInside(m_Grid.Regions[index], x, z));
                }
                return std::clamp(inside / kRegionFadeMeters, 0.0, 1.0);
            }

            // 点の細分の間隔と下限を決める範囲（最も内側にある範囲。格子の範囲は 1 つ以上ある）
            const CliffRegion &InnermostRegion(double x, double z) const
            {
                double best = -std::numeric_limits<double>::max();
                size_t bestIndex = 0;
                for (size_t index = 0; index < m_Grid.RegionCount; ++index)
                {
                    const double inside = RegionInside(m_Grid.Regions[index], x, z);
                    if (inside > best)
                    {
                        best = inside;
                        bestIndex = index;
                    }
                }
                return m_Grid.Regions[bestIndex];
            }

            // 傾きと範囲から決まる押し出しの重み（升目の印を除く）
            float SlopeWeight(double gx, double gz, const float normal[3]) const
            {
                const double fade = RegionFade(WorldX(gx), WorldZ(gz));
                if (fade <= 0.0)
                {
                    return 0.0f;
                }
                const float horizontal = std::sqrt(normal[0] * normal[0] + normal[2] * normal[2]);
                return SmoothStep(kSteepSinLow, kSteepSinHigh, horizontal) * static_cast<float>(fade);
            }

            // 押し出しの重み。細分した升目の外周（細分しない升目と接する頂点）で 0 になるよう、頂点の印を升目の中で
            // 双線形に補間して掛ける
            float Weight(double gx, double gz, const float normal[3]) const
            {
                const float slope = SlopeWeight(gx, gz, normal);
                if (slope <= 0.0f)
                {
                    return 0.0f;
                }
                const double fx = std::floor(gx);
                const double fz = std::floor(gz);
                const int column = static_cast<int>(fx);
                const int row = static_cast<int>(fz);
                const float tx = static_cast<float>(gx - fx);
                const float tz = static_cast<float>(gz - fz);
                const float g00 = IsInteriorPoint(column, row) ? 1.0f : 0.0f;
                const float g10 = IsInteriorPoint(column + 1, row) ? 1.0f : 0.0f;
                const float g01 = IsInteriorPoint(column, row + 1) ? 1.0f : 0.0f;
                const float g11 = IsInteriorPoint(column + 1, row + 1) ? 1.0f : 0.0f;
                const float interior = Mix(Mix(g00, g10, tx), Mix(g01, g11, tx), tz);
                return slope * interior;
            }

            // 格子の座標 (gx, gz) の、押し出した後の位置（m）
            void Position(double gx, double gz, double outPosition[3], bool &outDisplaced) const
            {
                const double x = WorldX(gx);
                const double z = WorldZ(gz);
                const double base = BaseHeight(gx, gz);
                outPosition[0] = x;
                outPosition[1] = base;
                outPosition[2] = z;
                outDisplaced = false;
                float normal[3];
                SmoothedNormal(gx, gz, normal);
                const float weight = Weight(gx, gz, normal);
                if (weight <= 0.0f)
                {
                    return;
                }
                const float fx = static_cast<float>(x);
                const float fz = static_cast<float>(z);
                // 押し出しで高さが動いた分を 1 回だけ戻して、層の境をシェーダーが見る高さにそろえる
                const float first = CliffDisplacement(fx, static_cast<float>(base), fz, normal[0], normal[1], normal[2]);
                const float movedY = static_cast<float>(base) + first * weight * normal[1];
                const float displacement = CliffDisplacement(fx, movedY, fz, normal[0], normal[1], normal[2]) * weight;
                outPosition[0] += static_cast<double>(normal[0] * displacement);
                outPosition[1] += static_cast<double>(normal[1] * displacement);
                outPosition[2] += static_cast<double>(normal[2] * displacement);
                outDisplaced = true;
            }

            // 格子の座標 (gx, gz) の頂点の位置と UV（法線は 0 のまま。後で隣の三角形から求める）
            bool EvaluatePosition(double gx, double gz, float textureTileMeters, TerrainCliffVertex &outVertex,
                                  double outPosition[3], bool &outDisplaced) const
            {
                Position(gx, gz, outPosition, outDisplaced);
                for (int axis = 0; axis < 3; ++axis)
                {
                    outVertex.Position[axis] = static_cast<float>(outPosition[axis]);
                    outVertex.Normal[axis] = 0.0f;
                }
                outVertex.TexCoord[0] = outVertex.Position[0] / textureTileMeters;
                outVertex.TexCoord[1] = outVertex.Position[2] / textureTileMeters;
                return std::isfinite(outPosition[0]) && std::isfinite(outPosition[1]) && std::isfinite(outPosition[2]);
            }

            // 格子の座標 (gx, gz) の頂点（位置と、押し出した面の差分から求めた法線）。区画の境の頂点に使い、隣の区画と
            // 同じ法線にする（位置を 5 回求めるので、区画の内側の頂点には使わない）
            bool EvaluateVertex(double gx, double gz, float textureTileMeters, TerrainCliffVertex &outVertex,
                                bool &outDisplaced) const
            {
                double center[3];
                Position(gx, gz, center, outDisplaced);
                const double step = kVertexNormalStepMeters / m_Layout.Spacing;
                double xPlus[3];
                double xMinus[3];
                double zPlus[3];
                double zMinus[3];
                bool bIgnored = false;
                Position(gx + step, gz, xPlus, bIgnored);
                Position(gx - step, gz, xMinus, bIgnored);
                Position(gx, gz + step, zPlus, bIgnored);
                Position(gx, gz - step, zMinus, bIgnored);
                const double tx[3] = {xPlus[0] - xMinus[0], xPlus[1] - xMinus[1], xPlus[2] - xMinus[2]};
                const double tz[3] = {zPlus[0] - zMinus[0], zPlus[1] - zMinus[1], zPlus[2] - zMinus[2]};
                // 法線 = tz × tx（平らな面で +Y）
                double normal[3] = {tz[1] * tx[2] - tz[2] * tx[1], tz[2] * tx[0] - tz[0] * tx[2],
                                    tz[0] * tx[1] - tz[1] * tx[0]};
                const double length = std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
                if (!(length > 0.0) || !std::isfinite(length))
                {
                    return false;
                }
                for (int axis = 0; axis < 3; ++axis)
                {
                    outVertex.Position[axis] = static_cast<float>(center[axis]);
                    outVertex.Normal[axis] = static_cast<float>(normal[axis] / length);
                }
                outVertex.TexCoord[0] = outVertex.Position[0] / textureTileMeters;
                outVertex.TexCoord[1] = outVertex.Position[2] / textureTileMeters;
                return std::isfinite(outVertex.Position[0]) && std::isfinite(outVertex.Position[1]) &&
                       std::isfinite(outVertex.Position[2]);
            }

            double WorldX(double gx) const
            {
                return m_Layout.FirstX + gx * m_Layout.Spacing;
            }

            double WorldZ(double gz) const
            {
                return m_Layout.FirstZ + gz * m_Layout.Spacing;
            }

        private:
            static constexpr size_t kNoCell = std::numeric_limits<size_t>::max();

            static void CatmullRomWeights(double t, double outWeights[4])
            {
                const double t2 = t * t;
                const double t3 = t2 * t;
                outWeights[0] = 0.5 * (-t + 2.0 * t2 - t3);
                outWeights[1] = 0.5 * (2.0 - 5.0 * t2 + 3.0 * t3);
                outWeights[2] = 0.5 * (t + 4.0 * t2 - 3.0 * t3);
                outWeights[3] = 0.5 * (-t2 + t3);
            }

            static uint32_t SubdivisionCount(double surfaceLength, const CliffRegion &region)
            {
                const double count = std::ceil(surfaceLength / region.TargetSpacing);
                return static_cast<uint32_t>(std::clamp(count, static_cast<double>(region.MinSubdivisions),
                                                        static_cast<double>(kMaxSubdivisions)));
            }

            bool IsCellInGrid(int column, int row) const
            {
                return column >= 0 && row >= 0 && column < m_CellColumns && row < m_CellRows;
            }

            size_t WindowCell(int column, int row) const
            {
                const int localColumn = column - m_WindowColumn;
                const int localRow = row - m_WindowRow;
                if (!IsCellInGrid(column, row) || localColumn < 0 || localRow < 0 || localColumn >= m_WindowColumns ||
                    localRow >= m_WindowRows)
                {
                    return kNoCell;
                }
                return static_cast<size_t>(localRow) * m_WindowColumns + localColumn;
            }

            // 升目の 4 隅と中心のどれかで押し出しの重み（升目の印を除く）が正か
            bool IsSteepCell(int column, int row) const
            {
                if (!IsCellInGrid(column, row))
                {
                    return false;
                }
                const double points[5][2] = {{0.0, 0.0}, {1.0, 0.0}, {0.0, 1.0}, {1.0, 1.0}, {0.5, 0.5}};
                for (const auto &offset : points)
                {
                    const double gx = static_cast<double>(column) + offset[0];
                    const double gz = static_cast<double>(row) + offset[1];
                    if (RegionFade(WorldX(gx), WorldZ(gz)) <= 0.0)
                    {
                        continue;
                    }
                    float normal[3];
                    SmoothedNormal(gx, gz, normal);
                    if (SlopeWeight(gx, gz, normal) > 0.0f)
                    {
                        return true;
                    }
                }
                return false;
            }

            const CliffGrid &m_Grid;
            Terrain::TerrainGridLayout m_Layout;
            int m_CellColumns = 0;
            int m_CellRows = 0;
            int m_WindowColumn = 0;
            int m_WindowRow = 0;
            int m_WindowColumns = 0;
            int m_WindowRows = 0;
            VariableArray<uint8_t> m_Refined;
            VariableArray<uint8_t> m_SubdivisionX;
            VariableArray<uint8_t> m_SubdivisionZ;
        };

        // 升目の外周の頂点（升目の左下の隅から反時計回りの周の位置 0〜4 と、頂点の番号）
        struct LoopVertex
        {
            uint32_t Index = 0;
            double Perimeter = 0.0;
        };
    } // namespace

    namespace
    {
        // 格子の区画の升目を、崖の細部を足して張る。
        // 近景（grid.Field がある）: outVertices・outIndices を空にして区画のすべての升目を張る。押し出さない格子の頂点は
        //   高さ場から作る。
        // 遠景の段（grid.Heights がある）: outVertices には区画の格子の頂点（(cellsX + 1) × (cellsZ + 1)、行ごと）が入っている。
        //   押し出さない格子の頂点はその頂点をそのまま使い、細分しない升目と接しない升目は張らない（元のメッシュの三角形を使う）。
        //   張った升目に outChangedCells で 1 を立てる
        bool BuildCliffCells(const CliffGrid &grid, uint32_t firstColumn, uint32_t cellsX, uint32_t firstRow,
                             uint32_t cellsZ, float textureTileMeters, VariableArray<TerrainCliffVertex> &outVertices,
                             VariableArray<uint32_t> &outIndices, VariableArray<uint8_t> &outChangedCells,
                             TerrainCliffTileStats &outStats, AnsiString &outError)
        {
            const CliffSurface surface(grid, firstColumn, cellsX, firstRow, cellsZ);
            const Terrain::TerrainGridLayout layout = grid.Layout;
            const bool bHorizon = grid.Field == nullptr;
            const uint32_t sideX = cellsX + 1;
            const uint32_t sideZ = cellsZ + 1;
            const float normalSpacing = static_cast<float>(layout.Spacing * 0.5);
            if (bHorizon && outVertices.size() != static_cast<size_t>(sideX) * sideZ)
            {
                outError = "遠景の段の区画の頂点の数が、格子の升目の数と合いません";
                return false;
            }
            if (!bHorizon)
            {
                outVertices.clear();
            }
            outIndices.clear();
            outChangedCells.assign(static_cast<size_t>(cellsX) * cellsZ, 0u);
            outStats = TerrainCliffTileStats{};
            // 頂点ごとの区画の中の格子の座標（三角形の向きをそろえるのに使う）
            VariableArray<float> parameters;
            // 法線を隣の三角形から求める頂点の、倍精度の位置（それ以外の頂点は使わない）と印
            VariableArray<double> precisePositions;
            VariableArray<uint8_t> accumulateNormal;
            const double lastColumn = static_cast<double>(firstColumn + cellsX);
            const double lastRow = static_cast<double>(firstRow + cellsZ);

            // 区画の内側の頂点は位置を 1 回だけ求め、法線は三角形を張った後に隣の三角形の面の向きから作る。区画の境の頂点は
            // 隣の区画からも同じ値になるよう、面の差分で法線を求める
            auto addVertex = [&](double gx, double gz, uint32_t &outIndex) -> bool
            {
                TerrainCliffVertex vertex;
                double position[3] = {0.0, 0.0, 0.0};
                bool bDisplaced = false;
                const bool bTileBorder = gx == static_cast<double>(firstColumn) || gx == lastColumn ||
                                         gz == static_cast<double>(firstRow) || gz == lastRow;
                const bool bValid = bTileBorder
                                        ? surface.EvaluateVertex(gx, gz, textureTileMeters, vertex, bDisplaced)
                                        : surface.EvaluatePosition(gx, gz, textureTileMeters, vertex, position, bDisplaced);
                if (!bValid)
                {
                    outError = "崖の細部の頂点の位置・法線が有限でありません";
                    return false;
                }
                if (bDisplaced)
                {
                    ++outStats.DisplacedVertices;
                }
                outIndex = static_cast<uint32_t>(outVertices.size());
                outVertices.push_back(vertex);
                parameters.push_back(static_cast<float>(gx - static_cast<double>(firstColumn)));
                parameters.push_back(static_cast<float>(gz - static_cast<double>(firstRow)));
                precisePositions.push_back(position[0]);
                precisePositions.push_back(position[1]);
                precisePositions.push_back(position[2]);
                accumulateNormal.push_back(bTileBorder ? 0u : 1u);
                return true;
            };
            // 高さ場から法線を引いた頂点（細分しない頂点・扇の中心）の印をそろえる
            auto addFixedVertexState = [&]()
            {
                precisePositions.push_back(0.0);
                precisePositions.push_back(0.0);
                precisePositions.push_back(0.0);
                accumulateNormal.push_back(0u);
            };

            // 遠景の段: 元の格子の頂点の印をそろえる
            if (bHorizon)
            {
                for (uint32_t row = 0; row < sideZ; ++row)
                {
                    for (uint32_t column = 0; column < sideX; ++column)
                    {
                        parameters.push_back(static_cast<float>(column));
                        parameters.push_back(static_cast<float>(row));
                        addFixedVertexState();
                    }
                }
            }

            // 格子の頂点。押し出さない頂点は今までの区画（近景は BuildDemTerrainTile、遠景は元のメッシュ）と同じ値にする
            VariableArray<uint32_t> gridIndex(static_cast<size_t>(sideX) * sideZ, 0u);
            for (uint32_t row = 0; row < sideZ; ++row)
            {
                for (uint32_t column = 0; column < sideX; ++column)
                {
                    const uint32_t globalColumn = firstColumn + column;
                    const uint32_t globalRow = firstRow + row;
                    uint32_t index = 0;
                    if (surface.IsInteriorPoint(static_cast<int>(globalColumn), static_cast<int>(globalRow)))
                    {
                        if (!addVertex(static_cast<double>(globalColumn), static_cast<double>(globalRow), index))
                        {
                            return false;
                        }
                    }
                    else if (bHorizon)
                    {
                        index = row * sideX + column;
                    }
                    else
                    {
                        const float x = static_cast<float>(layout.FirstX + static_cast<double>(globalColumn) * layout.Spacing);
                        const float z = static_cast<float>(layout.FirstZ + static_cast<double>(globalRow) * layout.Spacing);
                        const float y = grid.Height(globalColumn, globalRow);
                        const Terrain::TerrainNormal normal = grid.NearNormal(x, z, normalSpacing);
                        if (!std::isfinite(y) || !std::isfinite(normal.X) || !std::isfinite(normal.Y) || !std::isfinite(normal.Z))
                        {
                            outError = "DEM の区画の頂点の高さ・法線が有限でありません";
                            return false;
                        }
                        TerrainCliffVertex vertex;
                        vertex.Position[0] = x;
                        vertex.Position[1] = y;
                        vertex.Position[2] = z;
                        vertex.Normal[0] = normal.X;
                        vertex.Normal[1] = normal.Y;
                        vertex.Normal[2] = normal.Z;
                        vertex.TexCoord[0] = x / textureTileMeters;
                        vertex.TexCoord[1] = z / textureTileMeters;
                        index = static_cast<uint32_t>(outVertices.size());
                        outVertices.push_back(vertex);
                        parameters.push_back(static_cast<float>(column));
                        parameters.push_back(static_cast<float>(row));
                        addFixedVertexState();
                    }
                    gridIndex[static_cast<size_t>(row) * sideX + column] = index;
                }
            }

            // 辺の途中の頂点（両側の升目の細分の大きい方で割る）。位置は全体の格子の番号だけで決まり、隣の区画と同じになる
            VariableArray<uint32_t> edgeXBase(static_cast<size_t>(cellsX) * sideZ, 0u);
            VariableArray<uint32_t> edgeZBase(static_cast<size_t>(sideX) * cellsZ, 0u);
            for (uint32_t row = 0; row < sideZ; ++row)
            {
                for (uint32_t column = 0; column < cellsX; ++column)
                {
                    const int globalColumn = static_cast<int>(firstColumn + column);
                    const int globalRow = static_cast<int>(firstRow + row);
                    const uint32_t count = surface.EdgeCountX(globalColumn, globalRow);
                    edgeXBase[static_cast<size_t>(row) * cellsX + column] = static_cast<uint32_t>(outVertices.size());
                    for (uint32_t k = 1; k < count; ++k)
                    {
                        uint32_t index = 0;
                        if (!addVertex(static_cast<double>(globalColumn) + static_cast<double>(k) / count,
                                       static_cast<double>(globalRow), index))
                        {
                            return false;
                        }
                    }
                }
            }
            for (uint32_t row = 0; row < cellsZ; ++row)
            {
                for (uint32_t column = 0; column < sideX; ++column)
                {
                    const int globalColumn = static_cast<int>(firstColumn + column);
                    const int globalRow = static_cast<int>(firstRow + row);
                    const uint32_t count = surface.EdgeCountZ(globalColumn, globalRow);
                    edgeZBase[static_cast<size_t>(row) * sideX + column] = static_cast<uint32_t>(outVertices.size());
                    for (uint32_t k = 1; k < count; ++k)
                    {
                        uint32_t index = 0;
                        if (!addVertex(static_cast<double>(globalColumn),
                                       static_cast<double>(globalRow) + static_cast<double>(k) / count, index))
                        {
                            return false;
                        }
                    }
                }
            }

            // 三角形は区画の格子の座標で見て、今までの区画と同じ向き（外積が正）にそろえる
            auto emit = [&](uint32_t a, uint32_t b, uint32_t c)
            {
                const float ax = parameters[static_cast<size_t>(a) * 2];
                const float az = parameters[static_cast<size_t>(a) * 2 + 1];
                const float cross = (parameters[static_cast<size_t>(b) * 2] - ax) * (parameters[static_cast<size_t>(c) * 2 + 1] - az) -
                                    (parameters[static_cast<size_t>(b) * 2 + 1] - az) * (parameters[static_cast<size_t>(c) * 2] - ax);
                if (cross > 0.0f)
                {
                    outIndices.push_back(a);
                    outIndices.push_back(b);
                    outIndices.push_back(c);
                }
                else if (cross < 0.0f)
                {
                    outIndices.push_back(a);
                    outIndices.push_back(c);
                    outIndices.push_back(b);
                }
            };

            VariableArray<LoopVertex> outer;
            VariableArray<LoopVertex> inner;
            VariableArray<uint32_t> innerGrid;
            for (uint32_t row = 0; row < cellsZ; ++row)
            {
                for (uint32_t column = 0; column < cellsX; ++column)
                {
                    const int globalColumn = static_cast<int>(firstColumn + column);
                    const int globalRow = static_cast<int>(firstRow + row);
                    const uint32_t i00 = gridIndex[static_cast<size_t>(row) * sideX + column];
                    const uint32_t i10 = gridIndex[static_cast<size_t>(row) * sideX + column + 1];
                    const uint32_t i01 = gridIndex[static_cast<size_t>(row + 1) * sideX + column];
                    const uint32_t i11 = gridIndex[static_cast<size_t>(row + 1) * sideX + column + 1];
                    const uint32_t bottom = surface.EdgeCountX(globalColumn, globalRow);
                    const uint32_t top = surface.EdgeCountX(globalColumn, globalRow + 1);
                    const uint32_t left = surface.EdgeCountZ(globalColumn, globalRow);
                    const uint32_t right = surface.EdgeCountZ(globalColumn + 1, globalRow);
                    const bool bRefined = surface.IsRefined(globalColumn, globalRow);
                    if (!bRefined && bottom == 1u && top == 1u && left == 1u && right == 1u)
                    {
                        if (bHorizon)
                        {
                            // 遠景の段は元のメッシュの三角形を使う
                            continue;
                        }
                        // 今までの区画と同じ 2 つの三角形（対角線は Terrain::UsesMainDiagonal）
                        if (Terrain::UsesMainDiagonal(firstColumn + column, firstRow + row))
                        {
                            outIndices.push_back(i00);
                            outIndices.push_back(i10);
                            outIndices.push_back(i11);
                            outIndices.push_back(i00);
                            outIndices.push_back(i11);
                            outIndices.push_back(i01);
                        }
                        else
                        {
                            outIndices.push_back(i00);
                            outIndices.push_back(i10);
                            outIndices.push_back(i01);
                            outIndices.push_back(i10);
                            outIndices.push_back(i11);
                            outIndices.push_back(i01);
                        }
                        continue;
                    }

                    outChangedCells[static_cast<size_t>(row) * cellsX + column] = 1u;
                    // 升目の外周（左下の隅から反時計回り: 下・右・上・左の辺）
                    outer.clear();
                    const uint32_t bottomBase = edgeXBase[static_cast<size_t>(row) * cellsX + column];
                    const uint32_t topBase = edgeXBase[static_cast<size_t>(row + 1) * cellsX + column];
                    const uint32_t leftBase = edgeZBase[static_cast<size_t>(row) * sideX + column];
                    const uint32_t rightBase = edgeZBase[static_cast<size_t>(row) * sideX + column + 1];
                    outer.push_back({i00, 0.0});
                    for (uint32_t k = 1; k < bottom; ++k)
                    {
                        outer.push_back({bottomBase + k - 1, static_cast<double>(k) / bottom});
                    }
                    outer.push_back({i10, 1.0});
                    for (uint32_t k = 1; k < right; ++k)
                    {
                        outer.push_back({rightBase + k - 1, 1.0 + static_cast<double>(k) / right});
                    }
                    outer.push_back({i11, 2.0});
                    for (uint32_t k = top - 1; k >= 1; --k)
                    {
                        outer.push_back({topBase + k - 1, 3.0 - static_cast<double>(k) / top});
                    }
                    outer.push_back({i01, 3.0});
                    for (uint32_t k = left - 1; k >= 1; --k)
                    {
                        outer.push_back({leftBase + k - 1, 4.0 - static_cast<double>(k) / left});
                    }

                    if (!bRefined)
                    {
                        // 細分した升目と接する升目: 中心から外周の頂点へ扇に張る。中心は今までの 2 つの三角形の対角線の中点
                        ++outStats.TransitionCells;
                        const bool bMain = Terrain::UsesMainDiagonal(firstColumn + column, firstRow + row);
                        const float y = bMain ? 0.5f * (outVertices[i00].Position[1] + outVertices[i11].Position[1])
                                              : 0.5f * (outVertices[i10].Position[1] + outVertices[i01].Position[1]);
                        const float x = static_cast<float>(surface.WorldX(static_cast<double>(globalColumn) + 0.5));
                        const float z = static_cast<float>(surface.WorldZ(static_cast<double>(globalRow) + 0.5));
                        Terrain::TerrainNormal normal{};
                        if (bHorizon)
                        {
                            // 遠景の段: 4 隅（どれも押し出さない元の頂点）の法線の平均
                            float sum[3] = {0.0f, 0.0f, 0.0f};
                            for (const uint32_t corner : {i00, i10, i01, i11})
                            {
                                for (int axis = 0; axis < 3; ++axis)
                                {
                                    sum[axis] += outVertices[corner].Normal[axis];
                                }
                            }
                            const float length = std::sqrt(sum[0] * sum[0] + sum[1] * sum[1] + sum[2] * sum[2]);
                            normal.X = sum[0] / length;
                            normal.Y = sum[1] / length;
                            normal.Z = sum[2] / length;
                        }
                        else
                        {
                            normal = grid.NearNormal(x, z, normalSpacing);
                        }
                        if (!std::isfinite(y) || !std::isfinite(normal.X) || !std::isfinite(normal.Y) || !std::isfinite(normal.Z))
                        {
                            outError = "崖の細部の升目の中心の高さ・法線が有限でありません";
                            return false;
                        }
                        TerrainCliffVertex vertex;
                        vertex.Position[0] = x;
                        vertex.Position[1] = y;
                        vertex.Position[2] = z;
                        vertex.Normal[0] = normal.X;
                        vertex.Normal[1] = normal.Y;
                        vertex.Normal[2] = normal.Z;
                        vertex.TexCoord[0] = x / textureTileMeters;
                        vertex.TexCoord[1] = z / textureTileMeters;
                        const uint32_t center = static_cast<uint32_t>(outVertices.size());
                        outVertices.push_back(vertex);
                        parameters.push_back(static_cast<float>(column) + 0.5f);
                        parameters.push_back(static_cast<float>(row) + 0.5f);
                        addFixedVertexState();
                        for (size_t k = 0; k < outer.size(); ++k)
                        {
                            emit(center, outer[k].Index, outer[(k + 1) % outer.size()].Index);
                        }
                        continue;
                    }

                    // 細分した升目: 内側の格子（辺の外周を除く）を張り、外周と内側の格子の一番外の輪の間を周に沿って縫う
                    ++outStats.RefinedCells;
                    const uint32_t countX = std::max(bottom, top);
                    const uint32_t countZ = std::max(left, right);
                    const uint32_t innerX = countX - 1;
                    const uint32_t innerZ = countZ - 1;
                    innerGrid.assign(static_cast<size_t>(innerX) * innerZ, 0u);
                    for (uint32_t j = 0; j < innerZ; ++j)
                    {
                        for (uint32_t i = 0; i < innerX; ++i)
                        {
                            uint32_t index = 0;
                            if (!addVertex(static_cast<double>(globalColumn) + static_cast<double>(i + 1) / countX,
                                           static_cast<double>(globalRow) + static_cast<double>(j + 1) / countZ, index))
                            {
                                return false;
                            }
                            innerGrid[static_cast<size_t>(j) * innerX + i] = index;
                        }
                    }
                    for (uint32_t j = 0; j + 1 < innerZ; ++j)
                    {
                        for (uint32_t i = 0; i + 1 < innerX; ++i)
                        {
                            const uint32_t a = innerGrid[static_cast<size_t>(j) * innerX + i];
                            const uint32_t b = innerGrid[static_cast<size_t>(j) * innerX + i + 1];
                            const uint32_t c = innerGrid[static_cast<size_t>(j + 1) * innerX + i];
                            const uint32_t d = innerGrid[static_cast<size_t>(j + 1) * innerX + i + 1];
                            if (((i + j) & 1u) == 0u)
                            {
                                emit(a, b, d);
                                emit(a, d, c);
                            }
                            else
                            {
                                emit(a, b, c);
                                emit(b, d, c);
                            }
                        }
                    }
                    // 内側の格子の一番外の輪（周の位置は内側の格子の範囲を 0〜1 に引き延ばして測る）
                    inner.clear();
                    const double spanX = static_cast<double>(innerX - 1);
                    const double spanZ = static_cast<double>(innerZ - 1);
                    for (uint32_t i = 0; i < innerX; ++i)
                    {
                        inner.push_back({innerGrid[i], static_cast<double>(i) / spanX});
                    }
                    for (uint32_t j = 1; j < innerZ; ++j)
                    {
                        inner.push_back({innerGrid[static_cast<size_t>(j) * innerX + innerX - 1], 1.0 + static_cast<double>(j) / spanZ});
                    }
                    for (uint32_t i = innerX - 1; i-- > 0;)
                    {
                        inner.push_back({innerGrid[static_cast<size_t>(innerZ - 1) * innerX + i], 3.0 - static_cast<double>(i) / spanX});
                    }
                    for (uint32_t j = innerZ - 1; j-- > 1;)
                    {
                        inner.push_back({innerGrid[static_cast<size_t>(j) * innerX], 4.0 - static_cast<double>(j) / spanZ});
                    }
                    // 2 つの輪を周の位置の順に縫う（どちらも位置 0 の隅から始まる）
                    const size_t outerCount = outer.size();
                    const size_t innerCount = inner.size();
                    size_t a = 0;
                    size_t b = 0;
                    while (a < outerCount || b < innerCount)
                    {
                        const double nextOuter = a + 1 < outerCount ? outer[a + 1].Perimeter : 4.0;
                        const double nextInner = b + 1 < innerCount ? inner[b + 1].Perimeter : 4.0;
                        const bool bAdvanceOuter = b >= innerCount || (a < outerCount && nextOuter <= nextInner);
                        if (bAdvanceOuter)
                        {
                            emit(outer[a].Index, outer[(a + 1) % outerCount].Index, inner[b % innerCount].Index);
                            ++a;
                        }
                        else
                        {
                            emit(outer[a % outerCount].Index, inner[b].Index, inner[(b + 1) % innerCount].Index);
                            ++b;
                        }
                    }
                }
            }

            // 区画の内側の頂点の法線: 隣の三角形の面の法線を面積の重みで足す。三角形はどれも格子の座標で同じ向きに
            // 張ってあるので、(c − a) × (b − a) が平らな面で +Y を向く
            VariableArray<double> normalSums(outVertices.size() * 3, 0.0);
            for (size_t triangle = 0; triangle + 2 < outIndices.size(); triangle += 3)
            {
                const uint32_t corners[3] = {outIndices[triangle], outIndices[triangle + 1], outIndices[triangle + 2]};
                if (accumulateNormal[corners[0]] == 0u && accumulateNormal[corners[1]] == 0u &&
                    accumulateNormal[corners[2]] == 0u)
                {
                    continue;
                }
                double points[3][3];
                for (int corner = 0; corner < 3; ++corner)
                {
                    const uint32_t index = corners[corner];
                    for (int axis = 0; axis < 3; ++axis)
                    {
                        points[corner][axis] = accumulateNormal[index] != 0u
                                                   ? precisePositions[static_cast<size_t>(index) * 3 + axis]
                                                   : static_cast<double>(outVertices[index].Position[axis]);
                    }
                }
                const double ab[3] = {points[1][0] - points[0][0], points[1][1] - points[0][1], points[1][2] - points[0][2]};
                const double ac[3] = {points[2][0] - points[0][0], points[2][1] - points[0][1], points[2][2] - points[0][2]};
                const double face[3] = {ac[1] * ab[2] - ac[2] * ab[1], ac[2] * ab[0] - ac[0] * ab[2],
                                        ac[0] * ab[1] - ac[1] * ab[0]};
                for (const uint32_t index : corners)
                {
                    if (accumulateNormal[index] != 0u)
                    {
                        for (int axis = 0; axis < 3; ++axis)
                        {
                            normalSums[static_cast<size_t>(index) * 3 + axis] += face[axis];
                        }
                    }
                }
            }
            for (size_t vertex = 0; vertex < outVertices.size(); ++vertex)
            {
                if (accumulateNormal[vertex] == 0u)
                {
                    continue;
                }
                const double *sum = &normalSums[vertex * 3];
                const double length = std::sqrt(sum[0] * sum[0] + sum[1] * sum[1] + sum[2] * sum[2]);
                if (!(length > 0.0) || !std::isfinite(length))
                {
                    outError = "崖の細部の頂点の法線を隣の三角形から求められません";
                    return false;
                }
                for (int axis = 0; axis < 3; ++axis)
                {
                    outVertices[vertex].Normal[axis] = static_cast<float>(sum[axis] / length);
                }
            }
            return true;
        }
    } // namespace

    bool TerrainCliffTileIntersects(const Core::Rendering::Terrain::TerrainGridLayout &layout, uint32_t firstColumn,
                                    uint32_t cellsX, uint32_t firstRow, uint32_t cellsZ)
    {
        CliffGrid grid;
        grid.Layout = layout;
        grid.Regions = kNearRegions;
        grid.RegionCount = sizeof(kNearRegions) / sizeof(kNearRegions[0]);
        return GridTileIntersects(grid, firstColumn, cellsX, firstRow, cellsZ, kWindowMargin);
    }

    bool TerrainCliffCarveTouches(const Core::Rendering::Terrain::TerrainGridLayout &layout, uint32_t firstColumn,
                                  uint32_t cellsX, uint32_t firstRow, uint32_t cellsZ)
    {
        // 法線の差分と細分の周りの升目と、割れ目と滑らかな面を戻す幅の分だけ余白を足す
        const double margin = static_cast<double>(kWindowMargin + 2) * layout.Spacing + kBattleshipCarveFadeMeters;
        const CarveBounds &bounds = BattleshipCarveBounds();
        const double minX = layout.FirstX + static_cast<double>(firstColumn) * layout.Spacing;
        const double maxX = layout.FirstX + static_cast<double>(firstColumn + cellsX) * layout.Spacing;
        const double minZ = layout.FirstZ + static_cast<double>(firstRow) * layout.Spacing;
        const double maxZ = layout.FirstZ + static_cast<double>(firstRow + cellsZ) * layout.Spacing;
        return maxX >= bounds.MinX - margin && minX <= bounds.MaxX + margin && maxZ >= bounds.MinZ - margin &&
               minZ <= bounds.MaxZ + margin;
    }

    bool BuildTerrainCliffTile(const Core::Rendering::Terrain::TerrainHeightField &field, uint32_t firstColumn,
                               uint32_t cellsX, uint32_t firstRow, uint32_t cellsZ, float textureTileMeters,
                               VariableArray<TerrainCliffVertex> &outVertices, VariableArray<uint32_t> &outIndices,
                               TerrainCliffTileStats &outStats, AnsiString &outError)
    {
        VariableArray<uint8_t> changedCells;
        return BuildCliffCells(MakeNearGrid(field), firstColumn, cellsX, firstRow, cellsZ, textureTileMeters, outVertices,
                               outIndices, changedCells, outStats, outError);
    }

    bool SampleTerrainCliffSurface(const Core::Rendering::Terrain::TerrainHeightField &field, double x, double z,
                                   TerrainCliffSurfaceSample &outSample)
    {
        const CliffGrid grid = MakeNearGrid(field);
        const Terrain::TerrainGridLayout &layout = grid.Layout;
        const double gx = (x - layout.FirstX) / layout.Spacing;
        const double gz = (z - layout.FirstZ) / layout.Spacing;
        if (!(gx >= 0.0) || !(gz >= 0.0) || gx > static_cast<double>(layout.Columns - 1) ||
            gz > static_cast<double>(layout.Rows - 1))
        {
            return false;
        }
        // 点を含む 1 升目を区画として面を作る（細分の印は窓の余白の分まで求まるので、広い区画で焼いたときと同じ）
        const uint32_t column = std::min(static_cast<uint32_t>(gx), layout.Columns - 2);
        const uint32_t row = std::min(static_cast<uint32_t>(gz), layout.Rows - 2);
        const CliffSurface surface(grid, column, 1u, row, 1u);
        outSample.CarveAmount = BattleshipCarveAmount(x, z);
        outSample.BaseHeight = surface.BaseHeight(gx, gz);
        surface.Position(gx, gz, outSample.Position, outSample.bDisplaced);
        return true;
    }

    void TerrainCliffCarveExtent(double &outMinX, double &outMaxX, double &outMinZ, double &outMaxZ)
    {
        const CarveBounds &bounds = BattleshipCarveBounds();
        outMinX = bounds.MinX - kBattleshipCarveFadeMeters;
        outMaxX = bounds.MaxX + kBattleshipCarveFadeMeters;
        outMinZ = bounds.MinZ - kBattleshipCarveFadeMeters;
        outMaxZ = bounds.MaxZ + kBattleshipCarveFadeMeters;
    }

    bool TerrainCliffHorizonTileIntersects(const TerrainCliffHorizonGrid &grid, uint32_t firstColumn, uint32_t cellsX,
                                           uint32_t firstRow, uint32_t cellsZ)
    {
        return GridTileIntersects(MakeHorizonGrid(grid), firstColumn, cellsX, firstRow, cellsZ, kWindowMargin);
    }

    bool BuildTerrainCliffHorizonTile(const TerrainCliffHorizonGrid &grid, uint32_t firstColumn, uint32_t cellsX,
                                      uint32_t firstRow, uint32_t cellsZ, float textureTileMeters,
                                      VariableArray<TerrainCliffVertex> &inOutVertices, VariableArray<uint32_t> &inOutIndices,
                                      TerrainCliffTileStats &outStats, AnsiString &outError)
    {
        const uint32_t sideX = cellsX + 1;
        const uint32_t gridVertexCount = sideX * (cellsZ + 1);
        VariableArray<uint32_t> cellIndices;
        VariableArray<uint8_t> changedCells;
        if (!BuildCliffCells(MakeHorizonGrid(grid), firstColumn, cellsX, firstRow, cellsZ, textureTileMeters, inOutVertices,
                             cellIndices, changedCells, outStats, outError))
        {
            return false;
        }

        // 元の三角形のうち、張り直した升目のものを除き、張り直した三角形を足す。元の三角形の升目は 3 つの頂点の格子の
        // 番号の小さい方
        VariableArray<uint32_t> indices;
        indices.reserve(inOutIndices.size() + cellIndices.size());
        for (size_t triangle = 0; triangle + 2 < inOutIndices.size(); triangle += 3)
        {
            uint32_t column = std::numeric_limits<uint32_t>::max();
            uint32_t row = std::numeric_limits<uint32_t>::max();
            for (size_t corner = 0; corner < 3; ++corner)
            {
                const uint32_t index = inOutIndices[triangle + corner];
                if (index >= gridVertexCount)
                {
                    outError = "遠景の段の区画の三角形が、格子の頂点の外を指しています";
                    return false;
                }
                column = std::min(column, index % sideX);
                row = std::min(row, index / sideX);
            }
            if (column < cellsX && row < cellsZ && changedCells[static_cast<size_t>(row) * cellsX + column] != 0u)
            {
                continue;
            }
            indices.push_back(inOutIndices[triangle]);
            indices.push_back(inOutIndices[triangle + 1]);
            indices.push_back(inOutIndices[triangle + 2]);
        }
        for (const uint32_t index : cellIndices)
        {
            indices.push_back(index);
        }

        // 使われなくなった頂点（押し出した格子の点の元の頂点）を除いて詰める
        VariableArray<uint32_t> remap(inOutVertices.size(), std::numeric_limits<uint32_t>::max());
        VariableArray<TerrainCliffVertex> vertices;
        vertices.reserve(inOutVertices.size());
        for (uint32_t &index : indices)
        {
            if (remap[index] == std::numeric_limits<uint32_t>::max())
            {
                remap[index] = static_cast<uint32_t>(vertices.size());
                vertices.push_back(inOutVertices[index]);
            }
            index = remap[index];
        }
        inOutVertices = std::move(vertices);
        inOutIndices = std::move(indices);
        return true;
    }
} // namespace NorvesLib::Tools::AssetCook
