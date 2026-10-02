#include "Math/Curves.h"
#include "Input/InputAxisMath.h"
#include "Input/HapticsEnvelopeMath.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

using namespace NorvesLib;
using namespace Math;
using namespace Core::Input;
using Core::Container::Span;

namespace
{
    bool Near(double a, double b)
    {
        return std::fabs(a-b) <= 1e-12;
    }
}

int main()
{
    const CurveKeyframe keys[] = {{1, -2}, {3, 6}, {7, 2}};
    const Span<const CurveKeyframe> curve(keys);
    double result = 99;
    assert(IsValidPiecewiseLinearCurve(curve));
    for (const auto sample : {CurveKeyframe{-1, -2}, CurveKeyframe{0, -2}, CurveKeyframe{1, -2},
        CurveKeyframe{2, 2}, CurveKeyframe{3, 6}, CurveKeyframe{5, 4}, CurveKeyframe{7, 2}, CurveKeyframe{8, 2}})
    {
        assert(TryEvaluatePiecewiseLinear(curve, sample.Time, result) && result == sample.Value);
    }
    assert(TryEvaluatePiecewiseLinear(Span<const CurveKeyframe>{}, 0, result) && result == 0);
    const CurveKeyframe single[] = {{2, 5}};
    assert(TryEvaluatePiecewiseLinear(Span<const CurveKeyframe>(single), 100, result) && result == 5);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    for (const auto time : {nan, inf, -inf})
    {
        result = 99;
        assert(!TryEvaluatePiecewiseLinear(curve, time, result) && result == 99);
    }
    for (const auto invalid : {CurveKeyframe{-1, 0}, CurveKeyframe{nan, 0}, CurveKeyframe{inf, 0},
        CurveKeyframe{1, std::numeric_limits<float>::infinity()}, CurveKeyframe{1, std::numeric_limits<float>::quiet_NaN()}})
    {
        result = 99;
        assert(!TryEvaluatePiecewiseLinear(Span<const CurveKeyframe>(&invalid, 1), 0, result) && result == 99);
    }
    const CurveKeyframe duplicate[] = {{1, 0}, {1, 1}};
    const CurveKeyframe descending[] = {{2, 0}, {1, 1}};
    assert(!IsValidPiecewiseLinearCurve(Span<const CurveKeyframe>(duplicate)));
    assert(!IsValidPiecewiseLinearCurve(Span<const CurveKeyframe>(descending)));
    result = 99;
    assert(!TryEvaluatePiecewiseLinear(Span<const CurveKeyframe>(nullptr, 1), 0, result) && result == 99);
    const float largest = std::numeric_limits<float>::max();
    const double last = std::numeric_limits<double>::max();
    const CurveKeyframe wide[] = {{0, -largest}, {last, largest}};
    assert(TryEvaluatePiecewiseLinear(Span<const CurveKeyframe>(wide), last/2, result) && result == 0);
    assert(TryEvaluatePiecewiseLinear(Span<const CurveKeyframe>(wide), last, result) && result == largest);
    // 大小差がdoubleの仮数幅を超えても、右端と内部keyを正確に保持する。
    for (const float sign : {-1.0f, 1.0f})
    {
        const CurveKeyframe extreme[] = {{0, sign*largest}, {1, sign}, {2, sign*2}};
        const Span<const CurveKeyframe> extremeCurve(extreme);
        assert(TryEvaluatePiecewiseLinear(extremeCurve, 1, result) && result == sign);
        assert(TryEvaluatePiecewiseLinear(extremeCurve, 2, result) && result == sign*2);
        assert(TryEvaluatePiecewiseLinear(extremeCurve, std::nextafter(1.0, 2.0), result));
        assert(std::fabs(result-sign) < 1e-12);
        assert(TryEvaluatePiecewiseLinear(extremeCurve, std::nextafter(1.0, 0.0), result));
        assert(std::isfinite(result) && sign*result >= 1 && sign*result <= largest);
    }
    const CurveKeyframe tiny[] = {{0, 0}, {std::numeric_limits<double>::denorm_min(), 1}};
    assert(TryEvaluatePiecewiseLinear(Span<const CurveKeyframe>(tiny), tiny[1].Time, result) && result == 1);

    for (const auto kind : {ECurveEasing::Linear, ECurveEasing::Power, ECurveEasing::Expo, ECurveEasing::SmoothStep})
    {
        const double parameter = kind == ECurveEasing::Power ? 2 : 0.75;
        double previous = -1;
        for (int step = 0; step <= 1000; ++step)
        {
            const double x = step/1000.0;
            assert(TryEvaluateEasing(kind, x, parameter, result));
            const double expected = kind == ECurveEasing::Power ? x*x : kind == ECurveEasing::Expo ? x*0.25+x*x*x*0.75
                : kind == ECurveEasing::SmoothStep ? 3*x*x-2*x*x*x : x;
            assert(Near(result, expected) && result >= previous && result >= 0 && result <= 1);
            previous = result;
        }
        assert(TryEvaluateEasing(kind, 0, parameter, result) && result == 0);
        assert(TryEvaluateEasing(kind, 1, parameter, result) && result == 1);
        for (const double invalid : {-0.1, 1.1, nan, inf})
        {
            result = 99;
            assert(!TryEvaluateEasing(kind, invalid, parameter, result) && result == 99);
        }
        result = 99;
        assert(!TryEvaluateEasing(kind, 0.5, nan, result) && result == 99);
    }
    result = 99;
    assert(!TryEvaluateEasing(ECurveEasing::Power, 0.5, 0, result) && result == 99);
    assert(!TryEvaluateEasing(ECurveEasing::Expo, 0.5, 1.1, result) && result == 99);
    assert(!TryEvaluateEasing(static_cast<ECurveEasing>(255), 0.5, 1, result) && result == 99);

    // 既存入力のJSON enum/既定値を変えず、独立の従来式とfloat出力まで一致させる。
    for (const auto kind : {EInputResponseCurve::Linear, EInputResponseCurve::Power, EInputResponseCurve::Expo})
    {
        InputAxisResponse response{0.2f, kind, 2.2f, 0.75f};
        for (int step = -2000; step <= 2000; ++step)
        {
            const double x = step/1000.0;
            const double magnitude = std::fabs(x);
            const double t = magnitude <= response.DeadZone ? 0 : (std::fmin(magnitude, 1.0)-response.DeadZone)/(1.0-response.DeadZone);
            const double shaped = kind == EInputResponseCurve::Power ? std::pow(t,response.Gamma)
                : kind == EInputResponseCurve::Expo ? t*(1.0-response.Expo)+t*t*t*response.Expo : t;
            float actual = 99;
            assert(TryApplyAxisResponseWide(x,response,actual));
            assert(actual == static_cast<float>(x < 0 ? -shaped : shaped));
        }
    }
    const HapticsKeyframe hapticKeys[] = {{0, 0.2f}, {0.25, 0.8f}, {0.75, 0.4f}};
    HapticsEffectView effect;
    effect.Low = hapticKeys;
    for (int step = 0; step < 1000; ++step)
    {
        const double t = step/1000.0;
        const auto& a = hapticKeys[t <= 0.25 ? 0 : 1];
        const auto& b = hapticKeys[t <= 0.25 ? 1 : 2];
        const float expected = t >= 0.75 ? 0.4f : static_cast<float>(static_cast<double>(a.Value)+
            (static_cast<double>(b.Value)-a.Value)*((t-a.Time)/(b.Time-a.Time)));
        HapticsOutput output;
        assert(EvaluateHapticsEffect(effect,t,output) && output.Low == expected && output.High == 0);
    }
    std::cout << "CurvesTest PASS: piecewise_easing_input_haptics\n";
    return 0;
}
