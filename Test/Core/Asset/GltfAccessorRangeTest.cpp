#include "Resource/GltfAccessorRange.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>

using NorvesLib::Core::Gltf::ComputeAccessorByteRange;

int main()
{
    constexpr auto packed = ComputeAccessorByteRange(102, 102, 0, 36, 0, 3, 12, 12);
    static_assert(packed.bValid && packed.StartOffset == 0 && packed.RequiredBytes == 36);
    constexpr auto strided = ComputeAccessorByteRange(128, 120, 8, 80, 4, 3, 12, 24);
    static_assert(strided.bValid && strided.StartOffset == 12 && strided.RequiredBytes == 60);
    static_assert(!ComputeAccessorByteRange(128, 120, 8, 60, 4, 3, 12, 24).bValid);
    static_assert(!ComputeAccessorByteRange(128, 120, 0, 121, 0, 1, 1, 1).bValid);
    static_assert(!ComputeAccessorByteRange(119, 120, 0, 1, 0, 1, 1, 1).bValid);
    static_assert(!ComputeAccessorByteRange(16, 16, 0, 16, 0, 0, 4, 4).bValid);
    static_assert(!ComputeAccessorByteRange(16, 16, 0, 16, 0, 1, 0, 0).bValid);
    static_assert(!ComputeAccessorByteRange(16, 16, 0, 16, 0, 1, 4, 3).bValid);
    const auto max = std::numeric_limits<size_t>::max();
    assert(!ComputeAccessorByteRange(max, max, max, 1, 0, 1, 1, 1).bValid);
    assert(!ComputeAccessorByteRange(max, max, 1, max, 0, 1, 1, 1).bValid);
    assert(!ComputeAccessorByteRange(max, max, 0, max, 1, max, 1, 2).bValid);
    const auto edge = ComputeAccessorByteRange(max, max, max - 1, 1, 0, 1, 1, max);
    assert(edge.bValid && edge.StartOffset == max - 1 && edge.RequiredBytes == 1);
    const auto full = ComputeAccessorByteRange(max, max, 0, max, 0, max, 1, 1);
    assert(full.bValid && full.RequiredBytes == max);

    uint64_t cases = 0;
    for (size_t actual = 0; actual <= 16; ++actual)
    for (size_t declared = 0; declared <= 16; ++declared)
    for (size_t offset = 0; offset <= 16; offset += 4)
    for (size_t length = 0; length <= 16; length += 4)
    for (size_t local = 0; local <= 16; local += 4)
    for (size_t count = 0; count <= 4; ++count)
    for (size_t element = 0; element <= 4; ++element)
    for (size_t stride = 0; stride <= 5; ++stride)
    {
        const uint64_t required = count == 0 ? 0 : (count - 1) * stride + element;
        const bool bExpected = declared <= actual && offset + length <= declared && local <= length &&
            count > 0 && element > 0 && stride >= element && local + required <= length;
        const auto result = ComputeAccessorByteRange(actual, declared, offset, length, local, count, element, stride);
        assert(result.bValid == bExpected);
        if (bExpected)
        {
            assert(result.StartOffset == offset + local && result.RequiredBytes == required);
        }
        else
        {
            assert(result.StartOffset == 0 && result.RequiredBytes == 0);
        }
        ++cases;
    }
#if defined(__SIZEOF_INT128__)
    uint64_t state = 0x6172636573736f72ull;
    auto next = [&]()
    {
        state ^= state << 13; state ^= state >> 7; state ^= state << 17;
        return static_cast<size_t>(state);
    };
    size_t validWideCases = 0;
    for (size_t i = 0; i < 20000; ++i)
    {
        size_t actual = next(), declared = next(), offset = next(), length = next();
        size_t local = next(), count = next(), element = next(), stride = next();
        if ((i & 1) == 0)
        {
            actual = max;
            declared = max;
            offset = next() / 2;
            length = max - offset;
            local = next() % (length / 2);
            element = 1 + next() % 16;
            stride = element + next() % 32;
            count = 1 + next() % ((length - local - element) / stride + 1);
        }
        using Wide = unsigned __int128;
        const Wide required = count == 0 ? 0 : Wide(count - 1) * stride + element;
        const bool bExpected = declared <= actual && Wide(offset) + length <= declared && local <= length &&
            count > 0 && element > 0 && stride >= element && Wide(local) + required <= length;
        const auto result = ComputeAccessorByteRange(actual, declared, offset, length, local, count, element, stride);
        assert(result.bValid == bExpected);
        if (bExpected)
        {
            assert(result.StartOffset == Wide(offset) + local && result.RequiredBytes == required);
            ++validWideCases;
        }
        else
        {
            assert(result.StartOffset == 0 && result.RequiredBytes == 0);
        }
        ++cases;
    }
    assert(validWideCases >= 10000);
#endif
    std::cout << "GltfAccessorRangeTest PASS: " << cases << " bounded_and_overflow_cases\n";
    return 0;
}
