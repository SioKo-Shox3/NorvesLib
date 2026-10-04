#include "Asset/CookedSkeletalWireValidation.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>

using namespace NorvesLib::Core::Asset;
namespace
{
    using Status = CookedSkeletalWireStatus;
    void U32(uint8_t* bytes, uint32_t value)
    {
        for (size_t index = 0; index < 4; ++index)
        {
            bytes[index] = static_cast<uint8_t>(value >> (index * 8));
        }
    }
    void U64(uint8_t* bytes, uint64_t value)
    {
        U32(bytes, static_cast<uint32_t>(value));
        U32(bytes + 4, static_cast<uint32_t>(value >> 32));
    }
}
int main()
{
    static_assert(CookedSkeletalFormatV0::VersionMinor == 1 && CookedSkeletalFormatV0::HeaderSize == 256);
    static_assert(CookedSkeletalFormatV02::VersionMinor == 2 && CookedSkeletalFormatV02::HeaderSize == 320);
    static_assert(CookedSkeletalFormatV02::HeaderOffset::SubmeshRecordSize == 256);
    static_assert(CookedSkeletalFormatV02::HeaderOffset::ExtraVertexFlags + 4 == 320);
    static_assert(CookedSkeletalFormatV02::SubmeshOffset::Reserved + 24 == 64);
    static_assert(CookedSkeletalFormatV02::MaterialSlotOffset::Reserved + 48 == 64);
    CookedSkeletalWireProfile profile;
    for (uint16_t minor : {uint16_t{0}, uint16_t{1}, uint16_t{2}})
    {
        const uint32_t header = minor == 2 ? 320 : 256;
        assert(ResolveCookedSkeletalWireProfile(0, minor, header, profile) == Status::Success);
        assert(profile.VersionMinor == minor && profile.HeaderSize == header && profile.bHasSubmeshTables == (minor == 2) && profile.bAllowsMultipleClips == (minor == 2));
        const auto retained = profile;
        assert(ResolveCookedSkeletalWireProfile(0, minor, header + 16, profile) == Status::InvalidHeaderSize);
        assert(profile.VersionMinor == retained.VersionMinor && profile.HeaderSize == retained.HeaderSize);
    }
    assert(ResolveCookedSkeletalWireProfile(1, 0, 256, profile) == Status::UnsupportedVersion);
    assert(ResolveCookedSkeletalWireProfile(0, 3, 320, profile) == Status::UnsupportedVersion);
    assert(profile.HeaderSize == 320);
    const CookedSkeletalWireCounts oldCounts{3,3,2,1,2,4,0,0};
    const CookedSkeletalWireCounts newCounts{3,6,2,2,4,8,2,2};
    assert(ValidateCookedSkeletalWireCounts(0, oldCounts) == Status::Success);
    assert(ValidateCookedSkeletalWireCounts(1, oldCounts) == Status::Success);
    assert(ValidateCookedSkeletalWireCounts(2, newCounts) == Status::Success);
    assert(ValidateCookedSkeletalWireCounts(1, newCounts) == Status::InvalidCount);
    assert(ValidateCookedSkeletalWireCounts(2, oldCounts) == Status::InvalidCount);
    assert(ValidateCookedSkeletalWireCounts(3, newCounts) == Status::UnsupportedVersion);
    for (int invalid = 0; invalid < 12; ++invalid)
    {
        auto c = newCounts;
        switch (invalid)
        {
        case 0: c.Vertices = 0; break;
        case 1: c.Vertices = uint64_t{UINT32_MAX} + 1; break;
        case 2: c.Indices = 4; break;
        case 3: c.Joints = 129; break;
        case 4: c.Clips = 0; break;
        case 5: c.Clips = 5; break;
        case 6: c.Samples = 3; break;
        case 7: c.Submeshes = 0; break;
        case 8: c.Submeshes = 9; break;
        case 9: c.MaterialSlots = 0; break;
        case 10: c.MaterialSlots = 9; break;
        case 11: c.Submeshes = 3; break;
        }
        assert(ValidateCookedSkeletalWireCounts(2, c) == Status::InvalidCount);
    }
    auto limits = newCounts; limits.Joints = 128; limits.Indices = 24; limits.Submeshes = 8; limits.MaterialSlots = 8;
    assert(ValidateCookedSkeletalWireCounts(2, limits) == Status::Success);
    // 手書きの位置列。旧goldenは861byte、0.2の2clip/2表は1421byte。
    const CookedSkeletalWireSection oldSections[] = {{256,192},{448,12},{464,160},{624,32},{656,64},{720,128},{848,13}};
    const CookedSkeletalWireSection newSections[] = {{320,192},{512,24},{544,160},{704,64},{768,128},{896,256},{1152,128},{1280,128},{1408,13}};
    for (uint16_t minor : {uint16_t{0}, uint16_t{1}})
    {
        assert(ValidateCookedSkeletalWireSections(minor, 861, oldSections, oldCounts) == Status::Success);
    }
    assert(ValidateCookedSkeletalWireSections(2, 1421, newSections, newCounts) == Status::Success);
    assert(ValidateCookedSkeletalWireSections(2, 1421, {nullptr,9}, newCounts) == Status::InvalidInput);
    assert(ValidateCookedSkeletalWireSections(2, 1421, {newSections,std::numeric_limits<size_t>::max()}, newCounts) == Status::InvalidInput);
    assert(ValidateCookedSkeletalWireSections(2, 1421, oldSections, newCounts) == Status::InvalidInput);
    for (int invalid = 0; invalid < 8; ++invalid)
    {
        CookedSkeletalWireSection sections[9]; std::memcpy(sections, newSections, sizeof(sections));
        uint64_t fileSize = 1421;
        switch (invalid)
        {
        case 0: sections[0].Offset = 256; break;
        case 1: sections[2].Offset = 540; break;
        case 2: sections[2].Offset = 560; break;
        case 3: sections[1].Size = 12; break;
        case 4: sections[7].Offset = 1152; break;
        case 5: sections[8].Size = UINT64_MAX; break;
        case 6: fileSize = 1422; break;
        case 7: fileSize = 319; break;
        }
        assert(ValidateCookedSkeletalWireSections(2, fileSize, sections, newCounts) == Status::InvalidSectionRange);
    }
    uint8_t extension[64] = {};
    U32(extension,64); U32(extension+4,64); U64(extension+8,1152); U64(extension+16,128);
    U64(extension+24,1280); U64(extension+32,128); U32(extension+40,2); U32(extension+44,2);
    CookedSkeletalV02Extension decoded;
    assert(ReadCookedSkeletalV02Extension(extension,1421,decoded) == Status::Success);
    assert(decoded.SubmeshCount == 2 && decoded.MaterialSlotCount == 2 && decoded.Submeshes.Offset == 1152 && decoded.MaterialSlots.Offset == 1280);
    for (int invalid = 0; invalid < 10; ++invalid)
    {
        uint8_t bytes[64]; std::memcpy(bytes,extension,64);
        Status expected = Status::InvalidSectionRange;
        switch (invalid)
        {
        case 0: U32(bytes,32); expected=Status::InvalidRecordSize; break;
        case 1: U32(bytes+4,128); expected=Status::InvalidRecordSize; break;
        case 2: U32(bytes+40,0); expected=Status::InvalidCount; break;
        case 3: U32(bytes+44,9); expected=Status::InvalidCount; break;
        case 4: U64(bytes+48,1); expected=Status::UnsupportedExtraVertex; break;
        case 5: U32(bytes+56,1); expected=Status::UnsupportedExtraVertex; break;
        case 6: U32(bytes+60,1); expected=Status::UnsupportedExtraVertex; break;
        case 7: U64(bytes+8,UINT64_MAX-63); break;
        case 8: U64(bytes+32,UINT64_MAX); break;
        case 9: U64(bytes+24,1296); break;
        }
        auto unchanged = decoded;
        assert(ReadCookedSkeletalV02Extension(bytes,1421,unchanged) == expected);
        assert(unchanged.Submeshes.Offset == decoded.Submeshes.Offset && unchanged.MaterialSlots.Offset == decoded.MaterialSlots.Offset && unchanged.SubmeshCount == 2);
    }
    assert(ReadCookedSkeletalV02Extension({nullptr,64},1421,decoded) == Status::InvalidInput);
    assert(ReadCookedSkeletalV02Extension({extension,63},1421,decoded) == Status::InvalidInput);
    uint8_t transform[64], hashExtension[64], payload[31];
    for (size_t i=0;i<64;++i)
    {
        transform[i]=static_cast<uint8_t>(i); hashExtension[i]=static_cast<uint8_t>(i*3+1);
    }
    for (size_t i=0;i<31;++i)
    {
        payload[i]=static_cast<uint8_t>(i*7+5);
    }
    assert(ComputeCookedSkeletalPayloadHash(payload,31) == 0xa0b2c92400105b19ull);
    assert(ComputeCookedSkeletalV01Hash(transform,payload,31) == 0x94d5d17771c416d9ull);
    assert(ComputeCookedSkeletalV02Hash(transform,hashExtension,payload,31) == 0xd9b766e941a00f59ull);
    assert(ComputeCookedSkeletalV02Hash(transform,hashExtension,nullptr,0) == 0x486959f8d3834165ull);
    uint8_t blob[351] = {};
    std::memcpy(blob+192,transform,64); std::memcpy(blob+256,hashExtension,64); std::memcpy(blob+320,payload,31);
    uint64_t hash=123;
    assert(TryComputeCookedSkeletalWireHash(0,2,320,blob,hash) == Status::Success && hash == 0xd9b766e941a00f59ull);
    const auto retainedHash=hash;
    blob[300] ^= 1;
    assert(TryComputeCookedSkeletalWireHash(0,2,320,blob,hash) == Status::Success && hash != retainedHash);
    assert(TryComputeCookedSkeletalWireHash(0,2,320,{blob,319},hash) == Status::InvalidInput);
    hash=123;
    assert(TryComputeCookedSkeletalWireHash(0,2,256,blob,hash) == Status::InvalidHeaderSize && hash == 123);
    assert(TryComputeCookedSkeletalWireHash(1,0,256,blob,hash) == Status::UnsupportedVersion && hash == 123);
    assert(TryComputeCookedSkeletalWireHash(0,2,320,{nullptr,351},hash) == Status::InvalidInput && hash == 123);
    std::memcpy(blob+256,payload,31);
    assert(TryComputeCookedSkeletalWireHash(0,0,256,{blob,287},hash) == Status::Success && hash == 0xa0b2c92400105b19ull);
    assert(TryComputeCookedSkeletalWireHash(0,1,256,{blob,287},hash) == Status::Success && hash == 0x94d5d17771c416d9ull);
    std::cout << "CookedSkeletalWireContractTest PASS: legacy_v02_profiles_counts_sections_extension_hash_atomicity\n";
    return 0;
}
