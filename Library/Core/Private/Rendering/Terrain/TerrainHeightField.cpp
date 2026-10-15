#include "Core/Public/Rendering/Terrain/TerrainHeightField.h"

#include "Asset/AssetFileReader.h"
#include "Text/JsonDocument.h"

#include <algorithm>
#include <cmath>
#include <cstring>

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

    /// 格子の高さ場の中身。頂点 (column, row) はエンジンの (FirstX + column × Spacing, FirstZ + row × Spacing)。
    /// 行 0 が北端（-Z の端）で、列 0 が西端（-X の端）
    struct TerrainHeightField::GridData
    {
        /// float32 の little-endian、行優先、見出しなし（Columns × Rows × 4 バイト）
        Asset::AssetBlob Bytes;
        uint32_t Columns = 0;
        uint32_t Rows = 0;
        double FirstX = 0.0;
        double FirstZ = 0.0;
        double Spacing = 1.0;

        float At(uint32_t column, uint32_t row) const
        {
            float value = 0.0f;
            std::memcpy(&value, Bytes.GetData() + (static_cast<size_t>(row) * Columns + column) * sizeof(float),
                        sizeof(float));
            return value;
        }
    };

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

    TerrainHeightField::TerrainHeightField(GridTag, Container::TSharedPtr<const GridData> grid)
        : m_Grid(std::move(grid))
    {
    }

    Container::TSharedPtr<const TerrainHeightField> TerrainHeightField::LoadGrid(Container::AnsiStringView descriptorPath,
                                                                                Container::AnsiString &outError)
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

        return Container::MakeShared<const TerrainHeightField>(GridTag{}, Container::TSharedPtr<const GridData>(std::move(grid)));
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
        bool LoadTerrainMapLayout(Container::AnsiStringView descriptorPath, const char *const (&channelNames)[4],
                                  const char *errorLabel, TerrainSurfaceMaskLayout &outLayout, Container::AnsiString &outError)
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
