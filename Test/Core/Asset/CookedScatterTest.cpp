#include "Asset/CookedScatterFormat.h"
#include "Asset/AssetManifest.h"
#include "Tools/AssetCook/ScatterSpec.h"
#include "Tools/AssetCook/TerrainSpec.h"

#include <filesystem>
#include <fstream>
#include <cstdio>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

using namespace NorvesLib::Core::Asset;
using namespace NorvesLib::Core::Asset::CookedScatterFormatV0;
using NorvesLib::Core::Container::Span;
using NorvesLib::Core::Container::VariableArray;
using ByteArray = VariableArray<uint8_t>;

#define CHECK(expression)                                                                                              \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expression))                                                                                             \
        {                                                                                                              \
            std::cerr << "検査失敗: " << #expression << " 行 " << __LINE__ << "\n";                                     \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (false)

namespace
{
    // spec と参照入力の変更がハッシュと再焼きの必要性に反映されることを確かめる。
    void CheckSpecCookPath()
    {
        using namespace NorvesLib::Tools::AssetCook;
        using NorvesLib::Core::Container::AnsiString;
        char directoryName[80];
        std::snprintf(directoryName, sizeof(directoryName), "nvsct-spec-%lu", static_cast<unsigned long>(GetCurrentProcessId()));
        const auto directory = std::filesystem::temp_directory_path() / directoryName;
        std::filesystem::create_directories(directory);
        struct Cleanup
        {
            std::filesystem::path Path;
            ~Cleanup()
            {
                std::error_code error;
                std::filesystem::remove_all(Path, error);
            }
        } cleanup{directory};
        const auto fixture = std::filesystem::path(__FILE__).parent_path() / "Fixtures/Terrain/height_formula_33x33_16bit.png";
        std::filesystem::copy_file(fixture, directory / "height.png", std::filesystem::copy_options::overwrite_existing);
        const auto write = [&](const char* name, const AnsiString& text)
        {
            std::ofstream file(directory / name, std::ios::binary | std::ios::trunc);
            file.write(text.data(), static_cast<std::streamsize>(text.size()));
            CHECK(file.good());
        };
        const auto writeTerrain = [&](bool changed)
        {
            AnsiString text = R"({"version":1,"height_source":{"path":"height.png","format":"png16","flip_z":false},
                "height_mapping":{"min_m":0,"max_m":)";
            text += changed ? "131.07" : "65.535";
            text += R"(},"cell_size_m":1,"tile_cells":16,"fit":"error","require_bit_depth":16,"seed":1,"recipe":{}})";
            write("terrain.json", text);
        };
        const auto writeScatter = [&](uint64_t hash, bool changed)
        {
            AnsiString text = R"({"version":0,"terrain":{"path":"terrain.json","cooked_hash":")";
            text += FormatAssetHashHex(hash);
            text += R"("},"chunk_size_m":16,"seed":)";
            text += changed ? "18446744073709551614" : "18446744073709551615";
            text += R"(,"species":[{"name":"rock","render_path":"mega","lods":["Meshes/Rock"],
                "mesh_radius_m":0.5,"density":{"source":"constant","value":1},"base_density_per_m2":0.01,
                "scale_range":[0.7,1.4],"slope_follow":0.8,"brightness_variation":0.1}]})";
            write("scatter.json", text);
        };
        writeTerrain(false);
        AnsiString error;
        TerrainSpecCookResult terrain;
        CHECK(CookTerrainSpecFile(directory / "terrain.json", terrain, error));
        const uint64_t oldHash = ComputeAssetPackagePayloadHash(terrain.Build.NvtrnBytes.data(), terrain.Build.NvtrnBytes.size());
        writeScatter(oldHash, false);
        ScatterSpecCookResult first;
        CHECK(CookScatterSpecFile(directory / "scatter.json", first, error));
        CHECK(error.empty() && first.ChunkCount == 4 && first.SpeciesNames.size() == 1);
        const auto parsed = ParseCookedScatter(AssetBlob::CopyBytes({first.Bytes.data(), first.Bytes.size()}));
        CHECK(parsed.Succeeded() && parsed.Scatter.Seed == UINT64_MAX && parsed.Scatter.TerrainCookedHash == oldHash);
        writeScatter(oldHash, true);
        ScatterSpecCookResult changed;
        CHECK(CookScatterSpecFile(directory / "scatter.json", changed, error));
        CHECK(changed.SourceHash != first.SourceHash);
        writeScatter(oldHash, false);
        writeTerrain(true);
        CHECK(CookTerrainSpecFile(directory / "terrain.json", terrain, error));
        const uint64_t newHash = ComputeAssetPackagePayloadHash(terrain.Build.NvtrnBytes.data(), terrain.Build.NvtrnBytes.size());
        CHECK(newHash != oldHash);
        CHECK(!CookScatterSpecFile(directory / "scatter.json", changed, error));
        CHECK(error.find(reinterpret_cast<const char*>(u8"食い違い")) != AnsiString::npos);
        CHECK(error.find(FormatAssetHashHex(oldHash).c_str()) != AnsiString::npos);
        CHECK(error.find(FormatAssetHashHex(newHash).c_str()) != AnsiString::npos);
        writeScatter(newHash, false);
        CHECK(CookScatterSpecFile(directory / "scatter.json", changed, error));
        CHECK(error.empty() && changed.SourceHash != first.SourceHash);
    }

    uint64_t ReadLe(const ByteArray& bytes, size_t offset, size_t width)
    {
        uint64_t value = 0;
        for (size_t i = 0; i < width; ++i)
        {
            value |= static_cast<uint64_t>(bytes[offset + i]) << (8 * i);
        }
        return value;
    }

    void WriteLe(ByteArray& bytes, size_t offset, uint64_t value, size_t width)
    {
        for (size_t i = 0; i < width; ++i)
        {
            bytes[offset + i] = static_cast<uint8_t>(value >> (8 * i));
        }
    }

    void UpdateHash(ByteArray& bytes)
    {
        WriteLe(bytes, HeaderOffset::PayloadHash, ComputeCookedScatterFileHash(Span<const uint8_t>(bytes)), 8);
    }

    void CheckRejection(const ByteArray& bytes, CookedScatterParseStatus expected)
    {
        const auto result = ParseCookedScatter(AssetBlob::CopyBytes(Span<const uint8_t>(bytes)));
        if (result.Status != expected)
        {
            std::cerr << "期待した理由: " << GetCookedScatterParseStatusName(expected)
                      << "、実際の理由: " << GetCookedScatterParseStatusName(result.Status) << "\n";
            std::exit(1);
        }
    }
}

int main()
{
    CookedScatterWriteDesc input;
    input.Seed = 0x123456789abcdef0ull;
    input.TerrainCookedHash = 0xfedcba9876543210ull;
    input.ChunkSizeMeters = 64.0f;
    input.OriginX = 1000.25;
    input.OriginZ = -2000.5;
    input.GridCountX = 8;
    input.GridCountZ = 10;
    input.Species.resize(2);
    input.Species[0].Name = "rock";
    input.Species[0].RenderPath = ScatterRenderPath::Mega;
    input.Species[0].LodCount = 2;
    input.Species[0].LodPaths[0] = "Meshes/Rock0";
    input.Species[0].LodPaths[1] = "Meshes/Rock1";
    input.Species[0].MeshRadiusMeters = 2.0f;
    input.Species[0].MinSpacingMeters = 3.0f;
    input.Species[0].ScaleMin = 0.5f;
    input.Species[0].ScaleMax = 2.0f;
    input.Species[0].SlopeFollow = 0.25f;
    input.Species[0].BrightnessVariation = 0.125f;
    input.Species[0].WindStiffness = 0.75f;
    input.Species[0].WindPhaseSeed = 42;
    input.Species[0].Flags = 1;
    input.Species[1] = input.Species[0];
    input.Species[1].Name = "shrub";
    input.Species[1].RenderPath = ScatterRenderPath::Foliage;
    input.Species[1].LodCount = 1;
    input.Species[1].LodPaths[0] = "Meshes/Shrub";
    input.Species[1].LodPaths[1].clear();
    input.Species[1].Flags = 0;
    input.Chunks.resize(2);
    for (size_t i = 0; i < input.Chunks.size(); ++i)
    {
        auto& chunk = input.Chunks[i];
        chunk.ChunkX = static_cast<uint32_t>(i + 1);
        chunk.ChunkZ = static_cast<uint32_t>(i + 3);
        chunk.MinY = -10.0f;
        chunk.MaxY = 40.0f;
        chunk.AabbMin[0] = 1000.0f;
        chunk.AabbMin[1] = -10.0f;
        chunk.AabbMin[2] = -2000.0f;
        chunk.AabbMax[0] = 1500.0f;
        chunk.AabbMax[1] = 40.0f;
        chunk.AabbMax[2] = -1000.0f;
    }
    input.SpeciesRanges.resize(4);
    input.SpeciesRanges[0] = {0, 2};
    input.SpeciesRanges[1] = {2, 1};
    input.SpeciesRanges[2] = {3, 0};
    input.SpeciesRanges[3] = {3, 1};
    input.Instances.resize(4);
    ScatterInstanceValue value;
    value.PositionX = input.OriginX + input.Chunks[0].ChunkX * 64.0 + 12.345;
    value.PositionZ = input.OriginZ + input.Chunks[0].ChunkZ * 64.0 + 52.123;
    value.PositionY = 12.25;
    value.YawRadians = -1.25f;
    value.Scale = 1.25f;
    value.Rank = 17;
    value.Normal[0] = 0.2f;
    value.Normal[1] = 0.9f;
    value.Normal[2] = -0.3f;
    value.Tint[0] = 0.2f;
    value.Tint[1] = 0.7f;
    value.Tint[2] = 1.0f;
    value.Dry = 0.6f;
    CHECK(EncodeScatterInstance(value, input.Chunks[0], input.Species[0], input.ChunkSizeMeters,
                                input.OriginX, input.OriginZ, input.Instances[0]));
    for (size_t i = 1; i < input.Instances.size(); ++i)
    {
        input.Instances[i] = input.Instances[0];
        input.Instances[i].PosX += static_cast<uint16_t>(i);
        input.Instances[i].Rank += static_cast<uint8_t>(i);
    }

    ByteArray bytes;
    CHECK(WriteCookedScatter(input, bytes) == CookedScatterWriteStatus::Success);
    const auto parsed = ParseCookedScatter(AssetBlob::CopyBytes(Span<const uint8_t>(bytes)));
    CHECK(parsed.Succeeded());
    const auto& scatter = parsed.Scatter;
    CHECK(scatter.PayloadHash == ComputeCookedScatterFileHash(Span<const uint8_t>(bytes)));
    CHECK(scatter.Seed == input.Seed && scatter.TerrainCookedHash == input.TerrainCookedHash);
    CHECK(scatter.ChunkSizeMeters == input.ChunkSizeMeters && scatter.OriginX == input.OriginX && scatter.OriginZ == input.OriginZ);
    CHECK(scatter.GridCountX == input.GridCountX && scatter.GridCountZ == input.GridCountZ);
    CHECK(scatter.Species.size() == input.Species.size() && scatter.Chunks.size() == input.Chunks.size());
    CHECK(scatter.SpeciesRanges.size() == input.SpeciesRanges.size());
    CHECK(scatter.InstanceOffset % InstanceAlignment == 0 && scatter.InstanceSize == input.Instances.size() * InstanceRecordSize);
    for (size_t i = 0; i < input.Species.size(); ++i)
    {
        const auto& expected = input.Species[i];
        const auto& actual = scatter.Species[i];
        CHECK(actual.Name == expected.Name && actual.RenderPath == expected.RenderPath && actual.LodCount == expected.LodCount);
        for (uint32_t lod = 0; lod < MaxLodCount; ++lod)
        {
            CHECK(actual.LodPaths[lod] == expected.LodPaths[lod]);
        }
        CHECK(actual.MeshRadiusMeters == expected.MeshRadiusMeters && actual.MinSpacingMeters == expected.MinSpacingMeters);
        CHECK(actual.ScaleMin == expected.ScaleMin && actual.ScaleMax == expected.ScaleMax && actual.SlopeFollow == expected.SlopeFollow);
        CHECK(actual.BrightnessVariation == expected.BrightnessVariation && actual.WindStiffness == expected.WindStiffness);
        CHECK(actual.WindPhaseSeed == expected.WindPhaseSeed && actual.Flags == expected.Flags);
    }
    for (size_t i = 0; i < input.Chunks.size(); ++i)
    {
        const auto& expected = input.Chunks[i];
        const auto& actual = scatter.Chunks[i];
        CHECK(actual.ChunkX == expected.ChunkX && actual.ChunkZ == expected.ChunkZ);
        CHECK(actual.MinY == expected.MinY && actual.MaxY == expected.MaxY);
        for (size_t axis = 0; axis < 3; ++axis)
        {
            CHECK(actual.AabbMin[axis] == expected.AabbMin[axis] && actual.AabbMax[axis] == expected.AabbMax[axis]);
        }
    }
    for (uint32_t chunk = 0; chunk < input.Chunks.size(); ++chunk)
    {
        for (uint32_t species = 0; species < input.Species.size(); ++species)
        {
            const size_t index = chunk * input.Species.size() + species;
            const auto& expected = input.SpeciesRanges[index];
            const auto& actual = scatter.SpeciesRanges[index];
            CHECK(actual.Offset == expected.Offset && actual.Count == expected.Count);
            const auto instances = scatter.GetInstances(chunk, species);
            CHECK(instances.size() == expected.Count * InstanceRecordSize);
            for (size_t i = 0; i < expected.Count; ++i)
            {
                InstanceRecord16 record;
                CHECK(ReadScatterInstance({instances.data() + i * InstanceRecordSize, InstanceRecordSize}, record));
                CHECK(std::memcmp(&record, &input.Instances[static_cast<size_t>(expected.Offset) + i], InstanceRecordSize) == 0);
                CHECK(std::memcmp(instances.data() + i * InstanceRecordSize, &record, InstanceRecordSize) == 0);
            }
        }
    }
    ScatterInstanceValue decoded;
    CHECK(DecodeScatterInstance(input.Instances[0], input.Chunks[0], input.Species[0], input.ChunkSizeMeters,
                                input.OriginX, input.OriginZ, decoded));
    CHECK(std::abs(decoded.PositionX - value.PositionX) <= input.ChunkSizeMeters / 65535.0);
    CHECK(std::abs(decoded.PositionZ - value.PositionZ) <= input.ChunkSizeMeters / 65535.0);
    CHECK(std::abs(decoded.PositionY - value.PositionY) <= input.ChunkSizeMeters / 65535.0);
    CHECK(std::abs(decoded.YawRadians - (value.YawRadians + 6.283185307179586f)) < 0.010471976f);
    CHECK(std::abs(decoded.Scale - value.Scale) < 0.01f && decoded.Rank == value.Rank);
    CHECK(input.Instances[0].NormalOct[0] == 173 && input.Instances[0].NormalOct[1] == 237);
    const float normalLength = std::sqrt(0.2f * 0.2f + 0.9f * 0.9f + 0.3f * 0.3f);
    CHECK((decoded.Normal[0] * value.Normal[0] + decoded.Normal[1] * value.Normal[1] +
           decoded.Normal[2] * value.Normal[2]) / normalLength > 0.999f);
    for (size_t i = 0; i < 3; ++i)
    {
        CHECK(std::abs(decoded.Tint[i] - value.Tint[i]) <= 1.0f / 255.0f);
    }
    CHECK(std::abs(decoded.Dry - value.Dry) <= 1.0f / 255.0f);

    using Status = CookedScatterParseStatus;
    {
        ByteArray damaged = bytes;
        damaged.resize(HeaderSize - 1);
        CheckRejection(damaged, Status::TruncatedHeader);
    }
    {
        ByteArray damaged = bytes;
        WriteLe(damaged, HeaderOffset::InstanceOffset, ReadLe(damaged, HeaderOffset::InstanceOffset, 8) + 1, 8);
        UpdateHash(damaged);
        CheckRejection(damaged, Status::TableOutOfRange);
    }
    {
        ByteArray damaged = bytes;
        damaged[HeaderOffset::PayloadHash] ^= 1;
        CheckRejection(damaged, Status::HashMismatch);
    }
    {
        ByteArray damaged = bytes;
        const size_t chunkOffset = static_cast<size_t>(ReadLe(damaged, HeaderOffset::ChunkTableOffset, 8));
        WriteLe(damaged, chunkOffset + ChunkRecordOffset::ChunkX, input.GridCountX, 4);
        UpdateHash(damaged);
        CheckRejection(damaged, Status::ChunkOutOfRange);
    }
    {
        ByteArray damaged = bytes;
        const size_t speciesOffset = static_cast<size_t>(ReadLe(damaged, HeaderOffset::SpeciesTableOffset, 8));
        WriteLe(damaged, speciesOffset + SpeciesRecordOffset::LodCount, MaxLodCount + 1, 4);
        UpdateHash(damaged);
        CheckRejection(damaged, Status::SpeciesOutOfRange);
    }
    CheckSpecCookPath();
    std::cout << "CookedScatterTest 成功\n";
    return 0;
}
