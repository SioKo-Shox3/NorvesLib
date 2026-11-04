#include "Asset/CookedTerrainFormat.h"
#include "Tools/AssetCook/TerrainSpec.h"
#include "Tools/AssetCook/TextureCooker.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <utility>

#include "exr.h"
#include "Library/ThirdParty/stb/stb_image_write.h"
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
using namespace NorvesLib::Tools::AssetCook;
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


namespace
{
    constexpr uint32_t CookWidth = 65;
    constexpr uint32_t CookHeight = 49;
    constexpr uint32_t CookTileCells = 16;
    constexpr double CookCellSize = 1.75;
    constexpr double SurfaceA = 0.075;
    constexpr double SurfaceB = 0.027;
    constexpr double SurfaceC = 0.16;
    constexpr double SurfaceK = 0.075;
    std::filesystem::path TerrainTempRoot;

    void CleanupTerrainTemp()
    {
        if (!TerrainTempRoot.empty())
        {
            std::error_code error;
            std::filesystem::remove_all(TerrainTempRoot, error);
            if (error)
            {
                std::cerr << Utf8(u8"一時ディレクトリを削除できません: ") << error.message() << "\n";
            }
        }
    }

    void CreateTerrainTemp()
    {
        const auto parent = std::filesystem::temp_directory_path();
        const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        for (uint32_t attempt = 0; attempt < 100; ++attempt)
        {
            char name[80]{};
            std::snprintf(name, sizeof(name), "norves-terrain-%llx-%u",
                          static_cast<unsigned long long>(ticks), attempt);
            const auto candidate = parent / name;
            std::error_code error;
            if (std::filesystem::create_directory(candidate, error))
            {
                TerrainTempRoot = candidate;
                CHECK(std::atexit(CleanupTerrainTemp) == 0);
                return;
            }
            CHECK(!error);
        }
        CHECK(false);
    }

    void WriteTerrainFile(const std::filesystem::path& path, Span<const uint8_t> bytes)
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        CHECK(output.is_open());
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        output.close();
        CHECK(output.good());
    }

    std::filesystem::path WriteTerrainSpec(const char* name, const char* source, const char* mapping,
                                           double cellSize, uint32_t seed = 1, const char* extra = "")
    {
        char json[2048]{};
        const int count = std::snprintf(json, sizeof(json),
            "{\"version\":1,\"height_source\":{%s},\"height_mapping\":{%s},"
            "\"cell_size_m\":%.17g,\"tile_cells\":16,\"fit\":\"error\",\"seed\":%u%s}\n",
            source, mapping, cellSize, seed, extra);
        CHECK(count > 0 && static_cast<size_t>(count) < sizeof(json));
        const auto path = TerrainTempRoot / name;
        WriteTerrainFile(path, {reinterpret_cast<const uint8_t*>(json), static_cast<size_t>(count)});
        return path;
    }

    TerrainSpecCookResult CookTerrainChecked(const std::filesystem::path& path)
    {
        TerrainSpecCookResult result;
        AnsiString error;
        const bool bSuccess = CookTerrainSpecFile(path, result, error);
        if (!bSuccess)
        {
            std::cerr << Utf8(u8"地形 cook 失敗: ") << error.c_str() << "\n";
        }
        CHECK(bSuccess);
        return result;
    }

    double SurfaceHeight(double x, double z)
    {
        return SurfaceA * x + SurfaceB * z + SurfaceC * std::sin(SurfaceK * x) * std::cos(SurfaceK * z);
    }

    // LOD0 の各標本から粗いセルを引き、両対角の三角形を重心座標で評価する。
    float BruteLodError(const std::array<float, 17 * 17>& heights, uint32_t stride)
    {
        double maximum = 0.0;
        for (uint32_t z = 0; z <= CookTileCells; ++z)
        {
            for (uint32_t x = 0; x <= CookTileCells; ++x)
            {
                const uint32_t x0 = std::min((x / stride) * stride, CookTileCells - stride);
                const uint32_t z0 = std::min((z / stride) * stride, CookTileCells - stride);
                const double u = static_cast<double>(x - x0) / stride;
                const double v = static_cast<double>(z - z0) / stride;
                const double h00 = heights[z0 * 17 + x0];
                const double h10 = heights[z0 * 17 + x0 + stride];
                const double h01 = heights[(z0 + stride) * 17 + x0];
                const double h11 = heights[(z0 + stride) * 17 + x0 + stride];
                const double first = u >= v
                    ? (1.0 - u) * h00 + (u - v) * h10 + v * h11
                    : (1.0 - v) * h00 + (v - u) * h01 + u * h11;
                const double second = u + v <= 1.0
                    ? (1.0 - u - v) * h00 + u * h10 + v * h01
                    : (u + v - 1.0) * h11 + (1.0 - u) * h01 + (1.0 - v) * h10;
                const double actual = heights[z * 17 + x];
                maximum = std::max(maximum, std::max(std::abs(actual - first), std::abs(actual - second)));
            }
        }
        return static_cast<float>(maximum);
    }

    ByteArray SaveFloatExr(VariableArray<float>& values, uint32_t width, uint32_t height)
    {
        exr_channel channel{};
        std::memcpy(channel.name, "Y", 2);
        channel.pixel_type = EXR_PIXEL_FLOAT;
        channel.x_sampling = 1;
        channel.y_sampling = 1;
        exr_part part{};
        part.width = static_cast<int32_t>(width);
        part.height = static_cast<int32_t>(height);
        part.header.part_type = EXR_PART_SCANLINE;
        part.header.data_window = {0, 0, static_cast<int32_t>(width - 1), static_cast<int32_t>(height - 1)};
        part.header.display_window = part.header.data_window;
        part.header.pixel_aspect_ratio = 1.0f;
        part.header.screen_window_width = 1.0f;
        part.header.num_channels = 1;
        part.header.channels = &channel;
        void* planes[] = {values.data()};
        part.images = planes;
        exr_image image{};
        image.num_parts = 1;
        image.parts = &part;
        const exr_allocator allocator{
            nullptr,
            [](void*, size_t size) -> void* { return std::malloc(size); },
            [](void*, void* ptr) { std::free(ptr); }
        };
        void* data = nullptr;
        size_t size = 0;
        CHECK(exr_save_to_memory(&data, &size, &allocator, &image, EXR_COMPRESSION_NONE) == EXR_SUCCESS);
        CHECK(data != nullptr && size > 0);
        ByteArray bytes(size);
        std::memcpy(bytes.data(), data, size);
        allocator.free(allocator.user, data);
        return bytes;
    }

    void AppendPngBytes(void* context, void* data, int size)
    {
        auto& bytes = *static_cast<ByteArray*>(context);
        const auto* begin = static_cast<const uint8_t*>(data);
        for (int i = 0; i < size; ++i)
        {
            bytes.push_back(begin[i]);
        }
    }

    ByteArray SaveGray8Png(uint32_t width, uint32_t height)
    {
        ByteArray pixels(static_cast<size_t>(width) * height);
        for (uint32_t z = 0; z < height; ++z)
        {
            for (uint32_t x = 0; x < width; ++x)
            {
                pixels[static_cast<size_t>(z) * width + x] = static_cast<uint8_t>((x * 5 + z * 3) & 255);
            }
        }
        ByteArray result;
        CHECK(stbi_write_png_to_func(AppendPngBytes, &result, static_cast<int>(width), static_cast<int>(height),
                                     1, pixels.data(), static_cast<int>(width)) != 0);
        return result;
    }


    void CheckAnalyticTerrain()
    {
        const size_t count = static_cast<size_t>(CookWidth) * CookHeight;
        VariableArray<double> reference(count);
        double minimum = std::numeric_limits<double>::infinity();
        double maximum = -std::numeric_limits<double>::infinity();
        for (uint32_t z = 0; z < CookHeight; ++z)
        {
            for (uint32_t x = 0; x < CookWidth; ++x)
            {
                const double height = SurfaceHeight(x * CookCellSize, z * CookCellSize);
                reference[static_cast<size_t>(z) * CookWidth + x] = height;
                minimum = std::min(minimum, height);
                maximum = std::max(maximum, height);
            }
        }
        ByteArray raw(count * 2);
        for (size_t i = 0; i < count; ++i)
        {
            const auto value = static_cast<uint16_t>(std::round((reference[i] - minimum) / (maximum - minimum) * 65535.0));
            WriteLe16(raw, i * 2, value);
        }
        WriteTerrainFile(TerrainTempRoot / "surface.raw", Span<const uint8_t>(raw));
        char mapping[160]{};
        std::snprintf(mapping, sizeof(mapping), "\"min_m\":%.17g,\"max_m\":%.17g", minimum, maximum);
        const char* source = "\"path\":\"surface.raw\",\"format\":\"raw16\",\"width\":65,\"height\":49,\"endian\":\"little\",\"flip_z\":false";
        const auto spec = WriteTerrainSpec("surface.json", source, mapping, CookCellSize);
        const auto first = CookTerrainChecked(spec);
        const auto repeated = CookTerrainChecked(spec);
        CHECK(first.Build.NvtrnBytes == repeated.Build.NvtrnBytes);
        const uint64_t payloadHash = ComputeAssetPackagePayloadHash(first.Build.NvtrnBytes.data(), first.Build.NvtrnBytes.size());
        CHECK(payloadHash == ComputeAssetPackagePayloadHash(repeated.Build.NvtrnBytes.data(), repeated.Build.NvtrnBytes.size()));
        CHECK(first.SourceHash == repeated.SourceHash);

        const auto parsed = ParseCookedTerrain(AssetBlob::CopyBytes(Span<const uint8_t>(first.Build.NvtrnBytes)));
        CHECK(parsed.Succeeded());
        const auto& terrain = parsed.Terrain;
        CHECK(terrain.CellCountX == 64 && terrain.CellCountZ == 48 && terrain.TileCells == CookTileCells);
        CHECK(terrain.TileCountX == 4 && terrain.TileCountZ == 3 && terrain.LodCount == 4);
        CHECK(terrain.CellSizeM == static_cast<float>(CookCellSize));
        const int32_t normalLayer = terrain.FindLayer(TerrainNormalLayerFourCC);
        CHECK(normalLayer >= 0);
        VariableArray<ByteArray> normals(terrain.Tiles.size());

        // 範囲端の float 化と復元高さの float 化の丸めを覆うため、最大絶対値の 2 ULP を加える。
        const float magnitude = static_cast<float>(std::max(std::abs(minimum), std::abs(maximum)));
        const double ulp = static_cast<double>(std::nextafter(magnitude, std::numeric_limits<float>::infinity())) - magnitude;
        const double tolerance = (maximum - minimum) / (2.0 * 65535.0) + 2.0 * ulp;
        double maxHeightError = 0.0;
        double maxAngleDegrees = 0.0;
        double maxLodDifference = 0.0;
        constexpr double DegreesPerRadian = 57.2957795130823208768;
        for (uint32_t tz = 0; tz < terrain.TileCountZ; ++tz)
        {
            for (uint32_t tx = 0; tx < terrain.TileCountX; ++tx)
            {
                uint32_t tileIndex = 0;
                const auto* tile = terrain.FindTile(tx, tz, &tileIndex);
                CHECK(tile != nullptr);
                const auto texture = ParseCookedTexture(terrain.GetLayerBlob(tileIndex, static_cast<uint32_t>(normalLayer)));
                CHECK(texture.Succeeded());
                CHECK(texture.Texture.PixelFormat == CookedTexturePixelFormat::RG8UNorm);
                CHECK(texture.Texture.Width == 17 && texture.Texture.Height == 17);
                const auto mip = texture.Texture.GetMipBytes(0);
                CHECK(mip.size() == 17 * 17 * 2);
                normals[tileIndex].resize(mip.size());
                std::memcpy(normals[tileIndex].data(), mip.data(), mip.size());

                std::array<float, 17 * 17> heights{};
                float tileMin = std::numeric_limits<float>::infinity();
                float tileMax = -std::numeric_limits<float>::infinity();
                for (uint32_t z = 0; z <= CookTileCells; ++z)
                {
                    for (uint32_t x = 0; x <= CookTileCells; ++x)
                    {
                        const uint32_t gx = tx * CookTileCells + x;
                        const uint32_t gz = tz * CookTileCells + z;
                        const size_t local = z * 17 + x;
                        const float decoded = terrain.DecodeHeightM(terrain.GetHeightSample(tileIndex, x, z));
                        heights[local] = decoded;
                        tileMin = std::min(tileMin, decoded);
                        tileMax = std::max(tileMax, decoded);
                        const double expected = reference[static_cast<size_t>(gz) * CookWidth + gx];
                        const double error = std::abs(decoded - expected);
                        maxHeightError = std::max(maxHeightError, error);
                        if (error > tolerance)
                        {
                            std::cerr << Utf8(u8"高さ不一致 x,z=") << gx << "," << gz
                                      << Utf8(u8" 実際=") << decoded << Utf8(u8" 期待=") << expected
                                      << Utf8(u8" 誤差=") << error << Utf8(u8" 許容=") << tolerance << "\n";
                        }
                        CHECK(error <= tolerance);

                        const double px = gx * CookCellSize;
                        const double pz = gz * CookCellSize;
                        const double dx = SurfaceA + SurfaceC * SurfaceK * std::cos(SurfaceK * px) * std::cos(SurfaceK * pz);
                        const double dz = SurfaceB - SurfaceC * SurfaceK * std::sin(SurfaceK * px) * std::sin(SurfaceK * pz);
                        const double invLength = 1.0 / std::sqrt(dx * dx + 1.0 + dz * dz);
                        const double nx = normals[tileIndex][2 * local] * (2.0 / 255.0) - 1.0;
                        const double nz = normals[tileIndex][2 * local + 1] * (2.0 / 255.0) - 1.0;
                        const double ny = std::sqrt(std::max(0.0, 1.0 - nx * nx - nz * nz));
                        const double dot = std::clamp((-dx * nx + ny - dz * nz) * invLength, -1.0, 1.0);
                        const double angle = std::acos(dot) * DegreesPerRadian;
                        maxAngleDegrees = std::max(maxAngleDegrees, angle);
                        if (angle > 0.5)
                        {
                            std::cerr << Utf8(u8"法線不一致 x,z=") << gx << "," << gz
                                      << Utf8(u8" 角度=") << angle << "\n";
                        }
                        CHECK(angle <= 0.5);
                    }
                }

                const std::array<float, 3> expectedMin = {
                    static_cast<float>(tx * CookTileCells * CookCellSize), tileMin,
                    static_cast<float>(tz * CookTileCells * CookCellSize)};
                const std::array<float, 3> expectedMax = {
                    static_cast<float>((tx + 1) * CookTileCells * CookCellSize), tileMax,
                    static_cast<float>((tz + 1) * CookTileCells * CookCellSize)};
                for (size_t axis = 0; axis < 3; ++axis)
                {
                    if (tile->AabbMin[axis] != expectedMin[axis] || tile->AabbMax[axis] != expectedMax[axis])
                    {
                        std::cerr << Utf8(u8"AABB 不一致 tile=") << tx << "," << tz << Utf8(u8" 軸=") << axis
                                  << Utf8(u8" 実際=") << tile->AabbMin[axis] << "," << tile->AabbMax[axis]
                                  << Utf8(u8" 期待=") << expectedMin[axis] << "," << expectedMax[axis] << "\n";
                    }
                    CHECK(tile->AabbMin[axis] == expectedMin[axis] && tile->AabbMax[axis] == expectedMax[axis]);
                }
                for (uint32_t lod = 0; lod < 4; ++lod)
                {
                    const float expected = BruteLodError(heights, 1u << lod);
                    const float actual = tile->LodMaxErrorM[lod];
                    maxLodDifference = std::max(maxLodDifference, std::abs(static_cast<double>(actual) - expected));
                    if (actual != expected)
                    {
                        std::cerr << std::setprecision(10) << Utf8(u8"LOD 不一致 tile=") << tx << "," << tz
                                  << " stride=" << (1u << lod) << Utf8(u8" 実際=") << actual
                                  << Utf8(u8" 期待=") << expected << Utf8(u8" 差=") << (actual - expected) << "\n";
                    }
                    CHECK(actual == expected);
                }
            }
        }

        for (uint32_t tz = 0; tz < terrain.TileCountZ; ++tz)
        {
            for (uint32_t tx = 0; tx < terrain.TileCountX; ++tx)
            {
                uint32_t current = 0;
                CHECK(terrain.FindTile(tx, tz, &current) != nullptr);
                for (uint32_t direction = 0; direction < 2; ++direction)
                {
                    if ((direction == 0 && tx + 1 == terrain.TileCountX) ||
                        (direction == 1 && tz + 1 == terrain.TileCountZ))
                    {
                        continue;
                    }
                    uint32_t adjacent = 0;
                    CHECK(terrain.FindTile(tx + (direction == 0 ? 1 : 0), tz + (direction == 1 ? 1 : 0), &adjacent) != nullptr);
                    for (uint32_t i = 0; i <= CookTileCells; ++i)
                    {
                        const uint32_t x0 = direction == 0 ? CookTileCells : i;
                        const uint32_t z0 = direction == 0 ? i : CookTileCells;
                        const uint32_t x1 = direction == 0 ? 0 : i;
                        const uint32_t z1 = direction == 0 ? i : 0;
                        CHECK(terrain.GetHeightSample(current, x0, z0) == terrain.GetHeightSample(adjacent, x1, z1));
                        const size_t firstPixel = (z0 * 17 + x0) * 2;
                        const size_t nextPixel = (z1 * 17 + x1) * 2;
                        CHECK(normals[current][firstPixel] == normals[adjacent][nextPixel]);
                        CHECK(normals[current][firstPixel + 1] == normals[adjacent][nextPixel + 1]);
                    }
                }
            }
        }

        std::cout << std::setprecision(10) << Utf8(u8"(a) 最大高さ誤差=") << maxHeightError
                  << Utf8(u8" m 許容=") << tolerance << Utf8(u8" m、2 ULP=") << 2.0 * ulp << " m\n";
        std::cout << Utf8(u8"(b) x・z 方向の共有辺は HGT0・NRM0 とも一致\n");
        std::cout << Utf8(u8"(c) 最大法線角度=") << maxAngleDegrees << Utf8(u8" 度 / 0.5 度\n");
        std::cout << Utf8(u8"(d) AABB・全 LOD の不一致なし、LOD 最大差=") << maxLodDifference << "\n";

        raw[2 * (22 * CookWidth + 35)] ^= 1;
        WriteTerrainFile(TerrainTempRoot / "surface.raw", Span<const uint8_t>(raw));
        const auto changedInput = CookTerrainChecked(spec);
        CHECK(changedInput.SourceHash != first.SourceHash);
        raw[2 * (22 * CookWidth + 35)] ^= 1;
        WriteTerrainFile(TerrainTempRoot / "surface.raw", Span<const uint8_t>(raw));
        WriteTerrainSpec("surface.json", source, mapping, CookCellSize, 2);
        const auto changedSpec = CookTerrainChecked(spec);
        CHECK(changedSpec.SourceHash != first.SourceHash);
        std::cout << Utf8(u8"(g) cook 再現・payload hash 一致、入力と seed の変更で SourceHash が変化\n");
    }

    void CheckTerrainFormatsAndRejection()
    {
        constexpr uint32_t side = 33;
        constexpr double scale = 0.015625;
        constexpr double offset = -31.25;
        ByteArray little(side * side * 2);
        ByteArray big(little.size());
        ByteArray floats(side * side * 4);
        VariableArray<float> floatValues(side * side);
        for (uint32_t z = 0; z < side; ++z)
        {
            for (uint32_t x = 0; x < side; ++x)
            {
                const size_t i = static_cast<size_t>(z) * side + x;
                const uint16_t value = static_cast<uint16_t>(1985 * x + 62 * z - (7 * x * z) % 61);
                WriteLe16(little, i * 2, value);
                big[i * 2] = static_cast<uint8_t>(value >> 8);
                big[i * 2 + 1] = static_cast<uint8_t>(value);
                floatValues[i] = static_cast<float>(value);
                WriteLe32(floats, i * 4, std::bit_cast<uint32_t>(floatValues[i]));
            }
        }
        std::error_code copyError;
        CHECK(std::filesystem::copy_file("Test/Core/Asset/Fixtures/Terrain/height_formula_33x33_16bit.png",
                                         TerrainTempRoot / "fixture.png", copyError));
        CHECK(!copyError);
        WriteTerrainFile(TerrainTempRoot / "little.raw", Span<const uint8_t>(little));
        WriteTerrainFile(TerrainTempRoot / "big.raw", Span<const uint8_t>(big));
        WriteTerrainFile(TerrainTempRoot / "float.raw", Span<const uint8_t>(floats));
        const ByteArray exr = SaveFloatExr(floatValues, side, side);
        WriteTerrainFile(TerrainTempRoot / "float.exr", Span<const uint8_t>(exr));

        const char* integerMapping = "\"scale_m\":0.015625,\"offset_m\":-31.25";
        const char* floatMapping = "\"scale\":0.015625,\"offset\":-31.25";
        const auto png = CookTerrainChecked(WriteTerrainSpec("png.json",
            "\"path\":\"fixture.png\",\"format\":\"png16\",\"flip_z\":false", integerMapping, CookCellSize));
        const auto rawLittle = CookTerrainChecked(WriteTerrainSpec("little.json",
            "\"path\":\"little.raw\",\"format\":\"raw16\",\"width\":33,\"height\":33,\"endian\":\"little\"",
            integerMapping, CookCellSize));
        const auto rawBig = CookTerrainChecked(WriteTerrainSpec("big.json",
            "\"path\":\"big.raw\",\"format\":\"raw16\",\"width\":33,\"height\":33,\"endian\":\"big\"",
            integerMapping, CookCellSize));
        const auto rawFloat = CookTerrainChecked(WriteTerrainSpec("float.json",
            "\"path\":\"float.raw\",\"format\":\"raw32f\",\"width\":33,\"height\":33,\"endian\":\"little\"",
            floatMapping, CookCellSize));
        const auto exrFloat = CookTerrainChecked(WriteTerrainSpec("exr.json",
            "\"path\":\"float.exr\",\"format\":\"exr\",\"exr_channel\":\"Y\"", floatMapping, CookCellSize));
        CHECK(png.Build.NvtrnBytes == rawLittle.Build.NvtrnBytes);
        CHECK(png.Build.NvtrnBytes == rawBig.Build.NvtrnBytes);
        CHECK(png.Build.NvtrnBytes == rawFloat.Build.NvtrnBytes);
        CHECK(png.Build.NvtrnBytes == exrFloat.Build.NvtrnBytes);

        for (auto& value : floatValues)
        {
            value += 0.25f;
        }
        const ByteArray fractionalExr = SaveFloatExr(floatValues, side, side);
        HeightImportDesc desc;
        desc.Format = HeightSourceFormat::Exr;
        desc.ExrChannel = "Y";
        desc.Scale = scale;
        desc.Offset = offset;
        desc.TileCells = CookTileCells;
        HeightImportResult imported;
        AnsiString error;
        const bool bImported = ImportHeightfield(Span<const uint8_t>(fractionalExr), desc, imported, error);
        if (!bImported)
        {
            std::cerr << Utf8(u8"端数付き EXR の取り込み失敗: ") << error.c_str() << "\n";
        }
        CHECK(bImported);
        CHECK(imported.Grid.Width == side && imported.Grid.Height == side && imported.SourceBitDepth == 32);
        for (size_t i = 0; i < floatValues.size(); ++i)
        {
            CHECK(imported.Grid.HeightsM[i] == offset + scale * static_cast<double>(floatValues[i]));
        }
        std::cout << Utf8(u8"(e) PNG16・RAW16 両順序・RAW32F・EXR の全バイトと EXR の端数が一致\n");

        const ByteArray png8 = SaveGray8Png(33, 33);
        WriteTerrainFile(TerrainTempRoot / "gray8.png", Span<const uint8_t>(png8));
        const char* png8Source = "\"path\":\"gray8.png\",\"format\":\"png8\"";
        const auto allowedPath = WriteTerrainSpec("gray8.json", png8Source, integerMapping, CookCellSize);
        const auto allowed = CookTerrainChecked(allowedPath);
        bool bFoundWarning = false;
        for (const auto& warning : allowed.Warnings)
        {
            bFoundWarning = bFoundWarning || std::strstr(warning.c_str(), "terrain_height_bit_depth=8") != nullptr;
        }
        CHECK(bFoundWarning && allowed.SourceBitDepth == 8);
        const auto strictPath = WriteTerrainSpec("strict.json", png8Source, integerMapping, CookCellSize, 1,
                                                 ",\"require_bit_depth\":16");
        TerrainSpecCookResult rejected;
        error.clear();
        CHECK(!CookTerrainSpecFile(strictPath, rejected, error));
        CHECK(std::strstr(error.c_str(), "require_bit_depth=16") != nullptr);
        CHECK(std::strstr(error.c_str(), "8bit") != nullptr);
        const ByteArray wrongSize = SaveGray8Png(34, 33);
        WriteTerrainFile(TerrainTempRoot / "wrong.png", Span<const uint8_t>(wrongSize));
        const auto wrongPath = WriteTerrainSpec("wrong.json", "\"path\":\"wrong.png\",\"format\":\"png8\"",
                                                integerMapping, CookCellSize);
        error.clear();
        CHECK(!CookTerrainSpecFile(wrongPath, rejected, error));
        CHECK(std::strstr(error.c_str(), "TileCells=16") != nullptr);
        CHECK(std::strstr(error.c_str(), "34") != nullptr);
        std::cout << Utf8(u8"(f) 8bit 警告、16bit 指定と不正寸法の拒否を確認\n");
    }

    constexpr uint32_t MaskSide = 33;
    constexpr uint32_t MaskTileSide = CookTileCells + 1;

    void WriteMaskPaint(const ByteArray& pixels)
    {
        CHECK(pixels.size() == MaskSide * MaskSide);
        ByteArray png;
        CHECK(stbi_write_png_to_func(AppendPngBytes, &png, MaskSide, MaskSide, 1, pixels.data(), MaskSide) != 0);
        WriteTerrainFile(TerrainTempRoot / "mask-road.png", Span<const uint8_t>(png));
    }

    CookedTerrainData ParseMaskTerrain(const TerrainSpecCookResult& cooked)
    {
        auto parsed = ParseCookedTerrain(AssetBlob::CopyBytes(Span<const uint8_t>(cooked.Build.NvtrnBytes)));
        CHECK(parsed.Succeeded());
        CHECK(parsed.Terrain.TileCells == CookTileCells);
        CHECK(parsed.Terrain.TileCountX == 2 && parsed.Terrain.TileCountZ == 2);
        return std::move(parsed.Terrain);
    }

    uint32_t MaskTileIndex(const CookedTerrainData& terrain, uint32_t tx, uint32_t tz)
    {
        uint32_t index = 0;
        CHECK(terrain.FindTile(tx, tz, &index) != nullptr);
        return index;
    }

    VariableArray<ByteArray> ReadMaskLayer(const CookedTerrainData& terrain, AssetPackageFourCC fourCC,
                                          CookedTexturePixelFormat format, uint32_t channels)
    {
        const int32_t layer = terrain.FindLayer(fourCC);
        CHECK(layer >= 0);
        VariableArray<ByteArray> result(terrain.Tiles.size());
        for (uint32_t tile = 0; tile < terrain.Tiles.size(); ++tile)
        {
            const auto texture = ParseCookedTexture(terrain.GetLayerBlob(tile, static_cast<uint32_t>(layer)), false);
            CHECK(texture.Succeeded());
            CHECK(texture.Texture.PixelFormat == format && texture.Texture.ColorSpace == CookedTextureColorSpace::Linear);
            CHECK(texture.Texture.Width == MaskTileSide && texture.Texture.Height == MaskTileSide);
            const auto mip = texture.Texture.GetMipBytes(0);
            CHECK(mip.size() == static_cast<size_t>(MaskTileSide) * MaskTileSide * channels);
            result[tile].resize(mip.size());
            std::memcpy(result[tile].data(), mip.data(), mip.size());
        }
        return result;
    }

    uint8_t MaskSample(const CookedTerrainData& terrain, const VariableArray<ByteArray>& layer,
                       uint32_t x, uint32_t z, uint32_t channels, uint32_t channel)
    {
        CHECK(x < MaskSide && z < MaskSide && channel < channels);
        const uint32_t tx = std::min(x / CookTileCells, terrain.TileCountX - 1);
        const uint32_t tz = std::min(z / CookTileCells, terrain.TileCountZ - 1);
        const uint32_t localX = x - tx * CookTileCells;
        const uint32_t localZ = z - tz * CookTileCells;
        const uint32_t tile = MaskTileIndex(terrain, tx, tz);
        return layer[tile][(static_cast<size_t>(localZ) * MaskTileSide + localX) * channels + channel];
    }

    void CheckTerrainMasks()
    {
        ByteArray flat(MaskSide * MaskSide * 2);
        ByteArray slope(flat.size());
        ByteArray valley(flat.size());
        ByteArray paint(MaskSide * MaskSide);
        constexpr uint32_t valleyX = 16;
        for (uint32_t z = 0; z < MaskSide; ++z)
        {
            for (uint32_t x = 0; x < MaskSide; ++x)
            {
                const size_t index = static_cast<size_t>(z) * MaskSide + x;
                WriteLe16(flat, index * 2, 40);
                WriteLe16(slope, index * 2, static_cast<uint16_t>(x * x));
                const uint32_t distance = x < valleyX ? valleyX - x : x - valleyX;
                WriteLe16(valley, index * 2, static_cast<uint16_t>(distance * 100 + z * 3));
                paint[index] = x >= 10 && x <= 22 && z >= 10 && z <= 22 ? 255 : 0;
            }
        }
        WriteTerrainFile(TerrainTempRoot / "mask-flat.raw", Span<const uint8_t>(flat));
        WriteTerrainFile(TerrainTempRoot / "mask-slope.raw", Span<const uint8_t>(slope));
        WriteTerrainFile(TerrainTempRoot / "mask-valley.raw", Span<const uint8_t>(valley));
        WriteMaskPaint(paint);

        constexpr const char* flatSource = "\"path\":\"mask-flat.raw\",\"format\":\"raw16\",\"width\":33,\"height\":33,\"endian\":\"little\"";
        constexpr const char* slopeSource = "\"path\":\"mask-slope.raw\",\"format\":\"raw16\",\"width\":33,\"height\":33,\"endian\":\"little\"";
        constexpr const char* valleySource = "\"path\":\"mask-valley.raw\",\"format\":\"raw16\",\"width\":33,\"height\":33,\"endian\":\"little\"";
        constexpr const char* slopeRecipe =
            ",\"recipe\":{\"dry_grass\":{\"base\":0.5,\"noise\":{\"amplitude\":0.4,\"scale_m\":7}},"
            "\"splat\":{\"layers\":["
            "{\"name\":\"soil\",\"noise\":{\"amplitude\":0.8,\"scale_m\":7}},"
            "{\"name\":\"sand\",\"noise\":{\"amplitude\":0.6,\"scale_m\":11}},"
            "{\"name\":\"clay\"},{\"name\":\"moss\"},{\"name\":\"rock\"},"
            "{\"name\":\"snow\"},{\"name\":\"ash\"},{\"name\":\"peat\"}]},"
            "\"paint\":[{\"target\":\"road\",\"path\":\"mask-road.png\",\"mode\":\"override\"}]}";
        constexpr const char* flatRecipe =
            ",\"recipe\":{\"wetness\":{\"flow_weight\":0,\"low_weight\":0,\"concave_weight\":0},"
            "\"paint\":[{\"target\":\"road\",\"path\":\"mask-road.png\",\"mode\":\"override\"}]}";
        constexpr const char* valleyRecipe =
            ",\"recipe\":{\"wetness\":{\"flow_weight\":1,\"low_weight\":0,\"concave_weight\":0}}";

        const auto slopeSpec = WriteTerrainSpec("mask-slope.json", slopeSource, "\"scale_m\":0.05,\"offset_m\":0", 1.0, 17, slopeRecipe);
        const auto slopeFirst = CookTerrainChecked(slopeSpec);
        WriteTerrainSpec("mask-slope.json", slopeSource, "\"scale_m\":0.05,\"offset_m\":0", 1.0, 99, slopeRecipe);
        const auto slopeSecond = CookTerrainChecked(slopeSpec);
        const auto flatSpec = WriteTerrainSpec("mask-flat.json", flatSource, "\"scale_m\":0.05,\"offset_m\":0", 1.0, 1, flatRecipe);
        const auto flatCooked = CookTerrainChecked(flatSpec);
        const auto valleyCooked = CookTerrainChecked(WriteTerrainSpec("mask-valley.json", valleySource,
            "\"scale_m\":0.01,\"offset_m\":0", 1.0, 1, valleyRecipe));

        const auto slopeTerrain = ParseMaskTerrain(slopeFirst);
        const auto mask = ReadMaskLayer(slopeTerrain, TerrainMaskLayerFourCC, CookedTexturePixelFormat::RGBA8UNorm, 4);
        const auto splat0 = ReadMaskLayer(slopeTerrain, TerrainSplat0LayerFourCC, CookedTexturePixelFormat::RGBA8UNorm, 4);
        const auto splat1 = ReadMaskLayer(slopeTerrain, TerrainSplat1LayerFourCC, CookedTexturePixelFormat::RGBA8UNorm, 4);
        const auto grass = ReadMaskLayer(slopeTerrain, TerrainGrassLayerFourCC, CookedTexturePixelFormat::R8UNorm, 1);

        for (uint32_t tile = 0; tile < slopeTerrain.Tiles.size(); ++tile)
        {
            for (size_t pixel = 0; pixel < MaskTileSide * MaskTileSide; ++pixel)
            {
                unsigned sum = 0;
                for (uint32_t channel = 0; channel < 4; ++channel)
                {
                    sum += splat0[tile][pixel * 4 + channel];
                    sum += splat1[tile][pixel * 4 + channel];
                }
                if (sum < 254 || sum > 256)
                {
                    std::cerr << Utf8(u8"スプラット合計不一致 tile=") << tile << Utf8(u8" 画素=") << pixel
                              << Utf8(u8" 合計=") << sum << "\n";
                }
                CHECK(sum >= 254 && sum <= 256);
                for (uint32_t channel = 0; channel < 4; ++channel)
                {
                    CHECK(splat0[tile][pixel * 4 + channel] > 0 && splat1[tile][pixel * 4 + channel] > 0);
                }
            }
        }
        std::cout << Utf8(u8"マスク (a) 全タイルのスプラット 8 チャンネルの合計を確認\n");

        const auto flatTerrain = ParseMaskTerrain(flatCooked);
        const auto flatMask = ReadMaskLayer(flatTerrain, TerrainMaskLayerFourCC, CookedTexturePixelFormat::RGBA8UNorm, 4);
        const auto flatGrass = ReadMaskLayer(flatTerrain, TerrainGrassLayerFourCC, CookedTexturePixelFormat::R8UNorm, 1);
        bool bGrassOutside = false;
        for (uint32_t tz = 0; tz < flatTerrain.TileCountZ; ++tz)
        {
            for (uint32_t tx = 0; tx < flatTerrain.TileCountX; ++tx)
            {
                const uint32_t tile = MaskTileIndex(flatTerrain, tx, tz);
                for (uint32_t z = 0; z < MaskTileSide; ++z)
                {
                    for (uint32_t x = 0; x < MaskTileSide; ++x)
                    {
                        const uint32_t gx = tx * CookTileCells + x;
                        const uint32_t gz = tz * CookTileCells + z;
                        const size_t pixel = static_cast<size_t>(z) * MaskTileSide + x;
                        const uint8_t road = paint[static_cast<size_t>(gz) * MaskSide + gx];
                        const uint8_t density = flatGrass[tile][pixel];
                        if (flatMask[tile][pixel * 4 + 2] != road || (road == 255 && density != 0))
                        {
                            std::cerr << Utf8(u8"道路マスク不一致 x,z=") << gx << "," << gz
                                      << Utf8(u8" 道=") << unsigned(flatMask[tile][pixel * 4 + 2])
                                      << Utf8(u8" 期待=") << unsigned(road) << Utf8(u8" 草=") << unsigned(density) << "\n";
                        }
                        CHECK(flatMask[tile][pixel * 4 + 2] == road);
                        CHECK(road != 255 || density == 0);
                        bGrassOutside = bGrassOutside || (road == 0 && density > 0);
                    }
                }
            }
        }
        CHECK(bGrassOutside);
        std::cout << Utf8(u8"マスク (b) 道路ペイント内の草抑制と平坦な外側の草を確認\n");

        bool bRockZero = false;
        bool bRockFull = false;
        for (uint32_t z = 0; z < MaskSide; ++z)
        {
            uint8_t previous = 0;
            for (uint32_t x = 0; x < MaskSide; ++x)
            {
                const uint8_t rock = MaskSample(slopeTerrain, mask, x, z, 4, 3);
                if (x > 0 && rock < previous)
                {
                    std::cerr << Utf8(u8"岩の単調性不一致 x,z=") << x << "," << z
                              << Utf8(u8" 前=") << unsigned(previous) << Utf8(u8" 現在=") << unsigned(rock) << "\n";
                }
                CHECK(x == 0 || rock >= previous);
                previous = rock;
                bRockZero = bRockZero || rock == 0;
                bRockFull = bRockFull || rock == 255;
            }
        }
        CHECK(bRockZero && bRockFull);
        std::cout << Utf8(u8"マスク (c) 傾斜に対する岩の単調性と両端の値を確認\n");

        const auto valleyTerrain = ParseMaskTerrain(valleyCooked);
        const auto valleyMask = ReadMaskLayer(valleyTerrain, TerrainMaskLayerFourCC, CookedTexturePixelFormat::RGBA8UNorm, 4);
        bool bLowerSample = false;
        for (uint32_t z = 1; z + 1 < MaskSide; ++z)
        {
            const uint8_t center = MaskSample(valleyTerrain, valleyMask, valleyX, z, 4, 0);
            for (uint32_t x = 0; x < MaskSide; ++x)
            {
                const uint8_t wet = MaskSample(valleyTerrain, valleyMask, x, z, 4, 0);
                if (wet > center)
                {
                    std::cerr << Utf8(u8"谷の湿り不一致 x,z=") << x << "," << z
                              << Utf8(u8" 湿り=") << unsigned(wet) << Utf8(u8" 谷=") << unsigned(center) << "\n";
                }
                CHECK(wet <= center);
                bLowerSample = bLowerSample || wet < center;
            }
        }
        CHECK(bLowerSample);
        CHECK(MaskSample(valleyTerrain, valleyMask, valleyX, 1, 4, 0) >
              MaskSample(valleyTerrain, valleyMask, valleyX, MaskSide - 3, 4, 0));
        std::cout << Utf8(u8"マスク (d) 各行の湿りの最大が谷線上にあることを確認\n");

        const auto secondTerrain = ParseMaskTerrain(slopeSecond);
        const auto secondMask = ReadMaskLayer(secondTerrain, TerrainMaskLayerFourCC, CookedTexturePixelFormat::RGBA8UNorm, 4);
        const auto secondSplat = ReadMaskLayer(secondTerrain, TerrainSplat0LayerFourCC, CookedTexturePixelFormat::RGBA8UNorm, 4);
        const auto secondGrass = ReadMaskLayer(secondTerrain, TerrainGrassLayerFourCC, CookedTexturePixelFormat::R8UNorm, 1);
        const std::array<AssetPackageFourCC, 2> fixedLayers = {HeightLayerFourCC, TerrainNormalLayerFourCC};
        bool bDryChanged = false;
        bool bSplatChanged = false;
        for (uint32_t tz = 0; tz < slopeTerrain.TileCountZ; ++tz)
        {
            for (uint32_t tx = 0; tx < slopeTerrain.TileCountX; ++tx)
            {
                const uint32_t firstTile = MaskTileIndex(slopeTerrain, tx, tz);
                const uint32_t secondTile = MaskTileIndex(secondTerrain, tx, tz);
                for (const auto fourCC : fixedLayers)
                {
                    const int32_t firstLayer = slopeTerrain.FindLayer(fourCC);
                    const int32_t secondLayer = secondTerrain.FindLayer(fourCC);
                    CHECK(firstLayer >= 0 && secondLayer >= 0);
                    const auto firstBlob = slopeTerrain.GetLayerBlob(firstTile, static_cast<uint32_t>(firstLayer));
                    const auto secondBlob = secondTerrain.GetLayerBlob(secondTile, static_cast<uint32_t>(secondLayer));
                    CHECK(firstBlob.IsValid() && secondBlob.IsValid() && firstBlob.GetSize() == secondBlob.GetSize());
                    CHECK(std::memcmp(firstBlob.GetData(), secondBlob.GetData(), firstBlob.GetSize()) == 0);
                }
                CHECK(grass[firstTile] == secondGrass[secondTile]);
                for (size_t pixel = 0; pixel < MaskTileSide * MaskTileSide; ++pixel)
                {
                    for (uint32_t channel : {0u, 2u, 3u})
                    {
                        const uint8_t first = mask[firstTile][pixel * 4 + channel];
                        const uint8_t second = secondMask[secondTile][pixel * 4 + channel];
                        if (first != second)
                        {
                            std::cerr << Utf8(u8"seed による固定マスクの不一致 tile=") << tx << "," << tz
                                      << Utf8(u8" 画素=") << pixel << Utf8(u8" チャンネル=") << channel
                                      << Utf8(u8" 値=") << unsigned(first) << "," << unsigned(second) << "\n";
                        }
                        CHECK(first == second);
                    }
                    bDryChanged = bDryChanged || mask[firstTile][pixel * 4 + 1] != secondMask[secondTile][pixel * 4 + 1];
                    for (uint32_t channel = 0; channel < 4; ++channel)
                    {
                        bSplatChanged = bSplatChanged || splat0[firstTile][pixel * 4 + channel] != secondSplat[secondTile][pixel * 4 + channel];
                    }
                }
            }
        }
        CHECK(bDryChanged && bSplatChanged);
        std::cout << Utf8(u8"マスク (e) seed による雑音の変化と他の層の不変を確認\n");

        const std::array<const VariableArray<ByteArray>*, 4> seamLayers = {&mask, &splat0, &splat1, &grass};
        const std::array<const char*, 4> seamNames = {"MSK0", "SPL0", "SPL1", "GRS0"};
        for (uint32_t tz = 0; tz < slopeTerrain.TileCountZ; ++tz)
        {
            for (uint32_t tx = 0; tx < slopeTerrain.TileCountX; ++tx)
            {
                const uint32_t tile = MaskTileIndex(slopeTerrain, tx, tz);
                for (uint32_t direction = 0; direction < 2; ++direction)
                {
                    if ((direction == 0 && tx + 1 == slopeTerrain.TileCountX) ||
                        (direction == 1 && tz + 1 == slopeTerrain.TileCountZ))
                    {
                        continue;
                    }
                    const uint32_t adjacent = MaskTileIndex(slopeTerrain, tx + (direction == 0 ? 1 : 0),
                                                            tz + (direction == 1 ? 1 : 0));
                    for (size_t layer = 0; layer < seamLayers.size(); ++layer)
                    {
                        const uint32_t channels = layer == 3 ? 1 : 4;
                        for (uint32_t i = 0; i < MaskTileSide; ++i)
                        {
                            const uint32_t x0 = direction == 0 ? CookTileCells : i;
                            const uint32_t z0 = direction == 0 ? i : CookTileCells;
                            const uint32_t x1 = direction == 0 ? 0 : i;
                            const uint32_t z1 = direction == 0 ? i : 0;
                            for (uint32_t channel = 0; channel < channels; ++channel)
                            {
                                const uint8_t first = (*seamLayers[layer])[tile][(z0 * MaskTileSide + x0) * channels + channel];
                                const uint8_t second = (*seamLayers[layer])[adjacent][(z1 * MaskTileSide + x1) * channels + channel];
                                if (first != second)
                                {
                                    std::cerr << Utf8(u8"共有辺不一致 layer=") << seamNames[layer] << Utf8(u8" tile=") << tx << "," << tz
                                              << Utf8(u8" 方向=") << direction << Utf8(u8" 標本=") << i
                                              << Utf8(u8" チャンネル=") << channel << Utf8(u8" 値=") << unsigned(first) << "," << unsigned(second) << "\n";
                                }
                                CHECK(first == second);
                            }
                        }
                    }
                }
            }
        }
        std::cout << Utf8(u8"マスク (f) x・z 両方向の共有辺で全マスク層が一致\n");

        CHECK(flatCooked.InputFiles.size() == 3);
        CHECK(flatCooked.InputFiles[0] == flatSpec && flatCooked.InputFiles[1] == TerrainTempRoot / "mask-flat.raw");
        CHECK(flatCooked.InputFiles[2] == TerrainTempRoot / "mask-road.png");
        paint[2 * MaskSide + 2] = 255;
        WriteMaskPaint(paint);
        const auto changedPaint = CookTerrainChecked(flatSpec);
        CHECK(changedPaint.InputFiles == flatCooked.InputFiles);
        if (changedPaint.SourceHash == flatCooked.SourceHash)
        {
            std::cerr << Utf8(u8"ペイント変更前後の source_hash が同じ: ") << flatCooked.SourceHash << "\n";
        }
        CHECK(changedPaint.SourceHash != flatCooked.SourceHash);
        std::cout << Utf8(u8"マスク (g) ペイント入力の登録と 1 画素変更による source_hash の変化を確認\n");
    }

    void CheckTerrainCookPath()
    {
        CreateTerrainTemp();
        CheckAnalyticTerrain();
        CheckTerrainFormatsAndRejection();
        CheckTerrainMasks();
        std::error_code error;
        std::filesystem::remove_all(TerrainTempRoot, error);
        CHECK(!error && !std::filesystem::exists(TerrainTempRoot));
        TerrainTempRoot.clear();
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
    CheckTerrainCookPath();
    std::cout << Utf8(u8"CookedTerrainTest 成功\n");
    return 0;
}
