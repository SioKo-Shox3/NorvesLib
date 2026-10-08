#pragma once
#include "Engine/TimeScaleMath.h"
namespace NorvesLib::Core::Engine
{
    struct LinearTimeScale
    {
        double Begin = 1, End = 1;
    };
    // 同じ時間区間の線形倍率を掛け合わせ、積の区間平均を計算する。
    // 各倍率の平均の積ではない。空は1。非負のBernstein係数で桁落ちを避ける。
    // 平均はdouble演算の近似。計算値が上限を超えた場合（上限直近の丸めも含む）はOverflow。
    // subnormalより小さい値は0。失敗時outは不変。
    TimeScaleMathResult AverageLinearTimeScales(Container::Span<const LinearTimeScale> factors, double& out);
} // namespace NorvesLib::Core::Engine
