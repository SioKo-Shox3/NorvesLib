#include "Resource/GltfContainer.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstring>
#include <iostream>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Gltf;

namespace
{
    constexpr uint32_t JsonType = 0x4e4f534a;
    constexpr uint32_t BinType = 0x004e4942;
    constexpr uint32_t UnknownType = 0x12345678;
    const uint8_t Json[] = {'{', '}', ' ', ' '};
    const uint8_t Bin[] = {0, 17, 250, 0};

    struct Bytes
    {
        uint8_t Data[1024]{};
        size_t Size = 12;
        Bytes()
        {
            U32(0, 0x46546c67);
            U32(4, 2);
        }
        void U32(size_t offset, uint32_t value)
        {
            assert(offset+4 <= sizeof(Data));
            for (size_t index = 0; index < 4; ++index)
            {
                Data[offset+index] = static_cast<uint8_t>(value >> (index*8));
            }
        }
        void Chunk(uint32_t type, Container::Span<const uint8_t> payload)
        {
            assert(Size+8+payload.size() <= sizeof(Data));
            U32(Size, static_cast<uint32_t>(payload.size()));
            U32(Size+4, type);
            Size += 8;
            if (!payload.empty())
            {
                std::memcpy(Data+Size, payload.data(), payload.size());
            }
            Size += payload.size();
        }
        Container::Span<const uint8_t> Finish()
        {
            U32(8, static_cast<uint32_t>(Size));
            return {Data, Size};
        }
    };

    void Empty(const ContainerView& view)
    {
        assert(view.Json.data() == nullptr && view.Json.empty());
        assert(view.Bin.data() == nullptr && view.Bin.empty());
        assert(!view.IsGlb && !view.HasBin);
    }

    void Expect(Bytes& bytes, ContainerParseResult expected)
    {
        ContainerView view;
        view.Json = Json;
        view.Bin = Bin;
        view.IsGlb = view.HasBin = true;
        const auto input = bytes.Finish();
        const Bytes before = bytes;
        assert(ParseContainer(input, view) == expected);
        assert(std::memcmp(before.Data, bytes.Data, sizeof(bytes.Data)) == 0);
        if (expected != ContainerParseResult::Success && expected != ContainerParseResult::NotGlb)
        {
            Empty(view);
        }
    }
}

int main()
{
    static_assert(noexcept(ParseContainer({}, *static_cast<ContainerView*>(nullptr))));
    ContainerView view;
    Bytes good;
    good.Chunk(JsonType, Json);
    good.Chunk(BinType, Bin);
    const auto goodInput = good.Finish();
    assert(ParseContainer(goodInput, view) == ContainerParseResult::Success);
    assert(view.IsGlb && view.HasBin);
    assert(view.Json.data() == good.Data+20 && view.Json.size() == 4);
    assert(view.Bin.data() == good.Data+32 && view.Bin.size() == 4);
    assert(std::memcmp(view.Json.data(), Json, 4) == 0 && std::memcmp(view.Bin.data(), Bin, 4) == 0);

    Bytes jsonOnly;
    jsonOnly.Chunk(JsonType, Json);
    assert(ParseContainer(jsonOnly.Finish(), view) == ContainerParseResult::Success);
    assert(view.IsGlb && !view.HasBin && view.Bin.empty());
    jsonOnly.Chunk(UnknownType, Bin);
    Expect(jsonOnly, ContainerParseResult::Success);
    good.Chunk(UnknownType, {});
    good.Chunk(UnknownType, Bin);
    Expect(good, ContainerParseResult::Success);

    // 意味上のJSON検証は上位層。空chunkの有無はsizeではなくHasBinで区別する。
    Bytes zero;
    zero.Chunk(JsonType, {});
    zero.Chunk(BinType, {});
    assert(ParseContainer(zero.Finish(), view) == ContainerParseResult::Success);
    assert(view.Json.empty() && view.Bin.empty() && view.HasBin);
    const uint8_t raw[] = {0xef, 0xbb, 0xbf, '{', '}'};
    assert(ParseContainer(raw, view) == ContainerParseResult::NotGlb);
    assert(view.Json.data() == raw+3 && view.Json.size() == 2 && !view.IsGlb && !view.HasBin);
    assert(ParseContainer(Json, view) == ContainerParseResult::NotGlb && view.Json.data() == Json);
    assert(ParseContainer({raw, 3}, view) == ContainerParseResult::NotGlb && view.Json.empty());
    assert(ParseContainer({}, view) == ContainerParseResult::NotGlb && view.Json.empty());
    assert(ParseContainer({nullptr, 4}, view) == ContainerParseResult::InvalidArgument);
    Empty(view);

    Bytes missing;
    Expect(missing, ContainerParseResult::MissingJsonChunk);
    Bytes binFirst;
    binFirst.Chunk(BinType, Bin);
    Expect(binFirst, ContainerParseResult::InvalidChunkOrder);
    Bytes unknownFirst;
    unknownFirst.Chunk(UnknownType, Bin);
    Expect(unknownFirst, ContainerParseResult::InvalidChunkOrder);
    Bytes lateBin;
    lateBin.Chunk(JsonType, Json);
    lateBin.Chunk(UnknownType, Bin);
    lateBin.Chunk(BinType, Bin);
    Expect(lateBin, ContainerParseResult::InvalidChunkOrder);
    Bytes duplicateJson;
    duplicateJson.Chunk(JsonType, Json);
    duplicateJson.Chunk(JsonType, Json);
    Expect(duplicateJson, ContainerParseResult::DuplicateChunk);
    Bytes duplicateBin;
    duplicateBin.Chunk(JsonType, Json);
    duplicateBin.Chunk(BinType, Bin);
    duplicateBin.Chunk(BinType, Bin);
    Expect(duplicateBin, ContainerParseResult::DuplicateChunk);
    Bytes odd;
    odd.Chunk(JsonType, {Json, 3});
    Expect(odd, ContainerParseResult::InvalidChunkLength);
    Bytes oversized;
    oversized.Chunk(JsonType, Json);
    oversized.U32(12, 0xfffffffcu);
    Expect(oversized, ContainerParseResult::TruncatedChunk);
    Bytes brokenUnknown;
    brokenUnknown.Chunk(JsonType, Json);
    brokenUnknown.Chunk(UnknownType, Bin);
    brokenUnknown.U32(24, 0xfffffffcu);
    Expect(brokenUnknown, ContainerParseResult::TruncatedChunk);

    Bytes canonical;
    canonical.Chunk(JsonType, Json);
    canonical.Chunk(BinType, Bin);
    canonical.Finish();
    Bytes version = canonical;
    version.U32(4, 1);
    Expect(version, ContainerParseResult::UnsupportedVersion);
    version.U32(4, 3);
    Expect(version, ContainerParseResult::UnsupportedVersion);
    Bytes wrongMagic = canonical;
    wrongMagic.Data[0] = 'x';
    Expect(wrongMagic, ContainerParseResult::NotGlb);
    for (size_t size = 0; size < canonical.Size; ++size)
    {
        assert(ParseContainer(canonical.Finish(), view) == ContainerParseResult::Success);
        const auto result = ParseContainer({canonical.Data, size}, view);
        const auto expected = size < 4 ? ContainerParseResult::NotGlb : size < 12
            ? ContainerParseResult::TruncatedHeader : ContainerParseResult::LengthMismatch;
        assert(result == expected);
        if (size >= 4)
        {
            Empty(view);
        }
        if (size >= 12)
        {
            Bytes cut = canonical;
            cut.Size = size;
            const auto structural = size == 12 ? ContainerParseResult::MissingJsonChunk : size == 24
                ? ContainerParseResult::Success : ContainerParseResult::TruncatedChunk;
            Expect(cut, structural);
        }
    }
    canonical.U32(8, static_cast<uint32_t>(canonical.Size+4));
    assert(ParseContainer({canonical.Data, canonical.Size}, view) == ContainerParseResult::LengthMismatch);
    Empty(view);

    // 入力アドレスをずらしても整数ロードのalignmentへ依存しない。
    const auto aligned = canonical.Finish();
    uint8_t unaligned[128]{};
    std::memcpy(unaligned+1, aligned.data(), aligned.size());
    assert(ParseContainer({unaligned+1, aligned.size()}, view) == ContainerParseResult::Success);
    assert(view.Json.data() == unaligned+21 && view.Bin.data() == unaligned+33);
    // 有効入力の各所を固定seedで壊し、成功viewが常に入力内に収まることを反証する。
    uint32_t random = 0x67a39u;
    for (int sample = 0; sample < 4000; ++sample)
    {
        Bytes mutated = canonical;
        for (int change = 0; change < 4; ++change)
        {
            random = random*1664525u+1013904223u;
            const size_t index = random % mutated.Size;
            random = random*1664525u+1013904223u;
            mutated.Data[index] ^= static_cast<uint8_t>(random >> 24);
        }
        const Bytes before = mutated;
        const auto result = ParseContainer({mutated.Data, mutated.Size}, view);
        assert(std::memcmp(before.Data, mutated.Data, sizeof(mutated.Data)) == 0);
        if (result == ContainerParseResult::Success || result == ContainerParseResult::NotGlb)
        {
            const auto begin = reinterpret_cast<uintptr_t>(mutated.Data);
            const auto end = begin+mutated.Size;
            const auto check = [&](Container::Span<const uint8_t> part)
            {
                const auto start = reinterpret_cast<uintptr_t>(part.data());
                assert(start >= begin && start <= end && part.size() <= end-start);
            };
            check(view.Json);
            assert(view.IsGlb == (result == ContainerParseResult::Success));
            if (view.HasBin)
            {
                check(view.Bin);
            }
            else
            {
                assert(view.Bin.empty() && view.Bin.data() == nullptr);
            }
        }
        else
        {
            Empty(view);
        }
    }
    std::cout << "GltfContainerTest PASS: borrowed_views_text_chunks_truncation_order_overflow\n";
    return 0;
}
