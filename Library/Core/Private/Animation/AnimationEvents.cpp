#include "Animation/AnimationEvents.h"
#include "Logging/LogMacros.h"
#include <algorithm>
#include <cmath>
namespace NorvesLib::Core::Animation
{
    size_t AnimationEventQueue::FindWindow(uint32_t node, uint32_t event) const
    {
        for (size_t i = 0; i < Capacity; ++i)
            if (m_Windows[i].bActive && m_Windows[i].SourceNode == node && m_Windows[i].SourceEvent == event)
                return i;
        return Capacity;
    }
    bool AnimationEventQueue::Enqueue(AnimEventInfo event, bool reserveEnd)
    {
        if (m_Count + m_Active + 1 + (reserveEnd ? 1 : 0) > Capacity)
        {
            ++m_Dropped;
            return false;
        }
        event.Batch = m_Batch;
        if (event.SyncGroup.IsValid() && event.Kind == AnimEventKind::Point)
            for (size_t i = 0; i < m_Count; ++i)
            {
                const auto& queued = m_Queue[i];
                if (queued.Batch == event.Batch && queued.Node != event.Node && queued.Occurrence == event.Occurrence &&
                    queued.SyncGroup == event.SyncGroup && queued.Name == event.Name && queued.Kind == event.Kind &&
                    std::fabs(queued.OffsetSeconds - event.OffsetSeconds) <= 1e-4)
                    return false;
            }
        event.Sequence = ++m_Sequence;
        m_Queue[m_Count++] = event;
        return true;
    }
    bool AnimationEventQueue::Begin(const AnimEventInfo& source, uint64_t revision)
    {
        if (FindWindow(source.Node, source.EventIndex) != Capacity)
            return false;
        const Window* shared = nullptr;
        if (source.SyncGroup.IsValid())
            for (const auto& w : m_Windows)
                if (w.bActive && w.Info.SyncGroup == source.SyncGroup && w.Info.Name == source.Name &&
                    w.Info.Occurrence == source.Occurrence)
                {
                    shared = &w;
                    break;
                }
        auto event = source;
        event.Kind = AnimEventKind::Begin;
        if (shared)
        {
            // 同期中の同名窓は物理sourceを数え、最初のBegin/最後のEndだけを配送する。
            if (m_Count + m_Active + 1 > Capacity)
            {
                ++m_Dropped;
                return false;
            }
            event = shared->Info;
        }
        else
        {
            event.WindowToken = ++m_Sequence;
            if (!Enqueue(event, true))
                return false;
        }
        for (auto& w : m_Windows)
            if (!w.bActive)
            {
                w.Info = event;
                w.SourceNode = source.Node;
                w.SourceEvent = source.EventIndex;
                w.MetadataRevision = revision;
                w.bActive = true;
                ++m_Active;
                return true;
            }
        return false;
    }
    void AnimationEventQueue::End(size_t slot, bool interrupted, double offset)
    {
        auto& w = m_Windows[slot];
        if (!w.bActive)
            return;
        auto event = w.Info;
        w.bActive = false;
        --m_Active;
        for (const auto& other : m_Windows)
            if (other.bActive && other.Info.WindowToken == event.WindowToken)
                return;
        event.Kind = AnimEventKind::End;
        event.bInterrupted = interrupted;
        event.OffsetSeconds = offset;
        (void)Enqueue(event, false);
    }
    void AnimationEventQueue::Reset()
    {
        if (m_bDispatching)
            return;
        InterruptAll();
        Dispatch();
        m_Count = 0;
        m_Active = 0;
        m_Sequence = 0;
        m_Dropped = 0;
        m_Batch = 0;
        m_LastDelta = 0;
        for (auto& w : m_Windows)
            w = {};
    }
    void AnimationEventQueue::InterruptAll()
    {
        double offset = m_LastDelta;
        if (m_bDispatching)
            offset = std::max(offset, m_DispatchEndOffset);
        for (size_t i = 0; i < m_Count; ++i)
            if (m_Queue[i].Batch == m_Batch)
                offset = std::max(offset, m_Queue[i].OffsetSeconds);
        for (size_t i = 0; i < Capacity; ++i)
            End(i, true, offset);
    }
    void AnimationEventQueue::Update(const AnimGraphData& graph, Container::Span<const AnimClipTraversal> traversals,
                                     float dt)
    {
        if (m_bDispatching || !std::isfinite(dt) || dt < 0)
            return;
        ++m_Batch;
        m_LastDelta = dt;
        const uint64_t dropped = m_Dropped;
        for (size_t i = 0; i < Capacity; ++i)
        {
            const auto& w = m_Windows[i];
            if (!w.bActive)
                continue;
            bool keep = false;
            for (const auto& t : traversals)
            {
                if (t.Node != w.SourceNode || t.Clip >= graph.Clips.size())
                    continue;
                const auto& clip = *graph.Clips[t.Clip];
                const auto& events = clip.GetMetadata().Events;
                keep = clip.GetMetadataRevision() == w.MetadataRevision && w.SourceEvent < events.size() &&
                       t.Weight >= events[w.SourceEvent].MinWeight;
                break;
            }
            if (!keep)
                End(i, true, 0);
        }
        struct Candidate
        {
            AnimEventInfo Event;
            uint64_t Revision = 0;
            uint64_t Order = 0;
        };
        constexpr size_t CandidateLimit = 256;
        Container::FixedArray<Candidate, CandidateLimit> candidates;
        size_t count = 0;
        uint64_t order = 0;
        auto earlier = [](const Candidate& a, const Candidate& b) {
            if (a.Event.OffsetSeconds != b.Event.OffsetSeconds)
                return a.Event.OffsetSeconds < b.Event.OffsetSeconds;
            const auto priority = [](AnimEventKind kind) {
                return kind == AnimEventKind::End ? 0 : kind == AnimEventKind::Begin ? 1 : 2;
            };
            if (priority(a.Event.Kind) != priority(b.Event.Kind))
                return priority(a.Event.Kind) < priority(b.Event.Kind);
            return a.Order < b.Order;
        };
        auto collect = [&](AnimEventInfo info, uint64_t revision) {
            Candidate c{info, revision, ++order};
            if (count < CandidateLimit)
            {
                candidates[count++] = c;
                return;
            }
            // 溢れた場合も早い候補を優先する。終端で残った窓を必ず閉じる。
            size_t last = 0;
            for (size_t i = 1; i < count; ++i)
                if (earlier(candidates[last], candidates[i]))
                    last = i;
            if (earlier(c, candidates[last]))
                candidates[last] = c;
            ++m_Dropped;
        };
        auto phaseAt = [](double time, double start, double length, bool loop) {
            if (!loop)
                return time;
            double phase = start + std::fmod(time - start, length);
            if (phase < start)
                phase += length;
            return phase;
        };
        for (const auto& t : traversals)
        {
            if (t.Clip >= graph.Clips.size() || !std::isfinite(t.Previous) || !std::isfinite(t.Current) ||
                !std::isfinite(t.Weight))
                continue;
            const auto& clip = *graph.Clips[t.Clip];
            const auto& meta = clip.GetMetadata();
            const double start = meta.Loop.bEnabled ? meta.Loop.Start : 0;
            const double end = meta.Loop.bEnabled ? meta.Loop.End : clip.GetClip().DurationSeconds;
            const double length = end - start;
            const bool loop = t.bLoop && length > 0;
            if (loop && std::fabs((t.Previous - start) / length) > 4503599627370496.0)
            {
                ++m_Dropped;
                continue;
            }
            const bool forward = t.Current == t.Previous ? !t.bReverse : t.Current > t.Previous;
            const double previous = t.Previous, movement = t.Current - previous;
            double current = t.Current;
            if (loop && std::fabs(movement) > length * 8)
            {
                current = previous + std::copysign(length * 8, movement);
                ++m_Dropped;
            }
            auto offset = [&](double time) {
                return movement == 0 ? 0 : std::clamp((time - previous) / movement, 0.0, 1.0) * dt;
            };
            auto crossed = [&](double time) {
                return forward ? time > previous && time <= current : time >= current && time < previous;
            };
            for (uint32_t index = 0; index < meta.Events.size(); ++index)
            {
                const auto& event = meta.Events[index];
                if (t.Weight < event.MinWeight)
                    continue;
                if (loop && (event.Time < start || event.Time > end ||
                             (event.EndTime >= 0 && (event.EndTime < start || event.EndTime > end))))
                    continue;
                AnimEventInfo info;
                info.Name = event.Name;
                info.SyncGroup = t.SyncGroup;
                info.Node = t.Node;
                info.EventIndex = index;
                info.Value = event.Value;
                info.IntValue = event.IntValue;
                info.Occurrence = loop ? int64_t(std::floor((previous - start) / length)) : 0;
                double phase = phaseAt(previous, start, length, loop);
                if (loop && !forward && phase == start)
                {
                    phase = end;
                    --info.Occurrence;
                }
                if (event.EndTime >= 0 && FindWindow(t.Node, index) == Capacity &&
                    (forward ? (phase >= event.Time && phase < event.EndTime)
                             : (phase > event.Time && phase <= event.EndTime)))
                {
                    info.Kind = AnimEventKind::Begin;
                    collect(info, clip.GetMetadataRevision());
                }
                const double low = std::min(previous, current), high = std::max(previous, current);
                const int64_t first = loop ? int64_t(std::floor((low - end) / length)) : 0;
                const int64_t last = loop ? int64_t(std::ceil((high - start) / length)) : 0;
                const int64_t cycles = std::min<int64_t>(last - first + 1, 12);
                for (int64_t k = 0; k < cycles; ++k)
                {
                    const int64_t cycle = forward ? first + k : last - k;
                    const double shift = loop ? double(cycle) * length : 0;
                    info.Occurrence = cycle + (loop && event.EndTime < 0 && event.Time == end ? 1 : 0);
                    const double begin = (forward || event.EndTime < 0 ? event.Time : event.EndTime) + shift;
                    const double finish = (forward ? event.EndTime : event.Time) + shift;
                    if (crossed(begin))
                    {
                        info.Kind = event.EndTime < 0 ? AnimEventKind::Point : AnimEventKind::Begin;
                        info.OffsetSeconds = offset(begin);
                        collect(info, clip.GetMetadataRevision());
                    }
                    if (event.EndTime >= 0 && crossed(finish))
                    {
                        info.Kind = AnimEventKind::End;
                        info.OffsetSeconds = offset(finish);
                        collect(info, clip.GetMetadataRevision());
                    }
                }
            }
        }
        std::sort(candidates.begin(), candidates.begin() + count, earlier);
        for (size_t i = 0; i < count; ++i)
        {
            const auto& c = candidates[i];
            const auto& e = c.Event;
            if (e.Kind == AnimEventKind::Point)
                (void)Enqueue(e, false);
            else if (e.Kind == AnimEventKind::Begin)
                (void)Begin(e, c.Revision);
            else
            {
                const auto slot = FindWindow(e.Node, e.EventIndex);
                if (slot != Capacity)
                    End(slot, false, e.OffsetSeconds);
            }
        }
        // 候補上限や8周制限で終了候補が省略されても、窓を開いたままにしない。
        if (m_Dropped != dropped)
        {
            for (size_t i = 0; i < Capacity; ++i)
            {
                const auto& w = m_Windows[i];
                if (!w.bActive)
                    continue;
                bool inside = false;
                for (const auto& t : traversals)
                {
                    if (t.Node != w.SourceNode || t.Clip >= graph.Clips.size())
                        continue;
                    const auto& clip = *graph.Clips[t.Clip];
                    const auto& m = clip.GetMetadata();
                    if (w.SourceEvent >= m.Events.size())
                        break;
                    const auto& e = m.Events[w.SourceEvent];
                    const double start = m.Loop.bEnabled ? m.Loop.Start : 0;
                    const double end = m.Loop.bEnabled ? m.Loop.End : clip.GetClip().DurationSeconds;
                    double phase = phaseAt(t.Current, start, end - start, t.bLoop && end > start);
                    const bool forward = t.Current == t.Previous ? !t.bReverse : t.Current > t.Previous;
                    int64_t occurrence = 0;
                    if (t.bLoop && end > start)
                    {
                        if (std::fabs((t.Current - start) / (end - start)) > 4503599627370496.0 ||
                            std::fabs(t.Current - t.Previous) > (end - start) * 8)
                            break;
                        occurrence = int64_t(std::floor((t.Current - start) / (end - start)));
                        if (!forward && phase == start)
                        {
                            phase = end;
                            --occurrence;
                        }
                    }
                    inside = occurrence == w.Info.Occurrence && (forward ? (phase >= e.Time && phase < e.EndTime)
                                                                         : (phase > e.Time && phase <= e.EndTime));
                    break;
                }
                if (!inside)
                    End(i, true, dt);
            }
            NORVES_LOG_WARNING("Animation", "イベント配送の上限を超えました: dropped=%llu",
                               (unsigned long long)(m_Dropped - dropped));
        }
    }
    void AnimationEventQueue::Dispatch()
    {
        if (m_bDispatching)
            return;
        m_bDispatching = true;
        while (m_Count)
        {
            std::sort(m_Queue.begin(), m_Queue.begin() + m_Count, [](const auto& a, const auto& b) {
                if (a.Batch != b.Batch)
                    return a.Batch < b.Batch;
                if (a.OffsetSeconds != b.OffsetSeconds)
                    return a.OffsetSeconds < b.OffsetSeconds;
                return a.Sequence < b.Sequence;
            });
            auto batch = m_Queue;
            const size_t count = m_Count;
            m_Count = 0;
            if (batch[count - 1].Batch == m_Batch)
                m_DispatchEndOffset = std::max(m_DispatchEndOffset, batch[count - 1].OffsetSeconds);
            for (size_t i = 0; i < count; ++i)
                OnEvent.Broadcast(batch[i]);
        }
        m_bDispatching = false;
        m_DispatchEndOffset = 0;
    }
} // namespace NorvesLib::Core::Animation
