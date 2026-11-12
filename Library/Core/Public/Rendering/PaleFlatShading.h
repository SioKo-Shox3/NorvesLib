#pragma once

// PaleFlat の値の設定と、色・照明の係数を求める CPU の参照式。
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace NorvesLib::Core::Rendering
{
    /** @brief PaleFlat の画素の見え方を指定する、値でコピーできる設定。 */
    struct PaleFlatParameters
    {
        /// 基本色の赤。sRGB の符号値で [0, 1]（既定色は #E3E5DF）。
        float ColorSrgbR = 227.0f / 255.0f;
        /// 基本色の緑。sRGB の符号値で [0, 1]（既定色は #E3E5DF）。
        float ColorSrgbG = 229.0f / 255.0f;
        /// 基本色の青。sRGB の符号値で [0, 1]（既定色は #E3E5DF）。
        float ColorSrgbB = 223.0f / 255.0f;
        /// Flatness が 1 のときの直接光の係数の下限。[0, 1]。
        float WrapMin = 0.7f;
        /// 影を効かせる割合。[0, 1]。0 は影なし、1 は影を全部効かせる。
        float ShadowStrength = 0.5f;
        /// 環境光で局所の値を混ぜる割合。[0, 1]。0 は全球の平均照度だけ、1 は局所だけ。
        float AmbientLocalMix = 0.25f;
        /// 鏡面に掛ける倍率。[0, 1]。
        float SpecularScale = 0.0f;
        /// 平らにする度合い。[0, 1]。0 は通常の拡散（Lambert）、1 は完全な PaleFlat。
        float Flatness = 1.0f;
    };

    static_assert(std::is_trivially_copyable_v<PaleFlatParameters>,
        "PaleFlatParameters は値でコピーできる必要がある");

    /** @brief PaleFlat の既定の設定を返す。 */
    inline PaleFlatParameters MakeDefaultPaleFlatParameters()
    {
        return PaleFlatParameters{};
    }

    /** @brief 非有限の欄をその欄の既定へ戻し、有限の欄を [0, 1] に収める。 */
    inline PaleFlatParameters SanitizePaleFlatParameters(const PaleFlatParameters& parameters)
    {
        const PaleFlatParameters defaults = MakeDefaultPaleFlatParameters();
        const auto sanitize = [](float value, float defaultValue)
        {
            return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : defaultValue;
        };
        PaleFlatParameters result = parameters;
        result.ColorSrgbR = sanitize(parameters.ColorSrgbR, defaults.ColorSrgbR);
        result.ColorSrgbG = sanitize(parameters.ColorSrgbG, defaults.ColorSrgbG);
        result.ColorSrgbB = sanitize(parameters.ColorSrgbB, defaults.ColorSrgbB);
        result.WrapMin = sanitize(parameters.WrapMin, defaults.WrapMin);
        result.ShadowStrength = sanitize(parameters.ShadowStrength, defaults.ShadowStrength);
        result.AmbientLocalMix = sanitize(parameters.AmbientLocalMix, defaults.AmbientLocalMix);
        result.SpecularScale = sanitize(parameters.SpecularScale, defaults.SpecularScale);
        result.Flatness = sanitize(parameters.Flatness, defaults.Flatness);
        return result;
    }

    /** @brief sRGB の符号値を [0, 1] に収め、IEC 61966-2-1 の区分式で線形値に変換する。 */
    inline float ConvertSrgbToLinear(float value)
    {
        value = std::clamp(value, 0.0f, 1.0f);
        if (value <= 0.04045f)
        {
            return value / 12.92f;
        }
        return std::pow((value + 0.055f) / 1.055f, 2.4f);
    }

    /** @brief PaleFlat の基本色を線形 RGB に変換して出力する。 */
    inline void ConvertPaleFlatColorToLinear(const PaleFlatParameters& parameters, float outLinearRgb[3])
    {
        outLinearRgb[0] = ConvertSrgbToLinear(parameters.ColorSrgbR);
        outLinearRgb[1] = ConvertSrgbToLinear(parameters.ColorSrgbG);
        outLinearRgb[2] = ConvertSrgbToLinear(parameters.ColorSrgbB);
    }

    /**
     * @brief 光源の放射照度（lightColor × 減衰）に掛ける直接光の係数を返す。
     *
     * 拡散 = Color/π × 放射照度 × この係数。parameters は呼び出し側で Sanitize 済みとする。
     * shadow は [0, 1] に収めてから、次の式に使う。mix(a, b, t) = a + (b - a) * t。
     * lambert    = max(nDotL, 0) * shadow
     * wrapped    = clamp(nDotL * 0.5 + 0.5, 0, 1)
     * weakShadow = mix(1, shadow, ShadowStrength)
     * flat       = mix(WrapMin, 1, wrapped * weakShadow)
     * factor     = mix(lambert, flat, Flatness)
     */
    inline float EvaluatePaleFlatDirectFactor(
        float nDotL, float shadow, const PaleFlatParameters& parameters)
    {
        shadow = std::clamp(shadow, 0.0f, 1.0f);
        const float lambert = (std::max)(nDotL, 0.0f) * shadow;
        const float wrapped = std::clamp(nDotL * 0.5f + 0.5f, 0.0f, 1.0f);
        const float weakShadow = 1.0f + (shadow - 1.0f) * parameters.ShadowStrength;
        const float flat = parameters.WrapMin + (1.0f - parameters.WrapMin) * (wrapped * weakShadow);
        return lambert + (flat - lambert) * parameters.Flatness;
    }

    /**
     * @brief 環境光で局所の値に掛ける重み mix(1, AmbientLocalMix, Flatness) を返す。
     *
     * 環境光の拡散 = mix(Color/π × 全球の平均照度, 局所の環境光の拡散, この重み)。
     * Flatness が 0 なら局所の環境光だけ、1 なら AmbientLocalMix の割合を使う。
     * parameters は呼び出し側で Sanitize 済みとする。
     */
    inline float EvaluatePaleFlatAmbientLocalWeight(const PaleFlatParameters& parameters)
    {
        return 1.0f + (parameters.AmbientLocalMix - 1.0f) * parameters.Flatness;
    }

    /**
     * @brief 正距円筒の放射輝度から、全球の平均照度 E_avg = π × Σ L・dΩ / Σ dΩ を求める。
     *
     * 行優先の RGBA float 配列で、α は読まない。y = 0 は +Y の天頂、
     * 行 y の極角 θ = π (y + 0.5) / height、dΩ ∝ sin θ として double で積算する。
     * 全方向の法線について照度を平均した値に等しく、一定の L で πL となる。
     * 空の画像・無効なポインタ・添字の容量超過・非有限または負の RGB では false を返す。
     * 失敗時の outRgb は 0（outRgb 自体が nullptr なら書き込まない）。
     */
    inline bool ComputeEquirectangularAverageIrradiance(
        const float* rgbaPixels, uint32_t width, uint32_t height, double outRgb[3])
    {
        if (outRgb == nullptr)
        {
            return false;
        }
        outRgb[0] = 0.0;
        outRgb[1] = 0.0;
        outRgb[2] = 0.0;
        if (rgbaPixels == nullptr || width == 0 || height == 0)
        {
            return false;
        }
        if (static_cast<std::size_t>(width) > static_cast<std::size_t>(-1) / sizeof(float) / 4 / height)
        {
            return false;
        }

        constexpr double pi = 3.14159265358979323846264338327950288;
        double weightedRgb[3] = {0.0, 0.0, 0.0};
        double weightSum = 0.0;
        for (uint32_t y = 0; y < height; ++y)
        {
            const double theta = pi * (static_cast<double>(y) + 0.5) / static_cast<double>(height);
            const double weight = std::sin(theta);
            weightSum += weight * static_cast<double>(width);
            const std::size_t rowOffset = static_cast<std::size_t>(y) * width * 4;
            for (uint32_t x = 0; x < width; ++x)
            {
                const std::size_t pixelOffset = rowOffset + static_cast<std::size_t>(x) * 4;
                for (std::size_t channel = 0; channel < 3; ++channel)
                {
                    const float radiance = rgbaPixels[pixelOffset + channel];
                    if (!std::isfinite(radiance) || radiance < 0.0f)
                    {
                        return false;
                    }
                    weightedRgb[channel] += static_cast<double>(radiance) * weight;
                }
            }
        }
        for (std::size_t channel = 0; channel < 3; ++channel)
        {
            outRgb[channel] = pi * weightedRgb[channel] / weightSum;
        }
        return true;
    }

} // namespace NorvesLib::Core::Rendering
