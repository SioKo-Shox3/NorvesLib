#include "Animation/AnimSync.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace NorvesLib::Core::Animation
{
    double AnimSyncMap::TimeToPhase(double time) const
    {
        if (Knots.size() < 2 || !std::isfinite(time) || Length <= 0)
            return std::numeric_limits<double>::quiet_NaN();
        const double cycle = std::floor((time - Knots.front()) / Length);
        const double local = std::clamp(time - cycle * Length, Knots.front(), Knots.back());
        auto next = std::upper_bound(Knots.begin(), Knots.end(), local);
        const size_t i = next == Knots.end() ? Knots.size() - 2 : size_t(next - Knots.begin()) - 1;
        return cycle + (double(i) + (local - Knots[i]) / (Knots[i + 1] - Knots[i])) / double(Knots.size() - 1);
    }
    double AnimSyncMap::PhaseToTime(double phase) const
    {
        if (Knots.size() < 2 || !std::isfinite(phase) || Length <= 0)
            return std::numeric_limits<double>::quiet_NaN();
        const double cycle = std::floor(phase), segment = (phase - cycle) * double(Knots.size() - 1);
        const size_t i = std::min(size_t(segment), Knots.size() - 2);
        return cycle * Length + Knots[i] + (Knots[i + 1] - Knots[i]) * (segment - double(i));
    }
    bool BuildAnimSyncMap(const ClipMetadata& metadata, float duration, Container::Span<const Identity> names,
                          AnimSyncMap& out)
    {
        ClipMetadataReport report;
        if (!ValidateClipMetadata(metadata, duration, report))
            return false;
        const double start = metadata.Loop.bEnabled ? metadata.Loop.Start : 0,
                     end = metadata.Loop.bEnabled ? metadata.Loop.End : duration;
        if (end <= start)
            return false;
        AnimSyncMap map;
        map.Length = end - start;
        if (metadata.Markers.empty())
        {
            map.Knots = {start, end};
            map.bMarkerFallback = true;
            out = std::move(map);
            return true;
        }
        if (names.empty() || names.size() != metadata.Markers.size())
            return false;
        size_t first = SIZE_MAX;
        for (size_t i = 0; i < metadata.Markers.size(); ++i)
        {
            if (metadata.Markers[i].Time < start || metadata.Markers[i].Time >= end)
                return false;
            if (metadata.Markers[i].Name == names[0])
                first = i;
        }
        if (first == SIZE_MAX)
            return false;
        for (size_t i = 0; i < names.size(); ++i)
        {
            const size_t index = (first + i) % names.size();
            if (metadata.Markers[index].Name != names[i])
                return false;
            const double time = metadata.Markers[index].Time + (first + i >= names.size() ? map.Length : 0);
            if (!map.Knots.empty() && time <= map.Knots.back())
                return false;
            map.Knots.push_back(time);
        }
        map.Knots.push_back(map.Knots.front() + map.Length);
        out = std::move(map);
        return true;
    }
    double AnimSyncTiming::OffsetAt(double time, double dt) const
    {
        if (!Clip || !Leader || LeaderCurrent == LeaderPrevious)
            return 0;
        const double phase = Clip->TimeToPhase(time) - PhaseOffset;
        const double leaderTime = Leader->PhaseToTime(phase + LeaderPhaseOffset);
        return std::clamp((leaderTime - LeaderPrevious) / (LeaderCurrent - LeaderPrevious), 0.0, 1.0) * dt;
    }
    int64_t AnimSyncTiming::OccurrenceAt(double time) const
    {
        const double phase = Clip ? Clip->TimeToPhase(time) - PhaseOffset : 0;
        // 境界の片側に丸め誤差が出ても同じmarkerを別の周回に分けない。
        const double nearest = std::round(phase), value = std::fabs(phase - nearest) <= 1e-9 ? nearest : phase;
        if (!std::isfinite(value) || std::fabs(value) > 4503599627370496.0)
            return 0;
        return int64_t(std::floor(value));
    }
} // namespace NorvesLib::Core::Animation
