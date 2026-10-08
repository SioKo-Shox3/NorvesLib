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
    const uint8_t png[] = {137, 80, 78, 71, 13, 10, 26, 10};
    const uint8_t jpeg[] = {255, 216, 255};
    assert(MatchesEmbeddedImageMime(png, DataUriMime::Png));
    assert(MatchesEmbeddedImageMime(jpeg, DataUriMime::Jpeg));
    assert(!MatchesEmbeddedImageMime(png, DataUriMime::Jpeg));
    assert(!MatchesEmbeddedImageMime(jpeg, DataUriMime::Png));
    assert(!MatchesEmbeddedImageMime(bytes, DataUriMime::Png));
    assert(!MatchesEmbeddedImageMime({}, DataUriMime::Png));
    assert(!MatchesEmbeddedImageMime({nullptr,8}, DataUriMime::Png));
    for (const auto mime : {DataUriMime::Unknown, DataUriMime::OctetStream, DataUriMime::GltfBuffer, static_cast<DataUriMime>(255)})
    {
        assert(!MatchesEmbeddedImageMime(png,mime));
        assert(!MatchesEmbeddedImageMime(jpeg,mime));
    }
    assert(ProbeEmbeddedImageMime(png) == DataUriMime::Png);
    assert(ProbeEmbeddedImageMime(jpeg) == DataUriMime::Jpeg);
    for (size_t length = 0; length < sizeof(png); ++length)
    {
        assert(ProbeEmbeddedImageMime({png, length}) == DataUriMime::Unknown);
    }
    for (size_t length = 0; length < sizeof(jpeg); ++length)
    {
        assert(ProbeEmbeddedImageMime({jpeg, length}) == DataUriMime::Unknown);
    }
    assert(ProbeEmbeddedImageMime({nullptr, sizeof(png)}) == DataUriMime::Unknown);
    assert(ProbeEmbeddedImageMime(bytes) == DataUriMime::Unknown);
    std::cout << "GltfImageRangeTest PASS: declared_range_nonempty_null_overflow_borrow\n";
    return 0;
}
