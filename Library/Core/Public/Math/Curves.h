#pragma once

#include "Container/Span.h"
#include <cmath>
#include <cstdint>
#include <type_traits>

namespace NorvesLib::Math
{
    // G1の最小曲線。時間は非負・厳密昇順、値は有限float。所有権を持たない。
    struct CurveKeyframe
    {
        double Time = 0;
        float Value = 0;
    };

    template<typename Key>
    inline bool IsValidPiecewiseLinearCurve(Core::Container::Span<const Key> keys)
    {
        static_assert(std::is_same_v<decltype(Key::Time), double> && std::is_same_v<decltype(Key::Value), float>);
        if (!keys.empty() && keys.data() == nullptr)
        {
            return false;
        }
        double previous = -1;
        for (size_t index = 0; index < keys.size(); ++index)
        {
            const auto& key = keys[index];
            if (!std::isfinite(key.Time) || key.Time < 0 || key.Time <= previous || !std::isfinite(key.Value))
            {
                return false;
            }
            previous = key.Time;
        }
        return true;
    }

    // 検証済みkey列と有限time専用。入力定義を保有する呼出側で検証を済ませる。
    // 空は0、区間外は端点保持。値の用途別clampは呼出側が行う。
    template<typename Key>
    inline double EvaluatePiecewiseLinearUnchecked(Core::Container::Span<const Key> keys, double time)
    {
        static_assert(std::is_same_v<decltype(Key::Time), double> && std::is_same_v<decltype(Key::Value), float>);
        if (keys.empty())
        {
            return 0;
        }
        if (time <= keys.front().Time)
        {
            return keys.front().Value;
        }
        for (size_t index = 1; index < keys.size(); ++index)
        {
            const auto& right = keys[index];
            if (time <= right.Time)
            {
                const auto& left = keys[index-1];
                const double alpha = (time-left.Time)/(right.Time-left.Time);
                return std::lerp(static_cast<double>(left.Value), static_cast<double>(right.Value), alpha);
            }
        }
        return keys.back().Value;
    }

    // 失敗時はoutValueを保持。timeは有限なら負も許可し先頭値を返す。
    template<typename Key>
    inline bool TryEvaluatePiecewiseLinear(Core::Container::Span<const Key> keys, double time, double& outValue)
    {
        if (!std::isfinite(time) || !IsValidPiecewiseLinearCurve(keys))
        {
            return false;
        }
        outValue = EvaluatePiecewiseLinearUnchecked(keys, time);
        return true;
    }

    enum class ECurveEasing : uint8_t
    {
        Linear, Power, Expo, SmoothStep
    };

    // 正規化値[0,1]を評価する。Powerのparameterは正、Expoは[0,1]。
    // Linear/SmoothStepはparameterを使わないが有限であることを要求する。失敗時は出力を保持。
    inline bool TryEvaluateEasing(ECurveEasing kind, double value, double parameter, double& outValue)
    {
        if (!std::isfinite(value) || value < 0 || value > 1 || !std::isfinite(parameter))
        {
            return false;
        }
        double result;
        switch (kind)
        {
        case ECurveEasing::Linear:
            result = value;
            break;
        case ECurveEasing::Power:
            if (parameter <= 0)
            {
                return false;
            }
            result = std::pow(value, parameter);
            break;
        case ECurveEasing::Expo:
            if (parameter < 0 || parameter > 1)
            {
                return false;
            }
            result = value*(1.0-parameter) + value*value*value*parameter;
            break;
        case ECurveEasing::SmoothStep:
            result = value*value*(3.0-2.0*value);
            break;
        default:
            return false;
        }
        outValue = result;
        return true;
    }
}
