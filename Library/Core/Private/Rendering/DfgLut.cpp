// ラスタのIBLとパストレーサーが共有するsplit-sum DFG LUTの生成。
#include "Rendering/DfgLut.h"

#include "Container/Containers.h"

#include <algorithm>
#include <cmath>

namespace NorvesLib::Core::Rendering
{
    uint16_t FloatToHalfRne(float value)
    {
        union
        {
            float f;
            uint32_t u;
        } conv;
        conv.f = value;
        uint32_t f32 = conv.u;

        const uint16_t sign = static_cast<uint16_t>((f32 >> 16u) & 0x8000u);
        const uint32_t exponentBits = (f32 >> 23u) & 0xFFu;
        const uint32_t mantissa = f32 & 0x007FFFFFu;
        if (exponentBits == 0xFFu)
        {
            return mantissa == 0u ? sign | 0x7C00u : 0x7E00u;
        }

        const int32_t exponent = static_cast<int32_t>(exponentBits) - 127;
        if (exponent > 15)
        {
            return sign | 0x7C00u;
        }
        if (exponent >= -14)
        {
            uint32_t roundedMantissa = mantissa;
            const uint32_t truncated = roundedMantissa >> 13u;
            const uint32_t remainder = roundedMantissa & 0x1FFFu;
            const bool bRoundUp = remainder > 0x1000u ||
                                   (remainder == 0x1000u && (truncated & 1u) != 0u);
            roundedMantissa = truncated + (bRoundUp ? 1u : 0u);
            int32_t roundedExponent = exponent;
            if (roundedMantissa >= 0x400u)
            {
                roundedMantissa = 0u;
                ++roundedExponent;
            }
            if (roundedExponent > 15)
            {
                return sign | 0x7C00u;
            }
            return sign |
                   static_cast<uint16_t>((roundedExponent + 15) << 10u) |
                   static_cast<uint16_t>(roundedMantissa);
        }
        if (exponent >= -25)
        {
            const uint32_t normalizedMantissa = mantissa | 0x00800000u;
            const uint32_t shift = static_cast<uint32_t>(-exponent - 1);
            const uint32_t truncated = normalizedMantissa >> shift;
            const uint32_t remainderMask = (1u << shift) - 1u;
            const uint32_t remainder = normalizedMantissa & remainderMask;
            const uint32_t halfway = 1u << (shift - 1u);
            const bool bRoundUp = remainder > halfway ||
                                   (remainder == halfway && (truncated & 1u) != 0u);
            return sign | static_cast<uint16_t>(truncated + (bRoundUp ? 1u : 0u));
        }
        return sign;
    }

    namespace
    {
        void BuildDfgLutHalfData(uint16_t* outData)
        {
            constexpr float PI = 3.14159265359f;

            Container::VariableArray<float> halfVectors(DfgLutSampleCount * 3, 0.0f);

            for (uint32_t y = 0; y < DfgLutSize; ++y)
            {
                const float roughness = (static_cast<float>(y) + 0.5f) /
                                        static_cast<float>(DfgLutSize);

                for (uint32_t i = 0; i < DfgLutSampleCount; ++i)
                {
                    // Hammersley列
                    float u = static_cast<float>(i) / static_cast<float>(DfgLutSampleCount);
                    uint32_t bits = i;
                    bits = (bits << 16u) | (bits >> 16u);
                    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
                    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
                    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
                    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
                    float v = static_cast<float>(bits) * 2.3283064365386963e-10f;

                    // GGXの重点的標本化
                    float a = roughness * roughness;
                    float phi = 2.0f * PI * u;
                    float cosTheta = std::sqrt((1.0f - v) / (1.0f + (a * a - 1.0f) * v));
                    float sinTheta = std::sqrt(1.0f - cosTheta * cosTheta);

                    halfVectors[i * 3] = sinTheta * std::cos(phi);
                    halfVectors[i * 3 + 1] = sinTheta * std::sin(phi);
                    halfVectors[i * 3 + 2] = cosTheta;
                }

                for (uint32_t x = 0; x < DfgLutSize; ++x)
                {
                    const float NdotV = (static_cast<float>(x) + 0.5f) /
                                        static_cast<float>(DfgLutSize);

                    // 接空間の視線（N = (0,0,1)）
                    float Vx = std::sqrt(1.0f - NdotV * NdotV);
                    float Vy = 0.0f;
                    float Vz = NdotV;

                    float A = 0.0f;
                    float B = 0.0f;

                    for (uint32_t i = 0; i < DfgLutSampleCount; ++i)
                    {
                        const float Hx = halfVectors[i * 3];
                        const float Hy = halfVectors[i * 3 + 1];
                        const float Hz = halfVectors[i * 3 + 2];

                        // HでVを反射してLを得る
                        float VdotH = Vx * Hx + Vy * Hy + Vz * Hz;
                        float Lx = 2.0f * VdotH * Hx - Vx;
                        float Ly = 2.0f * VdotH * Hy - Vy;
                        float Lz = 2.0f * VdotH * Hz - Vz;
                        (void)Lx;
                        (void)Ly;

                        float NdotL = (std::max)(Lz, 0.0f);
                        float NdotH = (std::max)(Hz, 0.0f);
                        VdotH = (std::max)(VdotH, 0.0f);

                        if (NdotL > 0.0f)
                        {
                            // IBL用のSmith GGX: k = roughness^2 / 2
                            float k = (roughness * roughness) / 2.0f;
                            float G_V = NdotV / (NdotV * (1.0f - k) + k);
                            float G_L = NdotL / (NdotL * (1.0f - k) + k);
                            float G = G_V * G_L;

                            float G_Vis = (G * VdotH) / (NdotH * NdotV + 0.0001f);
                            float Fc = std::pow(1.0f - VdotH, 5.0f);

                            A += (1.0f - Fc) * G_Vis;
                            B += Fc * G_Vis;
                        }
                    }

                    A /= static_cast<float>(DfgLutSampleCount);
                    B /= static_cast<float>(DfgLutSampleCount);

                    // 有効範囲へ丸める
                    A = (std::max)(0.0f, (std::min)(1.0f, A));
                    B = (std::max)(0.0f, (std::min)(1.0f, B));

                    size_t idx = (static_cast<size_t>(y) * DfgLutSize + x) * 2;
                    outData[idx + 0] = FloatToHalfRne(A);
                    outData[idx + 1] = FloatToHalfRne(B);
                }
            }
        }

        // 積分結果を保持する固定長の表。破棄時に解放する資源を持たない。
        struct DfgLutTable
        {
            uint16_t Values[DfgLutSize * DfgLutSize * 2] = {};

            DfgLutTable()
            {
                BuildDfgLutHalfData(Values);
            }
        };
    }

    const uint16_t* GetDfgLutHalfData()
    {
        static const DfgLutTable table;
        return table.Values;
    }
}
