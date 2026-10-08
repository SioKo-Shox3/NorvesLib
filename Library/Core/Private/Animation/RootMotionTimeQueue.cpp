#include "Animation/RootMotionTimeQueue.h"
#include <algorithm>
#include <limits>
namespace NorvesLib::Core::Animation
{
    bool RootMotionTimeQueue::Push(const RootMotionDelta& delta, uint64_t duration)
    {
        if (!duration || !delta.IsFinite() || std::fabs(delta.Yaw) > MaximumAbsoluteYaw ||
            duration > std::numeric_limits<uint64_t>::max() - m_Remaining ||
            GetPendingSegmentCount() >= MaximumSegments)
            return false;
        // 消費済みのprefixだけを除く。allocation失敗でも論理的な保留区間は変えない。
        if (m_Begin && m_Segments.size() >= MaximumSegments)
        {
            m_Segments.erase(m_Segments.begin(), m_Segments.begin() + m_Begin);
            m_Begin = 0;
        }
        m_Segments.push_back({delta, duration, 0});
        m_Remaining += duration;
        return true;
    }
    bool RootMotionTimeQueue::Peek(uint64_t duration, RootMotionDelta& out) const
    {
        RootMotionDelta result;
        for (size_t i = m_Begin; duration && i < m_Segments.size(); ++i)
        {
            const auto& segment = m_Segments[i];
            const uint64_t take = std::min(duration, segment.Duration - segment.Consumed);
            // prefix(f)={X*f,Z*f,Yaw*f}の二点間差分。
            // f1-f0を引き算せずtake/Durationで求め、終端付近の桁落ちを避ける。
            const double fraction = double(take) / double(segment.Duration);
            const double startYaw = segment.Delta.Yaw * (double(segment.Consumed) / double(segment.Duration));
            const double x = segment.Delta.X * fraction, z = segment.Delta.Z * fraction;
            const double c = std::cos(startYaw), s = std::sin(startYaw);
            const RootMotionDelta part{c * x - s * z, s * x + c * z, segment.Delta.Yaw * fraction};
            result = ComposeRootMotion(result, part);
            if (!part.IsFinite() || !result.IsFinite())
                return false;
            duration -= take;
        }
        out = result;
        return true;
    }
    bool RootMotionTimeQueue::Consume(uint64_t duration, RootMotionDelta& out)
    {
        RootMotionDelta result;
        if (!Peek(duration, result))
            return false;
        Discard(duration);
        out = result;
        return true;
    }
    void RootMotionTimeQueue::Discard(uint64_t duration)
    {
        while (duration && m_Begin < m_Segments.size())
        {
            auto& segment = m_Segments[m_Begin];
            const uint64_t take = std::min(duration, segment.Duration - segment.Consumed);
            segment.Consumed += take;
            m_Remaining -= take;
            duration -= take;
            if (segment.Consumed == segment.Duration)
                ++m_Begin;
        }
        if (m_Begin == m_Segments.size())
            Clear();
    }
    void RootMotionTimeQueue::Clear()
    {
        m_Segments.clear();
        m_Begin = 0;
        m_Remaining = 0;
    }
} // namespace NorvesLib::Core::Animation
