#include "Core/Public/Rendering/Terrain/TerrainHeightField.h"

#include <algorithm>
#include <cmath>

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

        // メサは輪の上に、角度をほぼ等間隔（±25% ずらす）に並べる。半径・縦横比・高さは種で決める
        m_MesaCount = std::min(params.MesaCount, kMaxMesas);
        const float angleStep = m_MesaCount > 0 ? 2.0f * kPi / static_cast<float>(m_MesaCount) : 0.0f;
        const float angleOffset = UnitFloat(Hash(seed ^ 0x6a09e667u)) * 2.0f * kPi;
        for (uint32_t index = 0; index < m_MesaCount; ++index)
        {
            const uint32_t h = Hash(seed + index * 0x9e3779b9u);
            Mesa &mesa = m_Mesas[index];
            const float angle =
                angleOffset + angleStep * (static_cast<float>(index) + (UnitFloat(Hash(h ^ 0x1u)) - 0.5f) * 0.5f);
            const float ring = Lerp(params.MesaRingInner, params.MesaRingOuter, UnitFloat(Hash(h ^ 0x2u)));
            mesa.CenterX = std::cos(angle) * ring;
            mesa.CenterZ = std::sin(angle) * ring;
            mesa.Radius = Lerp(45.0f, 105.0f, UnitFloat(Hash(h ^ 0x3u)));
            mesa.Aspect = Lerp(1.0f, 2.0f, UnitFloat(Hash(h ^ 0x4u)));
            mesa.Angle = UnitFloat(Hash(h ^ 0x5u)) * kPi;
            mesa.Height = Lerp(28.0f, 62.0f, UnitFloat(Hash(h ^ 0x6u)));
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
            const float along = (dx * c + dz * s) / mesa.Aspect;
            const float across = -dx * s + dz * c;
            const float distance = std::sqrt(along * along + across * across);
            // 輪郭を揺らして、円でなく崩れた台地の縁にする
            const float edgeNoise = Fbm(x, z, 60.0f, 3, 0.5f, seed + index) * mesa.Radius * 0.28f;
            const float d = distance + edgeNoise;
            if (d > mesa.Radius + 80.0f)
            {
                continue;
            }
            // 崖（幅 7 m で頂から足元へ）と、崖の下の崖錐（40〜80 m のなだらかな裾）
            const float cliff = 1.0f - Smoothstep(mesa.Radius - 7.0f, mesa.Radius, d);
            const float talusT = 1.0f - Smoothstep(mesa.Radius - 4.0f, mesa.Radius + 70.0f, d);
            const float talus = 0.32f * talusT * talusT;
            // 頂には浅い起伏を付ける
            const float top = 1.0f + 0.04f * Fbm(x, z, 40.0f, 2, 0.5f, seed + 17u + index);
            const float profile = std::max(cliff * top, talus);
            result = std::max(result, profile * mesa.Height);
        }
        return result;
    }

    float TerrainHeightField::SampleHeight(float x, float z) const
    {
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

    namespace WastelandTerrain
    {
        TerrainHeightFieldParams MakeParams()
        {
            TerrainHeightFieldParams params;
            params.Seed = kSeed;
            return params;
        }
    } // namespace WastelandTerrain

} // namespace NorvesLib::Core::Rendering::Terrain
