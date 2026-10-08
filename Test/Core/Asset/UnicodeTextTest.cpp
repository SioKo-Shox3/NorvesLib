// UTF8/16/32とJSON字句用の文字判定を実helperで検証する。
#include "Text/UnicodeText.h"
#include "Text/JsonUnicodeScalar.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::abort(); } } while(false)

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::TextDetail;
template<typename Char>
void RoundTrip(uint32_t scalar)
{
    const auto encoded=EncodeJsonUnicodeScalar<Char>(scalar);
    size_t calls=0;
    CHECK(ForEachUnicodeScalar<Char>({encoded.Units,encoded.Count},[&](uint32_t value)
    {
        CHECK(value==scalar); ++calls;
    }));
    CHECK(calls==1);
}
int main()
{
    for (uint32_t scalar=0;scalar<=0x10ffff;++scalar)
    {
        if (scalar>=0xd800 && scalar<=0xdfff)
        {
            continue;
        }
        RoundTrip<char>(scalar);RoundTrip<char16_t>(scalar);RoundTrip<char32_t>(scalar);
    }
    const uint8_t invalid[][4]={{0xc0,0x80,0,0},{0xed,0xa0,0x80,0},{0xf4,0x90,0x80,0x80},{0x80,0,0,0},{0xe2,0x82,0,0}};
    const size_t lengths[]={2,3,4,1,2};
    for (size_t i=0;i<5;++i)
    {
        CHECK(!ForEachUnicodeScalar<uint8_t>({invalid[i],lengths[i]},[](uint32_t){}));
    }
    const char16_t high[]={0xd800},low[]={0xdc00},badPair[]={0xd800,'A'};
    CHECK(!ForEachUnicodeScalar<char16_t>(high,[](uint32_t){}));
    CHECK(!ForEachUnicodeScalar<char16_t>(low,[](uint32_t){}));
    CHECK(!ForEachUnicodeScalar<char16_t>(badPair,[](uint32_t){}));
    const char32_t tooLarge[]={0x110000},surrogate[]={0xdfff};
    CHECK(!ForEachUnicodeScalar<char32_t>(tooLarge,[](uint32_t){}));
    CHECK(!ForEachUnicodeScalar<char32_t>(surrogate,[](uint32_t){}));
    CHECK(!ForEachUnicodeScalar<char>({nullptr,1},[](uint32_t){}));
    CHECK(!ForEachUnicodeScalar<char32_t>({reinterpret_cast<const char32_t*>(UINTPTR_MAX-3),2},[](uint32_t){}));
    CHECK(ForEachUnicodeScalar<char>({},[](uint32_t){}));
    CHECK(!IsJsonWhitespace(U'\u010a') && !IsJsonWhitespace(U'\v') && !IsJsonWhitespace(U'\f'));
    CHECK(IsJsonWhitespace(U'\n') && IsJsonWhitespace(U'\r') && IsJsonWhitespace(U'\t') && IsJsonWhitespace(U' '));
    CHECK(!IsJsonDigit(U'\u0131') && IsJsonDigit(U'1'));
    std::puts("UnicodeTextTest PASS: all_scalar_roundtrips_invalid_sequences_json_ascii");
    return 0;
}
