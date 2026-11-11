// PaleFlat の直接光・環境光・全球の平均照度と設定の補正を CPU で検査する。
#if defined(_MSC_VER)
#pragma execution_character_set("utf-8")
#endif

#include "Container/VariableArray.h"
#include "Rendering/PaleFlatShading.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>

using NorvesLib::Core::Container::VariableArray;
using namespace NorvesLib::Core::Rendering;

namespace
{
    constexpr float DirectTolerance = 1.0e-6f;
    constexpr double Pi = 3.14159265358979323846264338327950288;

    void CheckFlatDirectFactor(int& failures)
    {
        const float wrapValues[] = {0.7f, 0.3f};
        const float strengthValues[] = {0.0f, 0.5f, 1.0f};
        const float shadowValues[] = {0.0f, 0.5f, 1.0f};
        PaleFlatParameters parameters = MakeDefaultPaleFlatParameters();
        parameters.Flatness = 1.0f;

        for (float wrap : wrapValues)
        {
            parameters.WrapMin = wrap;
            for (float strength : strengthValues)
            {
                parameters.ShadowStrength = strength;
                for (int step = -20; step <= 20; ++step)
                {
                    const float nDotL = static_cast<float>(step) / 20.0f;
                    for (float shadow : shadowValues)
                    {
                        const float actual = EvaluatePaleFlatDirectFactor(nDotL, shadow, parameters);
                        if (!(actual >= wrap - DirectTolerance && actual <= 1.0f + DirectTolerance))
                        {
                            std::cerr << "(i) 直接光の係数が範囲外: Flatness=1 WrapMin=" << wrap
                                      << " ShadowStrength=" << strength << " N・L=" << nDotL << " 影=" << shadow
                                      << " 期待値=[" << wrap - DirectTolerance << ", " << 1.0f + DirectTolerance
                                      << "] 実際=" << actual << '\n';
                            ++failures;
                        }
                        if (nDotL == 1.0f && shadow == 1.0f && !(std::fabs(actual - 1.0f) <= DirectTolerance))
                        {
                            std::cerr << "(i) 正面の直接光の係数が不正: Flatness=1 WrapMin=" << wrap
                                      << " ShadowStrength=" << strength << " N・L=" << nDotL << " 影=" << shadow
                                      << " 期待値=1 実際=" << actual << '\n';
                            ++failures;
                        }
                        if (nDotL == -1.0f && !(std::fabs(actual - wrap) <= DirectTolerance))
                        {
                            std::cerr << "(i) 背面の直接光の係数が不正: Flatness=1 WrapMin=" << wrap
                                      << " ShadowStrength=" << strength << " N・L=" << nDotL << " 影=" << shadow
                                      << " 期待値=" << wrap << " 実際=" << actual << '\n';
                            ++failures;
                        }
                    }
                }
            }
        }
    }

    void CheckLambertAndAmbient(int& failures)
    {
        const float shadowValues[] = {0.0f, 0.5f, 1.0f};
        PaleFlatParameters parameters = MakeDefaultPaleFlatParameters();
        parameters.Flatness = 0.0f;
        for (int step = -20; step <= 20; ++step)
        {
            const float nDotL = static_cast<float>(step) / 20.0f;
            for (float shadow : shadowValues)
            {
                const float expected = (nDotL > 0.0f ? nDotL : 0.0f) * shadow;
                const float actual = EvaluatePaleFlatDirectFactor(nDotL, shadow, parameters);
                if (!(std::fabs(actual - expected) <= DirectTolerance))
                {
                    std::cerr << "(ii) Lambert の係数と不一致: Flatness=0 WrapMin=" << parameters.WrapMin
                              << " ShadowStrength=" << parameters.ShadowStrength << " N・L=" << nDotL
                              << " 影=" << shadow << " 期待値=" << expected << " 実際=" << actual << '\n';
                    ++failures;
                }
            }
        }

        const float nDotL = 0.25f;
        const float shadow = 0.5f;
        const float lambert = EvaluatePaleFlatDirectFactor(nDotL, shadow, parameters);
        parameters.Flatness = 1.0e-3f;
        const float nearLambert = EvaluatePaleFlatDirectFactor(nDotL, shadow, parameters);
        const float change = std::fabs(nearLambert - lambert);
        if (!(change <= 1.0e-2f))
        {
            std::cerr << "(ii) Flatness が 0 に戻るときの変化が大きすぎる: Flatness=0 と 0.001 WrapMin="
                      << parameters.WrapMin << " ShadowStrength=" << parameters.ShadowStrength << " N・L=" << nDotL
                      << " 影=" << shadow << " 期待値=変化量 0.01 以下 実際=" << change << '\n';
            ++failures;
        }

        parameters.Flatness = 0.0f;
        float actual = EvaluatePaleFlatAmbientLocalWeight(parameters);
        if (!(std::fabs(actual - 1.0f) <= DirectTolerance))
        {
            std::cerr << "(ii) 環境光の局所重みが不正: Flatness=0 AmbientLocalMix=" << parameters.AmbientLocalMix
                      << " 期待値=1 実際=" << actual << '\n';
            ++failures;
        }
        parameters.Flatness = 1.0f;
        actual = EvaluatePaleFlatAmbientLocalWeight(parameters);
        if (!(std::fabs(actual - parameters.AmbientLocalMix) <= DirectTolerance))
        {
            std::cerr << "(ii) 環境光の局所重みが不正: Flatness=1 AmbientLocalMix=" << parameters.AmbientLocalMix
                      << " 期待値=" << parameters.AmbientLocalMix << " 実際=" << actual << '\n';
            ++failures;
        }
    }

    void CheckAverageIrradiance(uint32_t width, uint32_t height, bool bUpperOnly, int& failures)
    {
        const float radiance[] = {2.0f, 0.5f, 10.0f};
        VariableArray<float> pixels(static_cast<size_t>(width) * height * 4);
        for (uint32_t y = 0; y < height; ++y)
        {
            for (uint32_t x = 0; x < width; ++x)
            {
                const size_t offset = (static_cast<size_t>(y) * width + x) * 4;
                for (uint32_t channel = 0; channel < 3; ++channel)
                {
                    pixels[offset + channel] = bUpperOnly && y >= height / 2 ? 0.0f : radiance[channel];
                }
                pixels[offset + 3] = 1.0f;
            }
        }

        double actual[3] = {};
        if (!ComputeEquirectangularAverageIrradiance(pixels.data(), width, height, actual))
        {
            std::cerr << "(iii) 有効な平均照度の計算が失敗: 幅=" << width << " 高さ=" << height
                      << " 上半球のみ=" << bUpperOnly << " L=(2, 0.5, 10) 期待値=true 実際=false\n";
            ++failures;
            return;
        }
        const double relativeTolerance = bUpperOnly ? 1.0e-3 : 1.0e-9;
        for (uint32_t channel = 0; channel < 3; ++channel)
        {
            const double expected = Pi * static_cast<double>(radiance[channel]) * (bUpperOnly ? 0.5 : 1.0);
            if (!(std::fabs(actual[channel] - expected) <= expected * relativeTolerance))
            {
                std::cerr << "(iii) 全球の平均照度が不正: 幅=" << width << " 高さ=" << height
                          << " 上半球のみ=" << bUpperOnly << " L=(2, 0.5, 10) 色成分=" << channel
                          << " 相対許容誤差=" << relativeTolerance << " 期待値=" << expected
                          << " 実際=" << actual[channel] << '\n';
                ++failures;
            }
        }
    }

    void CheckInvalidIrradianceInputs(int& failures)
    {
        const float invalidValues[] = {
            std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::infinity(),
            -0.5f
        };
        float pixels[] = {2.0f, 0.5f, 10.0f, 1.0f};
        double actual[3] = {};
        for (float value : invalidValues)
        {
            pixels[0] = value;
            if (ComputeEquirectangularAverageIrradiance(pixels, 1, 1, actual))
            {
                std::cerr << "(iii) 不正な放射輝度を受理: 幅=1 高さ=1 L=(" << value
                          << ", 0.5, 10) 期待値=false 実際=true\n";
                ++failures;
            }
        }
        pixels[0] = 2.0f;
        if (ComputeEquirectangularAverageIrradiance(pixels, 0, 1, actual))
        {
            std::cerr << "(iii) 幅 0 を受理: 幅=0 高さ=1 L=(2, 0.5, 10) 期待値=false 実際=true\n";
            ++failures;
        }
    }

    void CheckSanitizedField(const char* field, float input, float expected, float actual, int& failures)
    {
        if (actual != expected)
        {
            std::cerr << "(iv) 設定の補正が不正: 欄=" << field << " 入力=" << input
                      << " 期待値=" << expected << " 実際=" << actual << '\n';
            ++failures;
        }
    }

    void CheckSanitize(int& failures)
    {
        const PaleFlatParameters defaults = MakeDefaultPaleFlatParameters();
        PaleFlatParameters input = defaults;
        input.WrapMin = std::numeric_limits<float>::infinity();
        input.Flatness = std::numeric_limits<float>::quiet_NaN();
        input.ColorSrgbR = 0.4f;
        PaleFlatParameters actual = SanitizePaleFlatParameters(input);
        CheckSanitizedField("WrapMin", input.WrapMin, defaults.WrapMin, actual.WrapMin, failures);
        CheckSanitizedField("Flatness", input.Flatness, defaults.Flatness, actual.Flatness, failures);
        CheckSanitizedField("ColorSrgbR", input.ColorSrgbR, input.ColorSrgbR, actual.ColorSrgbR, failures);

        input = defaults;
        input.WrapMin = 1.5f;
        input.Flatness = 0.35f;
        input.ColorSrgbR = -0.5f;
        actual = SanitizePaleFlatParameters(input);
        CheckSanitizedField("WrapMin", input.WrapMin, 1.0f, actual.WrapMin, failures);
        CheckSanitizedField("Flatness", input.Flatness, input.Flatness, actual.Flatness, failures);
        CheckSanitizedField("ColorSrgbR", input.ColorSrgbR, 0.0f, actual.ColorSrgbR, failures);
    }
}

int main()
{
    std::cerr.precision(17);
    int failures = 0;
    CheckFlatDirectFactor(failures);
    CheckLambertAndAmbient(failures);
    CheckAverageIrradiance(64, 32, false, failures);
    CheckAverageIrradiance(256, 128, false, failures);
    CheckAverageIrradiance(256, 128, true, failures);
    CheckInvalidIrradianceInputs(failures);
    CheckSanitize(failures);
    std::cout << "失敗件数: " << failures << '\n';
    return failures == 0 ? 0 : 1;
}
