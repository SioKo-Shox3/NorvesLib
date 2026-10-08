#pragma once
#include <cstddef>

namespace NorvesLib::Core::Skeletal
{
    struct SkeletalAnimationChannel;
    struct SkeletalValue;
} // namespace NorvesLib::Core::Skeletal
namespace NorvesLib::Core::Animation
{
    namespace Detail
    {
        // 時刻が厳密増加するキー列のlower_bound。値の算術には触れない。
        template <class Samples>
        [[nodiscard]] size_t FindNextClipKey(const Samples& samples, float timeSeconds) noexcept
        {
            size_t first = 0;
            size_t count = samples.size();
            while (count != 0)
            {
                const size_t half = count / 2;
                const size_t middle = first + half;
                if (samples[middle].TimeSeconds < timeSeconds)
                {
                    first = middle + 1;
                    count -= half + 1;
                }
                else
                {
                    count = half;
                }
            }
            return first;
        }
    } // namespace Detail

    class ClipChannelSampler final
    {
      public:
        // channelの検証は資産準備時に済ませる。端点とSTEPの境界は旧Samplerと同じ。
        [[nodiscard]] static Skeletal::SkeletalValue Sample(const Skeletal::SkeletalAnimationChannel& channel,
                                                            float timeSeconds);
    };
} // namespace NorvesLib::Core::Animation
