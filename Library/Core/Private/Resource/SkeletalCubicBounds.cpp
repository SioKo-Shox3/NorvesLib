#include "Resource/SkeletalCubicBounds.h"
#include <algorithm>
#include <cfenv>
#include <cmath>
#include <limits>

namespace NorvesLib::Core::Skeletal
{
    namespace
    {
        using I = CubicInterval;
        constexpr double Infinity = std::numeric_limits<double>::infinity();
        I Bad()
        {
            return {Infinity, -Infinity};
        }
        bool Valid(I x)
        {
            return std::isfinite(x.Lower) && std::isfinite(x.Upper) && x.Lower <= x.Upper;
        }
        double Down(double x)
        {
            return std::nextafter(x, -Infinity);
        }
        double Up(double x)
        {
            return std::nextafter(x, Infinity);
        }
        I Exact(double x)
        {
            return {x, x};
        }
        bool Supported()
        {
#if defined(__FAST_MATH__) || defined(_M_FP_FAST) || (defined(__FINITE_MATH_ONLY__) && __FINITE_MATH_ONLY__ > 0) || defined(__ASSOCIATIVE_MATH__) || defined(__RECIPROCAL_MATH__)
            return false;
#else
            if (!std::numeric_limits<double>::is_iec559 || std::numeric_limits<double>::digits != 53 ||
                std::fegetround() != FE_TONEAREST)
            {
                return false;
            }
            volatile double minimum = std::numeric_limits<double>::min();
            volatile double half = 0.5;
            volatile double subnormal = minimum * half;
            volatile double tiny = std::numeric_limits<double>::denorm_min();
            volatile double one = 1.0;
            volatile double preserved = tiny * one;
            return subnormal != 0 && preserved != 0;
#endif
        }
        I Add(I a, I b)
        {
            if (!Valid(a) || !Valid(b))
            {
                return Bad();
            }
            return {Down(a.Lower + b.Lower), Up(a.Upper + b.Upper)};
        }
        I Sub(I a, I b)
        {
            if (!Valid(a) || !Valid(b))
            {
                return Bad();
            }
            return {Down(a.Lower - b.Upper), Up(a.Upper - b.Lower)};
        }
        I Mul(I a, I b)
        {
            if (!Valid(a) || !Valid(b))
            {
                return Bad();
            }
            const double values[] = {a.Lower * b.Lower, a.Lower * b.Upper, a.Upper * b.Lower, a.Upper * b.Upper};
            for (double value : values)
            {
                if (!std::isfinite(value))
                {
                    return Bad();
                }
            }
            return {Down(*std::min_element(values, values + 4)), Up(*std::max_element(values, values + 4))};
        }
        I DivPositive(I a, I b)
        {
            if (!Valid(a) || !Valid(b) || b.Lower <= 0)
            {
                return Bad();
            }
            return Mul(a, {Down(1.0 / b.Upper), Up(1.0 / b.Lower)});
        }
        I Square(I a)
        {
            if (!Valid(a))
            {
                return Bad();
            }
            const double high = std::max(std::abs(a.Lower), std::abs(a.Upper));
            const double low = a.Lower <= 0 && a.Upper >= 0 ? 0 : std::min(std::abs(a.Lower), std::abs(a.Upper));
            return {low == 0 ? 0 : std::max(0.0, Down(low * low)), Up(high * high)};
        }
        I Sqrt(I a)
        {
            if (!Valid(a) || a.Upper < 0)
            {
                return Bad();
            }
            return {a.Lower <= 0 ? 0 : std::max(0.0, Down(std::sqrt(a.Lower))), Up(std::sqrt(a.Upper))};
        }
        I LengthSquared(const CubicBoundedPoint& point, uint32_t dimensions)
        {
            I sum = Exact(0);
            for (uint32_t component = 0; component < dimensions; ++component)
            {
                sum = Add(sum, Square(point.Values[component]));
            }
            if (Valid(sum))
            {
                sum.Lower = std::max(0.0, sum.Lower);
            }
            return sum;
        }
        I Length(const CubicBoundedPoint& point, uint32_t dimensions)
        {
            return Sqrt(LengthSquared(point, dimensions));
        }
        bool Valid(const CubicBoundedPoint& point, uint32_t dimensions)
        {
            for (uint32_t component = 0; component < dimensions; ++component)
            {
                if (!Valid(point.Values[component]))
                {
                    return false;
                }
            }
            return true;
        }
        bool Valid(const CubicBezierBounds& curve)
        {
            if (curve.Dimensions != 3 && curve.Dimensions != 4)
            {
                return false;
            }
            for (const auto& point : curve.Controls)
            {
                if (!Valid(point, curve.Dimensions))
                {
                    return false;
                }
            }
            return true;
        }
        CubicBoundedPoint Blend(const CubicBoundedPoint& a, const CubicBoundedPoint& b, I t, uint32_t dimensions)
        {
            CubicBoundedPoint out;
            const I oneMinus = Sub(Exact(1), t);
            for (uint32_t component = 0; component < dimensions; ++component)
            {
                out.Values[component] = Add(Mul(a.Values[component], oneMinus), Mul(b.Values[component], t));
            }
            return out;
        }
        CubicBoundedPoint FloatPoint(const CubicFloatPoint& value)
        {
            CubicBoundedPoint out;
            for (uint32_t component = 0; component < 4; ++component)
            {
                out.Values[component] = Exact(value.Values[component]);
            }
            return out;
        }
        bool Normalize(CubicBoundedPoint& point)
        {
            const I length = Length(point, 4);
            if (!Valid(length) || length.Lower <= 0)
            {
                return false;
            }
            for (auto& component : point.Values)
            {
                component = DivPositive(component, length);
            }
            return Valid(point, 4);
        }
        double MaxDifference(const CubicBezierBounds& first, const CubicBezierBounds& second)
        {
            double maximum = 0;
            for (uint32_t control = 0; control < 4; ++control)
            {
                CubicBoundedPoint difference;
                for (uint32_t component = 0; component < first.Dimensions; ++component)
                {
                    difference.Values[component] = Sub(first.Controls[control].Values[component], second.Controls[control].Values[component]);
                }
                const I length = Length(difference, first.Dimensions);
                if (!Valid(length))
                {
                    return Infinity;
                }
                maximum = std::max(maximum, length.Upper);
            }
            return maximum;
        }
    }

    static CubicBoundsStatus BuildHermiteBezierInterval(const CubicPoint& start, const CubicPoint& end,
        const CubicPoint& outgoing, const CubicPoint& incoming, CubicInterval duration, uint32_t dimensions,
        CubicBezierBounds& out) noexcept
    {
        if (!Supported())
        {
            return CubicBoundsStatus::UnsupportedArithmetic;
        }
        if ((dimensions != 3 && dimensions != 4) || !Valid(duration) || duration.Lower <= 0)
        {
            return CubicBoundsStatus::InvalidInput;
        }
        CubicBezierBounds candidate; candidate.Dimensions = dimensions;
        const I scale = DivPositive(duration, Exact(3));
        for (uint32_t component = 0; component < dimensions; ++component)
        {
            const I a = Exact(start.Values[component]), z = Exact(end.Values[component]);
            const I firstTangent = Exact(outgoing.Values[component]), lastTangent = Exact(incoming.Values[component]);
            if (!Valid(a) || !Valid(z) || !Valid(firstTangent) || !Valid(lastTangent))
            {
                return CubicBoundsStatus::InvalidInput;
            }
            candidate.Controls[0].Values[component] = a;
            candidate.Controls[1].Values[component] = Add(a, Mul(scale, firstTangent));
            candidate.Controls[2].Values[component] = Sub(z, Mul(scale, lastTangent));
            candidate.Controls[3].Values[component] = z;
        }
        if (!Valid(candidate))
        {
            return CubicBoundsStatus::InvalidInput;
        }
        out = candidate;
        return CubicBoundsStatus::Success;
    }

    CubicBoundsStatus BuildHermiteBezierBounds(const CubicPoint& start, const CubicPoint& end,
        const CubicPoint& outgoing, const CubicPoint& incoming, double duration, uint32_t dimensions,
        CubicBezierBounds& out) noexcept
    {
        return BuildHermiteBezierInterval(start, end, outgoing, incoming, Exact(duration), dimensions, out);
    }

    CubicBoundsStatus BuildHermiteBezierAtTimes(const CubicPoint& start, const CubicPoint& end,
        const CubicPoint& outgoing, const CubicPoint& incoming, float startTime, float endTime, uint32_t dimensions,
        CubicBezierBounds& out) noexcept
    {
        if (!Supported())
        {
            return CubicBoundsStatus::UnsupportedArithmetic;
        }
        if (!std::isfinite(startTime) || !std::isfinite(endTime) || startTime < 0 || endTime <= startTime)
        {
            return CubicBoundsStatus::InvalidInput;
        }
        return BuildHermiteBezierInterval(start, end, outgoing, incoming,
            Sub(Exact(endTime), Exact(startTime)), dimensions, out);
    }

    CubicBoundsStatus ScaleCubicBounds(const CubicBezierBounds& curve, double scale, CubicBezierBounds& out) noexcept
    {
        if (!Supported())
        {
            return CubicBoundsStatus::UnsupportedArithmetic;
        }
        if (!Valid(curve) || !std::isfinite(scale) || scale <= 0)
        {
            return CubicBoundsStatus::InvalidInput;
        }
        auto candidate = curve;
        for (auto& point : candidate.Controls)
        {
            for (uint32_t component = 0; component < curve.Dimensions; ++component)
            {
                point.Values[component] = Mul(point.Values[component], Exact(scale));
            }
        }
        if (!Valid(candidate))
        {
            return CubicBoundsStatus::InvalidInput;
        }
        out = candidate;
        return CubicBoundsStatus::Success;
    }

    CubicBoundsStatus EvaluateCubicBounds(const CubicBezierBounds& curve, double parameter, CubicBoundedPoint& out) noexcept
    {
        if (!Supported())
        {
            return CubicBoundsStatus::UnsupportedArithmetic;
        }
        if (!Valid(curve) || !std::isfinite(parameter) || parameter < 0 || parameter > 1)
        {
            return CubicBoundsStatus::InvalidInput;
        }
        if (parameter == 0)
        {
            out = curve.Controls[0]; return CubicBoundsStatus::Success;
        }
        if (parameter == 1)
        {
            out = curve.Controls[3]; return CubicBoundsStatus::Success;
        }
        CubicBoundedPoint level[4];
        for (uint32_t control = 0; control < 4; ++control)
        {
            level[control] = curve.Controls[control];
        }
        for (uint32_t count = 3; count > 0; --count)
        {
            for (uint32_t index = 0; index < count; ++index)
            {
                level[index] = Blend(level[index], level[index + 1], Exact(parameter), curve.Dimensions);
            }
        }
        if (!Valid(level[0], curve.Dimensions))
        {
            return CubicBoundsStatus::InvalidInput;
        }
        out = level[0]; return CubicBoundsStatus::Success;
    }

    static CubicBoundsStatus SplitCubicBoundsInterval(const CubicBezierBounds& curve, CubicInterval parameter, CubicBezierBounds& left, CubicBezierBounds& right) noexcept
    {
        if (!Supported())
        {
            return CubicBoundsStatus::UnsupportedArithmetic;
        }
        if (&left == &right || !Valid(curve) || !Valid(parameter) || parameter.Lower <= 0 || parameter.Upper >= 1)
        {
            return CubicBoundsStatus::InvalidInput;
        }
        CubicBezierBounds first, second;
        first.Dimensions = second.Dimensions = curve.Dimensions;
        CubicBoundedPoint level[4];
        for (uint32_t control = 0; control < 4; ++control)
        {
            level[control] = curve.Controls[control];
        }
        first.Controls[0] = level[0]; second.Controls[3] = level[3];
        for (uint32_t depth = 1; depth <= 3; ++depth)
        {
            for (uint32_t index = 0; index < 4 - depth; ++index)
            {
                level[index] = Blend(level[index], level[index + 1], parameter, curve.Dimensions);
            }
            first.Controls[depth] = level[0]; second.Controls[3 - depth] = level[3 - depth];
        }
        if (!Valid(first) || !Valid(second))
        {
            return CubicBoundsStatus::InvalidInput;
        }
        left = first; right = second; return CubicBoundsStatus::Success;
    }

    CubicBoundsStatus SplitCubicBounds(const CubicBezierBounds& curve, double parameter,
        CubicBezierBounds& left, CubicBezierBounds& right) noexcept
    {
        return SplitCubicBoundsInterval(curve, Exact(parameter), left, right);
    }

    CubicBoundsStatus SplitCubicBoundsAtTime(const CubicBezierBounds& curve,
        float startTime, float endTime, float splitTime, CubicBezierBounds& left, CubicBezierBounds& right) noexcept
    {
        if (!Supported())
        {
            return CubicBoundsStatus::UnsupportedArithmetic;
        }
        if (!std::isfinite(startTime) || !std::isfinite(endTime) || !std::isfinite(splitTime) ||
            startTime < 0 || splitTime <= startTime || splitTime >= endTime)
        {
            return CubicBoundsStatus::InvalidInput;
        }
        const I numerator = Sub(Exact(splitTime), Exact(startTime));
        const I denominator = Sub(Exact(endTime), Exact(startTime));
        return SplitCubicBoundsInterval(curve, DivPositive(numerator, denominator), left, right);
    }

    CubicChordBound BoundVectorChord(const CubicBezierBounds& curve, const CubicFloatPoint& start, const CubicFloatPoint& end) noexcept
    {
        if (!Supported())
        {
            return {CubicBoundsStatus::UnsupportedArithmetic};
        }
        if (!Valid(curve))
        {
            return {};
        }
        CubicBezierBounds chord; chord.Dimensions = curve.Dimensions;
        chord.Controls[0] = FloatPoint(start); chord.Controls[3] = FloatPoint(end);
        chord.Controls[1] = Blend(chord.Controls[0], chord.Controls[3], DivPositive(Exact(1), Exact(3)), curve.Dimensions);
        chord.Controls[2] = Blend(chord.Controls[0], chord.Controls[3], DivPositive(Exact(2), Exact(3)), curve.Dimensions);
        if (!Valid(chord))
        {
            return {};
        }
        const double error = MaxDifference(curve, chord);
        if (!std::isfinite(error))
        {
            return {};
        }
        return {CubicBoundsStatus::Success, error, 0};
    }

    CubicChordBound BoundRotationChord(const CubicBezierBounds& curve, const CubicFloatPoint& start, const CubicFloatPoint& end) noexcept
    {
        if (!Supported())
        {
            return {CubicBoundsStatus::UnsupportedArithmetic};
        }
        if (!Valid(curve) || curve.Dimensions != 4)
        {
            return {};
        }
        auto a = FloatPoint(start), z = FloatPoint(end);
        const I storedA = Length(a, 4), storedZ = Length(z, 4);
        if (!Normalize(a) || !Normalize(z))
        {
            return {};
        }
        I dot = Exact(0);
        CubicBoundedPoint delta;
        for (uint32_t component = 0; component < 4; ++component)
        {
            dot = Add(dot, Mul(a.Values[component], z.Values[component]));
            delta.Values[component] = Sub(a.Values[component], z.Values[component]);
        }
        if (!Valid(dot) || dot.Lower <= 0)
        {
            return {CubicBoundsStatus::Uncertified};
        }
        const I r0 = Length(curve.Controls[0], 4), r1 = Length(curve.Controls[3], 4);
        if (!Valid(r0) || !Valid(r1) || r0.Lower <= 0 || r1.Lower <= 0)
        {
            return {CubicBoundsStatus::Uncertified};
        }
        // 正の線形半径rhoと線形chord Rの積を二次Bezierとして作り、三次へ上げる。
        CubicBoundedPoint quadratic[3];
        for (uint32_t component = 0; component < 4; ++component)
        {
            quadratic[0].Values[component] = Mul(r0, a.Values[component]);
            quadratic[1].Values[component] = DivPositive(Add(Mul(r0, z.Values[component]), Mul(r1, a.Values[component])), Exact(2));
            quadratic[2].Values[component] = Mul(r1, z.Values[component]);
        }
        CubicBezierBounds reference; reference.Dimensions = 4;
        reference.Controls[0] = quadratic[0]; reference.Controls[3] = quadratic[2];
        reference.Controls[1] = Blend(quadratic[0], quadratic[1], DivPositive(Exact(2), Exact(3)), 4);
        reference.Controls[2] = Blend(quadratic[1], quadratic[2], DivPositive(Exact(1), Exact(3)), 4);
        const I radius = Exact(std::min(r0.Lower, r1.Lower));
        const I chordMinimum = Sqrt(DivPositive(Add(Exact(1), Exact(dot.Lower)), Exact(2)));
        const I minimum = Mul(radius, chordMinimum);
        const double error = MaxDifference(curve, reference);
        if (!Valid(minimum) || !std::isfinite(error) || minimum.Lower <= error)
        {
            return {CubicBoundsStatus::Uncertified};
        }
        // 正規化誤差<=4E/m、NLERP対SLERPのSO(3)角度差<=|A-Z|^2（同一半球）。
        const I normalizedError = Mul(Exact(4), DivPositive(Exact(error), Exact(minimum.Lower)));
        const I angular = Add(normalizedError, LengthSquared(delta, 4));
        const I curveMinimum = Sub(Exact(minimum.Lower), Exact(error));
        if (!Valid(angular) || !Valid(curveMinimum) || curveMinimum.Lower <= 0)
        {
            return {CubicBoundsStatus::Uncertified};
        }
        return {CubicBoundsStatus::Success, angular.Upper, curveMinimum.Lower, dot.Lower,
            std::min(storedA.Lower, storedZ.Lower), std::max(storedA.Upper, storedZ.Upper)};
    }
} // namespace NorvesLib::Core::Skeletal
