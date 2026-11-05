#include "Asset/CookedGrassFieldFormat.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

using namespace NorvesLib::Core::Asset;
using namespace NorvesLib::Core::Asset::CookedGrassFieldFormatV0;
using NorvesLib::Core::Container::Span;
using NorvesLib::Core::Container::VariableArray;
using ByteArray = VariableArray<uint8_t>;

#define CHECK(expression)                                                                                              \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expression))                                                                                             \
        {                                                                                                              \
            std::cerr << Utf8(u8"検査失敗: ") << #expression << Utf8(u8" 行 ") << __LINE__ << "\n";                   \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (false)

namespace
{
    const char* Utf8(const char8_t* text)
    {
        return reinterpret_cast<const char*>(text);
    }

    uint64_t ReadLe64(const ByteArray& bytes, size_t offset)
    {
        uint64_t value = 0;
        for (size_t i = 0; i < sizeof(value); ++i)
        {
            value |= static_cast<uint64_t>(bytes[offset + i]) << (i * 8);
        }
        return value;
    }

    void WriteLe(ByteArray& bytes, size_t offset, uint64_t value, size_t width)
    {
        for (size_t i = 0; i < width; ++i)
        {
            bytes[offset + i] = static_cast<uint8_t>(value >> (i * 8));
        }
    }

    void CheckStatus(const char* name, CookedGrassFieldParseStatus actual, CookedGrassFieldParseStatus expected)
    {
        if (actual != expected)
        {
            std::cerr << Utf8(u8"読み込みの検査失敗: ") << name << Utf8(u8"、期待した理由: ")
                      << GetCookedGrassFieldParseStatusName(expected) << Utf8(u8"、実際の理由: ")
                      << GetCookedGrassFieldParseStatusName(actual) << "\n";
            std::exit(1);
        }
    }

    void CheckRejection(const ByteArray& bytes, CookedGrassFieldParseStatus expected)
    {
        const auto result = ParseCookedGrassField(AssetBlob::CopyBytes(Span<const uint8_t>(bytes)));
        CheckStatus(GetCookedGrassFieldParseStatusName(expected), result.Status, expected);
    }
}

int main()
{
    constexpr uint32_t LayerAResolution = 4;
    constexpr uint32_t LayerBResolution = 2;
    constexpr size_t Channels = 4;
    constexpr size_t ChunkCount = 2;
    constexpr size_t AlphaChannel = 3;
    uint8_t layerA[ChunkCount][LayerAResolution * LayerAResolution * Channels]{};
    uint8_t layerB[ChunkCount][LayerBResolution * LayerBResolution * Channels]{};

    for (size_t chunkIndex = 0; chunkIndex < ChunkCount; ++chunkIndex)
    {
        for (size_t pixel = 0; pixel < LayerAResolution * LayerAResolution; ++pixel)
        {
            const size_t offset = pixel * Channels;
            layerA[chunkIndex][offset] = static_cast<uint8_t>((chunkIndex == 0 ? 20 : 80) + pixel * (chunkIndex == 0 ? 3 : 4));
            layerA[chunkIndex][offset + 1] = static_cast<uint8_t>((chunkIndex == 0 ? 170 : 220) - pixel * (chunkIndex == 0 ? 5 : 7));
            layerA[chunkIndex][offset + 2] = static_cast<uint8_t>(100 + chunkIndex * 17 + pixel);
            layerA[chunkIndex][offset + AlphaChannel] = static_cast<uint8_t>((pixel + chunkIndex) % 2);
        }
        for (size_t pixel = 0; pixel < LayerBResolution * LayerBResolution; ++pixel)
        {
            const size_t offset = pixel * Channels;
            layerB[chunkIndex][offset] = static_cast<uint8_t>(30 + chunkIndex * 40 + pixel);
            layerB[chunkIndex][offset + 1] = static_cast<uint8_t>(50 + chunkIndex * 40 + pixel * 2);
            layerB[chunkIndex][offset + 2] = static_cast<uint8_t>(70 + chunkIndex * 40 + pixel * 3);
            layerB[chunkIndex][offset + AlphaChannel] = static_cast<uint8_t>(200 + pixel);
        }
    }

    CookedGrassFieldWriteDesc input;
    input.ChunkSizeMeters = 4.0f;
    input.LayerAResolution = LayerAResolution;
    input.LayerBResolution = LayerBResolution;
    input.SpeciesNames.resize(2);
    input.SpeciesNames[0] = "meadow";
    input.SpeciesNames[1] = "reed";
    input.Chunks.resize(ChunkCount);
    input.Chunks[0].X = -3;
    input.Chunks[0].Z = 4;
    input.Chunks[1].X = 5;
    input.Chunks[1].Z = -2;
    for (size_t chunkIndex = 0; chunkIndex < ChunkCount; ++chunkIndex)
    {
        input.Chunks[chunkIndex].LayerA = Span<const uint8_t>(layerA[chunkIndex], sizeof(layerA[chunkIndex]));
        input.Chunks[chunkIndex].LayerB = Span<const uint8_t>(layerB[chunkIndex], sizeof(layerB[chunkIndex]));
    }

    ByteArray bytes;
    CHECK(WriteCookedGrassField(input, bytes) == CookedGrassFieldWriteStatus::Success);
    const auto parsed = ParseCookedGrassField(AssetBlob::CopyBytes(Span<const uint8_t>(bytes)));
    CheckStatus("Success", parsed.Status, CookedGrassFieldParseStatus::Success);
    const auto& field = parsed.Field;
    CHECK(field.ChunkSizeMeters == input.ChunkSizeMeters);
    CHECK(field.LayerAResolution == input.LayerAResolution);
    CHECK(field.LayerBResolution == input.LayerBResolution);
    CHECK(field.SpeciesNames.size() == input.SpeciesNames.size());
    for (size_t i = 0; i < input.SpeciesNames.size(); ++i)
    {
        CHECK(field.SpeciesNames[i] == input.SpeciesNames[i]);
    }
    CHECK(field.Chunks.size() == input.Chunks.size());
    const uint8_t expectedMaxDensity[ChunkCount] = {65, 140};
    const uint8_t expectedMaxHeightFactor[ChunkCount] = {170, 220};
    for (uint32_t index = 0; index < ChunkCount; ++index)
    {
        const size_t sourceIndex = ChunkCount - 1 - index;
        const auto& expected = input.Chunks[sourceIndex];
        const auto& actual = field.Chunks[index];
        CHECK(actual.X == expected.X && actual.Z == expected.Z);
        CHECK(actual.MaxDensity == expectedMaxDensity[sourceIndex]);
        CHECK(actual.MaxHeightFactor == expectedMaxHeightFactor[sourceIndex]);
        if (index > 0)
        {
            const auto& previous = field.Chunks[index - 1];
            CHECK(previous.Z < actual.Z || (previous.Z == actual.Z && previous.X < actual.X));
        }
        const auto actualLayerA = field.GetLayerA(index);
        const auto actualLayerB = field.GetLayerB(index);
        CHECK(actualLayerA.size() == expected.LayerA.size());
        CHECK(actualLayerB.size() == expected.LayerB.size());
        for (size_t byte = 0; byte < expected.LayerA.size(); ++byte)
        {
            CHECK(actualLayerA[byte] == expected.LayerA[byte]);
        }
        for (size_t pixel = 0; pixel < LayerBResolution * LayerBResolution; ++pixel)
        {
            const size_t offset = pixel * Channels;
            for (size_t channel = 0; channel < AlphaChannel; ++channel)
            {
                CHECK(actualLayerB[offset + channel] == expected.LayerB[offset + channel]);
            }
            CHECK(expected.LayerB[offset + AlphaChannel] != 0);
            CHECK(actualLayerB[offset + AlphaChannel] == 0);
        }
    }

    const size_t chunkTableOffset = static_cast<size_t>(ReadLe64(bytes, HeaderOffset::ChunkTableOffset));
    const size_t payloadOffset = static_cast<size_t>(ReadLe64(bytes, HeaderOffset::PayloadOffset));
    const uint64_t payloadSize = ReadLe64(bytes, HeaderOffset::PayloadSize);
    const size_t layerBOffset = static_cast<size_t>(ReadLe64(bytes, chunkTableOffset + ChunkRecordOffset::LayerBOffset));
    using Status = CookedGrassFieldParseStatus;

    {
        ByteArray damaged = bytes;
        damaged[HeaderOffset::Magic] ^= 1;
        CheckRejection(damaged, Status::BadMagic);
    }
    {
        ByteArray damaged = bytes;
        WriteLe(damaged, HeaderOffset::Version, 1, sizeof(uint32_t));
        CheckRejection(damaged, Status::UnsupportedVersion);
    }
    {
        ByteArray damaged = bytes;
        WriteLe(damaged, HeaderOffset::EndianMarker, 0x04030201u, sizeof(uint32_t));
        CheckRejection(damaged, Status::EndianMismatch);
    }
    {
        ByteArray damaged = bytes;
        WriteLe(damaged, HeaderOffset::HeaderSize, HeaderSize + sizeof(uint32_t), sizeof(uint32_t));
        CheckRejection(damaged, Status::HeaderSizeMismatch);
    }
    {
        ByteArray damaged = bytes;
        WriteLe(damaged, HeaderOffset::FileSize, bytes.size() + 1, sizeof(uint64_t));
        CheckRejection(damaged, Status::FileSizeMismatch);
    }
    {
        ByteArray damaged = bytes;
        WriteLe(damaged, HeaderOffset::Reserved2, 1, sizeof(uint64_t));
        CheckRejection(damaged, Status::ReservedFieldNonZero);
    }
    {
        ByteArray damaged = bytes;
        WriteLe(damaged, chunkTableOffset + ChunkRecordOffset::LayerBOffset, payloadSize + Channels, sizeof(uint64_t));
        CheckRejection(damaged, Status::ChunkRangeOutOfRange);
    }
    {
        ByteArray damaged = bytes;
        std::memcpy(damaged.data() + chunkTableOffset + ChunkRecordSize + ChunkRecordOffset::ChunkX,
                    bytes.data() + chunkTableOffset + ChunkRecordOffset::ChunkX,
                    ChunkRecordOffset::Flags - ChunkRecordOffset::ChunkX);
        CheckRejection(damaged, Status::DuplicateChunk);
    }
    {
        ByteArray damaged = bytes;
        damaged[payloadOffset + layerBOffset] ^= 1;
        CheckRejection(damaged, Status::PayloadHashMismatch);
    }
    {
        ByteArray damaged = bytes;
        damaged.resize(damaged.size() - Channels);
        WriteLe(damaged, HeaderOffset::FileSize, damaged.size(), sizeof(uint64_t));
        CheckRejection(damaged, Status::TruncatedPayload);
    }

    std::cout << Utf8(u8"CookedGrassFieldTest 成功\n");
    return 0;
}
