#pragma once

#include "Math/Quaternion.h"
#include <cmath>

namespace NorvesLib::Core::Animation::Detail
{
    // 実samplerとbakeの試験で同じ算術経路を使う。式と分岐を一元化する。
    inline Math::Quaternion NormalizeQuaternion(const Math::Quaternion& value)
    {
        const float lengthSquared = value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w;
        if (lengthSquared <= Math::Constants::EPSILON)
        {
            return Math::Quaternion::Identity;
        }
        const float inverseLength = 1.0f / std::sqrt(lengthSquared);
        return Math::Quaternion(
            value.x * inverseLength,
            value.y * inverseLength,
            value.z * inverseLength,
            value.w * inverseLength);
    }

    // clipの列quaternionを、Samplerの絶対local行quaternionへ変換する。
    inline Math::Quaternion SkeletalRotationFromColumn(float x, float y, float z, float w)
    {
        return NormalizeQuaternion(Math::Quaternion(-x, -y, -z, w));
    }

    inline Math::Quaternion Slerp(const Math::Quaternion& start, const Math::Quaternion& end, float alpha)
    {
        Math::Quaternion from = NormalizeQuaternion(start);
        Math::Quaternion to = NormalizeQuaternion(end);
        float dot = from.x * to.x + from.y * to.y + from.z * to.z + from.w * to.w;
        if (dot < 0.0f)
        {
            to = Math::Quaternion(-to.x, -to.y, -to.z, -to.w);
            dot = -dot;
        }
        if (dot > 0.9995f)
        {
            return NormalizeQuaternion(Math::Quaternion(
                from.x + (to.x - from.x) * alpha,
                from.y + (to.y - from.y) * alpha,
                from.z + (to.z - from.z) * alpha,
                from.w + (to.w - from.w) * alpha));
        }

        dot = std::fmax(-1.0f, std::fmin(1.0f, dot));
        const float theta = std::acos(dot);
        const float sinTheta = std::sin(theta);
        const float fromWeight = std::sin((1.0f - alpha) * theta) / sinTheta;
        const float toWeight = std::sin(alpha * theta) / sinTheta;
        return NormalizeQuaternion(Math::Quaternion(
            from.x * fromWeight + to.x * toWeight,
            from.y * fromWeight + to.y * toWeight,
            from.z * fromWeight + to.z * toWeight,
            from.w * fromWeight + to.w * toWeight));
    }

    inline float ComputeLinearAlpha(float previousTime, float nextTime, float timeSeconds)
    {
        const float duration = nextTime - previousTime;
        return duration > Math::Constants::EPSILON ? (timeSeconds - previousTime) / duration : 0.0f;
    }
} // namespace NorvesLib::Core::Animation::Detail
