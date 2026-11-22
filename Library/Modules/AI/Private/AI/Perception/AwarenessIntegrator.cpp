#include "AI/Perception/AwarenessIntegrator.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace NorvesLib::Modules::AI::Perception
{
    namespace
    {
        float ReactionSample(uint32_t seed, uint32_t count)
        {
            uint32_t hash = seed + 0x9e3779b9u * (count + 1u);
            hash ^= hash >> 16;
            hash *= 0x85ebca6bu;
            hash ^= hash >> 13;
            hash *= 0xc2b2ae35u;
            hash ^= hash >> 16;
            // 上位 24 ビットを使い、float に丸めても 1 に達しないようにする。
            return static_cast<float>(hash >> 8) * 0x1p-24f;
        }

        void UpdateLevel(AwarenessState& state, const AwarenessTuning& tuning, double before, double rate,
                         float deltaSeconds, uint32_t seed)
        {
            if (state.Level == EAwarenessLevel::Alert && state.Awareness < tuning.AlertThreshold - tuning.Hysteresis)
            {
                state.Level = EAwarenessLevel::Suspicious;
            }
            if (state.Level == EAwarenessLevel::Suspicious && state.Awareness < tuning.SuspiciousThreshold - tuning.Hysteresis)
            {
                state.Level = EAwarenessLevel::Unaware;
            }

            const auto isReady = [&](float threshold, float& remainingSeconds)
            {
                if (remainingSeconds > 0.0f)
                {
                    if (state.Awareness < threshold - tuning.Hysteresis)
                    {
                        remainingSeconds = 0.0f;
                        return false;
                    }
                    remainingSeconds -= deltaSeconds;
                    return remainingSeconds <= 0.0f;
                }
                if (state.Awareness >= threshold)
                {
                    const float sample = ReactionSample(seed, state.ReactionCount++);
                    const double delay = tuning.ReactionDelaySeconds + static_cast<double>(tuning.ReactionJitterSeconds) * sample;
                    const double dt = deltaSeconds;
                    const double crossingSeconds = before < threshold && rate > 0.0
                        ? std::clamp((threshold - before) / rate, 0.0, dt)
                        : 0.0;
                    const double remaining = std::clamp<double>(delay, 0.0, std::numeric_limits<float>::max()) - (dt - crossingSeconds);
                    remainingSeconds = static_cast<float>(remaining);
                    if (remainingSeconds <= 0.0f)
                    {
                        remainingSeconds = 0.0f;
                        return true;
                    }
                    return false;
                }
                return false;
            };

            if (state.Level < EAwarenessLevel::Suspicious && isReady(tuning.SuspiciousThreshold, state.SuspiciousPendingSeconds))
            {
                state.Level = EAwarenessLevel::Suspicious;
            }
            if (state.Level < EAwarenessLevel::Alert && isReady(tuning.AlertThreshold, state.AlertPendingSeconds))
            {
                state.Level = EAwarenessLevel::Alert;
            }

            if (state.Level >= EAwarenessLevel::Suspicious)
            {
                state.SuspiciousPendingSeconds = 0.0f;
            }
            if (state.Level == EAwarenessLevel::Alert)
            {
                state.AlertPendingSeconds = 0.0f;
            }
        }
    }

    void UpdateAwareness(AwarenessState& state, const AwarenessInput& input, const AwarenessTuning& tuning,
                         float deltaSeconds, uint32_t seed)
    {
        if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f)
        {
            return;
        }

        const double intensity = std::isfinite(input.Intensity) ? std::clamp(input.Intensity, 0.0f, 1.0f) : 0.0f;
        const double sensitivity = std::isfinite(input.Sensitivity) ? std::max(input.Sensitivity, 0.0f) : 0.0f;
        const double rate = tuning.Gain * intensity * sensitivity - tuning.Decay * (1.0 - intensity);
        const double before = state.Awareness;
        state.Awareness = static_cast<float>(std::clamp(before + rate * deltaSeconds, 0.0, 1.0));
        UpdateLevel(state, tuning, before, rate, deltaSeconds, seed);

        if (intensity > 0.0 && input.bHasStimulusPosition)
        {
            state.LastKnownPosition = input.StimulusPosition;
            state.bHasLastKnownPosition = true;
            state.SecondsSinceStimulus = 0.0f;
        }
        else
        {
            const double elapsed = static_cast<double>(state.SecondsSinceStimulus) + deltaSeconds;
            state.SecondsSinceStimulus = static_cast<float>(std::min<double>(elapsed, std::numeric_limits<float>::max()));
            if (state.SecondsSinceStimulus >= tuning.MemorySeconds)
            {
                state.bHasLastKnownPosition = false;
            }
        }
    }
}
