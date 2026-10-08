#include "Animation/ClipChannelSampler.h"
#include "Animation/SkeletalClipSampling.h"
#include <cmath>

namespace NorvesLib::Core::Animation
{
    Skeletal::SkeletalValue ClipChannelSampler::Sample(const Skeletal::SkeletalAnimationChannel& channel,
                                                       float timeSeconds)
    {
        const auto& samples = channel.Samples;
        if (samples.empty() || !std::isfinite(timeSeconds))
        {
            return {};
        }
        if (samples.size() == 1 || timeSeconds <= samples.front().TimeSeconds)
        {
            return samples.front().Value;
        }
        if (timeSeconds >= samples.back().TimeSeconds)
        {
            return samples.back().Value;
        }
        const size_t next = Detail::FindNextClipKey(samples, timeSeconds);
        return Detail::SampleSkeletalChannelInterval(channel, samples[next - 1], samples[next], timeSeconds);
    }
} // namespace NorvesLib::Core::Animation
