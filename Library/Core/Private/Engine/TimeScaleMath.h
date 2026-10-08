#pragma once
#include "Container/Span.h"
#include <cstdint>
namespace NorvesLib::Core::Engine
{
    inline constexpr uint32_t TimeScaleDenominator = 65536;
    enum class TimeScaleMathResult : uint8_t
    {
        Success,
        InvalidArgument,
        Overflow
    };
    struct ConstantTimeScaleSegment
    {
        int64_t DeltaNanoseconds = 0;
        uint32_t Numerator = TimeScaleDenominator;
    };
    // 定倍率用の純計算。フェード/要求の寿命/時計を扱うTimeSystem本体とは分離する。
    // 非負の倍率を最近傍、ちょうど半分は上へ丸める。失敗時outは変えない。
    TimeScaleMathResult QuantizeTimeScale(double scale, uint32_t& outNumerator);
    // 各要求をQ16へ変換した後の積を、途中では丸めず最後に一度だけQ16へ丸める。
    // 空は1、0が一つでもあれば0。要素順によらず同じ値。失敗時outは変えない。
    TimeScaleMathResult ComposeTimeScales(Container::Span<const uint32_t> numerators, uint32_t& outNumerator);
    // carryは分母65536未満の端数。倍率0や倍率変更でも端数を保持する。
    // raw=0は有効。失敗時carry/outは不変。outと入力の所有権は呼出側にある。
    TimeScaleMathResult ScaleTimeNanoseconds(int64_t rawNanoseconds, uint32_t numerator, uint32_t& carry, int64_t& out);
    // 期限で分割済みの定倍率区間をまとめる。途中失敗でもcallerのcarry/outは不変。
    TimeScaleMathResult AccumulateTimeSegments(Container::Span<const ConstantTimeScaleSegment> segments,
                                               uint32_t& carry, int64_t& out);
} // namespace NorvesLib::Core::Engine
