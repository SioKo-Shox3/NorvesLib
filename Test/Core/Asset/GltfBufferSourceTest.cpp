#include "Resource/GltfBufferSource.h"
#include "Text/Base64.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Gltf;

namespace
{
    Container::Span<const uint8_t> Bytes(const char* text)
    {
        return {reinterpret_cast<const uint8_t*>(text), std::strlen(text)};
    }
    void Reject(const char* text, BufferSourceResult result)
    {
        const auto parsed = ParseDataUri(Bytes(text));
        assert(parsed.Result == result);
        assert(parsed.View.Mime == DataUriMime::Unknown && parsed.View.EncodedPayload.data() == nullptr && parsed.View.PercentDecodedSize == 0);
    }
    void Decode(const char* uri, DataUriMime mime)
    {
        const auto input = Bytes(uri);
        const auto parsed = ParseDataUri(input);
        assert(parsed.Result == BufferSourceResult::Success && parsed.View.Mime == mime);
        assert(parsed.View.EncodedPayload.data() >= input.data() && parsed.View.EncodedPayload.data() <= input.data()+input.size());
        assert(parsed.View.PercentDecodedSize == 4);
        uint8_t ascii[16];
        uint8_t output[16];
        std::memset(ascii, 0xab, sizeof(ascii));
        std::memset(output, 0xab, sizeof(output));
        const auto unescaped = DecodePercentBytes(parsed.View.EncodedPayload, ascii);
        assert(unescaped.Result == BufferSourceResult::Success && unescaped.Size == 4);
        const auto decoded = Text::DecodeBase64({ascii,unescaped.Size},output);
        assert(decoded.Result == Text::Base64DecodeResult::Success && decoded.Size == 1 && output[0] == 'f');
        for (size_t index = 4; index < sizeof(ascii); ++index)
        {
            assert(ascii[index] == 0xab);
        }
        for (size_t index = 1; index < sizeof(output); ++index)
        {
            assert(output[index] == 0xab);
        }
    }
}

int main()
{
    Decode("data:application/octet-stream;base64,Zg==", DataUriMime::OctetStream);
    Decode("DATA:APPLICATION/GLTF-BUFFER;BASE64,Zg==", DataUriMime::GltfBuffer);
    Decode("data:image/png;charset=UTF-8;name=test%20image;base64,Zg%3D%3d", DataUriMime::Png);
    Decode("data:im%61ge%2Fjpeg;x=%22value%3Bpart%22;base64,%5Ag==", DataUriMime::Jpeg);
    Decode("data:image/png;x=%23%5E%7C%7B%7D%60;base64,Zg==", DataUriMime::Png);
    for (const char* invalid : {"data:image/png;x=#;base64,Zg==", "data:image/png;x=^;base64,Zg==",
        "data:image/png;x=|;base64,Zg==", "data:image/png;x={;base64,Zg==",
        "data:image/png;x=};base64,Zg==", "data:image/png;x=`;base64,Zg==",
        "data:image/png;#=x;base64,Zg==", "data:image/png;base64,Zg==#fragment"})
    {
        Reject(invalid,BufferSourceResult::InvalidDataUri);
    }
    const auto empty = ParseDataUri(Bytes("data:image/png;base64,"));
    assert(empty.Result == BufferSourceResult::Success && empty.View.PercentDecodedSize == 0);
    Reject("",BufferSourceResult::NotDataUri);
    Reject("file.bin",BufferSourceResult::NotDataUri);
    Reject("data:image/png;base64",BufferSourceResult::InvalidDataUri);
    Reject("data:image/webp;base64,Zg==",BufferSourceResult::UnsupportedMediaType);
    Reject("data:;base64,Zg==",BufferSourceResult::UnsupportedMediaType);
    Reject("data:application/octet-stream,Zg==",BufferSourceResult::UnsupportedEncoding);
    Reject("data:image/png;base64=1,Zg==",BufferSourceResult::UnsupportedEncoding);
    Reject("data:image/png;base64;base64,Zg==",BufferSourceResult::InvalidDataUri);
    Reject("data:image/png;base64;x=y,Zg==",BufferSourceResult::InvalidDataUri);
    Reject("data:image/png;;base64,Zg==",BufferSourceResult::InvalidDataUri);
    Reject("data:image/png;x=;base64,Zg==",BufferSourceResult::InvalidDataUri);
    Reject("data:image/png;=x;base64,Zg==",BufferSourceResult::InvalidDataUri);
    Reject("data:image/png;x=raw space;base64,Zg==",BufferSourceResult::InvalidDataUri);
    Reject("data:image/png;x=raw\\slash;base64,Zg==",BufferSourceResult::InvalidDataUri);
    Reject("data:image/png;x=%0a;base64,Zg==",BufferSourceResult::InvalidDataUri);
    Reject("data:image/png;x=%ZZ;base64,Zg==",BufferSourceResult::InvalidDataUri);
    for (const char* suffix : {"data:image/png;base64,%", "data:image/png;base64,%0", "data:image/png;base64,%GG"})
    {
        Reject(suffix,BufferSourceResult::InvalidPercentEscape);
    }
    assert(ParseDataUri({nullptr,9}).Result == BufferSourceResult::InvalidArgument);
    // URIの構造が正しくてもbase64の意味検証は必須。
    const auto bad64 = ParseDataUri(Bytes("data:application/octet-stream;base64,!!!!"));
    assert(bad64.Result == BufferSourceResult::Success);
    assert(Text::GetBase64DecodedSize(bad64.View.EncodedPayload).Result == Text::Base64DecodeResult::InvalidCharacter);

    constexpr char hex[] = "0123456789ABCDEF";
    for (unsigned value = 0; value < 256; ++value)
    {
        const uint8_t escaped[] = {'%', static_cast<uint8_t>(hex[value/16]), static_cast<uint8_t>(hex[value%16])};
        uint8_t output[2] = {0xab,0xab};
        const auto result = DecodePercentBytes(escaped,output);
        assert(result.Result == BufferSourceResult::Success && result.Size == 1);
        assert(output[0] == value && output[1] == 0xab);
    }
    uint8_t output[16];
    std::memset(output, 0xab, sizeof(output));
    assert(DecodePercentBytes(Bytes("abc%ZZ"),output).Result == BufferSourceResult::InvalidPercentEscape);
    assert(DecodePercentBytes(Bytes("%20"),{output,0}).Result == BufferSourceResult::InsufficientBuffer);
    assert(DecodePercentBytes({nullptr,1},output).Result == BufferSourceResult::InvalidArgument);
    assert(DecodePercentBytes(Bytes("a"),{nullptr,1}).Result == BufferSourceResult::InvalidArgument);
    for (const auto byte : output)
    {
        assert(byte == 0xab);
    }
    uint8_t shared[16] = {'a','%','2','0','b'};
    assert(DecodePercentBytes({shared,5},{shared+1,3}).Result == BufferSourceResult::OverlappingBuffers);
    assert(shared[0] == 'a' && shared[1] == '%');
    const auto adjacent = DecodePercentBytes({shared,5},{shared+5,3});
    assert(adjacent.Result == BufferSourceResult::Success && std::memcmp(shared+5,"a b",3) == 0);
    assert(DecodePercentBytes({},{}).Result == BufferSourceResult::Success);
    const uint8_t plus[] = {'+','%','0','0'};
    const auto literal = DecodePercentBytes(plus,output);
    assert(literal.Result == BufferSourceResult::Success && literal.Size == 2 && output[0] == '+' && output[1] == 0);

    uint8_t bin[8] = {17,0,0,0,0,0,0,0};
    ContainerView container;
    container.IsGlb = container.HasBin = true;
    container.Bin = {bin,4};
    for (size_t length = 1; length <= 4; ++length)
    {
        const auto bound = BindGlbBuffer(container,0,length);
        assert(bound.Result == BufferSourceResult::Success && bound.Bytes.data() == bin && bound.Bytes.size() == length);
    }
    const auto CheckRejected = [&](size_t index,size_t length,BufferSourceResult expected)
    {
        const auto bound = BindGlbBuffer(container,index,length);
        assert(bound.Result == expected && bound.Bytes.data() == nullptr && bound.Bytes.empty());
    };
    CheckRejected(1,1,BufferSourceResult::InvalidBinIndex);
    CheckRejected(0,0,BufferSourceResult::InvalidByteLength);
    CheckRejected(0,5,BufferSourceResult::InvalidByteLength);
    CheckRejected(0,std::numeric_limits<size_t>::max(),BufferSourceResult::InvalidByteLength);
    bin[3] = 1;
    CheckRejected(0,1,BufferSourceResult::InvalidBinPadding);
    bin[3] = 0;
    container.Bin = {bin,8};
    CheckRejected(0,4,BufferSourceResult::InvalidByteLength);
    container.Bin = {bin,3};
    CheckRejected(0,1,BufferSourceResult::InvalidArgument);
    container.Bin = {nullptr,4};
    CheckRejected(0,1,BufferSourceResult::InvalidArgument);
    container.Bin = {};
    CheckRejected(0,1,BufferSourceResult::InvalidByteLength);
    container.HasBin = false;
    CheckRejected(0,1,BufferSourceResult::MissingBin);
    container.IsGlb = false;
    container.HasBin = true;
    CheckRejected(0,1,BufferSourceResult::MissingBin);
    const char seed[] = "data:application/octet-stream;x=value;base64,Zg%3D%3D";
    uint32_t random = 0x283819u;
    for (int sample = 0; sample < 4000; ++sample)
    {
        uint8_t candidate[sizeof(seed)-1];
        std::memcpy(candidate,seed,sizeof(candidate));
        random = random*1664525u+1013904223u;
        const size_t length = random % (sizeof(candidate)+1);
        if (length > 0)
        {
            random = random*1664525u+1013904223u;
            candidate[random % length] ^= static_cast<uint8_t>(random >> 24);
        }
        const auto parsed = ParseDataUri({candidate,length});
        if (parsed.Result == BufferSourceResult::Success)
        {
            const auto begin = reinterpret_cast<uintptr_t>(candidate);
            const auto start = reinterpret_cast<uintptr_t>(parsed.View.EncodedPayload.data());
            assert(start >= begin && start-begin <= length);
            assert(parsed.View.EncodedPayload.size() <= length-(start-begin));
            uint8_t ascii[sizeof(seed)];
            const auto unescaped = DecodePercentBytes(parsed.View.EncodedPayload,ascii);
            assert(unescaped.Result == BufferSourceResult::Success && unescaped.Size == parsed.View.PercentDecodedSize);
        }
        else
        {
            assert(parsed.View.Mime == DataUriMime::Unknown && parsed.View.EncodedPayload.data() == nullptr);
        }
    }
    std::cout << "GltfBufferSourceTest PASS: data_uri_percent_base64_pipeline_bin_binding\n";
    return 0;
}
