// R8のEXR連番を検査するexe。欠番・重複・読めないフレーム・寸法の不一致・非有限の画素と、ACES 2.0 SDRで
// displayへ変換した隣接フレームのLDR-FLIP平均によるポッピングを検出し、問題が無ければ0を返す。
// 使い方: R8ExrSequenceValidator --dir=<directory> --scene=<name> --frames=<N> [--first=<k>] [--lut=<path>]
//         [--exposure=<x>] [--width=<w> --height=<h>]
#include "R8ExrSequenceValidation.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

namespace
{
    const char* MatchOption(const char* argument, const char* prefix)
    {
        const size_t length = std::strlen(prefix);
        return std::strncmp(argument, prefix, length) == 0 ? argument + length : nullptr;
    }
} // namespace

int main(int argc, char** argv)
{
    using namespace NorvesLib::Test::RenderingValidation;

    const char* directory = nullptr;
    const char* sceneName = nullptr;
    const char* lutPath = NORVES_SOURCE_ROOT "/Assets/ColorManagement/Aces20SdrRec709.lut3d";
    unsigned long frames = 0u;
    unsigned long first = 0u;
    float exposure = 1.0f;
    unsigned long width = 0u;
    unsigned long height = 0u;
    for (int index = 1; index < argc; ++index)
    {
        const char* argument = argv[index];
        if (const char* value = MatchOption(argument, "--dir="))
        {
            directory = value;
        }
        else if (const char* value = MatchOption(argument, "--scene="))
        {
            sceneName = value;
        }
        else if (const char* value = MatchOption(argument, "--frames="))
        {
            frames = std::strtoul(value, nullptr, 10);
        }
        else if (const char* value = MatchOption(argument, "--first="))
        {
            first = std::strtoul(value, nullptr, 10);
        }
        else if (const char* value = MatchOption(argument, "--lut="))
        {
            lutPath = value;
        }
        else if (const char* value = MatchOption(argument, "--exposure="))
        {
            exposure = std::strtof(value, nullptr);
        }
        else if (const char* value = MatchOption(argument, "--width="))
        {
            width = std::strtoul(value, nullptr, 10);
        }
        else if (const char* value = MatchOption(argument, "--height="))
        {
            height = std::strtoul(value, nullptr, 10);
        }
        else
        {
            std::cerr << "不明な引数です: " << argument << '\n';
            return 2;
        }
    }
    if (!directory || !sceneName || frames == 0u || frames > UINT32_MAX || first > UINT32_MAX ||
        !(exposure > 0.0f) || (width == 0u) != (height == 0u) || width > UINT32_MAX || height > UINT32_MAX)
    {
        std::cerr << "usage: R8ExrSequenceValidator --dir=<directory> --scene=<name> --frames=<N> [--first=<k>]"
                     " [--lut=<path>] [--exposure=<x>] [--width=<w> --height=<h>]\n";
        return 2;
    }

    AcesOutputLut lut;
    if (!LoadAcesOutputLut(lutPath, lut))
    {
        std::cerr << "LUTを読めません: " << lutPath << '\n';
        return 1;
    }
    ExrSequenceRules rules;
    rules.ExpectedWidth = static_cast<uint32_t>(width);
    rules.ExpectedHeight = static_cast<uint32_t>(height);
    std::cout << "r8_exr_rules popping_median_scale=" << rules.PoppingMedianScale
              << " popping_floor=" << rules.PoppingFloor << " exposure=" << exposure
              << " expected_size=" << rules.ExpectedWidth << "x" << rules.ExpectedHeight << '\n';
    ExrSequenceReport report;
    if (!ValidateExrSequence(directory, sceneName, static_cast<uint32_t>(first), static_cast<uint32_t>(frames), lut,
                             exposure, rules, report))
    {
        std::cerr << "連番を検査できません: " << directory << '\n';
        return 1;
    }
    PrintExrSequenceReport(report);
    return report.Passed() ? 0 : 1;
}
