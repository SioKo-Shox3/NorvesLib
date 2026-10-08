#include "Engine/TimeScaleMath.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Engine;
int main()
{
    constexpr auto ok = TimeScaleMathResult::Success;
    uint32_t scale = 7;
    assert(QuantizeTimeScale(.3, scale) == ok && scale == 19661);
    assert(QuantizeTimeScale(.5 / 65536, scale) == ok && scale == 1);
    assert(QuantizeTimeScale(std::nextafter(.5 / 65536, 0.0), scale) == ok && scale == 0);
    assert(QuantizeTimeScale(.5 / 65536, scale) == ok && scale == 1);
    for (const auto invalid : {-1., std::numeric_limits<double>::infinity(), std::nan("")})
    {
        assert(QuantizeTimeScale(invalid, scale) == TimeScaleMathResult::InvalidArgument && scale == 1);
    }
    assert(QuantizeTimeScale(65536, scale) == TimeScaleMathResult::Overflow && scale == 1);
    assert(ComposeTimeScales({}, scale) == ok && scale == 65536);
    const uint32_t product[] = {32768, 32768, 65536};
    assert(ComposeTimeScales({product, 3}, scale) == ok && scale == 16384);
    const uint32_t half[] = {1, 32768}, less[] = {1, 32767};
    assert(ComposeTimeScales({half, 2}, scale) == ok && scale == 1);
    assert(ComposeTimeScales({less, 2}, scale) == ok && scale == 0);
    const uint32_t large[] = {UINT32_MAX, UINT32_MAX, 1, 1};
    const uint32_t reverse[] = {1, 1, UINT32_MAX, UINT32_MAX};
    assert(ComposeTimeScales({large, 4}, scale) == ok && scale == 65536);
    uint32_t reordered = 0;
    assert(ComposeTimeScales({reverse, 4}, reordered) == ok && reordered == scale);
    assert(ComposeTimeScales({large, 2}, scale) == TimeScaleMathResult::Overflow && scale == 65536);
    const uint32_t zero[] = {UINT32_MAX, UINT32_MAX, 0};
    assert(ComposeTimeScales({zero, 3}, scale) == ok && scale == 0);
    uint32_t carry = 65535;
    int64_t out = -1;
    assert(ScaleTimeNanoseconds(INT64_MAX, 0, carry, out) == ok && out == 0 && carry == 65535);
    assert(ScaleTimeNanoseconds(INT64_MAX, 65536, carry, out) == ok && out == INT64_MAX && carry == 65535);
    assert(ScaleTimeNanoseconds(INT64_MAX, 65537, carry, out) == TimeScaleMathResult::Overflow && out == INT64_MAX &&
           carry == 65535);
    assert(ScaleTimeNanoseconds(0, UINT32_MAX, carry, out) == ok && out == 0 && carry == 65535);
    assert(ScaleTimeNanoseconds(-1, 1, carry, out) == TimeScaleMathResult::InvalidArgument && out == 0 &&
           carry == 65535);
    carry = 65536;
    assert(ScaleTimeNanoseconds(1, 1, carry, out) == TimeScaleMathResult::InvalidArgument && carry == 65536);
    for (uint32_t numerator : {0u, 1u, 19661u, 32768u, 65536u, 131072u, UINT32_MAX})
    {
        uint32_t singleCarry = 12345, splitCarry = singleCarry;
        int64_t single = -1, total = 0;
        assert(ScaleTimeNanoseconds(100003, numerator, singleCarry, single) == ok);
        for (int i = 0; i < 100003; ++i)
        {
            assert(ScaleTimeNanoseconds(1, numerator, splitCarry, out) == ok);
            total += out;
        }
        assert(total == single && splitCarry == singleCarry);
    }
    // 100msの停止を150msで跨ぐ区間。端数は停止によって捨てない。
    const ConstantTimeScaleSegment segments[] = {{100000000, 0}, {50000000, 65536}};
    carry = 123;
    assert(AccumulateTimeSegments({segments, 2}, carry, out) == ok && out == 50000000 && carry == 123);
    const ConstantTimeScaleSegment fail[] = {{1, 32768}, {INT64_MAX, UINT32_MAX}};
    out = 99;
    carry = 123;
    assert(AccumulateTimeSegments({fail, 2}, carry, out) == TimeScaleMathResult::Overflow && out == 99 && carry == 123);
    const ConstantTimeScaleSegment sumOverflow[] = {{INT64_MAX, 65536}, {1, 65536}};
    assert(AccumulateTimeSegments({sumOverflow, 2}, carry, out) == TimeScaleMathResult::Overflow && out == 99 &&
           carry == 123);
    std::cout << "TimeScaleMathTest passed\n";
    return 0;
}
