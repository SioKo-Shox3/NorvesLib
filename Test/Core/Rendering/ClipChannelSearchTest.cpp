// 共通の二分探索を、旧線形探索と端点の前後で比較する。Core/Windowsに依存しない。
#include "Animation/ClipChannelSampler.h"
#include "Container/Span.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>

void TestUniformClipChannelSearch()
{
    struct Key
    {
        float TimeSeconds = 0;
    };
    constexpr size_t keyCount = 300;
    constexpr float step = 1.0f / 30.0f;
    uint32_t state = 0x91e10da5u;
    for (int sequence = 0; sequence < 2; ++sequence)
    {
        const float start = sequence == 0 ? 0.25f : 0.0f;
        Key keys[keyCount];
        for (size_t i = 0; i < keyCount; ++i)
        {
            keys[i].TimeSeconds = sequence == 0
                                      ? NorvesLib::Core::Animation::Detail::UniformClipKeyTime(start, step, i)
                                      : float(i) / 30.0f;
        }
        const NorvesLib::Core::Container::Span<const Key> samples(keys, keyCount);
        for (size_t i = 0; i < 10000; ++i)
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            const float exact = keys[state % keyCount].TimeSeconds;
            float time = exact;
            switch (i % 4)
            {
            case 0:
                break;
            case 1:
                time = std::nextafter(exact, -std::numeric_limits<float>::infinity());
                break;
            case 2:
                time = std::nextafter(exact, std::numeric_limits<float>::infinity());
                break;
            default:
                time = start - 1.0f + 12.0f * (float(state >> 8) / float(0x00ffffffu));
                break;
            }
            const size_t expected = NorvesLib::Core::Animation::Detail::FindNextClipKey(samples, time);
            const size_t actual =
                NorvesLib::Core::Animation::Detail::FindNextUniformClipKey(samples, start, step, time);
            if (actual != expected)
            {
                std::fprintf(stderr, "一様キー探索の不一致 列=%d 時刻=%f 期待=%zu 結果=%zu\n", sequence, time,
                             expected, actual);
                std::abort();
            }
        }
    }
}

void TestClipChannelSearch()
{
    struct Key
    {
        float TimeSeconds = 0;
    };
    Key keys[129];
    for (size_t i = 0; i < 129; ++i)
    {
        keys[i].TimeSeconds = float(i) * 0.125f - 3.0f;
    }
    for (size_t count : {size_t{0}, size_t{1}, size_t{2}, size_t{17}, size_t{129}})
    {
        const NorvesLib::Core::Container::Span<const Key> samples(keys, count);
        for (int i = -8; i <= 140; ++i)
        {
            const float exact = float(i) * 0.125f - 3.0f;
            const float probes[] = {exact, std::nextafter(exact, -std::numeric_limits<float>::infinity()),
                                    std::nextafter(exact, std::numeric_limits<float>::infinity()), exact + 0.03125f};
            for (float time : probes)
            {
                size_t expected = 0;
                while (expected < count && keys[expected].TimeSeconds < time)
                {
                    ++expected;
                }
                const auto actual = NorvesLib::Core::Animation::Detail::FindNextClipKey(samples, time);
                if (actual != expected)
                {
                    std::fprintf(stderr, "clip key search mismatch count=%zu time=%f expected=%zu actual=%zu\n", count,
                                 time, expected, actual);
                    std::abort();
                }
            }
        }
    }
    TestUniformClipChannelSearch();
}
#if defined(NORVES_POSE_SEARCH_STANDALONE)
int main()
{
    TestClipChannelSearch();
    std::puts("ClipChannelSearchTest PASS");
}
#endif
