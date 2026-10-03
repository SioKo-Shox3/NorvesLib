#include "Text/Base64.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstring>
#include <iostream>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Text;

namespace
{
    // 既存の期待値照合を簡潔にする試験専用wrapper。production APIはサイズを値で返す。
    Base64DecodeResult GetSize(Container::Span<const uint8_t> encoded, size_t& size)
    {
        const auto outcome = GetBase64DecodedSize(encoded);
        size = outcome.Size;
        return outcome.Result;
    }
    Base64DecodeResult Decode(Container::Span<const uint8_t> encoded, Container::Span<uint8_t> output, size_t& size)
    {
        const auto outcome = DecodeBase64(encoded, output);
        size = outcome.Size;
        return outcome.Result;
    }
    Container::Span<const uint8_t> Bytes(const char* text)
    {
        return {reinterpret_cast<const uint8_t*>(text), std::strlen(text)};
    }
    void Check(const char* encoded, const char* expected)
    {
        uint8_t output[32];
        std::memset(output, 0xab, sizeof(output));
        size_t size = 99;
        assert(GetSize(Bytes(encoded), size) == Base64DecodeResult::Success);
        assert(size == std::strlen(expected));
        assert(Decode(Bytes(encoded), output, size) == Base64DecodeResult::Success);
        assert(size == std::strlen(expected) && std::memcmp(output, expected, size) == 0);
        for (size_t index = size; index < sizeof(output); ++index)
        {
            assert(output[index] == 0xab);
        }
    }
    void Reject(const char* encoded)
    {
        uint8_t output[32];
        std::memset(output, 0xab, sizeof(output));
        size_t size = 99;
        assert(GetSize(Bytes(encoded), size) != Base64DecodeResult::Success && size == 0);
        size = 99;
        assert(Decode(Bytes(encoded), output, size) != Base64DecodeResult::Success && size == 0);
        for (const auto byte : output)
        {
            assert(byte == 0xab);
        }
    }
    void RoundTrip(uint8_t a, uint8_t b, uint8_t c, size_t count)
    {
        // 復号器の実装を呼ばず、各byteのbitを直接alphabetの添字へ分割する。
        constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        const uint8_t encoded[4] = {
            static_cast<uint8_t>(alphabet[a/4]),
            static_cast<uint8_t>(alphabet[(a%4)*16+(count > 1 ? b/16 : 0)]),
            static_cast<uint8_t>(count > 1 ? alphabet[(b%16)*4+(count > 2 ? c/64 : 0)] : '='),
            static_cast<uint8_t>(count > 2 ? alphabet[c%64] : '=')};
        uint8_t decoded[4] = {0xab,0xab,0xab,0xab};
        size_t written = 99;
        assert(Decode(encoded, decoded, written) == Base64DecodeResult::Success);
        assert(written == count && decoded[0] == a);
        if (count > 1)
        {
            assert(decoded[1] == b);
        }
        if (count > 2)
        {
            assert(decoded[2] == c);
        }
        assert(decoded[count] == 0xab);
    }
}

int main()
{
    Check("", "");
    Check("Zg==", "f");
    Check("Zm8=", "fo");
    Check("Zm9v", "foo");
    Check("Zm9vYg==", "foob");
    Check("Zm9vYmE=", "fooba");
    Check("Zm9vYmFy", "foobar");
    for (const char* invalid : {"Z", "Zg", "Zg=", "Zg===", "====", "=g==", "Z===", "Zg=A",
        "Zg==AAAA", "Zm8=AAAA", "Zm9v!!!!", "Zm9v\n", "Zm 9v", "Zm9_", "Zm9-", "Zh==", "Zm9=", "AA\t=", "\xff" "AAA"})
    {
        Reject(invalid);
    }
    size_t written = 99;
    uint8_t output[16];
    std::memset(output, 0xab, sizeof(output));
    assert(Decode(Bytes("Zm9v"), {output, 2}, written) == Base64DecodeResult::InsufficientBuffer && written == 0);
    assert(Decode(Bytes("Zm9v"), {nullptr, 3}, written) == Base64DecodeResult::InvalidArgument && written == 0);
    assert(Decode({nullptr, 4}, output, written) == Base64DecodeResult::InvalidArgument && written == 0);
    const uint8_t withNull[] = {'Z', 'g', 0, '='};
    assert(Decode(withNull, output, written) != Base64DecodeResult::Success && written == 0);
    for (const auto value : output)
    {
        assert(value == 0xab);
    }
    assert(Decode({}, {}, written) == Base64DecodeResult::Success && written == 0);
    uint8_t shared[16] = {0,0,'Z','m','9','v'};
    uint8_t copy[16];
    std::memcpy(copy, shared, sizeof(shared));
    for (const size_t offset : {0u,2u,3u})
    {
        assert(Decode({shared+2,4},{shared+offset,3},written) == Base64DecodeResult::OverlappingBuffers);
        assert(written == 0 && std::memcmp(copy,shared,sizeof(shared)) == 0);
    }
    assert(Decode({shared+2,4},{shared+6,3},written) == Base64DecodeResult::Success && written == 3);
    assert(std::memcmp(shared+6,"foo",3) == 0);
    for (unsigned value = 0; value < 256; ++value)
    {
        RoundTrip(static_cast<uint8_t>(value),0,0,1);
        for (unsigned second = 0; second < 256; ++second)
        {
            RoundTrip(static_cast<uint8_t>(value),static_cast<uint8_t>(second),0,2);
        }
    }
    // サイズ参照を書き戻さないため、整数objectのbyte viewも復号先として正しく保持する。
    size_t byteStorage = 0;
    auto storage = Container::Span<uint8_t>(reinterpret_cast<uint8_t*>(&byteStorage), sizeof(byteStorage));
    const auto success = DecodeBase64(Bytes("Zg=="), storage);
    assert(success.Result == Base64DecodeResult::Success && success.Size == 1 && storage[0] == 'f');
    const size_t saved = byteStorage;
    const auto failure = DecodeBase64(Bytes("!!!!"), storage);
    assert(failure.Result == Base64DecodeResult::InvalidCharacter && failure.Size == 0 && byteStorage == saved);
    uint32_t random = 0x171733u;
    for (int index = 0; index < 4096; ++index)
    {
        random = random*1664525u+1013904223u;
        RoundTrip(static_cast<uint8_t>(random),static_cast<uint8_t>(random >> 8),static_cast<uint8_t>(random >> 16),3);
    }
    std::cout << "Base64DecodeTest PASS: rfc_vectors_256_single_65536_pair_4096_triple_failures\n";
    return 0;
}
