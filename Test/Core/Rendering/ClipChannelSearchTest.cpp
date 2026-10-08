// 共通の二分探索を、旧線形探索と端点の前後で比較する。Core/Windowsに依存しない。
#include "Animation/ClipChannelSampler.h"
#include "Container/Span.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

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
}
#if defined(NORVES_POSE_SEARCH_STANDALONE)
int main()
{
    TestClipChannelSearch();
    std::puts("ClipChannelSearchTest PASS");
}
#endif
