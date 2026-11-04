#include "Core/Public/Rendering/Terrain/TerrainHeightField.h"
#include "Rendering/Terrain/TerrainHeightFieldGrid.h"

#include "Asset/AssetFileReader.h"
#include "Asset/CookedTerrainFormat.h"
#include "Text/JsonDocument.h"

#include "stb_image.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>
#include <stdexcept>

namespace NorvesLib::Core::Rendering::Terrain
{

    namespace
    {
        constexpr float kPi = 3.14159265358979323846f;

        /// 整数のハッシュ（seed と格子点の番号だけで決まる）
        uint32_t Hash(uint32_t value)
        {
            value ^= value >> 16;
            value *= 0x7feb352du;
            value ^= value >> 15;
            value *= 0x846ca68bu;
            value ^= value >> 16;
            return value;
        }

        uint32_t Hash2(int32_t x, int32_t z, uint32_t seed)
        {
            return Hash(static_cast<uint32_t>(x) * 0x8da6b343u ^ Hash(static_cast<uint32_t>(z) * 0xd8163841u ^ seed));
        }

        /// [0,1) の一様な値
        float UnitFloat(uint32_t hash)
        {
            return static_cast<float>(hash & 0xFFFFFFu) / 16777216.0f;
        }

        float Smoothstep(float edge0, float edge1, float value)
        {
            const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }

        float Lerp(float a, float b, float t)
        {
            return a + (b - a) * t;
        }

        /// 2 次元の勾配ノイズ（値はおよそ [-1,1]）。格子点の勾配を seed で決め、5 次の補間でつなぐ
        float GradientNoise(float x, float z, uint32_t seed)
        {
            const float fx = std::floor(x);
            const float fz = std::floor(z);
            const int32_t ix = static_cast<int32_t>(fx);
            const int32_t iz = static_cast<int32_t>(fz);
            const float tx = x - fx;
            const float tz = z - fz;

            const auto corner = [seed](int32_t cx, int32_t cz, float dx, float dz)
            {
                const float angle = UnitFloat(Hash2(cx, cz, seed)) * 2.0f * kPi;
                return std::cos(angle) * dx + std::sin(angle) * dz;
            };
            const float n00 = corner(ix, iz, tx, tz);
            const float n10 = corner(ix + 1, iz, tx - 1.0f, tz);
            const float n01 = corner(ix, iz + 1, tx, tz - 1.0f);
            const float n11 = corner(ix + 1, iz + 1, tx - 1.0f, tz - 1.0f);
            const float ux = tx * tx * tx * (tx * (tx * 6.0f - 15.0f) + 10.0f);
            const float uz = tz * tz * tz * (tz * (tz * 6.0f - 15.0f) + 10.0f);
            // 勾配ノイズの値はおよそ ±0.7 に収まるので、±1 に広げる
            return Lerp(Lerp(n00, n10, ux), Lerp(n01, n11, ux), uz) * 1.4f;
        }

        /// 波長 wavelength[m] から始めて、オクターブごとに波長を半分・振幅を gain 倍にして重ねる（値はおよそ [-1,1]）
        float Fbm(float x, float z, float wavelength, uint32_t octaves, float gain, uint32_t seed)
        {
            float sum = 0.0f;
            float amplitude = 1.0f;
            float norm = 0.0f;
            float frequency = 1.0f / wavelength;
            for (uint32_t octave = 0; octave < octaves; ++octave)
            {
                sum += amplitude * GradientNoise(x * frequency, z * frequency, seed + octave * 0x9e3779b9u);
                norm += amplitude;
                amplitude *= gain;
                frequency *= 2.0f;
            }
            return sum / norm;
        }

        /// 尾根のノイズ（値は [0,1]、1 が尾根の線）。|ノイズ| を反転して、細い尾根の線を作る
        float RidgedFbm(float x, float z, float wavelength, uint32_t octaves, uint32_t seed)
        {
            float sum = 0.0f;
            float amplitude = 1.0f;
            float norm = 0.0f;
            float frequency = 1.0f / wavelength;
            float weight = 1.0f;
            for (uint32_t octave = 0; octave < octaves; ++octave)
            {
                float ridge = 1.0f - std::fabs(GradientNoise(x * frequency, z * frequency, seed + octave * 0x7f4a7c15u));
                ridge = std::clamp(ridge, 0.0f, 1.0f);
                ridge *= ridge;
                // 高い所ほど細かい凹凸を残す（尾根の上が荒れ、谷は滑らかになる）
                ridge *= weight;
                weight = std::clamp(ridge * 1.5f, 0.0f, 1.0f);
                sum += amplitude * ridge;
                norm += amplitude;
                amplitude *= 0.5f;
                frequency *= 2.0f;
            }
            return sum / norm;
        }
    } // namespace

    TerrainHeightField::TerrainHeightField(const TerrainHeightFieldParams &params)
        : m_Params(params)
    {
        const uint32_t seed = Hash(params.Seed ^ 0x51ed270bu);

        const float duneAngle = UnitFloat(Hash(seed ^ 0x2c1b3c6du)) * 2.0f * kPi;
        m_DuneDirX = std::cos(duneAngle);
        m_DuneDirZ = std::sin(duneAngle);

        // メサは輪の上に、角度をほぼ等間隔（±25% ずらす）に並べる。大きさ・縦横比・高さ・崖の幅・頂の傾き・段は種で決め、
        // 同じ高さの同じ形の台地が並ばないよう、値の幅を広く取る
        const uint32_t mainCount = std::min(params.MesaCount, kMaxMesas);
        const float angleStep = mainCount > 0 ? 2.0f * kPi / static_cast<float>(mainCount) : 0.0f;
        const float angleOffset = UnitFloat(Hash(seed ^ 0x6a09e667u)) * 2.0f * kPi;
        m_MesaCount = 0;
        for (uint32_t index = 0; index < mainCount; ++index)
        {
            const uint32_t h = Hash(seed + index * 0x9e3779b9u);
            Mesa &mesa = m_Mesas[m_MesaCount++];
            const float angle =
                angleOffset + angleStep * (static_cast<float>(index) + (UnitFloat(Hash(h ^ 0x1u)) - 0.5f) * 0.5f);
            const float ring = Lerp(params.MesaRingInner, params.MesaRingOuter, UnitFloat(Hash(h ^ 0x2u)));
            mesa.CenterX = std::cos(angle) * ring;
            mesa.CenterZ = std::sin(angle) * ring;
            mesa.Radius = Lerp(35.0f, 115.0f, UnitFloat(Hash(h ^ 0x3u)));
            mesa.Aspect = Lerp(1.0f, 2.4f, UnitFloat(Hash(h ^ 0x4u)));
            mesa.Angle = UnitFloat(Hash(h ^ 0x5u)) * kPi;
            mesa.Height = Lerp(22.0f, 78.0f, UnitFloat(Hash(h ^ 0x6u)));
            mesa.CliffWidth = Lerp(4.0f, 11.0f, UnitFloat(Hash(h ^ 0x7u)));
            mesa.EdgeRoughness = Lerp(0.2f, 0.38f, UnitFloat(Hash(h ^ 0x8u)));
            mesa.TopTilt = (UnitFloat(Hash(h ^ 0x9u)) - 0.5f) * 0.12f;
            mesa.BenchFraction[0] = Lerp(0.3f, 0.5f, UnitFloat(Hash(h ^ 0xau)));
            mesa.BenchFraction[1] = Lerp(0.6f, 0.8f, UnitFloat(Hash(h ^ 0xbu)));
            mesa.BenchWidth[0] = Lerp(10.0f, 30.0f, UnitFloat(Hash(h ^ 0xcu)));
            mesa.BenchWidth[1] = Lerp(6.0f, 18.0f, UnitFloat(Hash(h ^ 0xdu)));
            mesa.TalusFraction = Lerp(0.22f, 0.4f, UnitFloat(Hash(h ^ 0xeu)));
            mesa.TalusWidth = Lerp(45.0f, 85.0f, UnitFloat(Hash(h ^ 0xfu)));
            for (uint32_t lobe = 0; lobe < 2; ++lobe)
            {
                // 副の楕円は主の縁の近く（内側に埋もれない位置）に置く。向きの符号も種で決める
                const uint32_t lh = Hash(h ^ (0x10u + lobe));
                const float alongSign = (Hash(lh ^ 0x5u) & 1u) != 0u ? 1.0f : -1.0f;
                const float acrossSign = (Hash(lh ^ 0x6u) & 1u) != 0u ? 1.0f : -1.0f;
                mesa.LobeAlong[lobe] =
                    alongSign * mesa.Radius * mesa.Aspect * Lerp(0.5f, 1.0f, UnitFloat(Hash(lh ^ 0x1u)));
                mesa.LobeAcross[lobe] = acrossSign * mesa.Radius * Lerp(0.4f, 0.95f, UnitFloat(Hash(lh ^ 0x2u)));
                mesa.LobeScale[lobe] = Lerp(0.4f, 0.75f, UnitFloat(Hash(lh ^ 0x3u)));
                mesa.LobeAspect[lobe] = Lerp(1.0f, 1.8f, UnitFloat(Hash(lh ^ 0x4u)));
            }
        }

        // メサの 6 割の脇に、小さなビュート（崖で囲まれた細い塔状の丘）を 1 つ立てる。親のメサの外側の縁から
        // 25〜70 m 離し、高さは親の 4〜9 割にする
        const uint32_t parentCount = m_MesaCount;
        for (uint32_t index = 0; index < parentCount && m_MesaCount < kMaxMesas; ++index)
        {
            const uint32_t h = Hash(seed + index * 0x9e3779b9u + 0x632be5abu);
            if (UnitFloat(Hash(h ^ 0x1u)) > 0.6f)
            {
                continue;
            }
            const Mesa &parent = m_Mesas[index];
            Mesa &butte = m_Mesas[m_MesaCount++];
            const float angle = UnitFloat(Hash(h ^ 0x2u)) * 2.0f * kPi;
            const float reach = parent.Radius * parent.Aspect + Lerp(25.0f, 70.0f, UnitFloat(Hash(h ^ 0x3u)));
            butte.CenterX = parent.CenterX + std::cos(angle) * reach;
            butte.CenterZ = parent.CenterZ + std::sin(angle) * reach;
            butte.Radius = Lerp(10.0f, 26.0f, UnitFloat(Hash(h ^ 0x4u)));
            butte.Aspect = Lerp(1.0f, 1.6f, UnitFloat(Hash(h ^ 0x5u)));
            butte.Angle = UnitFloat(Hash(h ^ 0x6u)) * kPi;
            butte.Height = parent.Height * Lerp(0.4f, 0.9f, UnitFloat(Hash(h ^ 0x7u)));
            butte.CliffWidth = Lerp(3.0f, 6.0f, UnitFloat(Hash(h ^ 0x8u)));
            butte.EdgeRoughness = Lerp(0.15f, 0.3f, UnitFloat(Hash(h ^ 0x9u)));
            butte.TopTilt = (UnitFloat(Hash(h ^ 0xau)) - 0.5f) * 0.16f;
            butte.BenchFraction[0] = Lerp(0.35f, 0.55f, UnitFloat(Hash(h ^ 0xbu)));
            butte.BenchFraction[1] = 0.0f;
            butte.BenchWidth[0] = Lerp(4.0f, 10.0f, UnitFloat(Hash(h ^ 0xcu)));
            butte.BenchWidth[1] = 0.0f;
            butte.TalusFraction = Lerp(0.25f, 0.45f, UnitFloat(Hash(h ^ 0xdu)));
            butte.TalusWidth = Lerp(18.0f, 35.0f, UnitFloat(Hash(h ^ 0xeu)));
        }
    }

    float TerrainHeightField::SampleMesas(float x, float z) const
    {
        const uint32_t seed = m_Params.Seed * 0x85ebca6bu + 0x3u;
        float result = 0.0f;
        for (uint32_t index = 0; index < m_MesaCount; ++index)
        {
            const Mesa &mesa = m_Mesas[index];
            const float dx = x - mesa.CenterX;
            const float dz = z - mesa.CenterZ;
            // 楕円の長軸を Angle の向きに取る
            const float c = std::cos(mesa.Angle);
            const float s = std::sin(mesa.Angle);
            const float along = dx * c + dz * s;
            const float across = -dx * s + dz * c;
            // 主の楕円と副の楕円の和: 半径で割った距離の小さい方を、主の半径の尺度へ戻す
            const float mainAlong = along / mesa.Aspect;
            float normalized = std::sqrt(mainAlong * mainAlong + across * across) / mesa.Radius;
            for (uint32_t lobe = 0; lobe < 2; ++lobe)
            {
                if (mesa.LobeScale[lobe] <= 0.0f)
                {
                    continue;
                }
                const float la = (along - mesa.LobeAlong[lobe]) / mesa.LobeAspect[lobe];
                const float lc = across - mesa.LobeAcross[lobe];
                normalized = std::min(normalized, std::sqrt(la * la + lc * lc) / (mesa.Radius * mesa.LobeScale[lobe]));
            }
            const float distance = normalized * mesa.Radius;
            // 輪郭の崩れ・切れ込み・段・崖錐の届く最も外（これより遠ければ、このメサは高さに効かない）
            const float reach = mesa.Radius * (1.0f + mesa.EdgeRoughness) + 3.0f + mesa.BenchWidth[0] +
                                mesa.BenchWidth[1] + mesa.TalusWidth;
            if (distance > reach)
            {
                continue;
            }
            // 輪郭: 大きな崩れ（波長 60 m）で円でなくし、細かな凹凸（波長 14 m）で縁を欠けさせる
            float edge = Fbm(x, z, 60.0f, 3, 0.5f, seed + index) * mesa.Radius * mesa.EdgeRoughness +
                         Fbm(x, z, 14.0f, 2, 0.5f, seed + 31u + index) * 2.5f;
            // 浸食の切れ込み: 尾根のノイズの線が縁を横切る所で縁を内側へ削る（崖に深い溝と、その間の岬ができる）
            const float gully = RidgedFbm(x, z, 38.0f, 2, seed + 47u + index);
            edge += gully * gully * gully * gully * std::min(14.0f, mesa.Radius * 0.3f);
            const float d = distance + edge;

            // 頂: 傾き（長軸と直交する向き）と浅い起伏。縁の手前 25 m で肩を少し落とす
            const float cliffWidth = mesa.CliffWidth;
            const float top = mesa.Height + mesa.TopTilt * across +
                              0.05f * mesa.Height * Fbm(x, z, 40.0f, 2, 0.5f, seed + 17u + index) -
                              0.04f * mesa.Height * Smoothstep(mesa.Radius - 25.0f, mesa.Radius - cliffWidth, d);
            float height = (1.0f - Smoothstep(mesa.Radius - cliffWidth, mesa.Radius, d)) * top;

            // 崖の途中の段: 上の段（高い方）から順に外へ張り出す。縁のうちノイズが正の所だけに付け、段の無い所は
            // 1 枚の崖のまま。段の高さと張り出しは同じ重みで 0 へ近づける（段の端で崖が折れ曲がらない）
            float outerEdge = mesa.Radius;
            for (uint32_t bench = 2; bench-- > 0;)
            {
                if (mesa.BenchWidth[bench] <= 0.0f)
                {
                    continue;
                }
                const float gate =
                    Smoothstep(0.0f, 0.3f, Fbm(x, z, 110.0f, 2, 0.5f, seed + 61u + bench * 7u + index * 13u));
                if (gate <= 0.0f)
                {
                    continue;
                }
                outerEdge += mesa.BenchWidth[bench] * gate;
                const float benchCliff = cliffWidth * 0.8f;
                const float benchHeight = mesa.Height * mesa.BenchFraction[bench] * gate *
                                          (1.0f + 0.04f * Fbm(x, z, 30.0f, 2, 0.5f, seed + 83u + bench + index));
                height = std::max(height, (1.0f - Smoothstep(outerEdge - benchCliff, outerEdge, d)) * benchHeight);
            }

            // 崖の下の崖錐（いちばん外の崖の足元から、なだらかな裾）
            const float talusT = 1.0f - Smoothstep(outerEdge - 4.0f, outerEdge + mesa.TalusWidth, d);
            height = std::max(height, mesa.TalusFraction * talusT * talusT * mesa.Height);
            result = std::max(result, height);
        }
        return result;
    }

    float TerrainHeightField::SampleHeight(float x, float z) const
    {
        if (m_Grid)
        {
            return SampleGridHeight(x, z);
        }

        const TerrainHeightFieldParams &p = m_Params;
        const uint32_t seed = p.Seed;
        const float radius = std::sqrt(x * x + z * z);

        // なだらかな起伏（数百 m の波長）
        const float rolling = Fbm(x, z, 420.0f, 4, 0.5f, seed ^ 0xa511e9b3u) * p.RollingAmplitude;

        // 砂丘: 進む向きに沿った非対称の波（風上はなだらか、風下は急）。波の位相をノイズで曲げて、まっすぐな縞にしない
        const float along = (x * m_DuneDirX + z * m_DuneDirZ) / p.DuneWavelength +
                            Fbm(x, z, 180.0f, 3, 0.5f, seed ^ 0x63d83595u) * 1.1f;
        const float phase = along - std::floor(along);
        const float duneShape = phase < 0.78f ? Smoothstep(0.0f, 0.78f, phase) : 1.0f - Smoothstep(0.78f, 1.0f, phase);
        const float duneStrength = std::clamp(0.55f + 0.75f * Fbm(x, z, 300.0f, 2, 0.5f, seed ^ 0x2b992ddfu), 0.0f, 1.0f);
        // 細かい交差の砂丘（主の砂丘に斜めに重なる小さな波）
        const float cross = Fbm(x, z, 26.0f, 2, 0.45f, seed ^ 0x8ebc6af0u) * 0.8f;
        const float dunes = (duneShape * duneStrength * p.DuneAmplitude + cross) *
                            (1.0f - Smoothstep(p.RidgeStart - 60.0f, p.RidgeCrest, radius));

        // 外周の稜線: 円と正方形の間の距離（角を正方形に寄せる）で立ち上げ、尾根のノイズで峰と鞍部を作る
        const float squareDistance = std::max(std::fabs(x), std::fabs(z));
        const float edgeDistance = Lerp(radius, squareDistance, 0.55f) + Fbm(x, z, 260.0f, 3, 0.5f, seed ^ 0x1f83d9abu) * 70.0f;
        const float ridgeRise = Smoothstep(p.RidgeStart, p.RidgeCrest, edgeDistance);
        const float ridged = RidgedFbm(x, z, 240.0f, 5, seed ^ 0x5be0cd19u);
        const float ridge = ridgeRise * (p.RidgeHeight * (0.55f + 0.45f * ridgeRise) + p.RidgeRoughness * ridged);

        // 周りの地面（起伏と砂丘）の上に稜線を足し、メサは周りの地面から立ち上げる
        // 盆地の周りは外へ向かってゆるく上がり、盆地をくぼみにする
        const float bowl = p.BowlHeight * Smoothstep(p.BasinRadius, p.RidgeStart, radius);
        const float surroundings = bowl + rolling + dunes;
        const float mesas = SampleMesas(x, z);
        const float height = surroundings + std::max(ridge, mesas);

        // 中央の盆地は平ら（高さ 0）。縁から周りの起伏へなめらかに移る
        const float basin = Smoothstep(p.BasinRadius, p.BasinRadius + p.BasinBlend, radius);
        return height * basin;
    }

    TerrainNormal TerrainHeightField::SampleNormal(float x, float z, float sampleSpacing) const
    {
        if (m_Grid)
        {
            return SampleGridNormal(x, z, sampleSpacing);
        }

        const float e = sampleSpacing > 0.0f ? sampleSpacing : 0.5f;
        const float dx = SampleHeight(x + e, z) - SampleHeight(x - e, z);
        const float dz = SampleHeight(x, z + e) - SampleHeight(x, z - e);
        TerrainNormal normal;
        normal.X = -dx;
        normal.Y = 2.0f * e;
        normal.Z = -dz;
        const float length = std::sqrt(normal.X * normal.X + normal.Y * normal.Y + normal.Z * normal.Z);
        normal.X /= length;
        normal.Y /= length;
        normal.Z /= length;
        return normal;
    }

    void TerrainHeightField::GridData::BuildBlockBounds()
    {
        if (Columns < 2u || Rows < 2u)
        {
            BlockColumns = 0;
            BlockRows = 0;
            BlockMinHeight.clear();
            BlockMaxHeight.clear();
            return;
        }

        // 加算で切り上げると頂点数が上限近くのときにあふれるため、先に 1 を引いて割る
        BlockColumns = (Columns - 2u) / kBlockCells + 1u;
        BlockRows = (Rows - 2u) / kBlockCells + 1u;
        const size_t blockCount = static_cast<size_t>(BlockColumns) * BlockRows;
        BlockMinHeight.resize(blockCount);
        BlockMaxHeight.resize(blockCount);
        for (uint32_t bz = 0; bz < BlockRows; ++bz)
        {
            const uint32_t firstRow = bz * kBlockCells;
            const uint32_t lastRow = firstRow + std::min(kBlockCells, Rows - 1u - firstRow);
            for (uint32_t bx = 0; bx < BlockColumns; ++bx)
            {
                const uint32_t firstColumn = bx * kBlockCells;
                const uint32_t lastColumn = firstColumn + std::min(kBlockCells, Columns - 1u - firstColumn);
                float minHeight = At(firstColumn, firstRow);
                float maxHeight = minHeight;
                // 升目の両端の頂点を含むため、隣接ブロックは境界の頂点を共有する
                for (uint32_t row = firstRow; row <= lastRow; ++row)
                {
                    for (uint32_t column = firstColumn; column <= lastColumn; ++column)
                    {
                        const float height = At(column, row);
                        minHeight = std::min(minHeight, height);
                        maxHeight = std::max(maxHeight, height);
                    }
                }
                const size_t index = static_cast<size_t>(bz) * BlockColumns + bx;
                BlockMinHeight[index] = minHeight;
                BlockMaxHeight[index] = maxHeight;
            }
        }
    }

    TerrainNormal TerrainHeightField::SampleNormal(float x, float z, TerrainNormalKind kind) const
    {
        if (!m_Grid)
        {
            return SampleNormal(x, z, 0.5f);
        }
        if (!Contains(x, z))
        {
            return TerrainNormal{kOutOfRangeHeight, kOutOfRangeHeight, kOutOfRangeHeight};
        }

        const GridData &grid = *m_Grid;
        const double u = (static_cast<double>(x) - grid.FirstX) / grid.Spacing;
        const double v = (static_cast<double>(z) - grid.FirstZ) / grid.Spacing;
        const uint32_t column = std::min(static_cast<uint32_t>(u), grid.Columns - 2u);
        const uint32_t row = std::min(static_cast<uint32_t>(v), grid.Rows - 2u);
        const double fu = u - static_cast<double>(column);
        const double fv = v - static_cast<double>(row);
        const bool mainDiagonal = UsesMainDiagonal(column, row);
        const bool firstTriangle = mainDiagonal ? fu >= fv : fu + fv <= 1.0;

        struct NormalComponents
        {
            double X;
            double Y;
            double Z;
        };
        // 高さ差と間隔を先に割らずに正規化し、非常に大きな勾配での中間値のあふれを避ける
        const auto normalize = [](double nx, double ny, double nz) -> NormalComponents
        {
            const double length = std::hypot(nx, ny, nz);
            return {nx / length, std::max(ny / length, std::numeric_limits<double>::min()), nz / length};
        };

        NormalComponents normal{};
        if (kind == TerrainNormalKind::Face)
        {
            const double h00 = grid.At(column, row);
            const double h10 = grid.At(column + 1u, row);
            const double h01 = grid.At(column, row + 1u);
            const double h11 = grid.At(column + 1u, row + 1u);
            double dx = 0.0;
            double dz = 0.0;
            if (mainDiagonal)
            {
                dx = firstTriangle ? h10 - h00 : h11 - h01;
                dz = firstTriangle ? h11 - h10 : h01 - h00;
            }
            else
            {
                dx = firstTriangle ? h10 - h00 : h11 - h01;
                dz = firstTriangle ? h01 - h00 : h11 - h10;
            }
            normal = normalize(-dx, grid.Spacing, -dz);
        }
        else
        {
            // 重みの並びは 00, 10, 01, 11。高さの補間と同じ比較・同じ重心座標を使う
            double weights[4] = {};
            if (mainDiagonal && firstTriangle)
            {
                weights[0] = 1.0 - fu;
                weights[1] = fu - fv;
                weights[3] = fv;
            }
            else if (mainDiagonal)
            {
                weights[0] = 1.0 - fv;
                weights[2] = fv - fu;
                weights[3] = fu;
            }
            else if (firstTriangle)
            {
                weights[0] = 1.0 - fu - fv;
                weights[1] = fu;
                weights[2] = fv;
            }
            else
            {
                weights[1] = 1.0 - fv;
                weights[2] = 1.0 - fu;
                weights[3] = fu + fv - 1.0;
            }

            for (uint32_t index = 0; index < 4u; ++index)
            {
                if (weights[index] == 0.0)
                {
                    continue;
                }
                const uint32_t vertexColumn = column + (index & 1u);
                const uint32_t vertexRow = row + (index >> 1u);
                const uint32_t left = vertexColumn > 0u ? vertexColumn - 1u : vertexColumn;
                const uint32_t right = vertexColumn < grid.Columns - 1u ? vertexColumn + 1u : vertexColumn;
                const uint32_t north = vertexRow > 0u ? vertexRow - 1u : vertexRow;
                const uint32_t south = vertexRow < grid.Rows - 1u ? vertexRow + 1u : vertexRow;
                const double xFactor = left != vertexColumn && right != vertexColumn ? 0.5 : 1.0;
                const double zFactor = north != vertexRow && south != vertexRow ? 0.5 : 1.0;
                const double dx = (static_cast<double>(grid.At(right, vertexRow)) - grid.At(left, vertexRow)) * xFactor;
                const double dz = (static_cast<double>(grid.At(vertexColumn, south)) - grid.At(vertexColumn, north)) * zFactor;
                const NormalComponents vertexNormal = normalize(-dx, grid.Spacing, -dz);
                normal.X += weights[index] * vertexNormal.X;
                normal.Y += weights[index] * vertexNormal.Y;
                normal.Z += weights[index] * vertexNormal.Z;
            }
            normal = normalize(normal.X, normal.Y, normal.Z);
        }

        TerrainNormal result{static_cast<float>(normal.X), static_cast<float>(normal.Y), static_cast<float>(normal.Z)};
        // float に丸めたときも上向き成分を保つ
        if (result.Y == 0.0f)
        {
            result.Y = std::numeric_limits<float>::denorm_min();
        }
        return result;
    }

    namespace
    {
        /// 格子の記述の文字列が ASCII の期待の値と同じか
        bool JsonStringEquals(const JsonValue &value, const char *expected)
        {
            if (!value.IsString())
            {
                return false;
            }
            const Container::String &text = value.AsString();
            const size_t length = std::strlen(expected);
            if (text.size() != length)
            {
                return false;
            }
            for (size_t index = 0; index < length; ++index)
            {
                if (static_cast<uint32_t>(text.data()[index]) != static_cast<uint32_t>(static_cast<unsigned char>(expected[index])))
                {
                    return false;
                }
            }
            return true;
        }

        bool JsonFiniteNumber(const JsonValue &value, double &outValue)
        {
            if (!value.IsNumber() || !std::isfinite(value.AsNumber()))
            {
                return false;
            }
            outValue = value.AsNumber();
            return true;
        }

        /// 格子のファイル名（記述と同じフォルダの、区切り・親への移動を含まない ASCII の名前）を取り出す
        bool JsonGridFileName(const JsonValue &value, Container::AnsiString &outName)
        {
            if (!value.IsString())
            {
                return false;
            }
            const Container::String &text = value.AsString();
            if (text.size() == 0 || text.size() > 255)
            {
                return false;
            }
            outName.clear();
            for (size_t index = 0; index < text.size(); ++index)
            {
                const uint32_t ch = static_cast<uint32_t>(text.data()[index]);
                const bool bAllowed = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
                                      ch == '_' || ch == '-' || ch == '.';
                if (!bAllowed)
                {
                    return false;
                }
                outName += static_cast<char>(ch);
            }
            return outName != "." && outName != "..";
        }

        Container::AnsiString MakeGridError(const char *reason, Container::AnsiStringView path)
        {
            Container::AnsiString error(reason);
            error += "（";
            error.append(path.data(), path.size());
            error += "）";
            return error;
        }
    } // namespace

    namespace
    {
        /// (x,z) を造成の平面へ寄せる重み（0〜1）。範囲ごとに、箱の内側で 1、箱からの距離が Blend に達すると 0 になる
        /// smoothstep を求め、1 − Π(1 − w) でまとめる
        double GradingWeight(const TerrainGrading &grading, double x, double z)
        {
            double keep = 1.0;
            for (const TerrainGradePad &pad : grading.Pads)
            {
                const double dx = std::max(std::fabs(x - pad.CenterX) - pad.HalfX, 0.0);
                const double dz = std::max(std::fabs(z - pad.CenterZ) - pad.HalfZ, 0.0);
                const double t = std::min(std::sqrt(dx * dx + dz * dz) / pad.Blend, 1.0);
                const double weight = 1.0 - t * t * (3.0 - 2.0 * t);
                keep *= 1.0 - weight;
            }
            return 1.0 - keep;
        }
    } // namespace

    bool TerrainHeightField::ApplyGrading(const TerrainGrading &grading, GridData &grid)
    {
        if (!std::isfinite(grading.OriginX) || !std::isfinite(grading.OriginZ) || !std::isfinite(grading.Height) ||
            !std::isfinite(grading.SlopeX) || !std::isfinite(grading.SlopeZ))
        {
            return false;
        }
        if (grading.Pads.size() == 0)
        {
            return true;
        }
        // 造成が届く範囲（箱 + ぼかしの幅）を囲む頂点の矩形
        double minX = std::numeric_limits<double>::max();
        double maxX = -std::numeric_limits<double>::max();
        double minZ = std::numeric_limits<double>::max();
        double maxZ = -std::numeric_limits<double>::max();
        for (const TerrainGradePad &pad : grading.Pads)
        {
            if (!std::isfinite(pad.CenterX) || !std::isfinite(pad.CenterZ) || !std::isfinite(pad.HalfX) ||
                !std::isfinite(pad.HalfZ) || !std::isfinite(pad.Blend) || pad.HalfX < 0.0f || pad.HalfZ < 0.0f ||
                pad.Blend <= 0.0f)
            {
                return false;
            }
            minX = std::min(minX, static_cast<double>(pad.CenterX) - pad.HalfX - pad.Blend);
            maxX = std::max(maxX, static_cast<double>(pad.CenterX) + pad.HalfX + pad.Blend);
            minZ = std::min(minZ, static_cast<double>(pad.CenterZ) - pad.HalfZ - pad.Blend);
            maxZ = std::max(maxZ, static_cast<double>(pad.CenterZ) + pad.HalfZ + pad.Blend);
        }
        const double lastColumn = static_cast<double>(grid.Columns - 1);
        const double lastRow = static_cast<double>(grid.Rows - 1);
        const double firstColumn = std::clamp(std::floor((minX - grid.FirstX) / grid.Spacing), 0.0, lastColumn);
        const double endColumn = std::clamp(std::ceil((maxX - grid.FirstX) / grid.Spacing), 0.0, lastColumn);
        const double firstRow = std::clamp(std::floor((minZ - grid.FirstZ) / grid.Spacing), 0.0, lastRow);
        const double endRow = std::clamp(std::ceil((maxZ - grid.FirstZ) / grid.Spacing), 0.0, lastRow);
        if (endColumn < firstColumn || endRow < firstRow)
        {
            return true;
        }

        const uint32_t columns = static_cast<uint32_t>(endColumn - firstColumn) + 1u;
        const uint32_t rows = static_cast<uint32_t>(endRow - firstRow) + 1u;
        grid.GradedHeights.resize(static_cast<size_t>(columns) * rows);
        for (uint32_t row = 0; row < rows; ++row)
        {
            for (uint32_t column = 0; column < columns; ++column)
            {
                const uint32_t globalColumn = static_cast<uint32_t>(firstColumn) + column;
                const uint32_t globalRow = static_cast<uint32_t>(firstRow) + row;
                const double x = grid.FirstX + static_cast<double>(globalColumn) * grid.Spacing;
                const double z = grid.FirstZ + static_cast<double>(globalRow) * grid.Spacing;
                const double height = grid.FileAt(globalColumn, globalRow);
                const double plane = grading.Height + grading.SlopeX * (x - grading.OriginX) +
                                     grading.SlopeZ * (z - grading.OriginZ);
                const double weight = GradingWeight(grading, x, z);
                grid.GradedHeights[static_cast<size_t>(row) * columns + column] =
                    static_cast<float>(height + weight * (plane - height));
            }
        }
        grid.GradedFirstColumn = static_cast<uint32_t>(firstColumn);
        grid.GradedFirstRow = static_cast<uint32_t>(firstRow);
        grid.GradedColumns = columns;
        grid.GradedRows = rows;
        return true;
    }

    TerrainHeightField::TerrainHeightField(GridTag, Container::TSharedPtr<const GridData> grid)
        : m_Grid(std::move(grid))
    {
    }

    Container::TSharedPtr<const TerrainHeightField> TerrainHeightField::LoadGrid(Container::AnsiStringView descriptorPath,
                                                                                Container::AnsiString &outError,
                                                                                const TerrainGrading *grading)
    {
        constexpr uint64_t kMaxDescriptorBytes = 1024u * 1024u;
        constexpr int64_t kMaxGridSide = 65536;

        Asset::AssetFileReader reader;
        Asset::AssetReadRequest descriptorRequest;
        descriptorRequest.InputPath.append(descriptorPath.data(), descriptorPath.size());
        descriptorRequest.bAllowAbsolutePath = false;
        descriptorRequest.MaxReadBytes = kMaxDescriptorBytes;
        const Asset::AssetReadResult descriptor = reader.Read(descriptorRequest);
        if (!descriptor.Succeeded())
        {
            outError = MakeGridError("地形の格子の記述を読めません", descriptorPath);
            return {};
        }

        Container::Span<const uint8_t> text(descriptor.Blob.GetData(), descriptor.Blob.GetSize());
        if (text.size() >= 3 && text[0] == 0xefu && text[1] == 0xbbu && text[2] == 0xbfu)
        {
            text = {text.data() + 3, text.size() - 3};
        }
        JsonDocument document;
        if (!JsonDocument::TryParseUtf8(text, document))
        {
            outError = MakeGridError("地形の格子の記述が JSON として読めません", descriptorPath);
            return {};
        }

        // 補間と座標の対応が前提にしている約束（形式・行の向き・軸の向き）を確かめる
        const JsonValue root = document.GetRoot();
        const JsonValue axes = root.FindMember("axes");
        if (!JsonStringEquals(root.FindMember("format"), "float32 little-endian, row-major, no header") ||
            !JsonStringEquals(root.FindMember("row_order"), "north_to_south") || !axes.IsObject() ||
            !JsonStringEquals(axes.FindMember("engine_x"), "east") || !JsonStringEquals(axes.FindMember("engine_y"), "up") ||
            !JsonStringEquals(axes.FindMember("engine_z"), "south"))
        {
            outError = MakeGridError("地形の格子の記述の形式・行の向き・軸の向きが約束と違います", descriptorPath);
            return {};
        }

        const JsonValue width = root.FindMember("width");
        const JsonValue height = root.FindMember("height");
        if (!width.IsIntegerLiteral() || !height.IsIntegerLiteral() || width.AsInt64() < 2 || height.AsInt64() < 2 ||
            width.AsInt64() > kMaxGridSide || height.AsInt64() > kMaxGridSide)
        {
            outError = MakeGridError("地形の格子の大きさが不正です", descriptorPath);
            return {};
        }

        double spacing = 0.0;
        double firstX = 0.0;
        double firstZ = 0.0;
        const JsonValue firstVertex = root.FindMember("first_vertex_engine");
        if (!JsonFiniteNumber(root.FindMember("vertex_spacing_m"), spacing) || spacing <= 0.0 || !firstVertex.IsObject() ||
            !JsonFiniteNumber(firstVertex.FindMember("x"), firstX) || !JsonFiniteNumber(firstVertex.FindMember("z"), firstZ))
        {
            outError = MakeGridError("地形の格子の間隔・最初の頂点の位置が不正です", descriptorPath);
            return {};
        }

        auto grid = Container::MakeShared<GridData>();
        grid->Columns = static_cast<uint32_t>(width.AsInt64());
        grid->Rows = static_cast<uint32_t>(height.AsInt64());
        grid->FirstX = firstX;
        grid->FirstZ = firstZ;
        grid->Spacing = spacing;

        // 範囲の記述がある場合は、最初の頂点・間隔・大きさから決まる範囲と同じか確かめる
        const JsonValue bounds = root.FindMember("engine_bounds");
        if (bounds.IsValid())
        {
            double xMin = 0.0;
            double xMax = 0.0;
            double zMin = 0.0;
            double zMax = 0.0;
            const double lastX = firstX + static_cast<double>(grid->Columns - 1) * spacing;
            const double lastZ = firstZ + static_cast<double>(grid->Rows - 1) * spacing;
            constexpr double kTolerance = 1.0e-3;
            if (!bounds.IsObject() || !JsonFiniteNumber(bounds.FindMember("x_min"), xMin) ||
                !JsonFiniteNumber(bounds.FindMember("x_max"), xMax) || !JsonFiniteNumber(bounds.FindMember("z_min"), zMin) ||
                !JsonFiniteNumber(bounds.FindMember("z_max"), zMax) || std::fabs(xMin - firstX) > kTolerance ||
                std::fabs(xMax - lastX) > kTolerance || std::fabs(zMin - firstZ) > kTolerance ||
                std::fabs(zMax - lastZ) > kTolerance)
            {
                outError = MakeGridError("地形の格子の範囲が、最初の頂点・間隔・大きさと合いません", descriptorPath);
                return {};
            }
        }

        // 格子のファイルは記述と同じフォルダにある
        Container::AnsiString fileName;
        if (!JsonGridFileName(root.FindMember("file"), fileName))
        {
            outError = MakeGridError("地形の格子のファイル名が不正です", descriptorPath);
            return {};
        }
        size_t folderLength = 0;
        for (size_t index = 0; index < descriptorPath.size(); ++index)
        {
            if (descriptorPath.data()[index] == '/' || descriptorPath.data()[index] == '\\')
            {
                folderLength = index + 1;
            }
        }
        Asset::AssetReadRequest gridRequest;
        gridRequest.InputPath.append(descriptorPath.data(), folderLength);
        gridRequest.InputPath += fileName;
        gridRequest.bAllowAbsolutePath = false;
        const uint64_t expectedBytes = static_cast<uint64_t>(grid->Columns) * grid->Rows * sizeof(float);
        gridRequest.MaxReadBytes = expectedBytes;
        Asset::AssetReadResult gridFile = reader.Read(gridRequest);
        if (!gridFile.Succeeded() || static_cast<uint64_t>(gridFile.Blob.GetSize()) != expectedBytes)
        {
            outError = MakeGridError("地形の格子のファイルを読めないか、大きさが幅 × 高さ × 4 バイトと違います",
                                     Container::AnsiStringView(gridRequest.InputPath.data(), gridRequest.InputPath.size()));
            return {};
        }
        grid->Bytes = std::move(gridFile.Blob);

        // NaN は範囲の外の印と区別できないので、格子の値はすべて有限でなければならない
        for (uint32_t row = 0; row < grid->Rows; ++row)
        {
            for (uint32_t column = 0; column < grid->Columns; ++column)
            {
                if (!std::isfinite(grid->At(column, row)))
                {
                    outError = MakeGridError("地形の格子に有限でない高さがあります", descriptorPath);
                    return {};
                }
            }
        }

        if (grading != nullptr && !ApplyGrading(*grading, *grid))
        {
            outError = MakeGridError("地形の造成の値が不正です（有限でない値・負の大きさ・0 以下のぼかしの幅）", descriptorPath);
            return {};
        }

        grid->BuildBlockBounds();
        return Container::MakeShared<const TerrainHeightField>(GridTag{}, Container::TSharedPtr<const GridData>(std::move(grid)));
    }

    Container::TSharedPtr<const TerrainHeightField> TerrainHeightField::CreateFromCookedTerrain(
        const Asset::CookedTerrainData &terrain, Container::AnsiString &outError)
    {
        outError.clear();
        if (terrain.CellCountX == 0 || terrain.CellCountZ == 0 || terrain.TileCells == 0 ||
            terrain.CellCountX == UINT32_MAX || terrain.CellCountZ == UINT32_MAX ||
            terrain.CellCountX % terrain.TileCells != 0 || terrain.CellCountZ % terrain.TileCells != 0 ||
            terrain.TileCountX != terrain.CellCountX / terrain.TileCells ||
            terrain.TileCountZ != terrain.CellCountZ / terrain.TileCells ||
            !std::isfinite(terrain.CellSizeM) || terrain.CellSizeM <= 0.0f ||
            !std::isfinite(terrain.OriginX) || !std::isfinite(terrain.OriginY) || !std::isfinite(terrain.OriginZ) ||
            !std::isfinite(terrain.HeightMinM) || !std::isfinite(terrain.HeightMaxM) ||
            terrain.HeightMinM > terrain.HeightMaxM)
        {
            outError = "NVTRN の格子の寸法・原点・高さの写像が不正です";
            return {};
        }

        const uint64_t columns = static_cast<uint64_t>(terrain.CellCountX) + 1;
        const uint64_t rows = static_cast<uint64_t>(terrain.CellCountZ) + 1;
        const uint64_t tileCount = static_cast<uint64_t>(terrain.TileCountX) * terrain.TileCountZ;
        const uint64_t side = static_cast<uint64_t>(terrain.TileCells) + 1;
        if (columns * rows > std::numeric_limits<size_t>::max() / sizeof(float) ||
            side * side > std::numeric_limits<size_t>::max() / sizeof(uint16_t) ||
            tileCount > UINT32_MAX || tileCount > std::numeric_limits<size_t>::max() / sizeof(Asset::AssetBlob))
        {
            outError = "NVTRN から作る格子の大きさが格納上限を超えています";
            return {};
        }
        if (terrain.Tiles.size() != tileCount)
        {
            outError = "NVTRN のタイル数が格子の寸法と合いません";
            return {};
        }
        const int32_t heightLayer = terrain.FindLayer(Asset::CookedTerrainFormatV0::HeightLayerFourCC);
        if (heightLayer < 0 ||
            terrain.Layers[heightLayer].Encoding != Asset::CookedTerrainFormatV0::LayerEncodingRawHeight16 ||
            terrain.Layers[heightLayer].PixelFormat != Asset::CookedTexturePixelFormat::R16UNorm ||
            terrain.Layers[heightLayer].ColorSpace != Asset::CookedTextureColorSpace::Linear)
        {
            outError = "NVTRN に生の uint16 の HGT0 レイヤがありません";
            return {};
        }

        try
        {
            // ファイル内のタイル順によらず座標順に並べ、欠落・重複と全 HGT0 の大きさを先に確かめる。
            Container::VariableArray<Asset::AssetBlob> heightTiles(static_cast<size_t>(tileCount));
            const size_t tileBytes = static_cast<size_t>(side * side) * sizeof(uint16_t);
            for (size_t index = 0; index < terrain.Tiles.size(); ++index)
            {
                const auto &tile = terrain.Tiles[index];
                if (tile.TileX >= terrain.TileCountX || tile.TileZ >= terrain.TileCountZ)
                {
                    outError = "NVTRN のタイル座標が格子の範囲外です";
                    return {};
                }
                auto &blob = heightTiles[static_cast<size_t>(tile.TileZ) * terrain.TileCountX + tile.TileX];
                if (blob.IsValid())
                {
                    outError = "NVTRN のタイル座標が重複し、必要なタイルが欠けています";
                    return {};
                }
                blob = terrain.GetLayerBlob(static_cast<uint32_t>(index), static_cast<uint32_t>(heightLayer));
                if (!blob.IsValid() || blob.GetSize() != tileBytes)
                {
                    outError = "NVTRN のタイルの HGT0 が欠けているか、大きさが (TileCells + 1) × (TileCells + 1) × 2 バイトと違います";
                    return {};
                }
            }

            auto grid = Container::MakeShared<GridData>();
            grid->Columns = static_cast<uint32_t>(columns);
            grid->Rows = static_cast<uint32_t>(rows);
            grid->FirstX = terrain.OriginX;
            grid->FirstZ = terrain.OriginZ;
            grid->Spacing = terrain.CellSizeM;
            auto bytes = Container::MakeShared<Asset::AssetBlob::ByteArray>(static_cast<size_t>(columns * rows) * sizeof(float));
            const auto readSample = [side](const Asset::AssetBlob &blob, uint32_t x, uint32_t z)
            {
                const size_t offset = (static_cast<size_t>(z) * static_cast<size_t>(side) + x) * sizeof(uint16_t);
                const uint8_t *data = blob.GetData() + offset;
                return static_cast<uint16_t>(data[0] | (static_cast<uint16_t>(data[1]) << 8));
            };
            for (uint32_t tz = 0; tz < terrain.TileCountZ; ++tz)
            {
                for (uint32_t tx = 0; tx < terrain.TileCountX; ++tx)
                {
                    const size_t tileIndex = static_cast<size_t>(tz) * terrain.TileCountX + tx;
                    const auto &blob = heightTiles[tileIndex];
                    for (uint32_t z = 0; z <= terrain.TileCells; ++z)
                    {
                        for (uint32_t x = 0; x <= terrain.TileCells; ++x)
                        {
                            const uint16_t value = readSample(blob, x, z);
                            // 復元後の float が同じでも、生の標本が違えば共有辺の約束違反とする。
                            if ((tx > 0 && x == 0 && value != readSample(heightTiles[tileIndex - 1], terrain.TileCells, z)) ||
                                (tz > 0 && z == 0 && value != readSample(heightTiles[tileIndex - terrain.TileCountX], x, terrain.TileCells)))
                            {
                                outError = "NVTRN の隣接タイルの共有辺で HGT0 の標本が一致しません";
                                return {};
                            }
                            // cook と同じ double 補間を float に丸めた高さに、仕様の世界座標の原点を加える。
                            const float height = static_cast<float>(terrain.OriginY + terrain.DecodeHeightM(value));
                            if (!std::isfinite(height))
                            {
                                outError = "NVTRN から復元した世界座標の高さが有限の float に収まりません";
                                return {};
                            }
                            const size_t column = static_cast<size_t>(tx) * terrain.TileCells + x;
                            const size_t row = static_cast<size_t>(tz) * terrain.TileCells + z;
                            const size_t offset = (row * grid->Columns + column) * sizeof(float);
                            uint32_t bits = 0;
                            std::memcpy(&bits, &height, sizeof(float));
                            for (size_t byte = 0; byte < sizeof(float); ++byte)
                            {
                                (*bytes)[offset + byte] = static_cast<uint8_t>(bits >> (byte * 8));
                            }
                        }
                    }
                }
            }
            grid->Bytes = Asset::AssetBlob::FromOwnedBytes(bytes);
            grid->BuildBlockBounds();
            return Container::MakeShared<const TerrainHeightField>(GridTag{}, Container::TSharedPtr<const GridData>(std::move(grid)));
        }
        catch (const std::bad_alloc &)
        {
            outError = "NVTRN から格子を作るメモリを確保できません";
            return {};
        }
        catch (const std::length_error &)
        {
            outError = "NVTRN から作る格子の配列が格納上限を超えています";
            return {};
        }
    }

    bool LoadTerrainHorizonMeshes(Container::AnsiStringView descriptorPath,
                                  Container::VariableArray<TerrainHorizonMesh> &outMeshes, Container::AnsiString &outError)
    {
        constexpr uint64_t kMaxDescriptorBytes = 4u * 1024u * 1024u;
        constexpr size_t kMaxMeshes = 4096u;
        constexpr int64_t kMaxVertices = int64_t{1} << 26;
        // Scripts/Terrain/HorizonDem.py の MESH_FORMAT と同じ文字列
        constexpr const char *kMeshFormat =
            "float32 x8 per vertex (position xyz, normal xyz, uv), then uint32 x3 per triangle, little-endian, no header";

        outMeshes.clear();
        Asset::AssetFileReader reader;
        Asset::AssetReadRequest request;
        request.InputPath.append(descriptorPath.data(), descriptorPath.size());
        request.bAllowAbsolutePath = false;
        request.MaxReadBytes = kMaxDescriptorBytes;
        const Asset::AssetReadResult descriptor = reader.Read(request);
        if (!descriptor.Succeeded())
        {
            outError = MakeGridError("遠景の地形の記述を読めません", descriptorPath);
            return false;
        }
        Container::Span<const uint8_t> text(descriptor.Blob.GetData(), descriptor.Blob.GetSize());
        if (text.size() >= 3 && text[0] == 0xefu && text[1] == 0xbbu && text[2] == 0xbfu)
        {
            text = {text.data() + 3, text.size() - 3};
        }
        JsonDocument document;
        if (!JsonDocument::TryParseUtf8(text, document))
        {
            outError = MakeGridError("遠景の地形の記述が JSON として読めません", descriptorPath);
            return false;
        }
        const JsonValue root = document.GetRoot();
        Container::AnsiString meshFolder;
        if (!JsonStringEquals(root.FindMember("format"), kTerrainHorizonFormat) ||
            !JsonStringEquals(root.FindMember("mesh_format"), kMeshFormat) ||
            !JsonGridFileName(root.FindMember("mesh_folder"), meshFolder))
        {
            outError = MakeGridError("遠景の地形の記述の形式・メッシュの形式・フォルダが約束と違います", descriptorPath);
            return false;
        }
        const JsonValue meshes = root.FindMember("meshes");
        if (!meshes.IsArray() || meshes.GetArraySize() == 0 || meshes.GetArraySize() > kMaxMeshes)
        {
            outError = MakeGridError("遠景の地形の記述にメッシュの並びがありません", descriptorPath);
            return false;
        }

        // メッシュのファイルは記述と同じフォルダの mesh_folder の中にある
        size_t folderLength = 0;
        for (size_t index = 0; index < descriptorPath.size(); ++index)
        {
            if (descriptorPath.data()[index] == '/' || descriptorPath.data()[index] == '\\')
            {
                folderLength = index + 1;
            }
        }
        for (size_t index = 0; index < meshes.GetArraySize(); ++index)
        {
            const JsonValue entry = meshes.GetArrayElement(index);
            TerrainHorizonMesh mesh;
            Container::AnsiString fileName;
            const JsonValue vertices = entry.FindMember("vertices");
            const JsonValue triangles = entry.FindMember("triangles");
            const JsonValue sha256 = entry.FindMember("sha256");
            bool bValid = entry.IsObject() && JsonGridFileName(entry.FindMember("name"), mesh.Name) &&
                          JsonGridFileName(entry.FindMember("file"), fileName) && vertices.IsIntegerLiteral() &&
                          triangles.IsIntegerLiteral() && vertices.AsInt64() >= 3 && vertices.AsInt64() <= kMaxVertices &&
                          triangles.AsInt64() >= 1 && triangles.AsInt64() <= 2 * kMaxVertices && sha256.IsString() &&
                          sha256.AsString().size() == 64;
            for (size_t c = 0; bValid && c < mesh.Name.size(); ++c)
            {
                const char ch = mesh.Name[c];
                bValid = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_';
            }
            for (size_t c = 0; bValid && c < 64; ++c)
            {
                const uint32_t ch = static_cast<uint32_t>(sha256.AsString().data()[c]);
                bValid = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
                mesh.Sha256 += static_cast<char>(ch);
            }
            if (!bValid)
            {
                outMeshes.clear();
                outError = MakeGridError("遠景の地形の記述のメッシュの項目（名前・ファイル・頂点と三角形の数・SHA-256）が不正です",
                                         descriptorPath);
                return false;
            }
            mesh.FilePath.append(descriptorPath.data(), folderLength);
            mesh.FilePath += meshFolder;
            mesh.FilePath += "/";
            mesh.FilePath += fileName;
            mesh.VertexCount = static_cast<uint32_t>(vertices.AsInt64());
            mesh.TriangleCount = static_cast<uint32_t>(triangles.AsInt64());
            outMeshes.push_back(std::move(mesh));
        }
        return true;
    }

    bool TerrainHeightField::Contains(float x, float z) const
    {
        if (!m_Grid)
        {
            return true;
        }
        const GridData &grid = *m_Grid;
        const double u = (static_cast<double>(x) - grid.FirstX) / grid.Spacing;
        const double v = (static_cast<double>(z) - grid.FirstZ) / grid.Spacing;
        // NaN は比較がすべて偽になるので範囲の外になる
        return u >= 0.0 && u <= static_cast<double>(grid.Columns - 1) && v >= 0.0 && v <= static_cast<double>(grid.Rows - 1);
    }

    TerrainGridLayout TerrainHeightField::GetGridLayout() const
    {
        TerrainGridLayout layout;
        if (m_Grid)
        {
            layout.Columns = m_Grid->Columns;
            layout.Rows = m_Grid->Rows;
            layout.FirstX = m_Grid->FirstX;
            layout.FirstZ = m_Grid->FirstZ;
            layout.Spacing = m_Grid->Spacing;
        }
        return layout;
    }

    float TerrainHeightField::GetGridVertexHeight(uint32_t column, uint32_t row) const
    {
        if (!m_Grid || column >= m_Grid->Columns || row >= m_Grid->Rows)
        {
            return kOutOfRangeHeight;
        }
        return m_Grid->At(column, row);
    }

    Container::Span<const uint8_t> TerrainHeightField::GetGridBytes() const
    {
        if (!m_Grid)
        {
            return {};
        }
        return Container::Span<const uint8_t>(m_Grid->Bytes.GetData(), m_Grid->Bytes.GetSize());
    }

    float TerrainHeightField::SampleGridHeight(float x, float z) const
    {
        if (!Contains(x, z))
        {
            return kOutOfRangeHeight;
        }

        const GridData &grid = *m_Grid;
        const double u = (static_cast<double>(x) - grid.FirstX) / grid.Spacing;
        const double v = (static_cast<double>(z) - grid.FirstZ) / grid.Spacing;
        // 外周の頂点（u = Columns - 1 など）は内側の升目の端として引く
        const uint32_t column = std::min(static_cast<uint32_t>(u), grid.Columns - 2);
        const uint32_t row = std::min(static_cast<uint32_t>(v), grid.Rows - 2);
        const double fu = u - static_cast<double>(column);
        const double fv = v - static_cast<double>(row);

        const double h00 = grid.At(column, row);
        const double h10 = grid.At(column + 1, row);
        const double h01 = grid.At(column, row + 1);
        const double h11 = grid.At(column + 1, row + 1);

        // 焼いたメッシュと同じ 2 つの三角形のどちらかの中で、重心座標の重みで線形に補間する。頂点の上では重みが
        // (1, 0, 0) になり、ファイルの値をそのまま返す。重みは非負で和が 1 なので、3 頂点の値の範囲を越えない
        double value = 0.0;
        if (UsesMainDiagonal(column, row))
        {
            // 対角線 (0,0)〜(1,1)。三角形 (00, 10, 11) と (00, 11, 01)
            value = fu >= fv ? (1.0 - fu) * h00 + (fu - fv) * h10 + fv * h11
                             : (1.0 - fv) * h00 + (fv - fu) * h01 + fu * h11;
        }
        else
        {
            // 対角線 (1,0)〜(0,1)。三角形 (00, 10, 01) と (10, 11, 01)
            value = fu + fv <= 1.0 ? (1.0 - fu - fv) * h00 + fu * h10 + fv * h01
                                   : (fu + fv - 1.0) * h11 + (1.0 - fv) * h10 + (1.0 - fu) * h01;
        }
        return static_cast<float>(value);
    }

    TerrainNormal TerrainHeightField::SampleGridNormal(float x, float z, float sampleSpacing) const
    {
        const float center = SampleGridHeight(x, z);
        if (std::isnan(center))
        {
            return TerrainNormal{kOutOfRangeHeight, kOutOfRangeHeight, kOutOfRangeHeight};
        }

        const float e = sampleSpacing > 0.0f ? sampleSpacing : 0.5f;
        // 片側が範囲の外なら、その側は (x,z) の高さで置き換えて片側の差分にする（端を延ばした高さは作らない）
        auto slope = [&](float minusHeight, float plusHeight) -> float
        {
            float span = 2.0f * e;
            if (std::isnan(minusHeight))
            {
                minusHeight = center;
                span -= e;
            }
            if (std::isnan(plusHeight))
            {
                plusHeight = center;
                span -= e;
            }
            return span > 0.0f ? (plusHeight - minusHeight) / span : 0.0f;
        };
        const float slopeX = slope(SampleGridHeight(x - e, z), SampleGridHeight(x + e, z));
        const float slopeZ = slope(SampleGridHeight(x, z - e), SampleGridHeight(x, z + e));

        TerrainNormal normal;
        normal.X = -slopeX;
        normal.Y = 1.0f;
        normal.Z = -slopeZ;
        const float length = std::sqrt(normal.X * normal.X + normal.Y * normal.Y + normal.Z * normal.Z);
        normal.X /= length;
        normal.Y /= length;
        normal.Z /= length;
        return normal;
    }

    namespace
    {
        // 地形の地図の記述（surface.json・far.json）を読む。2 つは同じ並び（format・channels・width・height・texel_size_m・
        // engine_edges）で、チャンネルの名前 channelNames（r・g・b・a）だけが違う。errorLabel はエラー文の先頭の地図の呼び名
        // outFileName が空でなければ、記述の file（記述と同じフォルダの画像の名前）も入れる
        bool LoadTerrainMapLayout(Container::AnsiStringView descriptorPath, const char *const (&channelNames)[4],
                                  const char *errorLabel, TerrainSurfaceMaskLayout &outLayout, Container::AnsiString &outError,
                                  Container::AnsiString *outFileName = nullptr)
        {
            constexpr uint64_t kMaxDescriptorBytes = 1024u * 1024u;
            constexpr int64_t kMaxPixels = 16384;
            // Scripts/Terrain/TerrainMasks.py・FarMacro.py の記述の format と同じ文字列
            constexpr const char *kMapFormat = "PNG RGBA8, row 0 = north (engine z min), column 0 = west (engine x min)";
            const auto fail = [&](const char *reason)
            {
                Container::AnsiString message(errorLabel);
                message += reason;
                outError = MakeGridError(message.c_str(), descriptorPath);
                return false;
            };

            outLayout = TerrainSurfaceMaskLayout();
            Asset::AssetFileReader reader;
            Asset::AssetReadRequest request;
            request.InputPath.append(descriptorPath.data(), descriptorPath.size());
            request.bAllowAbsolutePath = false;
            request.MaxReadBytes = kMaxDescriptorBytes;
            const Asset::AssetReadResult descriptor = reader.Read(request);
            if (!descriptor.Succeeded())
            {
                return fail("の記述を読めません");
            }
            Container::Span<const uint8_t> text(descriptor.Blob.GetData(), descriptor.Blob.GetSize());
            if (text.size() >= 3 && text[0] == 0xefu && text[1] == 0xbbu && text[2] == 0xbfu)
            {
                text = {text.data() + 3, text.size() - 3};
            }
            JsonDocument document;
            if (!JsonDocument::TryParseUtf8(text, document))
            {
                return fail("の記述が JSON として読めません");
            }
            const JsonValue root = document.GetRoot();
            const JsonValue channels = root.FindMember("channels");
            if (!JsonStringEquals(root.FindMember("format"), kMapFormat) || !channels.IsObject() ||
                !JsonStringEquals(channels.FindMember("r"), channelNames[0]) ||
                !JsonStringEquals(channels.FindMember("g"), channelNames[1]) ||
                !JsonStringEquals(channels.FindMember("b"), channelNames[2]) ||
                !JsonStringEquals(channels.FindMember("a"), channelNames[3]))
            {
                return fail("の記述の形式・チャンネルの並びが約束と違います");
            }
            const JsonValue width = root.FindMember("width");
            const JsonValue height = root.FindMember("height");
            const JsonValue edges = root.FindMember("engine_edges");
            double texel = 0.0;
            double minX = 0.0;
            double maxX = 0.0;
            double minZ = 0.0;
            double maxZ = 0.0;
            if (!width.IsIntegerLiteral() || !height.IsIntegerLiteral() || width.AsInt64() < 2 || height.AsInt64() < 2 ||
                width.AsInt64() > kMaxPixels || height.AsInt64() > kMaxPixels ||
                !JsonFiniteNumber(root.FindMember("texel_size_m"), texel) || texel <= 0.0 || !edges.IsObject() ||
                !JsonFiniteNumber(edges.FindMember("x_min"), minX) || !JsonFiniteNumber(edges.FindMember("x_max"), maxX) ||
                !JsonFiniteNumber(edges.FindMember("z_min"), minZ) || !JsonFiniteNumber(edges.FindMember("z_max"), maxZ))
            {
                return fail("の大きさ・画素の広さ・外縁が読めません");
            }
            const double expectedWidth = static_cast<double>(width.AsInt64()) * texel;
            const double expectedDepth = static_cast<double>(height.AsInt64()) * texel;
            if (std::fabs((maxX - minX) - expectedWidth) > 1.0e-3 * texel ||
                std::fabs((maxZ - minZ) - expectedDepth) > 1.0e-3 * texel)
            {
                return fail("の外縁の幅が、列・行の数 × 画素の広さと合いません");
            }
            if (outFileName != nullptr && !JsonGridFileName(root.FindMember("file"), *outFileName))
            {
                return fail("の画像のファイル名が不正です");
            }
            outLayout.MinX = static_cast<float>(minX);
            outLayout.MaxX = static_cast<float>(maxX);
            outLayout.MinZ = static_cast<float>(minZ);
            outLayout.MaxZ = static_cast<float>(maxZ);
            outLayout.Width = static_cast<uint32_t>(width.AsInt64());
            outLayout.Height = static_cast<uint32_t>(height.AsInt64());
            return true;
        }
    } // namespace

    bool LoadTerrainSurfaceMaskLayout(Container::AnsiStringView descriptorPath, TerrainSurfaceMaskLayout &outLayout,
                                      Container::AnsiString &outError)
    {
        static constexpr const char *kChannels[4] = {"rock", "talus", "ground_fine", "ground_gravel"};
        return LoadTerrainMapLayout(descriptorPath, kChannels, "地表の分類の地図", outLayout, outError);
    }

    bool LoadTerrainFarMapLayout(Container::AnsiStringView descriptorPath, TerrainSurfaceMaskLayout &outLayout,
                                 Container::AnsiString &outError)
    {
        static constexpr const char *kChannels[4] = {"normal_x", "normal_z", "rock", "dirt"};
        return LoadTerrainMapLayout(descriptorPath, kChannels, "遠景の地図", outLayout, outError);
    }

    TerrainSurfaceMask::TerrainSurfaceMask(const TerrainSurfaceMaskLayout &layout, Container::VariableArray<uint8_t> &&pixels)
        : m_Layout(layout), m_Pixels(std::move(pixels))
    {
    }

    Container::TSharedPtr<const TerrainSurfaceMask> TerrainSurfaceMask::Load(Container::AnsiStringView descriptorPath,
                                                                            Container::AnsiString &outError)
    {
        constexpr uint64_t kMaxImageBytes = 256u * 1024u * 1024u;
        static constexpr const char *kChannels[4] = {"rock", "talus", "ground_fine", "ground_gravel"};
        TerrainSurfaceMaskLayout layout;
        Container::AnsiString fileName;
        if (!LoadTerrainMapLayout(descriptorPath, kChannels, "地表の分類の地図", layout, outError, &fileName))
        {
            return {};
        }

        // 画像は記述と同じフォルダにある
        size_t folderLength = 0;
        for (size_t index = 0; index < descriptorPath.size(); ++index)
        {
            if (descriptorPath.data()[index] == '/' || descriptorPath.data()[index] == '\\')
            {
                folderLength = index + 1;
            }
        }
        Asset::AssetFileReader reader;
        Asset::AssetReadRequest imageRequest;
        imageRequest.InputPath.append(descriptorPath.data(), folderLength);
        imageRequest.InputPath += fileName;
        imageRequest.bAllowAbsolutePath = false;
        imageRequest.MaxReadBytes = kMaxImageBytes;
        const Asset::AssetReadResult image = reader.Read(imageRequest);
        const Container::AnsiStringView imagePath(imageRequest.InputPath.data(), imageRequest.InputPath.size());
        if (!image.Succeeded() || image.Blob.GetSize() == 0 || image.Blob.GetSize() > static_cast<size_t>(INT32_MAX))
        {
            outError = MakeGridError("地表の分類の地図の画像を読めません", imagePath);
            return {};
        }
        int width = 0;
        int height = 0;
        int channels = 0;
        unsigned char *decoded = stbi_load_from_memory(image.Blob.GetData(), static_cast<int>(image.Blob.GetSize()), &width,
                                                       &height, &channels, 4);
        if (decoded == nullptr)
        {
            outError = MakeGridError("地表の分類の地図の画像を PNG として読めません", imagePath);
            return {};
        }
        if (static_cast<uint32_t>(width) != layout.Width || static_cast<uint32_t>(height) != layout.Height || channels != 4)
        {
            stbi_image_free(decoded);
            outError = MakeGridError("地表の分類の地図の画像の大きさ・チャンネルの数が記述と違います", imagePath);
            return {};
        }
        Container::VariableArray<uint8_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);
        std::memcpy(pixels.data(), decoded, pixels.size());
        stbi_image_free(decoded);
        return Container::MakeShared<const TerrainSurfaceMask>(layout, std::move(pixels));
    }

    bool TerrainSurfaceMask::Sample(float x, float z, TerrainSurfaceWeights &outWeights) const
    {
        if (m_Layout.Width < 2 || m_Layout.Height < 2 || !(x >= m_Layout.MinX && x <= m_Layout.MaxX) ||
            !(z >= m_Layout.MinZ && z <= m_Layout.MaxZ))
        {
            return false;
        }
        // 画素の中心が (i + 0.5) × 画素の広さにある。外縁の半画素は端の画素の値
        const float texelX = (m_Layout.MaxX - m_Layout.MinX) / static_cast<float>(m_Layout.Width);
        const float texelZ = (m_Layout.MaxZ - m_Layout.MinZ) / static_cast<float>(m_Layout.Height);
        const float fx = std::clamp((x - m_Layout.MinX) / texelX - 0.5f, 0.0f, static_cast<float>(m_Layout.Width - 1));
        const float fz = std::clamp((z - m_Layout.MinZ) / texelZ - 0.5f, 0.0f, static_cast<float>(m_Layout.Height - 1));
        const uint32_t column = std::min(static_cast<uint32_t>(fx), m_Layout.Width - 2);
        const uint32_t row = std::min(static_cast<uint32_t>(fz), m_Layout.Height - 2);
        const float tx = fx - static_cast<float>(column);
        const float tz = fz - static_cast<float>(row);
        float channels[4] = {};
        for (uint32_t channel = 0; channel < 4; ++channel)
        {
            const auto at = [&](uint32_t c, uint32_t r)
            { return static_cast<float>(m_Pixels[(static_cast<size_t>(r) * m_Layout.Width + c) * 4u + channel]); };
            const float north = at(column, row) + (at(column + 1, row) - at(column, row)) * tx;
            const float south = at(column, row + 1) + (at(column + 1, row + 1) - at(column, row + 1)) * tx;
            channels[channel] = (north + (south - north) * tz) * (1.0f / 255.0f);
        }
        outWeights.Rock = channels[0];
        outWeights.Talus = channels[1];
        outWeights.Fine = channels[2];
        outWeights.Gravel = channels[3];
        outWeights.Sand = std::max(0.0f, 1.0f - channels[0] - channels[1] - channels[2] - channels[3]);
        return true;
    }

    namespace WastelandTerrain
    {
        TerrainHeightFieldParams MakeParams()
        {
            TerrainHeightFieldParams params;
            params.Seed = kSeed;
            return params;
        }

        TerrainGrading MakeOutpostGrading()
        {
            TerrainGrading grading;
            grading.OriginX = kOutpostX;
            grading.OriginZ = kOutpostZ;
            grading.Height = kOutpostGradeHeight;
            grading.SlopeX = kOutpostGradeSlopeX;
            grading.SlopeZ = kOutpostGradeSlopeZ;
            grading.Pads = Container::Span<const TerrainGradePad>(kOutpostPads, kOutpostPadCount);
            return grading;
        }

        Container::TSharedPtr<const TerrainHeightField> LoadDemGrid(Container::AnsiString &outError)
        {
            const TerrainGrading grading = MakeOutpostGrading();
            return TerrainHeightField::LoadGrid(kDemGridDescriptorPath, outError, &grading);
        }
    } // namespace WastelandTerrain

} // namespace NorvesLib::Core::Rendering::Terrain
