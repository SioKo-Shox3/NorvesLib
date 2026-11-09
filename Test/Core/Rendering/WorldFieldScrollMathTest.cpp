// 世界フィールドの負の座標、境界、整数差分と全域破棄を確かめる。
#include "Rendering/WorldFieldTypes.h"

#include <cmath>
#include <iostream>
#include <limits>

using namespace NorvesLib::Core::Rendering;

int main()
{
    const WorldFieldRange ranges[] = {WorldFieldRange::Range64m, WorldFieldRange::Range128m};
    for (auto range : ranges)
    {
        const double texel = GetWorldFieldTexelSizeMeters(range);
        const auto initial = CalculateWorldFieldScroll({}, 0.0, 0.0, texel);
        const auto negative = CalculateWorldFieldScroll(initial.Origin, -0.1, -0.1, texel);
        const auto positive = CalculateWorldFieldScroll(negative.Origin, 0.1, 0.1, texel);
        if (!initial.bReset || !initial.Origin.bValid || initial.Origin.X != -128 || initial.Origin.Z != -128 ||
            negative.bReset || negative.Origin.X != -129 || negative.Origin.Z != -129 ||
            negative.ShiftX != -1 || negative.ShiftZ != -1 || positive.bReset ||
            positive.Origin.X != -128 || positive.Origin.Z != -128 || positive.ShiftX != 1 || positive.ShiftZ != 1 ||
            negative.OriginXMeters != -129 * texel || negative.OriginZMeters != -129 * texel)
        {
            std::cout << "失敗: 初回の破棄、負の座標の床丸め、世界座標への変換が違います\n";
            return 1;
        }
        const auto boundary = CalculateWorldFieldScroll(initial.Origin, texel, -texel, texel);
        const auto below = CalculateWorldFieldScroll(initial.Origin,
            std::nextafter(texel, 0.0), std::nextafter(-texel, -2.0 * texel), texel);
        const auto moved = CalculateWorldFieldScroll(initial.Origin, 3.0 * texel, -3.0 * texel, texel);
        if (boundary.bReset || boundary.Origin.X != -127 || boundary.Origin.Z != -129 ||
            below.Origin.X != -128 || below.Origin.Z != -130 || moved.bReset ||
            moved.ShiftX != 3 || moved.ShiftZ != -3)
        {
            std::cout << "失敗: 境界ちょうどと直前の所属画素、または3画素の差分が違います\n";
            return 1;
        }
        const auto retained = CalculateWorldFieldScroll(initial.Origin, 255.0 * texel, -255.0 * texel, texel);
        const auto resetX = CalculateWorldFieldScroll(initial.Origin, 256.0 * texel, 0.0, texel);
        const auto resetZ = CalculateWorldFieldScroll(initial.Origin, 0.0, -256.0 * texel, texel);
        const double extreme = std::numeric_limits<double>::max();
        const auto huge = CalculateWorldFieldScroll(initial.Origin, extreme, -extreme, texel);
        const auto reversed = CalculateWorldFieldScroll(huge.Origin, -extreme, extreme, texel);
        if (retained.bReset || !resetX.bReset || resetX.ShiftX != 256 || !resetZ.bReset || resetZ.ShiftZ != -256 ||
            !huge.bReset || !huge.Origin.bValid || !reversed.bReset ||
            huge.Origin.X <= 0 || huge.Origin.Z >= 0 || reversed.ShiftX >= 0 || reversed.ShiftZ <= 0 ||
            !std::isfinite(huge.OriginXMeters) || !std::isfinite(huge.OriginZMeters))
        {
            std::cout << "失敗: 解像度以上の移動または極端な座標が安全に全域破棄されません\n";
            return 1;
        }
    }

    const WorldFieldOrigin extremePrevious = {
        std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::max(), true};
    const auto saturated = CalculateWorldFieldScroll(extremePrevious, 1.0e30, -1.0e30, 0.25);
    if (!saturated.bReset || saturated.ShiftX != std::numeric_limits<int64_t>::max() ||
        saturated.ShiftZ != std::numeric_limits<int64_t>::min())
    {
        std::cout << "失敗: 前原点との差分が桁あふれを防いで丸められません\n";
        return 1;
    }
    const double invalid[] = {std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
    for (double value : invalid)
    {
        const auto invalidX = CalculateWorldFieldScroll({}, value, 0.0, 0.25);
        const auto invalidZ = CalculateWorldFieldScroll({}, 0.0, value, 0.25);
        const auto invalidTexel = CalculateWorldFieldScroll({}, 0.0, 0.0, value);
        if (!invalidX.bReset || invalidX.Origin.bValid || !invalidZ.bReset || invalidZ.Origin.bValid ||
            !invalidTexel.bReset || invalidTexel.Origin.bValid)
        {
            std::cout << "失敗: 非有限のスクロール入力から有効な原点が作られました\n";
            return 1;
        }
    }
    if (CalculateWorldFieldScroll({}, 0.0, 0.0, 0.0).Origin.bValid ||
        CalculateWorldFieldScroll({}, 0.0, 0.0, -0.25).Origin.bValid ||
        CalculateWorldFieldScroll({}, 0.0, 0.0, 0.25, 0).Origin.bValid)
    {
        std::cout << "失敗: 非正の画素サイズまたは解像度ゼロが許可されました\n";
        return 1;
    }
    std::cout << "WorldFieldScrollMathTest: 両範囲の格子への丸め・整数差分・全域破棄を確認しました\n";
    return 0;
}
