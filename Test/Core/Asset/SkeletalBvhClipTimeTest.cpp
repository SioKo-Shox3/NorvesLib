// 時刻と整数予算の実helperをhostでも検査する。束ねる入口にも明示returnを持つ。
#include "Animation/SkeletalBvhClipTime.h"
#include <cstdio>
#include <cstdlib>
#include <cfenv>
#include <cmath>
#include <limits>
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "Clip time check failed %s:%d %s\n", __FILE__, __LINE__, #x);                         \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (false)
namespace A = NorvesLib::Core::Animation;
using Mode = A::SkeletalBvhClipTimeMode;
using Status = A::SkeletalBvhClipTimeStatus;
namespace
{
    void TestIndependentTime()
    {
        float previousHeader = 0, previousOverride = 0;
        for (uint32_t i = 0; i < 67; ++i)
        {
            const auto header =
                A::ComputeSkeletalBvhClipTime(Mode::HeaderFrameTime, 1.0 / 30.0, 0, i, i != 0, previousHeader);
            const auto overridden =
                A::ComputeSkeletalBvhClipTime(Mode::OverrideFps, 1.0 / 30.0, 16, i, i != 0, previousOverride);
            CHECK(header.Succeeded() && overridden.Succeeded());
            CHECK(header.ExactSeconds == static_cast<double>(i) * (1.0 / 30.0));
            CHECK(overridden.ExactSeconds == static_cast<double>(i) / 16.0);
            CHECK(overridden.StoredSeconds == static_cast<float>(i) / 16.0f);
            CHECK(overridden.RoundingErrorSeconds == 0);
            CHECK(header.RoundingErrorSeconds ==
                  std::abs(static_cast<double>(header.StoredSeconds) - header.ExactSeconds));
            previousHeader = header.StoredSeconds;
            previousOverride = overridden.StoredSeconds;
        }
        CHECK(previousOverride == 4.125f);
        CHECK(previousHeader != previousOverride);
        const auto single =
            A::ComputeSkeletalBvhClipTime(Mode::HeaderFrameTime, std::numeric_limits<double>::max(), 0, 0, false, 0);
        CHECK(single.Succeeded() && single.StoredSeconds == 0 && single.ExactSeconds == 0);
    }
    void TestRefusalsAndFloatEdges()
    {
        const auto call =
            [](Mode mode, double interval, double fps, uint32_t frame = 0, bool hasPrevious = false, float previous = 0)
        {
            return A::ComputeSkeletalBvhClipTime(mode, interval, fps, frame, hasPrevious, previous);
        };
        CHECK(call(Mode::Unspecified, 1, 1).Status == Status::InvalidInput);
        const double bad[] = {0, -1, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()};
        for (double value : bad)
        {
            CHECK(call(Mode::HeaderFrameTime, value, 0).Status == Status::InvalidInput);
            CHECK(call(Mode::OverrideFps, 1, value).Status == Status::InvalidInput);
        }
        CHECK(call(Mode::HeaderFrameTime, 1, 0, 0, true, 0).Status == Status::InvalidInput);
        CHECK(call(Mode::HeaderFrameTime, 1, 0, 1, false, 0).Status == Status::InvalidInput);
        CHECK(call(Mode::HeaderFrameTime, 1, 0, 1, true, -1).Status == Status::InvalidInput);
        CHECK(call(Mode::HeaderFrameTime, 1, 0, 1, true, std::numeric_limits<float>::quiet_NaN()).Status ==
              Status::InvalidInput);
        CHECK(call(Mode::HeaderFrameTime, 1, 0, 1, true, 2).Status == Status::NonIncreasingTime);
        const double largest = static_cast<double>(std::numeric_limits<float>::max());
        CHECK(call(Mode::HeaderFrameTime, largest, 0, 1, true, 0).StoredSeconds == std::numeric_limits<float>::max());
        CHECK(call(Mode::HeaderFrameTime, largest, 0, 2, true, 0).Status == Status::UnrepresentableTime);
        CHECK(call(Mode::OverrideFps, 1, std::numeric_limits<double>::denorm_min()).Status ==
              Status::UnrepresentableTime);
        const double minimum = static_cast<double>(std::numeric_limits<float>::denorm_min());
        CHECK(call(Mode::HeaderFrameTime, minimum * .25, 0, 1, true, 0).Status == Status::UnrepresentableTime);
        const auto first = call(Mode::HeaderFrameTime, minimum * .6, 0, 1, true, 0);
        CHECK(first.Succeeded());
        CHECK(first.StoredSeconds == std::numeric_limits<float>::denorm_min());
        CHECK(call(Mode::HeaderFrameTime, minimum * .6, 0, 2, true, first.StoredSeconds).Status ==
              Status::NonIncreasingTime);
        CHECK(call(Mode::OverrideFps, 1, 1, 16777216, true, 16777215).Succeeded());
        CHECK(call(Mode::OverrideFps, 1, 1, 16777217, true, 16777216).Status == Status::NonIncreasingTime);
        const int old = std::fegetround();
        CHECK(std::fesetround(FE_DOWNWARD) == 0);
        CHECK(call(Mode::HeaderFrameTime, 1, 0).Status == Status::UnsupportedFloatEnvironment);
        CHECK(std::fegetround() == FE_DOWNWARD);
        CHECK(std::fesetround(old) == 0);
    }
    void TestCheckedBudget()
    {
        using namespace A::Detail;
        uint64_t out = 77;
        CHECK(BvhClipCheckedAdd(3, 5, out) && out == 8);
        CHECK(BvhClipCheckedMultiply(out, 4, out) && out == 32);
        CHECK(!BvhClipCheckedAdd(UINT64_MAX, 1, out) && out == 32);
        CHECK(!BvhClipCheckedMultiply(UINT64_MAX, 2, out) && out == 32);
        CHECK(ComputeSkeletalBvhClipWork(1, 1, 1, 3, 9, 4, out) && out == 1264);
        const uint64_t saved = out;
        CHECK(!ComputeSkeletalBvhClipWork(UINT64_MAX, 1, 1, 3, 9, 4, out) && out == saved);
        CHECK(!ComputeSkeletalBvhClipWork(1, UINT64_MAX, 1, 3, 9, 4, out) && out == saved);
        CHECK(!ComputeSkeletalBvhClipWork(1, 1, UINT64_MAX, 3, 9, 4, out) && out == saved);
        CHECK(!ComputeSkeletalBvhClipWork(1, 1, 1, UINT64_MAX, 9, 4, out) && out == saved);
        CHECK(!ComputeSkeletalBvhClipWork(1, 1, 1, 3, UINT64_MAX, 4, out) && out == saved);
        CHECK(!ComputeSkeletalBvhClipWork(1, 1, 1, 3, 9, UINT64_MAX, out) && out == saved);
        CHECK(ComputeSkeletalBvhClipWork(53, 53, 53, 67, 67 * 159, 318, out) && out < (uint64_t{1} << 26));
        CHECK(ComputeSkeletalBvhClipWork(1, 1024, 1024, 10000, 30000, 1024, out) && out > (uint64_t{1} << 26));
    }
} // namespace
int main()
{
    TestIndependentTime();
    TestRefusalsAndFloatEdges();
    TestCheckedBudget();
    std::puts(
        "SKELETAL_BVH_CLIP_TIME result=pass explicit_header_override_independent_float_time_limits_checked_work_no_inference");
    return 0;
}
