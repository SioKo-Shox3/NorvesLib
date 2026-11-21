#pragma once

#include "Container/Span.h"
#include "Container/VariableArray.h"
#include "Math/Vector3.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace NorvesLib::Math
{
    struct PolylineClosestPoint
    {
        float S = 0.0f; // 始点からの弧長
        Vector3 Point; // 折れ線の上の最寄り点
        float DistanceSquared = 0.0f;
        uint32_t Segment = 0; // 点 i と i+1 の間を区間 i とする
    };

    class PolylinePath
    {
    public:
        /// 空の折れ線を作る。
        PolylinePath() = default;

        /// 点列を写す。非有限の点があれば、その直前までの点だけを保持する。
        explicit PolylinePath(Core::Container::Span<const Vector3> points)
        {
            Assign(points);
        }

        /// 点列と累積弧長を作り直す。非有限の点以降は保持しない。
        void Assign(Core::Container::Span<const Vector3> points)
        {
            // 自分の点列を渡された場合も、読み終えるまで元の配列を保持する。
            Core::Container::VariableArray<Vector3> newPoints;
            Core::Container::VariableArray<float> newCumulative;
            double length = 0.0;
            for (size_t index = 0; index < points.size(); ++index)
            {
                const Vector3& point = points[index];
                if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))
                {
                    break;
                }
                if (!newPoints.empty())
                {
                    length += std::sqrt(DistanceSquared(newPoints.back(), point));
                }
                newPoints.push_back(point);
                newCumulative.push_back(static_cast<float>(length));
            }
            m_Points.swap(newPoints);
            m_Cumulative.swap(newCumulative);
        }

        /// 点列と累積弧長を空にする。
        void Clear()
        {
            m_Points.clear();
            m_Cumulative.clear();
        }

        /// 点が一つもなければ true を返す。
        bool IsEmpty() const
        {
            return m_Points.empty();
        }

        /// 保持している点の数を返す。
        uint32_t GetPointCount() const
        {
            return static_cast<uint32_t>(m_Points.size());
        }

        /// 保持している点列への読み取り専用ビューを返す。
        Core::Container::Span<const Vector3> GetPoints() const
        {
            return {m_Points.data(), m_Points.size()};
        }

        /// 折れ線の全長を返し、点が二つ未満なら 0 を返す。
        float GetLength() const
        {
            return m_Cumulative.empty() ? 0.0f : m_Cumulative.back();
        }

        /// 有効な点番号に対し、始点からその点までの累積弧長を返す。
        float GetCumulativeLength(uint32_t pointIndex) const
        {
            return m_Cumulative[pointIndex];
        }

        /// 弧長を経路内に制限して補間し、空なら原点、一点ならその点を返す（NaN は始点）。
        Vector3 Evaluate(float s) const
        {
            if (IsEmpty())
            {
                return Vector3();
            }
            const float clampedS = ClampS(s);
            if (m_Points.size() == 1 || clampedS <= 0.0f)
            {
                return m_Points.front();
            }
            if (clampedS >= GetLength())
            {
                return m_Points.back();
            }
            const uint32_t segment = FindSegment(clampedS);
            const double start = m_Cumulative[segment];
            const double length = static_cast<double>(m_Cumulative[segment + 1]) - start;
            return Interpolate(m_Points[segment], m_Points[segment + 1], (clampedS - start) / length);
        }

        /// 弧長に対応する単位接線を返し、ゼロ長区間を飛ばし、全区間がゼロ長なら零ベクトルを返す。
        Vector3 GetTangent(float s) const
        {
            if (m_Points.size() < 2)
            {
                return Vector3();
            }
            const uint32_t segment = FindSegment(ClampS(s));
            for (uint32_t index = segment; index < GetPointCount() - 1; ++index)
            {
                const Vector3 tangent = SegmentTangent(index);
                if (tangent != Vector3())
                {
                    return tangent;
                }
            }
            for (uint32_t index = segment; index > 0; --index)
            {
                const Vector3 tangent = SegmentTangent(index - 1);
                if (tangent != Vector3())
                {
                    return tangent;
                }
            }
            return Vector3();
        }

        /// 全区間から最寄り点を求め、距離が同じなら小さい区間番号を選ぶ（空なら原点）。
        PolylineClosestPoint FindClosest(const Vector3& position) const
        {
            return FindClosestInRange(position, 0.0f, GetLength(), 0.0f, false);
        }

        /// ヒント前後の範囲内を検索し、同距離ならヒントに近い弧長を選ぶ（不正な maxJump は 0、非有限ヒントは全区間）。
        PolylineClosestPoint FindClosest(const Vector3& position, float hintS, float maxJump) const
        {
            if (!std::isfinite(hintS))
            {
                return FindClosest(position);
            }
            if (!std::isfinite(maxJump) || maxJump < 0.0f)
            {
                maxJump = 0.0f;
            }
            const double length = GetLength();
            const float lower = static_cast<float>(std::clamp(static_cast<double>(hintS) - maxJump, 0.0, length));
            const float upper = static_cast<float>(std::clamp(static_cast<double>(hintS) + maxJump, 0.0, length));
            return FindClosestInRange(position, lower, upper, hintS, true);
        }

        /// 現在の弧長から指定距離だけ先の点を Evaluate で求める。
        Vector3 GetLookAhead(float s, float distance) const
        {
            return Evaluate(s + distance);
        }

        /// 経路内に制限した弧長から終点までの残りの長さを返す。
        float GetRemainingLength(float s) const
        {
            return GetLength() - ClampS(s);
        }

    private:
        float ClampS(float s) const
        {
            return s > 0.0f ? (std::min)(s, GetLength()) : 0.0f;
        }

        uint32_t FindSegment(float s) const
        {
            // upper_bound 相当の二分探索で、同じ累積弧長の区間を飛ばす。
            uint32_t lower = 1;
            uint32_t upper = GetPointCount();
            while (lower < upper)
            {
                const uint32_t middle = lower + (upper - lower) / 2;
                if (m_Cumulative[middle] <= s)
                {
                    lower = middle + 1;
                }
                else
                {
                    upper = middle;
                }
            }
            return (std::min)(lower - 1, GetPointCount() - 2);
        }

        static double DistanceSquared(const Vector3& a, const Vector3& b)
        {
            const double x = static_cast<double>(a.x) - b.x;
            const double y = static_cast<double>(a.y) - b.y;
            const double z = static_cast<double>(a.z) - b.z;
            return x * x + y * y + z * z;
        }

        static Vector3 Interpolate(const Vector3& a, const Vector3& b, double t)
        {
            return Vector3(
                static_cast<float>(std::lerp(static_cast<double>(a.x), static_cast<double>(b.x), t)),
                static_cast<float>(std::lerp(static_cast<double>(a.y), static_cast<double>(b.y), t)),
                static_cast<float>(std::lerp(static_cast<double>(a.z), static_cast<double>(b.z), t)));
        }

        Vector3 SegmentTangent(uint32_t segment) const
        {
            const Vector3& a = m_Points[segment];
            const Vector3& b = m_Points[segment + 1];
            const double length = std::sqrt(DistanceSquared(a, b));
            if (length == 0.0)
            {
                return Vector3();
            }
            return Vector3(
                static_cast<float>((static_cast<double>(b.x) - a.x) / length),
                static_cast<float>((static_cast<double>(b.y) - a.y) / length),
                static_cast<float>((static_cast<double>(b.z) - a.z) / length));
        }

        PolylineClosestPoint FindClosestInRange(const Vector3& position, float lower, float upper, float hintS, bool bUseHint) const
        {
            PolylineClosestPoint result;
            if (m_Points.size() < 2)
            {
                result.Point = IsEmpty() ? Vector3() : m_Points.front();
                result.DistanceSquared = static_cast<float>(DistanceSquared(position, result.Point));
                return result;
            }

            bool bFound = false;
            double bestDistance = 0.0;
            double bestHintDistance = 0.0;
            for (uint32_t segment = 0; segment < GetPointCount() - 1; ++segment)
            {
                const double start = m_Cumulative[segment];
                const double end = m_Cumulative[segment + 1];
                if (end < lower || start > upper)
                {
                    continue;
                }
                const Vector3& a = m_Points[segment];
                const Vector3& b = m_Points[segment + 1];
                const double lengthSquared = DistanceSquared(a, b);
                double t = 0.0;
                if (lengthSquared > 0.0)
                {
                    const double x = static_cast<double>(b.x) - a.x;
                    const double y = static_cast<double>(b.y) - a.y;
                    const double z = static_cast<double>(b.z) - a.z;
                    t = ((static_cast<double>(position.x) - a.x) * x
                        + (static_cast<double>(position.y) - a.y) * y
                        + (static_cast<double>(position.z) - a.z) * z) / lengthSquared;
                }
                // 区間を窓との共通部分に制限してから射影する。
                const double minT = end > start ? (std::max)(0.0, (lower - start) / (end - start)) : 0.0;
                const double maxT = end > start ? (std::min)(1.0, (upper - start) / (end - start)) : 1.0;
                t = std::clamp(t, minT, maxT);
                const Vector3 point = Interpolate(a, b, t);
                const float s = std::clamp(static_cast<float>(std::lerp(start, end, t)), lower, upper);
                const double distance = DistanceSquared(position, point);
                const double hintDistance = std::abs(static_cast<double>(s) - hintS);
                if (!bFound || distance < bestDistance
                    || (bUseHint && distance == bestDistance && hintDistance < bestHintDistance))
                {
                    bFound = true;
                    bestDistance = distance;
                    bestHintDistance = hintDistance;
                    result.S = s;
                    result.Point = point;
                    result.DistanceSquared = static_cast<float>(distance);
                    result.Segment = segment;
                }
            }
            return result;
        }

        Core::Container::VariableArray<Vector3> m_Points;
        Core::Container::VariableArray<float> m_Cumulative; // 点 0 から各点までの弧長
    };
} // namespace NorvesLib::Math
