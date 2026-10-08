#include "Resource/SkeletalCubicBake.h"
#include "Math/MathTypes.h"
#include <algorithm>
#include <cfenv>
#include <cmath>
#include <limits>
#include <numeric>

namespace NorvesLib::Core::Skeletal
{
    namespace
    {
        constexpr uint32_t DepthLimit = MaximumCubicSubdivisionDepth;
        constexpr double Epsilon = std::numeric_limits<float>::epsilon();
        constexpr double RotationNumericBudget = CubicRotationNumericErrorBudget;
        static_assert(Math::Constants::EPSILON == std::numeric_limits<float>::epsilon());
        struct Region
        {
            uintptr_t Begin = 0;
            uintptr_t End = 0;
        };
        bool MakeRegion(const void* pointer, size_t count, size_t size, size_t alignment, Region& out)
        {
            const auto begin = reinterpret_cast<uintptr_t>(pointer);
            if ((count != 0 && !pointer) || begin % alignment != 0 || count > SIZE_MAX / size)
            {
                return false;
            }
            const size_t bytes = count * size;
            if (begin > UINTPTR_MAX - bytes)
            {
                return false;
            }
            out = {begin, begin + bytes};
            return true;
        }
        bool Overlaps(const Region& a, const Region& b)
        {
            return a.Begin < a.End && b.Begin < b.End && a.Begin < b.End && b.Begin < a.End;
        }
        bool FloatArithmeticSupported()
        {
            if (!std::numeric_limits<float>::is_iec559 || std::numeric_limits<float>::digits != 24 ||
                std::fegetround() != FE_TONEAREST)
            {
                return false;
            }
            volatile float minimum = std::numeric_limits<float>::min();
            volatile float half = 0.5f;
            volatile float subnormal = minimum * half;
            volatile float tiny = std::numeric_limits<float>::denorm_min();
            volatile float one = 1;
            volatile float preserved = tiny * one;
            return subnormal != 0 && preserved != 0;
        }
        CubicPoint Promote(const CubicFloatPoint& point)
        {
            CubicPoint result;
            for (size_t component = 0; component < 4; ++component)
            {
                result.Values[component] = point.Values[component];
            }
            return result;
        }
        CubicBakeStatus StorePoint(const CubicBoundedPoint& point, float time, bool bRotation, CubicBakeKey& out)
        {
            double values[4] = {};
            const size_t dimensions = bRotation ? 4 : 3;
            double lengthSquared = 0;
            for (size_t component = 0; component < dimensions; ++component)
            {
                values[component] = std::midpoint(point.Values[component].Lower, point.Values[component].Upper);
                if (!std::isfinite(values[component]))
                {
                    return CubicBakeStatus::UnrepresentableValue;
                }
                if (bRotation)
                {
                    lengthSquared += values[component] * values[component];
                }
            }
            if (bRotation)
            {
                if (!std::isfinite(lengthSquared) || lengthSquared <= 0)
                {
                    return CubicBakeStatus::InvalidQuaternion;
                }
                const double length = std::sqrt(lengthSquared);
                for (double& value : values)
                {
                    value /= length;
                }
            }
            CubicBakeKey result;
            result.Time = time;
            for (size_t component = 0; component < dimensions; ++component)
            {
                if (std::abs(values[component]) > std::numeric_limits<float>::max())
                {
                    return CubicBakeStatus::UnrepresentableValue;
                }
                result.Value.Values[component] = static_cast<float>(values[component]);
            }
            out = result;
            return CubicBakeStatus::Success;
        }
        double VectorNumericBudget(const CubicFloatPoint& a, const CubicFloatPoint& z)
        {
            double first = 0;
            double last = 0;
            constexpr double MaximumComponent = double(std::numeric_limits<float>::max()) / 16;
            for (size_t component = 0; component < 3; ++component)
            {
                if (std::abs(double(a.Values[component])) > MaximumComponent ||
                    std::abs(double(z.Values[component])) > MaximumComponent)
                {
                    return std::numeric_limits<double>::infinity();
                }
                first = std::nextafter(first + std::abs(double(a.Values[component])), std::numeric_limits<double>::infinity());
                last = std::nextafter(last + std::abs(double(z.Values[component])), std::numeric_limits<double>::infinity());
            }
            return std::nextafter(64 * Epsilon * std::max({1.0, first, last}), std::numeric_limits<double>::infinity());
        }
        struct Work
        {
            CubicBezierBounds Curve;
            CubicBakeKey First;
            CubicBakeKey Last;
            uint32_t Depth = 0;
        };
    }

    CubicBakeResult BakeCubicChannel(Container::Span<const CubicBakeInputKey> input,
        const CubicBakeOptions& options, Container::Span<CubicBakeKey> workspace,
        Container::Span<CubicBakeKey> out) noexcept
    {
        const bool bRotation = options.Kind == CubicBakeKind::Rotation;
        if ((options.Kind != CubicBakeKind::Vector3 && !bRotation) || !std::isfinite(options.Tolerance) ||
            options.Tolerance <= 0 || !std::isfinite(options.ValueScale) || options.ValueScale <= 0 ||
            (bRotation && options.ValueScale != 1) || options.MaximumDepth > DepthLimit || options.MaximumSamples < 2 ||
            options.MaximumSamples > MaximumCubicSamplesPerChannel)
        {
            return {CubicBakeStatus::InvalidOptions};
        }
        Region sourceRegion, workRegion, outRegion, optionsRegion;
        if (input.size() < 2 || input.size() > UINT32_MAX ||
            !MakeRegion(input.data(), input.size(), sizeof(CubicBakeInputKey), alignof(CubicBakeInputKey), sourceRegion) ||
            !MakeRegion(workspace.data(), workspace.size(), sizeof(CubicBakeKey), alignof(CubicBakeKey), workRegion) ||
            !MakeRegion(out.data(), out.size(), sizeof(CubicBakeKey), alignof(CubicBakeKey), outRegion) ||
            !MakeRegion(&options, 1, sizeof(options), alignof(CubicBakeOptions), optionsRegion))
        {
            return {CubicBakeStatus::InvalidInput};
        }
        if (Overlaps(sourceRegion, workRegion) || Overlaps(sourceRegion, outRegion) || Overlaps(workRegion, outRegion) ||
            Overlaps(optionsRegion, workRegion) || Overlaps(optionsRegion, outRegion))
        {
            return {CubicBakeStatus::OverlappingStorage};
        }
        const size_t capacity = std::min(workspace.size(), out.size());
        if (capacity < input.size())
        {
            return {CubicBakeStatus::InsufficientStorage};
        }
        if (options.MaximumSamples < input.size())
        {
            return {CubicBakeStatus::SampleLimitExceeded};
        }
        CubicBezierBounds arithmeticProbe;
        if (BuildHermiteBezierBounds({}, {}, {}, {}, 1, 3, arithmeticProbe) == CubicBoundsStatus::UnsupportedArithmetic ||
            !FloatArithmeticSupported())
        {
            return {CubicBakeStatus::UnsupportedArithmetic};
        }
        if (options.Tolerance <= (bRotation ? RotationNumericBudget : CubicVectorNumericErrorFloor))
        {
            return {CubicBakeStatus::NumericBudgetExceeded};
        }
        const uint32_t dimensions = bRotation ? 4 : 3;
        for (size_t index = 0; index < input.size(); ++index)
        {
            const auto& key = input[index];
            if (!std::isfinite(key.Time) || key.Time < 0 || (index != 0 && key.Time <= input[index - 1].Time))
            {
                return {CubicBakeStatus::InvalidInput};
            }
            if (index != 0 && key.Time - input[index - 1].Time <= Math::Constants::EPSILON)
            {
                return {CubicBakeStatus::ShortInterval};
            }
            for (size_t component = 0; component < dimensions; ++component)
            {
                if (!std::isfinite(key.Value.Values[component]) || !std::isfinite(key.Incoming.Values[component]) ||
                    !std::isfinite(key.Outgoing.Values[component]))
                {
                    return {CubicBakeStatus::InvalidInput};
                }
            }
        }
        CubicBakeResult result;
        size_t used = 0;
        Work stack[DepthLimit + 2];
        for (size_t interval = 1; interval < input.size(); ++interval)
        {
            const auto& first = input[interval - 1];
            const auto& last = input[interval];
            Work initial;
            if (BuildHermiteBezierAtTimes(Promote(first.Value), Promote(last.Value), Promote(first.Outgoing),
                Promote(last.Incoming), first.Time, last.Time, dimensions, initial.Curve) != CubicBoundsStatus::Success ||
                ScaleCubicBounds(initial.Curve, options.ValueScale, initial.Curve) != CubicBoundsStatus::Success)
            {
                return {CubicBakeStatus::Uncertified};
            }
            auto status = StorePoint(initial.Curve.Controls[0], first.Time, bRotation, initial.First);
            if (status != CubicBakeStatus::Success)
            {
                return {status};
            }
            status = StorePoint(initial.Curve.Controls[3], last.Time, bRotation, initial.Last);
            if (status != CubicBakeStatus::Success)
            {
                return {status};
            }
            if (used == 0)
            {
                workspace[used++] = initial.First;
            }
            else
            {
                initial.First = workspace[used - 1];
            }
            size_t stackSize = 1;
            stack[0] = initial;
            while (stackSize != 0)
            {
                const Work current = stack[--stackSize];
                const float duration = current.Last.Time - current.First.Time;
                if (!std::isfinite(duration) || duration <= Math::Constants::EPSILON)
                {
                    return {CubicBakeStatus::ShortInterval};
                }
                const auto bound = bRotation ? BoundRotationChord(current.Curve, current.First.Value, current.Last.Value) :
                    BoundVectorChord(current.Curve, current.First.Value, current.Last.Value);
                const double numericBudget = bRotation ? RotationNumericBudget : VectorNumericBudget(current.First.Value, current.Last.Value);
                if (numericBudget >= options.Tolerance)
                {
                    return {CubicBakeStatus::NumericBudgetExceeded};
                }
                const bool bProfile = !bRotation || (bound.StoredDotLower > 0.9996 &&
                    bound.StoredNormLower >= 0.99 && bound.StoredNormUpper <= 1.01);
                const double error = std::nextafter(bound.ErrorUpper + numericBudget, std::numeric_limits<double>::infinity());
                if (bound.Status == CubicBoundsStatus::Success && bProfile && error <= options.Tolerance)
                {
                    if (used >= options.MaximumSamples)
                    {
                        return {CubicBakeStatus::SampleLimitExceeded};
                    }
                    if (used >= capacity)
                    {
                        return {CubicBakeStatus::InsufficientStorage};
                    }
                    if (current.Last.Time <= workspace[used - 1].Time)
                    {
                        return {CubicBakeStatus::TimeCollision};
                    }
                    workspace[used++] = current.Last;
                    result.MaximumDepthUsed = std::max(result.MaximumDepthUsed, current.Depth);
                    result.MaximumAcceptedErrorUpper = std::max(result.MaximumAcceptedErrorUpper, error);
                    continue;
                }
                if (current.Depth >= options.MaximumDepth || stackSize + 2 > DepthLimit + 2)
                {
                    return {CubicBakeStatus::DepthExceeded};
                }
                const float middle = static_cast<float>(std::midpoint(double(current.First.Time), double(current.Last.Time)));
                if (middle <= current.First.Time || middle >= current.Last.Time)
                {
                    return {CubicBakeStatus::TimeCollision};
                }
                Work left, right;
                if (SplitCubicBoundsAtTime(current.Curve, current.First.Time, current.Last.Time, middle, left.Curve, right.Curve) != CubicBoundsStatus::Success)
                {
                    return {CubicBakeStatus::Uncertified};
                }
                CubicBakeKey middleKey;
                status = StorePoint(left.Curve.Controls[3], middle, bRotation, middleKey);
                if (status != CubicBakeStatus::Success)
                {
                    return {status};
                }
                left.First = current.First;
                left.Last = middleKey;
                right.First = middleKey;
                right.Last = current.Last;
                left.Depth = right.Depth = current.Depth + 1;
                stack[stackSize++] = right;
                stack[stackSize++] = left;
            }
        }
        std::copy_n(workspace.data(), used, out.data());
        result.Status = CubicBakeStatus::Success;
        result.SampleCount = static_cast<uint32_t>(used);
        return result;
    }
} // namespace NorvesLib::Core::Skeletal
