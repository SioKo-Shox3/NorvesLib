#pragma once
#include "Animation/ClipMetadata.h"
namespace NorvesLib::Core::Animation
{
    // 名前集合の正準順を等間隔の位相へ写す。末尾knotは先頭+一周期。
    struct AnimSyncMap
    {
        Container::VariableArray<double> Knots;
        double Length = 0;
        bool bMarkerFallback = false;
        [[nodiscard]] double TimeToPhase(double time) const;
        [[nodiscard]] double PhaseToTime(double phase) const;
    };
    [[nodiscard]] bool BuildAnimSyncMap(const ClipMetadata&, float duration,
                                        Container::Span<const Identity> canonicalMarkers, AnimSyncMap&);
    // Update中のみ有効。候補イベントの秒をleaderの実時間へ戻し、区分境界でも配送時刻を揃える。
    struct AnimSyncTiming
    {
        const AnimSyncMap* Clip = nullptr;
        const AnimSyncMap* Leader = nullptr;
        double LeaderPrevious = 0, LeaderCurrent = 0;
        double PhaseOffset = 0, LeaderPhaseOffset = 0;
        [[nodiscard]] double OffsetAt(double clipTime, double frameSeconds) const;
        [[nodiscard]] int64_t OccurrenceAt(double clipTime) const;
    };
} // namespace NorvesLib::Core::Animation
