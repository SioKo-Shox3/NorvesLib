#pragma once

#include "Math/Vector2.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace NorvesLib::Core::Input
{
    enum class EInputResponseCurve : uint8_t { Linear, Power, Expo };

    struct InputAxisResponse
    {
        float DeadZone = 0.0f;
        EInputResponseCurve Curve = EInputResponseCurve::Linear;
        float Gamma = 1.0f;
        float Expo = 0.0f;
    };

    inline bool IsValidAxisResponse(const InputAxisResponse& response)
    {
        return std::isfinite(response.DeadZone) && response.DeadZone >= 0.0f && response.DeadZone < 1.0f &&
            std::isfinite(response.Gamma) && response.Gamma > 0.0f &&
            std::isfinite(response.Expo) && response.Expo >= 0.0f && response.Expo <= 1.0f &&
            (response.Curve == EInputResponseCurve::Linear || response.Curve == EInputResponseCurve::Power ||
             response.Curve == EInputResponseCurve::Expo);
    }

    namespace Detail
    {
        // 引数は検証済み、長さは[0,1]。deadzone後の長さへ曲線を適用する。
        inline double ShapeAxisMagnitude(double length, const InputAxisResponse& response)
        {
            if (length <= response.DeadZone) return 0.0;
            const double value = (std::min(length, 1.0) - response.DeadZone) / (1.0 - response.DeadZone);
            switch (response.Curve)
            {
            case EInputResponseCurve::Power: return std::pow(value, response.Gamma);
            case EInputResponseCurve::Expo: return value * (1.0 - response.Expo) + value*value*value * response.Expo;
            default: return value;
            }
        }
    }

    // 正規化軸専用。失敗はfalseと0。mouseのフレーム変位には適用しない。
    inline bool TryApplyAxisResponse(float input, const InputAxisResponse& response, float& outValue)
    {
        outValue = 0.0f;
        if (!std::isfinite(input) || !IsValidAxisResponse(response)) return false;
        const double magnitude = Detail::ShapeAxisMagnitude(std::fabs(static_cast<double>(input)), response);
        outValue = static_cast<float>(input < 0.0f ? -magnitude : magnitude);
        return true;
    }

    // 放射状deadzoneと長さのcurveにより方向を維持する。有限の過大入力も長さ1へ制限。
    inline bool TryApplyRadialAxisResponse(const Math::Vector2& input, const InputAxisResponse& response,
        Math::Vector2& outValue)
    {
        // 入出力aliasを許すため、先に値を保存する。
        const double x = input.x, y = input.y;
        outValue = Math::Vector2(0.0f, 0.0f);
        if (!std::isfinite(x) || !std::isfinite(y) || !IsValidAxisResponse(response)) return false;
        const double length = std::hypot(x, y);
        if (length == 0.0) return true;
        const double magnitude = Detail::ShapeAxisMagnitude(length, response);
        outValue = Math::Vector2(static_cast<float>((x / length) * magnitude),
            static_cast<float>((y / length) * magnitude));
        return true;
    }

    // 視点の単位を度/frameへ合わせる。mouseは変位なのでdtを掛けず、stickだけ実時間を掛ける。
    // stickはresponse適用後の[-1,1]。反転は呼出側が入力符号へ適用する。
    inline bool TryComputeLookDelta(float mouseDisplacement, float normalizedStick,
        float mouseDegreesPerUnit, float stickDegreesPerSecond, double unscaledDeltaSeconds, float& outDegrees)
    {
        outDegrees = 0.0f;
        if (!std::isfinite(mouseDisplacement) || !std::isfinite(normalizedStick) ||
            std::fabs(normalizedStick) > 1.0f || !std::isfinite(mouseDegreesPerUnit) || mouseDegreesPerUnit < 0.0f ||
            !std::isfinite(stickDegreesPerSecond) || stickDegreesPerSecond < 0.0f ||
            !std::isfinite(unscaledDeltaSeconds) || unscaledDeltaSeconds < 0.0) return false;
        const double delta = static_cast<double>(mouseDisplacement) * mouseDegreesPerUnit +
            static_cast<double>(normalizedStick) * stickDegreesPerSecond * unscaledDeltaSeconds;
        if (!std::isfinite(delta) || std::fabs(delta) > std::numeric_limits<float>::max()) return false;
        outDegrees = static_cast<float>(delta);
        return true;
    }
} // namespace NorvesLib::Core::Input
