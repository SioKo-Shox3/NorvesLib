#pragma once
// float境界の小さい直交誤差だけを正規化する。shear/反射から回転を推測しない。
#include "Resource/BvhEvaluate.h"
#include <cmath>
#include <algorithm>
#include <limits>

namespace NorvesLib::Core::Animation::Detail
{
    template <class T> bool RetargetValidSpan(Container::Span<T> values) noexcept
    {
        if (values.size() == 0)
        {
            return true;
        }
        const auto address = reinterpret_cast<uintptr_t>(values.data());
        return address != 0 && address % alignof(T) == 0 &&
               values.size() <= static_cast<size_t>(PTRDIFF_MAX) / sizeof(T) &&
               values.size() <= (std::numeric_limits<uintptr_t>::max() - address) / sizeof(T);
    }
    constexpr double RetargetRotationTolerance = 1e-5;
    constexpr double RetargetAngularTolerance = 0.05 * 3.14159265358979323846 / 180.0;
    struct RetargetQuaterniond
    {
        double X = 0, Y = 0, Z = 0, W = 1;
    };
    inline Bvh::Matrix3d RetargetMultiply(const Bvh::Matrix3d& a, const Bvh::Matrix3d& b) noexcept
    {
        Bvh::Matrix3d out;
        for (size_t i = 0; i < 3; ++i)
        {
            for (size_t j = 0; j < 3; ++j)
            {
                out.Values[i * 3 + j] = a.Values[i * 3] * b.Values[j] + a.Values[i * 3 + 1] * b.Values[3 + j] +
                                        a.Values[i * 3 + 2] * b.Values[6 + j];
            }
        }
        return out;
    }
    inline Bvh::Matrix3d RetargetTranspose(const Bvh::Matrix3d& m) noexcept
    {
        Bvh::Matrix3d out;
        for (size_t i = 0; i < 3; ++i)
        {
            for (size_t j = 0; j < 3; ++j)
            {
                out.Values[i * 3 + j] = m.Values[j * 3 + i];
            }
        }
        return out;
    }
    // 呼出し側で直交性を検査した行列専用。最大対角枝で半回転も安定して扱う。
    inline bool RetargetExtractQuaternion(const Bvh::Matrix3d& m, RetargetQuaterniond& out) noexcept
    {
        const auto& v = m.Values;
        RetargetQuaterniond q;
        const double trace = v[0] + v[4] + v[8];
        if (trace > 0)
        {
            const double s = 2 * std::sqrt(trace + 1);
            q = {(v[7] - v[5]) / s, (v[2] - v[6]) / s, (v[3] - v[1]) / s, s / 4};
        }
        else if (v[0] >= v[4] && v[0] >= v[8])
        {
            const double s = 2 * std::sqrt(1 + v[0] - v[4] - v[8]);
            q = {s / 4, (v[1] + v[3]) / s, (v[2] + v[6]) / s, (v[7] - v[5]) / s};
        }
        else if (v[4] >= v[8])
        {
            const double s = 2 * std::sqrt(1 + v[4] - v[0] - v[8]);
            q = {(v[1] + v[3]) / s, s / 4, (v[5] + v[7]) / s, (v[2] - v[6]) / s};
        }
        else
        {
            const double s = 2 * std::sqrt(1 + v[8] - v[0] - v[4]);
            q = {(v[2] + v[6]) / s, (v[5] + v[7]) / s, s / 4, (v[3] - v[1]) / s};
        }
        const double length = std::hypot(std::hypot(q.X, q.Y), std::hypot(q.Z, q.W));
        if (!std::isfinite(length) || length == 0)
        {
            return false;
        }
        q.X /= length;
        q.Y /= length;
        q.Z /= length;
        q.W /= length;
        // 単独frameの符号を決定する。時系列の半球連続化とは別契約。
        const double values[4] = {q.W, q.X, q.Y, q.Z};
        size_t largest = 0;
        for (size_t i = 1; i < 4; ++i)
        {
            if (std::abs(values[i]) > std::abs(values[largest]))
            {
                largest = i;
            }
        }
        if (values[largest] < 0)
        {
            q.X = -q.X;
            q.Y = -q.Y;
            q.Z = -q.Z;
            q.W = -q.W;
        }
        out = q;
        return true;
    }
    inline bool RetargetProjectRotation(const Bvh::Matrix3d& m, Bvh::Matrix3d& out) noexcept
    {
        for (double value : m.Values)
        {
            if (!std::isfinite(value) || std::abs(value) > 1 + RetargetRotationTolerance)
            {
                return false;
            }
        }
        const auto gram = RetargetMultiply(m, RetargetTranspose(m));
        for (size_t i = 0; i < 9; ++i)
        {
            if (std::abs(gram.Values[i] - (i % 4 == 0 ? 1.0 : 0.0)) > RetargetRotationTolerance)
            {
                return false;
            }
        }
        const auto& v = m.Values;
        const double det = v[0] * (v[4] * v[8] - v[5] * v[7]) - v[1] * (v[3] * v[8] - v[5] * v[6]) +
                           v[2] * (v[3] * v[7] - v[4] * v[6]);
        if (std::abs(det - 1) > 2 * RetargetRotationTolerance)
        {
            return false;
        }
        RetargetQuaterniond q;
        if (!RetargetExtractQuaternion(m, q))
        {
            return false;
        }
        // 許容内の量子化誤差を除き、後続の転置を逆回転として使えるようにする。
        Bvh::Matrix3d candidate;
        candidate.Values = {
            1 - 2 * (q.Y * q.Y + q.Z * q.Z), 2 * (q.X * q.Y - q.Z * q.W),     2 * (q.X * q.Z + q.Y * q.W),
            2 * (q.X * q.Y + q.Z * q.W),     1 - 2 * (q.X * q.X + q.Z * q.Z), 2 * (q.Y * q.Z - q.X * q.W),
            2 * (q.X * q.Z - q.Y * q.W),     2 * (q.Y * q.Z + q.X * q.W),     1 - 2 * (q.X * q.X + q.Y * q.Y)};
        out = candidate;
        return true;
    }
    inline double RetargetRotationAngle(const Bvh::Matrix3d& a, const Bvh::Matrix3d& b) noexcept
    {
        RetargetQuaterniond p, q;
        if (!RetargetExtractQuaternion(a, p) || !RetargetExtractQuaternion(b, q))
        {
            return INFINITY;
        }
        const double sign = p.X * q.X + p.Y * q.Y + p.Z * q.Z + p.W * q.W < 0 ? -1.0 : 1.0;
        const double difference =
            std::hypot(std::hypot(p.X - sign * q.X, p.Y - sign * q.Y), std::hypot(p.Z - sign * q.Z, p.W - sign * q.W));
        const double sum =
            std::hypot(std::hypot(p.X + sign * q.X, p.Y + sign * q.Y), std::hypot(p.Z + sign * q.Z, p.W + sign * q.W));
        return 4 * std::atan2(difference, sum);
    }
} // namespace NorvesLib::Core::Animation::Detail
