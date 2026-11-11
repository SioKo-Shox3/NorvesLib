#pragma once
#include <cmath>
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
        // 一様な時刻の i 番目のキー時刻。書き手・読み手・探索で同じ式を使う。
        [[nodiscard]] inline float UniformClipKeyTime(float startSeconds, float stepSeconds, size_t index) noexcept
        {
            return float(double(startSeconds) + double(index) * double(stepSeconds));
        }

        // FindNextClipKey と同じ添字を返す。step が有限で正でなければ二分探索へ落ちる。
        template <class Samples>
        [[nodiscard]] size_t FindNextUniformClipKey(const Samples& samples, float startSeconds, float stepSeconds,
                                                    float timeSeconds) noexcept
        {
            const size_t count = samples.size();
            if (count == 0 || !std::isfinite(stepSeconds) || stepSeconds <= 0.0f)
            {
                return FindNextClipKey(samples, timeSeconds);
            }

            const double estimate = std::ceil((double(timeSeconds) - double(startSeconds)) / double(stepSeconds));
            size_t next = 0;
            if (estimate >= double(count))
            {
                next = count;
            }
            else if (estimate > 0.0)
            {
                next = static_cast<size_t>(estimate);
            }

            size_t corrections = 0;
            while (next > 0 && samples[next - 1].TimeSeconds >= timeSeconds)
            {
                if (corrections == 2)
                {
                    return FindNextClipKey(samples, timeSeconds);
                }
                --next;
                ++corrections;
            }
            corrections = 0;
            while (next < count && samples[next].TimeSeconds < timeSeconds)
            {
                if (corrections == 2)
                {
                    return FindNextClipKey(samples, timeSeconds);
                }
                ++next;
                ++corrections;
            }
            return next;
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
