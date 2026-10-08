#include "Resource/GltfBufferJson.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

using namespace NorvesLib::Core::Gltf;
int main()
{
    assert(ParseBufferByteLength(1).bValid && ParseBufferByteLength(1).Value == 1);
    assert(ParseBufferByteLength(1234).bValid && ParseBufferByteLength(1234).Value == 1234);
    for (const double bad : {0.0,-0.0,-1.0,0.5,1.5,9007199254740992.0,
        std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::max()})
    {
        const auto result = ParseBufferByteLength(bad);
        assert(!result.bValid && result.Value == 0);
    }
    if constexpr (sizeof(size_t) >= 8)
    {
        const auto maximum = ParseBufferByteLength(9007199254740991.0);
        assert(maximum.bValid && maximum.Value == 9007199254740991ull);
    }
    else
    {
        const double maximum = static_cast<double>(std::numeric_limits<size_t>::max());
        assert(ParseBufferByteLength(maximum).bValid);
        assert(!ParseBufferByteLength(maximum+1).bValid);
    }
    assert(!ParseBufferByteLength(std::nextafter(1.0,0.0)).bValid);
    assert(!ParseBufferByteLength(std::nextafter(1.0,2.0)).bValid);
    for (size_t size = 1; size < 10000; ++size)
    {
        const auto result = ParseBufferByteLength(static_cast<double>(size));
        assert(result.bValid && result.Value == size);
    }
    std::cout << "GltfBufferNumberTest PASS: safe_positive_exact_size\n";
    return 0;
}
