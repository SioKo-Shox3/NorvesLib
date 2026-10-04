#include "Library/Core/Private/Text/JsonUnicodeScalar.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <cstring>
#include <iostream>

using namespace NorvesLib::Core::TextDetail;
int main()
{
    JsonUnicodeEscape out{123,456};
    const auto decode = [&out](const char* text)
    {
        return DecodeJsonUnicodeEscape<char>({text,std::strlen(text)},out);
    };
    assert(decode("9aa8") && out.Scalar == 0x9aa8 && out.Consumed == 4);
    assert(decode("D83D\\uDC3A") && out.Scalar == 0x1f43a && out.Consumed == 10);
    assert(decode("0000") && out.Scalar == 0 && out.Consumed == 4);
    for (const char* invalid : {"", "000", "gggg", "DC00", "D800", "D800\\u0000", "D800\\uD800", "D800\\uDC0", "D800xuDC00"})
    {
        out = {123,456};
        assert(!decode(invalid) && out.Scalar == 123 && out.Consumed == 456);
    }
    assert(!DecodeJsonUnicodeEscape<char>({nullptr,10},out));
    const auto wolf = EncodeJsonUnicodeScalar<unsigned char>(0x1f43a);
    const uint8_t expected[] = {0xf0,0x9f,0x90,0xba};
    assert(wolf.Count == 4 && std::memcmp(wolf.Units,expected,4) == 0);
    const auto wide = EncodeJsonUnicodeScalar<char16_t>(0x1f43a);
    assert(wide.Count == 2 && wide.Units[0] == 0xd83d && wide.Units[1] == 0xdc3a);
    assert(EncodeJsonUnicodeScalar<char32_t>(0x1f43a).Units[0] == 0x1f43a);
    assert(EncodeJsonUnicodeScalar<char>(0).Count == 1);
    assert(EncodeJsonUnicodeScalar<char>(0xd800).Count == 0);
    assert(EncodeJsonUnicodeScalar<char>(0x110000).Count == 0);
    uint64_t hash = 14695981039346656037ull;
    uint64_t bytes = 0;
    for (uint32_t scalar = 1; scalar <= 0x10ffff; ++scalar)
    {
        if (scalar >= 0xd800 && scalar <= 0xdfff)
        {
            continue;
        }
        char escaped[11]{};
        if (scalar < 0x10000)
        {
            std::snprintf(escaped,sizeof(escaped),"%04x",scalar);
        }
        else
        {
            const uint32_t value = scalar - 0x10000;
            std::snprintf(escaped,sizeof(escaped),"%04x\\u%04x",0xd800+(value>>10),0xdc00+(value&1023));
        }
        assert(decode(escaped) && out.Scalar == scalar);
        const auto encoded = EncodeJsonUnicodeScalar<unsigned char>(scalar);
        for (size_t index = 0; index < encoded.Count; ++index)
        {
            hash = (hash ^ encoded.Units[index]) * 1099511628211ull;
            ++bytes;
        }
    }
    assert(hash == 0x20a9b4125d4cb87ull && bytes == 4382591);
    std::cout << "JsonUnicodeScalarTest PASS: escapes_surrogate_pairs_invalid_atomic_all_scalars_utf8_golden\n";
    return 0;
}
