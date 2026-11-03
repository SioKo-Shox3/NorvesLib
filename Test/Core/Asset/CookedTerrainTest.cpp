#include "Asset/CookedTerrainFormat.h"
#include "Tools/AssetCook/TextureCooker.h"

#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <utility>
#if defined(_MSC_VER)
#include <crtdbg.h>
#endif

using namespace NorvesLib::Core::Asset;
using namespace NorvesLib::Core::Asset::CookedTerrainFormatV0;
using NorvesLib::Core::Container::AnsiString;
using NorvesLib::Core::Container::Span;
using NorvesLib::Core::Container::VariableArray;
using NorvesLib::Tools::AssetCook::CookRgba8ToNvtex;
using NorvesLib::Tools::AssetCook::TextureCookResult;
using ByteArray = VariableArray<uint8_t>;

#define CHECK(expression)                                                                                              \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expression))                                                                                             \
        {                                                                                                              \
            std::cerr << Utf8(u8"検査失敗: ") << #expression << Utf8(u8" 行 ") << __LINE__ << "\n";                                  \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (false)

namespace
{
    static_assert(HeaderSize == 192 && LayerRecordSize == 32 && TileRecordHeadSize == 80 && TileLayerRangeSize == 24);
    static_assert(VersionMajor == 0 && VersionMinor == 0 && EndianMarker == 0x01020304u && PayloadAlignment == 64);
    static_assert(HeaderOffset::Magic == 0 && HeaderOffset::HeaderSize == 8 && HeaderOffset::VersionMajor == 12 &&
                  HeaderOffset::VersionMinor == 14 && HeaderOffset::EndianMarker == 16 && HeaderOffset::Flags == 20 &&
                  HeaderOffset::FileSize == 24 && HeaderOffset::PayloadHash == 32 && HeaderOffset::CellCountX == 40 &&
                  HeaderOffset::CellCountZ == 44 && HeaderOffset::TileCells == 48 && HeaderOffset::TileCountX == 52 &&
                  HeaderOffset::TileCountZ == 56 && HeaderOffset::LodCount == 60 && HeaderOffset::CellSizeM == 64 &&
                  HeaderOffset::Reserved0 == 68 && HeaderOffset::OriginX == 72 && HeaderOffset::OriginY == 80 &&
                  HeaderOffset::OriginZ == 88 && HeaderOffset::HeightMinM == 96 && HeaderOffset::HeightMaxM == 100 &&
                  HeaderOffset::LayerCount == 104 && HeaderOffset::LayerRecordSize == 108 && HeaderOffset::TileRecordSize == 112 &&
                  HeaderOffset::Reserved1 == 116 && HeaderOffset::LayerTableOffset == 120 && HeaderOffset::LayerTableSize == 128 &&
                  HeaderOffset::TileTableOffset == 136 && HeaderOffset::TileTableSize == 144 && HeaderOffset::StringTableOffset == 152 &&
                  HeaderOffset::StringTableSize == 160 && HeaderOffset::PayloadOffset == 168 && HeaderOffset::PayloadSize == 176 &&
                  HeaderOffset::Reserved2 == 184);

    const char* Utf8(const char8_t* text)
    {
        return reinterpret_cast<const char*>(text);
    }

    uint16_t ReadLe16(const ByteArray& bytes, size_t offset)
    {
        return static_cast<uint16_t>(static_cast<uint16_t>(bytes[offset]) |
                                     (static_cast<uint16_t>(bytes[offset + 1]) << 8));
    }

    uint32_t ReadLe32(const ByteArray& bytes, size_t offset)
    {
        return static_cast<uint32_t>(bytes[offset]) | (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
               (static_cast<uint32_t>(bytes[offset + 2]) << 16) | (static_cast<uint32_t>(bytes[offset + 3]) << 24);
    }

    uint64_t ReadLe64(const ByteArray& bytes, size_t offset)
    {
        return static_cast<uint64_t>(ReadLe32(bytes, offset)) |
               (static_cast<uint64_t>(ReadLe32(bytes, offset + 4)) << 32);
    }

    void WriteLe16(ByteArray& bytes, size_t offset, uint16_t value)
    {
        for (size_t i = 0; i < 2; ++i)
        {
            bytes[offset + i] = static_cast<uint8_t>(value >> (i * 8));
        }
    }

    void WriteLe32(ByteArray& bytes, size_t offset, uint32_t value)
    {
        for (size_t i = 0; i < 4; ++i)
        {
            bytes[offset + i] = static_cast<uint8_t>(value >> (i * 8));
        }
    }

    void WriteLe64(ByteArray& bytes, size_t offset, uint64_t value)
    {
        for (size_t i = 0; i < 8; ++i)
        {
            bytes[offset + i] = static_cast<uint8_t>(value >> (i * 8));
        }
    }

    void Rehash(ByteArray& bytes)
    {
        WriteLe64(bytes, HeaderOffset::PayloadHash, ComputeCookedTerrainFileHash(Span<const uint8_t>(bytes)));
    }

    const char* StatusName(CookedTerrainParseStatus status)
    {
        switch (status)
        {
        case CookedTerrainParseStatus::Success: return "Success";
        case CookedTerrainParseStatus::InvalidBlob: return "InvalidBlob";
        case CookedTerrainParseStatus::HeaderTooSmall: return "HeaderTooSmall";
        case CookedTerrainParseStatus::BadMagic: return "BadMagic";
        case CookedTerrainParseStatus::UnsupportedVersion: return "UnsupportedVersion";
        case CookedTerrainParseStatus::EndianMismatch: return "EndianMismatch";
        case CookedTerrainParseStatus::HeaderSizeMismatch: return "HeaderSizeMismatch";
        case CookedTerrainParseStatus::FileSizeMismatch: return "FileSizeMismatch";
        case CookedTerrainParseStatus::ReservedFieldNonZero: return "ReservedFieldNonZero";
        case CookedTerrainParseStatus::PayloadHashMismatch: return "PayloadHashMismatch";
        case CookedTerrainParseStatus::InvalidDimensions: return "InvalidDimensions";
        case CookedTerrainParseStatus::InvalidLodCount: return "InvalidLodCount";
        case CookedTerrainParseStatus::InvalidLayerCount: return "InvalidLayerCount";
        case CookedTerrainParseStatus::RecordSizeMismatch: return "RecordSizeMismatch";
        case CookedTerrainParseStatus::TableLayoutMismatch: return "TableLayoutMismatch";
        case CookedTerrainParseStatus::IntegerOverflow: return "IntegerOverflow";
        case CookedTerrainParseStatus::InvalidLayerRecord: return "InvalidLayerRecord";
        case CookedTerrainParseStatus::DuplicateLayerFourCC: return "DuplicateLayerFourCC";
        case CookedTerrainParseStatus::TileIndexOutOfRange: return "TileIndexOutOfRange";
        case CookedTerrainParseStatus::DuplicateTile: return "DuplicateTile";
        case CookedTerrainParseStatus::InvalidTileBounds: return "InvalidTileBounds";
        case CookedTerrainParseStatus::LayerRangeOutOfRange: return "LayerRangeOutOfRange";
        case CookedTerrainParseStatus::HeightSizeMismatch: return "HeightSizeMismatch";
        case CookedTerrainParseStatus::LayerHashMismatch: return "LayerHashMismatch";
        case CookedTerrainParseStatus::EmbeddedTextureInvalid: return "EmbeddedTextureInvalid";
        }
        return "Unknown";
    }

    void CheckRejection(const char* name, const ByteArray& bytes, CookedTerrainParseStatus expected)
    {
        const auto result = ParseCookedTerrain(AssetBlob::CopyBytes(Span<const uint8_t>(bytes)));
        if (result.Status != expected)
        {
            std::cerr << Utf8(u8"拒否の検査失敗: ") << name << Utf8(u8"、期待した種別: ") << StatusName(expected)
                      << Utf8(u8"、実際の種別: ") << StatusName(result.Status) << "\n";
            std::exit(1);
        }
        std::cout << Utf8(u8"拒否: ") << name << Utf8(u8"、実際の種別: ") << StatusName(result.Status) << "\n";
    }

    void CheckHeader(const ByteArray& bytes, const CookedTerrainWriteDesc& input, const CookedTerrainData& terrain)
    {
        const uint64_t layerTableSize = input.Layers.size() * LayerRecordSize;
        const uint64_t tileRecordSize = TileRecordHeadSize + input.Layers.size() * TileLayerRangeSize;
        const uint64_t tileTableOffset = HeaderSize + layerTableSize;
        const uint64_t tileTableSize = input.Tiles.size() * tileRecordSize;
        const uint64_t stringTableOffset = tileTableOffset + tileTableSize;
        const uint64_t stringTableSize = input.Layers[0].Semantic.size() + input.Layers[1].Semantic.size();
        const uint64_t payloadOffset = (stringTableOffset + stringTableSize + PayloadAlignment - 1) /
                                       PayloadAlignment * PayloadAlignment;

        const uint8_t expectedMagic[] = {'N', 'V', 'T', 'R', 'N', 'v', '0', '\0'};
        CHECK(MagicSize == sizeof(expectedMagic));
        CHECK(std::memcmp(bytes.data() + HeaderOffset::Magic, expectedMagic, sizeof(expectedMagic)) == 0);
        CHECK(ReadLe32(bytes, HeaderOffset::HeaderSize) == HeaderSize);
        CHECK(ReadLe16(bytes, HeaderOffset::VersionMajor) == VersionMajor);
        CHECK(ReadLe16(bytes, HeaderOffset::VersionMinor) == VersionMinor);
        CHECK(ReadLe32(bytes, HeaderOffset::EndianMarker) == EndianMarker);
        CHECK(ReadLe32(bytes, HeaderOffset::Flags) == 0);
        CHECK(ReadLe64(bytes, HeaderOffset::FileSize) == bytes.size());
        CHECK(ReadLe64(bytes, HeaderOffset::PayloadHash) == ComputeCookedTerrainFileHash(Span<const uint8_t>(bytes)));
        CHECK(terrain.PayloadHash == ReadLe64(bytes, HeaderOffset::PayloadHash));
        CHECK(ReadLe32(bytes, HeaderOffset::CellCountX) == input.CellCountX && terrain.CellCountX == input.CellCountX);
        CHECK(ReadLe32(bytes, HeaderOffset::CellCountZ) == input.CellCountZ && terrain.CellCountZ == input.CellCountZ);
        CHECK(ReadLe32(bytes, HeaderOffset::TileCells) == input.TileCells && terrain.TileCells == input.TileCells);
        CHECK(ReadLe32(bytes, HeaderOffset::TileCountX) == 2 && terrain.TileCountX == 2);
        CHECK(ReadLe32(bytes, HeaderOffset::TileCountZ) == 1 && terrain.TileCountZ == 1);
        CHECK(ReadLe32(bytes, HeaderOffset::LodCount) == input.LodCount && terrain.LodCount == input.LodCount);
        CHECK(std::bit_cast<float>(ReadLe32(bytes, HeaderOffset::CellSizeM)) == input.CellSizeM &&
              terrain.CellSizeM == input.CellSizeM);
        CHECK(ReadLe32(bytes, HeaderOffset::Reserved0) == 0);
        CHECK(std::bit_cast<double>(ReadLe64(bytes, HeaderOffset::OriginX)) == input.OriginX && terrain.OriginX == input.OriginX);
        CHECK(std::bit_cast<double>(ReadLe64(bytes, HeaderOffset::OriginY)) == input.OriginY && terrain.OriginY == input.OriginY);
        CHECK(std::bit_cast<double>(ReadLe64(bytes, HeaderOffset::OriginZ)) == input.OriginZ && terrain.OriginZ == input.OriginZ);
        CHECK(std::bit_cast<float>(ReadLe32(bytes, HeaderOffset::HeightMinM)) == input.HeightMinM && terrain.HeightMinM == input.HeightMinM);
        CHECK(std::bit_cast<float>(ReadLe32(bytes, HeaderOffset::HeightMaxM)) == input.HeightMaxM && terrain.HeightMaxM == input.HeightMaxM);
        CHECK(ReadLe32(bytes, HeaderOffset::LayerCount) == input.Layers.size() && terrain.Layers.size() == input.Layers.size());
        CHECK(ReadLe32(bytes, HeaderOffset::LayerRecordSize) == LayerRecordSize);
        CHECK(ReadLe32(bytes, HeaderOffset::TileRecordSize) == tileRecordSize);
        CHECK(ReadLe32(bytes, HeaderOffset::Reserved1) == 0);
        CHECK(ReadLe64(bytes, HeaderOffset::LayerTableOffset) == HeaderSize);
        CHECK(ReadLe64(bytes, HeaderOffset::LayerTableSize) == layerTableSize);
        CHECK(ReadLe64(bytes, HeaderOffset::TileTableOffset) == tileTableOffset);
        CHECK(ReadLe64(bytes, HeaderOffset::TileTableSize) == tileTableSize);
        CHECK(ReadLe64(bytes, HeaderOffset::StringTableOffset) == stringTableOffset);
        CHECK(ReadLe64(bytes, HeaderOffset::StringTableSize) == stringTableSize);
        CHECK(ReadLe64(bytes, HeaderOffset::PayloadOffset) == payloadOffset);
        CHECK(ReadLe64(bytes, HeaderOffset::PayloadSize) == bytes.size() - payloadOffset);
        CHECK(ReadLe64(bytes, HeaderOffset::Reserved2) == 0);
    }
}

int main()
{
#if defined(_MSC_VER)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

    std::cout << Utf8(u8"CookedTerrainTest 開始\n");
    const std::array<uint8_t, 16> pixels = {200, 120, 60, 255, 100, 240, 10, 255,
                                            70, 90, 220, 255, 255, 30, 80, 255};
    TextureCookResult texture;
    AnsiString error;
    CHECK(CookRgba8ToNvtex(Span<const uint8_t>(pixels), 2, 2, "nvtex.v0.rgba8.srgb", texture, error));
    CHECK(!texture.NvtexBytes.empty());

    std::array<std::array<uint16_t, 25>, 2> samples{};
    std::array<std::array<uint8_t, 50>, 2> heightBytes{};
    for (size_t tile = 0; tile < samples.size(); ++tile)
    {
        for (size_t i = 0; i < samples[tile].size(); ++i)
        {
            samples[tile][i] = static_cast<uint16_t>((i / 5) * 4096 + (tile * 4 + i % 5) * 1703);
            heightBytes[tile][i * 2] = static_cast<uint8_t>(samples[tile][i]);
            heightBytes[tile][i * 2 + 1] = static_cast<uint8_t>(samples[tile][i] >> 8);
        }
    }

    CookedTerrainWriteDesc input;
    input.CellCountX = 8;
    input.CellCountZ = 4;
    input.TileCells = 4;
    input.LodCount = 2;
    input.CellSizeM = 1.5f;
    input.OriginX = 1234.5;
    input.OriginY = -42.25;
    input.OriginZ = 6789.75;
    input.HeightMinM = -32.0f;
    input.HeightMaxM = 128.0f;
    input.Layers.resize(2);
    input.Layers[0].FourCC = HeightLayerFourCC;
    input.Layers[0].Encoding = LayerEncodingRawHeight16;
    input.Layers[0].PixelFormat = CookedTexturePixelFormat::R16UNorm;
    input.Layers[0].ColorSpace = CookedTextureColorSpace::Linear;
    input.Layers[0].Semantic = "height";
    input.Layers[1].FourCC = MakeAssetPackageFourCC('A', 'L', 'B', '0');
    input.Layers[1].Encoding = LayerEncodingEmbeddedNvtex;
    input.Layers[1].PixelFormat = CookedTexturePixelFormat::RGBA8UNorm;
    input.Layers[1].ColorSpace = CookedTextureColorSpace::SRGB;
    input.Layers[1].Semantic = "albedo";
    input.Tiles.resize(2);
    for (size_t index = 0; index < input.Tiles.size(); ++index)
    {
        const size_t tileX = 1 - index;
        auto& tile = input.Tiles[index];
        tile.TileX = static_cast<uint32_t>(tileX);
        tile.TileZ = 0;
        const auto decodeHeight = [&input](uint16_t value)
        {
            return static_cast<float>(static_cast<double>(input.HeightMinM) +
                                      (static_cast<double>(input.HeightMaxM) - input.HeightMinM) * value / 65535.0);
        };
        tile.HeightMinM = decodeHeight(samples[tileX].front());
        tile.HeightMaxM = decodeHeight(samples[tileX].back());
        tile.AabbMin[0] = static_cast<float>(tileX) * 6.0f;
        tile.AabbMin[1] = tile.HeightMinM;
        tile.AabbMin[2] = 0.0f;
        tile.AabbMax[0] = static_cast<float>(tileX + 1) * 6.0f;
        tile.AabbMax[1] = tile.HeightMaxM;
        tile.AabbMax[2] = 6.0f;
        tile.LodMaxErrorM[0] = 0.25f + static_cast<float>(tileX);
        tile.LodMaxErrorM[1] = 1.5f + static_cast<float>(tileX);
        tile.LayerBytes.push_back(Span<const uint8_t>(heightBytes[tileX]));
        tile.LayerBytes.push_back(Span<const uint8_t>(texture.NvtexBytes));
    }

    ByteArray bytes;
    CHECK(WriteCookedTerrain(input, bytes) == CookedTerrainWriteStatus::Success);
    ByteArray repeated;
    CHECK(WriteCookedTerrain(input, repeated) == CookedTerrainWriteStatus::Success && repeated == bytes);
    std::swap(input.Tiles[0], input.Tiles[1]);
    ByteArray ordered;
    CHECK(WriteCookedTerrain(input, ordered) == CookedTerrainWriteStatus::Success && ordered == bytes);
    std::swap(input.Tiles[0], input.Tiles[1]);

    const auto parsed = ParseCookedTerrain(AssetBlob::CopyBytes(Span<const uint8_t>(bytes)));
    CHECK(parsed.Status == CookedTerrainParseStatus::Success);
    const auto& terrain = parsed.Terrain;
    CheckHeader(bytes, input, terrain);
    CHECK(terrain.Tiles.size() == input.Tiles.size());
    CHECK(terrain.GetSamplesPerSide() == 5);
    for (size_t layerIndex = 0; layerIndex < input.Layers.size(); ++layerIndex)
    {
        const auto& expected = input.Layers[layerIndex];
        const auto& actual = terrain.Layers[layerIndex];
        CHECK(actual.FourCC == expected.FourCC);
        CHECK(actual.Encoding == expected.Encoding);
        CHECK(actual.PixelFormat == expected.PixelFormat);
        CHECK(actual.ColorSpace == expected.ColorSpace);
        CHECK(actual.Semantic == expected.Semantic);
    }
    for (uint32_t tileIndex = 0; tileIndex < terrain.Tiles.size(); ++tileIndex)
    {
        const auto& expected = input.Tiles[1 - tileIndex];
        const auto& actual = terrain.Tiles[tileIndex];
        CHECK(actual.TileX == tileIndex && actual.TileX == expected.TileX && actual.TileZ == expected.TileZ);
        CHECK(actual.HeightMinM == expected.HeightMinM && actual.HeightMaxM == expected.HeightMaxM);
        for (size_t axis = 0; axis < 3; ++axis)
        {
            CHECK(actual.AabbMin[axis] == expected.AabbMin[axis] && actual.AabbMax[axis] == expected.AabbMax[axis]);
        }
        for (size_t lod = 0; lod < MaxLodCount; ++lod)
        {
            CHECK(actual.LodMaxErrorM[lod] == expected.LodMaxErrorM[lod]);
        }
        for (uint32_t z = 0; z < 5; ++z)
        {
            for (uint32_t x = 0; x < 5; ++x)
            {
                CHECK(terrain.GetHeightSample(tileIndex, x, z) == samples[tileIndex][z * 5 + x]);
            }
        }
        for (uint32_t layerIndex = 0; layerIndex < input.Layers.size(); ++layerIndex)
        {
            const AssetBlob blob = terrain.GetLayerBlob(tileIndex, layerIndex);
            const auto source = expected.LayerBytes[layerIndex];
            CHECK(blob.IsValid() && blob.GetSize() == source.size());
            CHECK(std::memcmp(blob.GetData(), source.data(), source.size()) == 0);
            if (layerIndex == 1)
            {
                const auto embedded = ParseCookedTexture(blob, false);
                CHECK(embedded.Succeeded());
                CHECK(embedded.Texture.PixelFormat == input.Layers[layerIndex].PixelFormat);
                CHECK(embedded.Texture.ColorSpace == input.Layers[layerIndex].ColorSpace);
            }
        }
    }
    std::cout << Utf8(u8"往復: Success、全欄とバイト列が一致\n");

    const size_t tileTable = static_cast<size_t>(ReadLe64(bytes, HeaderOffset::TileTableOffset));
    const size_t tileRecordSize = ReadLe32(bytes, HeaderOffset::TileRecordSize);
    const size_t heightRange = tileTable + TileRecordOffset::LayerRanges;
    const size_t payloadOffset = static_cast<size_t>(ReadLe64(bytes, HeaderOffset::PayloadOffset));
    {
        ByteArray changed = bytes;
        changed.pop_back();
        CheckRejection(Utf8(u8"末尾の切り詰め"), changed, CookedTerrainParseStatus::FileSizeMismatch);
    }
    {
        ByteArray changed = bytes;
        const uint64_t outside = (bytes.size() + PayloadAlignment - 1) / PayloadAlignment * PayloadAlignment;
        WriteLe64(changed, heightRange + TileLayerRangeOffset::DataOffset, outside);
        Rehash(changed);
        CheckRejection(Utf8(u8"レイヤの DataOffset が範囲外"), changed, CookedTerrainParseStatus::LayerRangeOutOfRange);
    }
    {
        ByteArray changed = bytes;
        // 区間内の高さレイヤは範囲検査を通るため、次の高さサイズ検査で拒否される。
        WriteLe64(changed, heightRange + TileLayerRangeOffset::DataSize,
                  ReadLe64(bytes, heightRange + TileLayerRangeOffset::DataSize) + 1);
        Rehash(changed);
        CheckRejection(Utf8(u8"高さレイヤの DataSize が不正"), changed, CookedTerrainParseStatus::HeightSizeMismatch);
    }
    {
        ByteArray changed = bytes;
        changed[payloadOffset] ^= 1;
        CheckRejection(Utf8(u8"ペイロードの改ざん"), changed, CookedTerrainParseStatus::PayloadHashMismatch);
    }
    {
        ByteArray changed = bytes;
        WriteLe32(changed, tileTable + tileRecordSize + TileRecordOffset::TileX, 0);
        Rehash(changed);
        CheckRejection(Utf8(u8"タイル座標の重複"), changed, CookedTerrainParseStatus::DuplicateTile);
    }
    {
        ByteArray changed = bytes;
        WriteLe32(changed, tileTable + TileRecordOffset::TileX, terrain.TileCountX);
        Rehash(changed);
        CheckRejection(Utf8(u8"タイル番号が範囲外"), changed, CookedTerrainParseStatus::TileIndexOutOfRange);
    }
    {
        ByteArray changed = bytes;
        WriteLe32(changed, HeaderOffset::Reserved0, 1);
        CheckRejection(Utf8(u8"ヘッダの予約欄"), changed, CookedTerrainParseStatus::ReservedFieldNonZero);
    }
    {
        ByteArray changed = bytes;
        WriteLe16(changed, HeaderOffset::VersionMinor, 1);
        CheckRejection(Utf8(u8"副版の不一致"), changed, CookedTerrainParseStatus::UnsupportedVersion);
    }
    {
        ByteArray changed = bytes;
        WriteLe32(changed, HeaderOffset::EndianMarker, 0x04030201u);
        CheckRejection(Utf8(u8"エンディアンの不一致"), changed, CookedTerrainParseStatus::EndianMismatch);
    }
    std::cout << Utf8(u8"CookedTerrainTest 成功\n");
    return 0;
}
