#include "Animation/TwoBoneIK.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace NorvesLib::Core::Animation
{
    namespace
    {
        struct DoubleVector
        {
            double x;
            double y;
            double z;
        };

        struct DoubleQuaternion
        {
            double x;
            double y;
            double z;
            double w;
        };

        DoubleVector operator+(const DoubleVector& a, const DoubleVector& b) noexcept
        {
            return {a.x + b.x, a.y + b.y, a.z + b.z};
        }

        DoubleVector operator-(const DoubleVector& a, const DoubleVector& b) noexcept
        {
            return {a.x - b.x, a.y - b.y, a.z - b.z};
        }

        DoubleVector operator*(const DoubleVector& v, double scale) noexcept
        {
            return {v.x * scale, v.y * scale, v.z * scale};
        }

        DoubleVector ToDouble(const Math::Vector3& value) noexcept
        {
            return {double(value.x), double(value.y), double(value.z)};
        }

        Math::Vector3 ToFloat(const DoubleVector& value) noexcept
        {
            return {float(value.x), float(value.y), float(value.z)};
        }

        Math::Quaternion ToFloat(const DoubleQuaternion& value) noexcept
        {
            return {float(value.x), float(value.y), float(value.z), float(value.w)};
        }

        double Dot(const DoubleVector& a, const DoubleVector& b) noexcept
        {
            return a.x * b.x + a.y * b.y + a.z * b.z;
        }

        DoubleVector Cross(const DoubleVector& a, const DoubleVector& b) noexcept
        {
            return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
        }

        double Length(const DoubleVector& value) noexcept
        {
            return std::sqrt(Dot(value, value));
        }

        DoubleVector Normalize(const DoubleVector& value, double length) noexcept
        {
            return value * (1.0 / length);
        }

        bool IsFinite(const Math::Vector3& value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool IsFinite(const Math::Quaternion& value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) &&
                   std::isfinite(value.w);
        }

        bool CanStore(double value) noexcept
        {
            return std::isfinite(value) && std::abs(value) <= double(std::numeric_limits<float>::max());
        }

        bool CanStore(const DoubleVector& value) noexcept
        {
            return CanStore(value.x) && CanStore(value.y) && CanStore(value.z);
        }

        bool CanStore(const DoubleQuaternion& value) noexcept
        {
            return CanStore(value.x) && CanStore(value.y) && CanStore(value.z) && CanStore(value.w);
        }

        DoubleVector Perpendicular(const DoubleVector& unit) noexcept
        {
            // 最も平行から遠い座標軸を選び、外積の長さを十分に保つ。
            DoubleVector basis;
            if (std::abs(unit.x) <= std::abs(unit.y) && std::abs(unit.x) <= std::abs(unit.z))
            {
                basis = {1.0, 0.0, 0.0};
            }
            else if (std::abs(unit.y) <= std::abs(unit.z))
            {
                basis = {0.0, 1.0, 0.0};
            }
            else
            {
                basis = {0.0, 0.0, 1.0};
            }
            const DoubleVector perpendicular = Cross(unit, basis);
            return Normalize(perpendicular, Length(perpendicular));
        }

        bool ProjectPole(const DoubleVector& direction, const DoubleVector& axis, DoubleVector& pole) noexcept
        {
            const DoubleVector projected = direction - axis * Dot(direction, axis);
            const double projectedLength = Length(projected);
            if (!(projectedLength >= 1e-6))
            {
                return false;
            }
            pole = Normalize(projected, projectedLength);
            return true;
        }

        DoubleQuaternion ShortestRotation(const DoubleVector& from, const DoubleVector& to,
                                           bool& bDegenerate) noexcept
        {
            const double dot = std::clamp(Dot(from, to), -1.0, 1.0);
            if (dot < -1.0 + 1e-12)
            {
                const DoubleVector axis = Perpendicular(from);
                bDegenerate = true;
                return {axis.x, axis.y, axis.z, 0.0};
            }

            const DoubleVector cross = Cross(from, to);
            const double w = 1.0 + dot;
            const double inverseLength = 1.0 / std::sqrt(Dot(cross, cross) + w * w);
            return {cross.x * inverseLength, cross.y * inverseLength, cross.z * inverseLength, w * inverseLength};
        }

        float FiniteOrZero(float value) noexcept
        {
            return std::isfinite(value) ? value : 0.0f;
        }

        TwoBoneIKResult FallbackResult(const TwoBoneIKInput& input) noexcept
        {
            TwoBoneIKResult result;
            result.Mid = {FiniteOrZero(input.Mid.x), FiniteOrZero(input.Mid.y), FiniteOrZero(input.Mid.z)};
            result.End = {FiniteOrZero(input.End.x), FiniteOrZero(input.End.y), FiniteOrZero(input.End.z)};
            result.RootDelta = Math::Quaternion::Identity;
            result.MidDelta = Math::Quaternion::Identity;
            const DoubleVector fallback = ToDouble(input.FallbackPole);
            const double length = Length(fallback);
            result.PoleUsed = length > 0.0 && std::isfinite(length)
                                  ? ToFloat(Normalize(fallback, length))
                                  : Math::Vector3(1.0f, 0.0f, 0.0f);
            result.Degenerate = true;
            return result;
        }
    } // namespace

    TwoBoneIKResult SolveTwoBoneIK(const TwoBoneIKInput& input) noexcept
    {
        if (!IsFinite(input.Root) || !IsFinite(input.Mid) || !IsFinite(input.End) || !IsFinite(input.Target) ||
            !IsFinite(input.Pole) || !IsFinite(input.FallbackPole) || !std::isfinite(input.MaxStretch) ||
            !std::isfinite(input.SoftLimitWidth))
        {
            return FallbackResult(input);
        }

        const DoubleVector root = ToDouble(input.Root);
        const DoubleVector oldRootBone = ToDouble(input.Mid) - root;
        const DoubleVector oldMidBone = ToDouble(input.End) - ToDouble(input.Mid);
        const double length1 = Length(oldRootBone);
        const double length2 = Length(oldMidBone);
        if (length1 < 1e-8 || length2 < 1e-8)
        {
            return FallbackResult(input);
        }

        TwoBoneIKResult result;
        const DoubleVector targetOffset = ToDouble(input.Target) - root;
        const double distance = Length(targetOffset);
        const double totalLength = length1 + length2;
        const double maxReach = totalLength * std::max(double(input.MaxStretch), 1.0);
        const double stretch = distance > totalLength ? std::min(distance, maxReach) / totalLength : 1.0;
        const double length1s = length1 * stretch;
        const double length2s = length2 * stretch;
        const double hi = totalLength * stretch;
        // 区間の長さが極端に異なる場合も下限が上限を超えないようにする。
        const double lo = std::min(std::abs(length1 - length2) * stretch + 1e-6 * totalLength, hi);
        const double width = std::clamp(double(input.SoftLimitWidth), 0.0, hi * 0.5);
        double reach = distance;
        if (width > 0.0 && distance > hi - width)
        {
            // expm1 は和らげ始める境界付近の桁落ちを抑える。
            reach = (hi - width) - width * std::expm1(-(distance - (hi - width)) / width);
        }
        reach = std::clamp(reach, lo, hi);

        DoubleVector axis;
        if (distance >= 1e-8)
        {
            axis = Normalize(targetOffset, distance);
        }
        else
        {
            const DoubleVector oldEndOffset = ToDouble(input.End) - root;
            const double oldEndDistance = Length(oldEndOffset);
            axis = oldEndDistance >= 1e-8 ? Normalize(oldEndOffset, oldEndDistance)
                                          : Normalize(oldRootBone, length1);
            result.Degenerate = true;
        }

        DoubleVector pole;
        if (!ProjectPole(ToDouble(input.Pole), axis, pole))
        {
            result.Degenerate = true;
            if (!ProjectPole(ToDouble(input.FallbackPole), axis, pole) && !ProjectPole(oldRootBone, axis, pole))
            {
                pole = Perpendicular(axis);
            }
        }

        // 差の積にして、r と第 2 区間の長さが近い場合の桁落ちを抑える。
        double cosA = (length1s * length1s + (reach - length2s) * (reach + length2s)) /
                      (2.0 * length1s * reach);
        cosA = reach >= hi ? 1.0 : std::clamp(cosA, -1.0, 1.0);
        const double sinA = std::sqrt(std::max(0.0, 1.0 - cosA * cosA));
        const DoubleVector newRootBone = (axis * cosA + pole * sinA) * length1s;
        const DoubleVector newEndOffset = axis * reach;
        const DoubleVector newMid = root + newRootBone;
        const DoubleVector newEnd = root + newEndOffset;
        if (!CanStore(newMid) || !CanStore(newEnd) || !CanStore(pole) || !CanStore(stretch))
        {
            return FallbackResult(input);
        }

        result.Mid = ToFloat(newMid);
        result.End = ToFloat(newEnd);
        // 公開する位置に丸めた後の向きから差分を作り、位置と回転の不一致を避ける。
        const DoubleVector outputRootBone = ToDouble(result.Mid) - root;
        const DoubleVector outputMidBone = ToDouble(result.End) - ToDouble(result.Mid);
        const double outputRootLength = Length(outputRootBone);
        const double outputMidLength = Length(outputMidBone);
        if (!(outputRootLength > 0.0) || !(outputMidLength > 0.0))
        {
            return FallbackResult(input);
        }

        const DoubleQuaternion rootDelta = ShortestRotation(Normalize(oldRootBone, length1),
                                                             Normalize(outputRootBone, outputRootLength), result.Degenerate);
        const DoubleQuaternion midDelta = ShortestRotation(Normalize(oldMidBone, length2),
                                                            Normalize(outputMidBone, outputMidLength), result.Degenerate);
        if (!CanStore(rootDelta) || !CanStore(midDelta))
        {
            return FallbackResult(input);
        }

        result.RootDelta = ToFloat(rootDelta);
        result.MidDelta = ToFloat(midDelta);
        result.PoleUsed = ToFloat(pole);
        result.Stretch = float(stretch);
        if (!IsFinite(result.Mid) || !IsFinite(result.End) || !IsFinite(result.RootDelta) ||
            !IsFinite(result.MidDelta) || !IsFinite(result.PoleUsed) || !std::isfinite(result.Stretch))
        {
            return FallbackResult(input);
        }
        return result;
    }
} // namespace NorvesLib::Core::Animation
