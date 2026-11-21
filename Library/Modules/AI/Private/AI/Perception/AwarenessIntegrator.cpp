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

        EAwarenessLevel TargetLevel(const AwarenessState& state, const AwarenessTuning& tuning)
        {
            const float alert = state.Level == EAwarenessLevel::Alert
                ? tuning.AlertThreshold - tuning.Hysteresis : tuning.AlertThreshold;
            if (state.Awareness >= alert)
            {
                return EAwarenessLevel::Alert;
            }
            const float suspicious = state.Level >= EAwarenessLevel::Suspicious
                ? tuning.SuspiciousThreshold - tuning.Hysteresis : tuning.SuspiciousThreshold;
            return state.Awareness >= suspicious ? EAwarenessLevel::Suspicious : EAwarenessLevel::Unaware;
        }

        void UpdateLevel(AwarenessState& state, const AwarenessTuning& tuning, float deltaSeconds, uint32_t seed)
        {
            const EAwarenessLevel target = TargetLevel(state, tuning);
            if (target < state.Level)
            {
                state.Level = target;
                state.PendingLevel = target;
                state.PendingSeconds = 0.0f;
                return;
            }

            if (state.PendingLevel > state.Level)
            {
                const float threshold = state.PendingLevel == EAwarenessLevel::Alert
                    ? tuning.AlertThreshold : tuning.SuspiciousThreshold;
                if (state.Awareness >= threshold - tuning.Hysteresis)
                {
                    state.PendingSeconds -= deltaSeconds;
                    if (state.PendingSeconds > 0.0f)
                    {
                        return;
                    }
                    state.Level = state.PendingLevel;
                }
                state.PendingLevel = state.Level;
                state.PendingSeconds = 0.0f;
            }

            if (target > state.Level)
            {
                const float sample = ReactionSample(seed, state.ReactionCount++);
                const double delay = tuning.ReactionDelaySeconds + static_cast<double>(tuning.ReactionJitterSeconds) * sample;
                state.PendingLevel = target;
                state.PendingSeconds = static_cast<float>(std::clamp<double>(delay, 0.0, std::numeric_limits<float>::max()));
                if (state.PendingSeconds == 0.0f)
                {
                    state.Level = target;
                }
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
        state.Awareness = static_cast<float>(std::clamp(state.Awareness + rate * deltaSeconds, 0.0, 1.0));
        UpdateLevel(state, tuning, deltaSeconds, seed);

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
