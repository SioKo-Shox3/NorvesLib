#pragma once
#include <cmath>
#include <cstdint>
namespace NorvesLib::Core::Animation
{
    // Y上向きの平面剛体差分。移動側が消費し、Entityへ直接は書かない。
    struct RootMotionDelta
    {
        double X = 0, Z = 0, Yaw = 0;
        bool IsFinite() const noexcept
        {
            return std::isfinite(X) && std::isfinite(Z) && std::isfinite(Yaw);
        }
    };
    inline RootMotionDelta ComposeRootMotion(const RootMotionDelta& a, const RootMotionDelta& b)
    {
        const double c = std::cos(a.Yaw), s = std::sin(a.Yaw);
        return {a.X + c * b.X + s * b.Z, a.Z - s * b.X + c * b.Z, a.Yaw + b.Yaw};
    }
    inline RootMotionDelta InverseRootMotion(const RootMotionDelta& a)
    {
        const double c = std::cos(a.Yaw), s = std::sin(a.Yaw);
        return {-c * a.X + s * a.Z, -s * a.X - c * a.Z, -a.Yaw};
    }
    inline RootMotionDelta RootMotionBetween(const RootMotionDelta& a, const RootMotionDelta& b)
    {
        return ComposeRootMotion(InverseRootMotion(a), b);
    }
    inline RootMotionDelta ScaleRootMotion(const RootMotionDelta& value, double weight)
    {
        return {value.X * weight, value.Z * weight, value.Yaw * weight};
    }
    inline RootMotionDelta AddRootMotion(const RootMotionDelta& a, const RootMotionDelta& b)
    {
        return {a.X + b.X, a.Z + b.Z, a.Yaw + b.Yaw};
    }
    // 周回のXZを向き込みで累積する。逆再生も同じ演算で扱う。
    inline bool RepeatRootMotion(RootMotionDelta delta, int64_t cycles, RootMotionDelta& out)
    {
        uint64_t n = cycles < 0 ? uint64_t(-(cycles + 1)) + 1 : uint64_t(cycles);
        if (cycles < 0)
            delta = InverseRootMotion(delta);
        RootMotionDelta result;
        while (n)
        {
            if (n & 1)
                result = ComposeRootMotion(result, delta);
            n >>= 1;
            if (n)
                delta = ComposeRootMotion(delta, delta);
            if (!result.IsFinite() || !delta.IsFinite())
                return false;
        }
        out = result;
        return true;
    }
} // namespace NorvesLib::Core::Animation
