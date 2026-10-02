#pragma once

#include "Container/Span.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace NorvesLib::Core::Input
{
    struct HapticsOutput
    {
        float Low = 0;
        float High = 0;
    };
    struct HapticsKeyframe
    {
        double Time = 0;
        float Value = 0;
    };
    // 呼出中だけ借用する定義view。service側がkey配列を所有する。
    struct HapticsEffectView
    {
        double Duration = 1;
        bool Loop = false;
        int32_t Priority = 0;
        Container::Span<const HapticsKeyframe> Low;
        Container::Span<const HapticsKeyframe> High;
    };
    enum class EHapticsMixMode : uint8_t
    {
        Maximum, AddClamp
    };
    struct HapticsMixSample
    {
        HapticsOutput Output;
        int32_t Priority = 0;
        bool Active = true;
    };
    inline bool IsValidHapticsOutput(const HapticsOutput& value)
    {
        return std::isfinite(value.Low) && std::isfinite(value.High) &&
            value.Low >= 0 && value.Low <= 1 && value.High >= 0 && value.High <= 1;
    }
    inline bool IsZeroHapticsOutput(const HapticsOutput& value)
    {
        return value.Low == 0 && value.High == 0;
    }
    inline bool IsValidHapticsCurve(Container::Span<const HapticsKeyframe> keys, double duration)
    {
        if (!std::isfinite(duration) || duration <= 0)
        {
            return false;
        }
        double previous = -1;
        for (const auto& key : keys)
        {
            if (!std::isfinite(key.Time) || key.Time < 0 || key.Time > duration || key.Time <= previous ||
                !std::isfinite(key.Value) || key.Value < 0 || key.Value > 1)
            {
                return false;
            }
            previous = key.Time;
        }
        return true;
    }
    inline bool IsValidHapticsEffect(const HapticsEffectView& effect)
    {
        return IsValidHapticsCurve(effect.Low, effect.Duration) &&
            IsValidHapticsCurve(effect.High, effect.Duration);
    }
    namespace Detail
    {
        // 検証済み曲線を評価。空channelは0、最初/最後のkey外は端点を保持する。
        inline float EvaluateHapticsCurve(Container::Span<const HapticsKeyframe> keys, double time)
        {
            if (keys.empty())
            {
                return 0;
            }
            if (time <= keys.front().Time)
            {
                return keys.front().Value;
            }
            for (size_t index = 1; index < keys.size(); ++index)
            {
                const auto& right = keys[index];
                if (time <= right.Time)
                {
                    const auto& left = keys[index - 1];
                    const double alpha = (time - left.Time) / (right.Time - left.Time);
                    const double value = static_cast<double>(left.Value) +
                        (static_cast<double>(right.Value) - left.Value) * alpha;
                    return static_cast<float>(std::clamp(value, 0.0, 1.0));
                }
            }
            return keys.back().Value;
        }
    }
    // 無効値はresultを保持。非loopはduration到達時に0、loopは境界で先頭へ戻る。
    inline bool EvaluateHapticsEffect(const HapticsEffectView& effect, double elapsed, HapticsOutput& result)
    {
        if (!IsValidHapticsEffect(effect) || !std::isfinite(elapsed) || elapsed < 0)
        {
            return false;
        }
        HapticsOutput candidate;
        if (effect.Loop || elapsed < effect.Duration)
        {
            const double time = effect.Loop ? std::fmod(elapsed, effect.Duration) : elapsed;
            candidate.Low = Detail::EvaluateHapticsCurve(effect.Low, time);
            candidate.High = Detail::EvaluateHapticsCurve(effect.High, time);
        }
        result = candidate;
        return true;
    }
    // slotごとに呼ぶ。最高priorityのactive効果だけを両channel共通で採用する。
    // その効果が一時的に0でも低priorityへ漏らさない。倍率は合成/clampの後で適用。
    inline bool MixHapticsSamples(Container::Span<const HapticsMixSample> samples, EHapticsMixMode mode,
        float strength, HapticsOutput& result)
    {
        if ((mode != EHapticsMixMode::Maximum && mode != EHapticsMixMode::AddClamp) ||
            !std::isfinite(strength) || strength < 0 || strength > 1)
        {
            return false;
        }
        bool hasActive = false;
        int32_t priority = 0;
        for (const auto& sample : samples)
        {
            if (!IsValidHapticsOutput(sample.Output))
            {
                return false;
            }
            if (sample.Active && (!hasActive || sample.Priority > priority))
            {
                hasActive = true;
                priority = sample.Priority;
            }
        }
        double low = 0;
        double high = 0;
        for (const auto& sample : samples)
        {
            if (!sample.Active || sample.Priority != priority)
            {
                continue;
            }
            if (mode == EHapticsMixMode::Maximum)
            {
                low = std::max(low, static_cast<double>(sample.Output.Low));
                high = std::max(high, static_cast<double>(sample.Output.High));
            }
            else
            {
                low = std::min(1.0, low + sample.Output.Low);
                high = std::min(1.0, high + sample.Output.High);
            }
        }
        HapticsOutput candidate;
        candidate.Low = static_cast<float>(low * strength);
        candidate.High = static_cast<float>(high * strength);
        result = candidate;
        return true;
    }
} // namespace NorvesLib::Core::Input
