#include "Engine/TimeSystem.h"
#include "Engine/TimeScaleFadeMath.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Engine;
namespace
{
    constexpr auto success = TimeSystemResult::Success;
    bool Near(double a, double b, double tolerance = 1e-6)
    {
        return std::fabs(a - b) <= tolerance;
    }
    TimeScaleHandle Push(TimeSystem& clock, double scale, double duration, TimeChannelMask channels, double fadeIn = 0,
                         double fadeOut = 0)
    {
        TimeScaleRequest request;
        request.Scale = scale;
        request.DurationSeconds = duration;
        request.Channels = channels;
        request.FadeInSeconds = fadeIn;
        request.FadeOutSeconds = fadeOut;
        TimeScaleHandle handle;
        assert(clock.PushScale(request, handle) == success);
        return handle;
    }
} // namespace
int main()
{
    constexpr auto world = TimeChannelBit(TimeChannel::World), physics = TimeChannelBit(TimeChannel::Physics);
    constexpr TimeChannelMask hitstop = world | physics | TimeChannelBit(TimeChannel::Animation);
    {
        TimeSystem clock;
        assert(clock.BeginFrame(100000000, .1f) == success);
        assert(clock.GetFrameTimes().World == .1f && clock.GetFrameTimes().PhysicsDeltaNanoseconds == 100000000);
        const auto handle = Push(clock, 0, .1, hitstop);
        assert(clock.BeginFrame(150000000, .1f) == success);
        const auto& frame = clock.GetFrameTimes();
        assert(Near(frame.World, 1. / 30) && Near(frame.Animation, 1. / 30));
        assert(frame.PhysicsDeltaNanoseconds == 50000000 && frame.Unscaled == .1f && frame.Audio == .1f &&
               frame.Particle == .1f);
        assert(clock.GetActiveRequestCount() == 0 && clock.RemoveScale(handle) == TimeSystemResult::StaleHandle);
    }
    {
        TimeSystem clock;
        Push(clock, .5, 10, physics);
        assert(clock.BeginFrame(1, 1e-9f) == success && clock.GetPhysicsRemainder() == 32768);
        Push(clock, 0, .1, hitstop);
        assert(clock.BeginFrame(50000000, .05f) == success && clock.GetFrameTimes().PhysicsDeltaNanoseconds == 0 &&
               clock.GetPhysicsRemainder() == 32768);
        assert(clock.BeginFrame(100000000, .1f, false) == success && clock.GetPhysicsRemainder() == 32768);
        assert(clock.GetFrameTimes().World == 0 && clock.GetFrameTimes().Unscaled == .1f);
        assert(clock.BeginFrame(1, 1e-9f) == success && clock.GetFrameTimes().PhysicsDeltaNanoseconds == 1 &&
               clock.GetPhysicsRemainder() == 0);
    }
    {
        TimeSystem one, split;
        for (auto* clock : {&one, &split})
        {
            Push(*clock, .3, 2, physics);
            Push(*clock, .5, 2, physics);
        }
        assert(one.BeginFrame(100003, .000100003f) == success);
        int64_t total = 0;
        for (int i = 0; i < 100003; ++i)
        {
            assert(split.BeginFrame(1, 1e-9f) == success);
            total += split.GetFrameTimes().PhysicsDeltaNanoseconds;
        }
        assert(total == one.GetFrameTimes().PhysicsDeltaNanoseconds &&
               split.GetPhysicsRemainder() == one.GetPhysicsRemainder());
    }
    {
        TimeSystem clock;
        Push(clock, 0, 1, hitstop, 1);
        Push(clock, 0, 1, hitstop, 1);
        assert(clock.BeginFrame(1000000000, 1) == success);
        assert(Near(clock.GetFrameTimes().World, 1. / 3));
        assert(std::llabs(clock.GetFrameTimes().PhysicsDeltaNanoseconds - 333333333) <= 7630);
        assert(clock.GetActiveRequestCount() == 0);
        TimeSystem tent;
        Push(tent, 0, 1, world, .5, .5);
        assert(tent.BeginFrame(1000000000, 1) == success && Near(tent.GetFrameTimes().World, .5));
    }
    {
        TimeSystem one, split;
        for (auto* clock : {&one, &split})
        {
            Push(*clock, .2, 1, hitstop, .2, .3);
            Push(*clock, 1.7, .8, hitstop, .3, .1);
        }
        assert(one.BeginFrame(1000000000, 1) == success);
        int64_t total = 0;
        double worldTotal = 0;
        for (int i = 0; i < 100; ++i)
        {
            assert(split.BeginFrame(10000000, .01f) == success);
            total += split.GetFrameTimes().PhysicsDeltaNanoseconds;
            worldTotal += split.GetFrameTimes().World;
        }
        assert(std::llabs(total - one.GetFrameTimes().PhysicsDeltaNanoseconds) <= 15260);
        assert(Near(worldTotal, one.GetFrameTimes().World));
    }
    {
        TimeSystem clock;
        auto handle = Push(clock, 0, 1, world);
        assert(clock.RemoveScale(handle) == success);
        assert(clock.BeginFrame(100000000, .1f) == success && clock.GetFrameTimes().World == .1f);
        handle = Push(clock, 0, 1, world);
        clock.Reset();
        const auto replacement = Push(clock, 0, 1, world);
        assert(handle.Value != replacement.Value && clock.RemoveScale(handle) == TimeSystemResult::StaleHandle);
        TimeScaleRequest invalid;
        invalid.Channels = TimeChannelBit(TimeChannel::Unscaled);
        TimeScaleHandle out{77};
        assert(clock.PushScale(invalid, out) == TimeSystemResult::InvalidArgument && out.Value == 77);
        invalid.Channels = world;
        invalid.FadeInSeconds = .1;
        invalid.FadeOutSeconds = .1;
        assert(clock.PushScale(invalid, out) == TimeSystemResult::InvalidArgument);
        const auto count = clock.GetActiveRequestCount();
        const auto now = clock.GetElapsedNanoseconds();
        assert(clock.BeginFrame(-1, 0) == TimeSystemResult::InvalidArgument && clock.GetActiveRequestCount() == count &&
               clock.GetElapsedNanoseconds() == now);
    }
    {
        TimeSystem clock;
        Push(clock, 65535, 100, physics);
        Push(clock, 65535, 100, physics);
        assert(clock.BeginFrame(1, 1e-9f) == TimeSystemResult::Overflow);
        assert(clock.GetElapsedNanoseconds() == 0 && clock.GetActiveRequestCount() == 2 &&
               clock.GetPhysicsRemainder() == 0);
        assert(clock.BeginFrame(100000000000LL, .1f, false) == success && clock.GetActiveRequestCount() == 0);
        assert(clock.BeginFrame(1, 1e-9f) == success);
    }
    {
        const LinearTimeScale opposed[] = {{0, 1}, {1, 0}};
        double result = 99;
        assert(AverageLinearTimeScales({opposed, 2}, result) == TimeScaleMathResult::Success &&
               Near(result, 1. / 6, 1e-15));
        const LinearTimeScale large[] = {{1e300, 1e300}, {1e300, 1e300}, {1e-300, 1e-300}, {1e-300, 1e-300}};
        assert(AverageLinearTimeScales({large, 4}, result) == TimeScaleMathResult::Success && Near(result, 1, 1e-14));
        const double max = std::numeric_limits<double>::max();
        const LinearTimeScale edge[] = {{max, max}, {1, 1}};
        assert(AverageLinearTimeScales({edge, 2}, result) == TimeScaleMathResult::Success && result == max);
    }
    {
        Container::VariableArray<LinearTimeScale> factors;
        for (unsigned i = 0; i < 1100; ++i)
            factors.push_back({.5, 1});
        for (unsigned i = 0; i < 1100; ++i)
            factors.push_back({2, 0});
        double expected = 1, result = 0;
        for (unsigned n = 1; n <= 1100; ++n)
            expected *= double(2 * n) / double(2 * n + 1);
        assert(AverageLinearTimeScales({factors.data(), factors.size()}, result) == TimeScaleMathResult::Success);
        assert(Near(result, expected, 1e-13));
    }
    {
        TimeSystem clock;
        TimeScaleRequest request;
        request.DurationSeconds = 1;
        request.FadeInSeconds = 1;
        request.FadeOutSeconds = .4e-9;
        TimeScaleHandle handle;
        assert(clock.PushScale(request, handle) == success);
        request.FadeOutSeconds = .6e-9;
        assert(clock.PushScale(request, handle) == TimeSystemResult::InvalidArgument);
    }
    {
        TimeSystem plain, identityFade;
        for (auto* clock : {&plain, &identityFade})
            for (uint32_t numerator : {83989u, 85489u, 771853811u})
                Push(*clock, double(numerator) / 65536, 1, physics);
        Push(identityFade, 1, 1, physics, 1);
        assert(plain.BeginFrame(65536, .000065536f) == success);
        assert(identityFade.BeginFrame(65536, .000065536f) == success);
        assert(plain.GetFrameTimes().PhysicsDeltaNanoseconds == identityFade.GetFrameTimes().PhysicsDeltaNanoseconds);
        assert(plain.GetPhysicsRemainder() == identityFade.GetPhysicsRemainder());
    }
    std::cout << "TimeSystemTest passed\n";
    return 0;
}
