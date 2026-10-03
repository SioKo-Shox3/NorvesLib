#include "Resource/GltfImageSource.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <limits>
#include <type_traits>

using namespace NorvesLib::Core::Gltf;

int main()
{
    static_assert(std::is_nothrow_move_constructible_v<ImageSource>);
    static_assert(std::is_nothrow_move_assignable_v<ImageSource>);
    const uint8_t bytes[] = {1, 2, 3, 4, 5, 6, 7, 8};
    for (size_t offset = 0; offset <= 9; ++offset)
    {
        for (size_t length = 0; length <= 10; ++length)
        {
            const auto result = BindImageByteRange(bytes, offset, length);
            const bool bExpected = length > 0 && offset < 8 && length <= 8 - offset;
            assert(result.bValid == bExpected);
            if (bExpected)
            {
                assert(result.Bytes.data() == bytes + offset && result.Bytes.size() == length);
            }
            else
            {
                assert(result.Bytes.empty() && result.Bytes.data() == nullptr);
            }
        }
    }
    assert(!BindImageByteRange({}, 0, 1).bValid);
    assert(!BindImageByteRange({nullptr, 8}, 0, 1).bValid);
    const auto max = std::numeric_limits<size_t>::max();
    assert(!BindImageByteRange(bytes, max, 1).bValid);
    assert(!BindImageByteRange(bytes, 1, max).bValid);
    assert(!BindImageByteRange(bytes, max, max).bValid);
    std::cout << "GltfImageRangeTest PASS: declared_range_nonempty_null_overflow_borrow\n";
    return 0;
}
