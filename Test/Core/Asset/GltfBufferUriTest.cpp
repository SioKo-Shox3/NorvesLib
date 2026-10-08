#include "Resource/GltfBufferSet.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstring>
#include <iostream>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Gltf;
int main()
{
    const auto span = [](const char* text)
    {
        return Container::Span<const uint8_t>(reinterpret_cast<const uint8_t*>(text),std::strlen(text));
    };
    for (const char* valid : {"file.bin", "sub/file.bin", ".hidden", "a b/file.bin", "file#name.bin", "nested/a_b-1.bin"})
    {
        assert(IsValidRelativeBufferUri(span(valid)));
    }
    for (const char* invalid : {"", ".", "..", "../a", "/a", "a/..", "a/.", "a//b", "a/", "C:/a", "a\\b", "a:stream", "a/.. /b", "a./b", "a /b", "a\t/b"})
    {
        assert(!IsValidRelativeBufferUri(span(invalid)));
    }
    const uint8_t nul[] = {'a',0,'b'};
    const uint8_t unicode[] = {0xe3,0x81,0x82};
    assert(!IsValidRelativeBufferUri(nul));
    assert(!IsValidRelativeBufferUri(unicode));
    assert(!IsValidRelativeBufferUri({nullptr,1}));
    std::cout << "GltfBufferUriTest PASS: relative_ascii_segments_platform_aliases\n";
    return 0;
}
