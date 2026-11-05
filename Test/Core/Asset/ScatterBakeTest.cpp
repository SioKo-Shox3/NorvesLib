// 散布の決定性・独立性・間隔・密度・除外・量子化の契約を確かめる。
#include "Tools/AssetCook/ScatterBaker.h"
#include "Tools/AssetCook/TerrainMasks.h"
#include "Tools/AssetCook/TextureCooker.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>

using namespace NorvesLib::Core::Asset;
using namespace NorvesLib::Tools::AssetCook;
using NorvesLib::Core::Container::AnsiString;
using NorvesLib::Core::Container::Span;
using NorvesLib::Core::Container::VariableArray;
using ByteArray = VariableArray<uint8_t>;

#define CHECK(expression) \
    do \
    { \
        if (!(expression)) \
        { \
            std::cerr << Utf8(u8"検査失敗: ") << #expression << Utf8(u8" 行 ") << __LINE__ << "\n"; \
            std::exit(1); \
        } \
    } while (false)

namespace
{
    const char* Utf8(const char8_t* value)
    {
        return reinterpret_cast<const char*>(value);
    }

    constexpr double Pi = 3.14159265358979323846;

    CookedTerrainData MakeTerrain(uint32_t cellsX, uint32_t cellsZ, uint32_t tileCells, bool bRelief)
    {
        // 各タイルの端でも全体座標から同じ標本を作る。高さとマスクの双方を実際の形式へ書く。
        CookedTerrainWriteDesc desc;
        desc.CellCountX = cellsX;
        desc.CellCountZ = cellsZ;
        desc.TileCells = tileCells;
        desc.LodCount = 1;
        desc.CellSizeM = 1.0f;
        desc.OriginX = 123.25;
        desc.OriginZ = -64.5;
        desc.OriginY = 1.25;
        desc.HeightMinM = 0;
        desc.HeightMaxM = 64;
        desc.Layers.resize(2);
        desc.Layers[0].FourCC = CookedTerrainFormatV0::HeightLayerFourCC;
        desc.Layers[0].Encoding = CookedTerrainFormatV0::LayerEncodingRawHeight16;
        desc.Layers[0].PixelFormat = CookedTexturePixelFormat::R16UNorm;
        desc.Layers[0].Semantic = "height";
        desc.Layers[1].FourCC = TerrainMaskLayerFourCC;
        desc.Layers[1].Encoding = CookedTerrainFormatV0::LayerEncodingEmbeddedNvtex;
        desc.Layers[1].PixelFormat = CookedTexturePixelFormat::RGBA8UNorm;
        desc.Layers[1].Semantic = TerrainMaskSemantic;
        const uint32_t countX = cellsX / tileCells, countZ = cellsZ / tileCells;
        const uint32_t side = tileCells + 1;
        const size_t count = static_cast<size_t>(countX) * countZ;
        VariableArray<ByteArray> heights(count), masks(count);
        desc.Tiles.resize(count);
        AnsiString error;
        for (uint32_t tz = 0; tz < countZ; ++tz)
        {
            for (uint32_t tx = 0; tx < countX; ++tx)
            {
                const size_t index = static_cast<size_t>(tz) * countX + tx;
                auto& height = heights[index];
                height.resize(static_cast<size_t>(side) * side * 2);
                ByteArray pixels(static_cast<size_t>(side) * side * 4);
                for (uint32_t z = 0; z < side; ++z)
                {
                    for (uint32_t x = 0; x < side; ++x)
                    {
                        const uint32_t gx = tx * tileCells + x, gz = tz * tileCells + z;
                        const double h = !bRelief || gx < 8 ? 0.0 :
                            (gx < 20 ? (gx - 8) * 0.2 : 2.4 + (gx - 20) * 0.8);
                        const uint16_t encoded = static_cast<uint16_t>(std::lround(h * 65535.0 / 64.0));
                        const size_t sample = static_cast<size_t>(z) * side + x;
                        height[sample * 2] = static_cast<uint8_t>(encoded);
                        height[sample * 2 + 1] = static_cast<uint8_t>(encoded >> 8);
                        pixels[sample * 4] = static_cast<uint8_t>(gx * 255 / cellsX);
                        pixels[sample * 4 + 1] = static_cast<uint8_t>((gx * 7 + gz * 11) % 256);
                        pixels[sample * 4 + 2] = gz >= 12 && gz <= 18 ? 255 : 0;
                        pixels[sample * 4 + 3] = 0;
                    }
                }
                TextureCookResult texture;
                CHECK(CookRgba8ToNvtex(Span<const uint8_t>(pixels), side, side, "nvtex.v0.rgba8.linear", texture, error));
                masks[index] = texture.NvtexBytes;
                auto& tile = desc.Tiles[index];
                tile.TileX = tx;
                tile.TileZ = tz;
                tile.HeightMinM = 0;
                tile.HeightMaxM = 64;
                tile.AabbMin[0] = static_cast<float>(tx * tileCells);
                tile.AabbMin[2] = static_cast<float>(tz * tileCells);
                tile.AabbMax[0] = static_cast<float>((tx + 1) * tileCells);
                tile.AabbMax[1] = 64;
                tile.AabbMax[2] = static_cast<float>((tz + 1) * tileCells);
                tile.LayerBytes.push_back(Span<const uint8_t>(height));
                tile.LayerBytes.push_back(Span<const uint8_t>(masks[index]));
            }
        }
        ByteArray bytes;
        CHECK(WriteCookedTerrain(desc, bytes) == CookedTerrainWriteStatus::Success);
        auto parsed = ParseCookedTerrain(AssetBlob::CopyBytes(Span<const uint8_t>(bytes)));
        CHECK(parsed.Succeeded());
        return parsed.Terrain;
    }

    ScatterBakeDesc MakeDesc(const CookedTerrainData& terrain, double chunkSize = 16.0)
    {
        ScatterBakeDesc desc;
        desc.TerrainCookedHash = terrain.PayloadHash;
        desc.Params.OriginX = terrain.OriginX;
        desc.Params.OriginZ = terrain.OriginZ;
        desc.Params.ChunkSizeM = chunkSize;
        desc.Params.Seed = 1234567;
        ScatterSpeciesRule rule;
        rule.Name = "stone";
        rule.LodMeshLogicalPaths.push_back("Meshes/Stone0");
        rule.LodMeshLogicalPaths.push_back("Meshes/Stone1");
        rule.MinSpacingM = 1;
        rule.BaseDensityPerM2 = 0.2;
        desc.Species.push_back(rule);
        return desc;
    }

    ByteArray Bake(const ScatterBakeDesc& desc, const CookedTerrainData& terrain,
                   Span<const ScatterChunkCoord> chunks = {}, ScatterBakeStats* stats = nullptr)
    {
        ByteArray bytes;
        AnsiString error;
        const bool bSuccess = BakeScatter(desc, terrain, chunks, bytes, stats, error);
        if (!bSuccess)
        {
            std::cerr << Utf8(u8"散布の焼き込み失敗: ") << error.c_str() << "\n";
        }
        CHECK(bSuccess && error.empty());
        return bytes;
    }

    CookedScatterData Parse(const ByteArray& bytes)
    {
        auto parsed = ParseCookedScatter(AssetBlob::CopyBytes(Span<const uint8_t>(bytes)));
        CHECK(parsed.Succeeded());
        return parsed.Scatter;
    }

    InstanceRecord16 Record(Span<const uint8_t> bytes, size_t index)
    {
        InstanceRecord16 record;
        CHECK(ReadScatterInstance(Span<const uint8_t>(bytes.data() + index * 16, 16), record));
        return record;
    }

    void CheckDeterminism()
    {
        const auto terrain = MakeTerrain(32, 32, 16, true);
        auto desc = MakeDesc(terrain);
        const auto first = Bake(desc, terrain);
        CHECK(first == Bake(desc, terrain));
        ++desc.Params.Seed;
        CHECK(first != Bake(desc, terrain));
        std::cout << Utf8(u8"(a) 同じ入力のバイト一致と seed による差を確認\n");
    }

    void CheckIndependentChunk()
    {
        const auto terrain = MakeTerrain(64, 96, 16, true);
        const auto desc = MakeDesc(terrain);
        const auto all = Parse(Bake(desc, terrain));
        const ScatterChunkCoord selected[] = {{3, 5}};
        const auto one = Parse(Bake(desc, terrain, Span<const ScatterChunkCoord>(selected)));
        CHECK(all.GridCountX == 4 && all.GridCountZ == 6 && all.Chunks.size() == 24 && one.Chunks.size() == 1);
        const auto& a = all.Chunks[23];
        const auto& b = one.Chunks[0];
        CHECK(a.ChunkX == 3 && a.ChunkZ == 5 && b.ChunkX == 3 && b.ChunkZ == 5);
        CHECK(a.MinY == b.MinY && a.MaxY == b.MaxY);
        for (size_t axis = 0; axis < 3; ++axis)
        {
            CHECK(a.AabbMin[axis] == b.AabbMin[axis] && a.AabbMax[axis] == b.AabbMax[axis]);
        }
        const auto whole = all.GetInstances(23, 0), part = one.GetInstances(0, 0);
        CHECK(!part.empty() && whole.size() == part.size());
        CHECK(std::memcmp(whole.data(), part.data(), part.size()) == 0);
        for (size_t i = 0; i < all.Chunks.size(); ++i)
        {
            CHECK(all.Chunks[i].ChunkX == i % 4 && all.Chunks[i].ChunkZ == i / 4);
        }
        std::cout << Utf8(u8"(b) チャンク (3,5) のレコード・Y 範囲・AABB が全体と一致\n");
    }

    void CheckSpacing()
    {
        const auto terrain = MakeTerrain(32, 32, 16, false);
        auto desc = MakeDesc(terrain);
        auto& rule = desc.Species[0];
        rule.MinSpacingM = 2;
        rule.BaseDensityPerM2 = 2;
        ScatterBakeStats stats;
        const auto baked = Parse(Bake(desc, terrain, {}, &stats));
        CHECK(baked.Chunks.size() == 4 && stats.Species[0].bDensitySaturated);
        ScatterTerrainSampler sampler;
        AnsiString error;
        CHECK(sampler.Initialize(terrain, error));
        VariableArray<ScatterCandidate> all;
        VariableArray<size_t> owners;
        for (size_t c = 0; c < baked.Chunks.size(); ++c)
        {
            VariableArray<ScatterCandidate> candidates;
            CHECK(PlaceScatterChunk(desc.Params, rule, sampler, baked.Chunks[c].ChunkX, baked.Chunks[c].ChunkZ,
                                    candidates, nullptr, error));
            CHECK(candidates.size() * 16 == baked.GetInstances(static_cast<uint32_t>(c), 0).size());
            for (const auto& candidate : candidates)
            {
                all.push_back(candidate);
                owners.push_back(c);
            }
        }
        uint64_t pairs = 0, crossPairs = 0, closeCrossPairs = 0;
        for (size_t i = 0; i < all.size(); ++i)
        {
            for (size_t j = i + 1; j < all.size(); ++j)
            {
                const double dx = all[i].X - all[j].X, dz = all[i].Z - all[j].Z;
                const double distanceSquared = dx * dx + dz * dz;
                ++pairs;
                CHECK(distanceSquared >= rule.MinSpacingM * rule.MinSpacingM);
                if (owners[i] != owners[j])
                {
                    ++crossPairs;
                    if (distanceSquared < 4 * rule.MinSpacingM * rule.MinSpacingM)
                    {
                        ++closeCrossPairs;
                    }
                }
            }
        }
        CHECK(pairs > 0 && closeCrossPairs > 0);
        std::cout << Utf8(u8"(c) 全組=") << pairs << Utf8(u8" 境をまたぐ組=") << crossPairs
                  << Utf8(u8" 境をまたぐ 2r 未満の組=") << closeCrossPairs << "\n";
    }

    void CheckDensity()
    {
        const auto terrain = MakeTerrain(64, 64, 64, false);
        auto desc = MakeDesc(terrain, 64);
        desc.Species[0].BaseDensityPerM2 = 2;
        desc.Species[0].MinSpacingM = 0.15;
        // 逆変換の近似が有効な低占有率 rho*pi*r^2 = 約 0.141 を使う。
        const uint64_t seeds[] = {17, 2027, 8191, 65537, 104729, 1000003, 0x123456789abcdef0ull,
                                  0xdeadbeef99887766ull, 0x3141592653589793ull, 0xfedcba9876543210ull};
        uint64_t total = 0;
        std::cout << "(d) r=" << desc.Species[0].MinSpacingM << " m\n";
        for (uint64_t seed : seeds)
        {
            desc.Params.Seed = seed;
            ScatterBakeStats stats;
            const auto scatter = Parse(Bake(desc, terrain, {}, &stats));
            const uint64_t count = scatter.SpeciesRanges[0].Count;
            CHECK(count == stats.Species[0].InstanceCount && !stats.Species[0].bDensitySaturated);
            CHECK(stats.Species[0].ExaminedCells > count && stats.Species[0].ThinnedCount > 0);
            total += count;
            std::cout << "  seed=" << seed << Utf8(u8" 本数=") << count << "\n";
        }
        const double mean = total / 10.0;
        std::cout << Utf8(u8"  平均=") << mean << Utf8(u8" 期待値=8192\n");
        CHECK(std::abs(mean - 8192) <= 8192 * 0.05);
    }

    void CheckExclusion()
    {
        const auto terrain = MakeTerrain(32, 32, 16, true);
        auto desc = MakeDesc(terrain);
        auto& rule = desc.Species[0];
        rule.BaseDensityPerM2 = 1;
        rule.MinSpacingM = 0.5;
        rule.bHeightBandEnabled = true;
        rule.HeightMinM = 1.7;
        rule.HeightMaxM = 5;
        rule.bSlopeBandEnabled = true;
        rule.SlopeMinDeg = 5;
        rule.SlopeMaxDeg = 20;
        rule.bExcludeRoad = true;
        rule.RoadThreshold = 0.5;
        rule.bWetnessBandEnabled = true;
        rule.WetnessMin = 0.3;
        rule.WetnessMax = 0.7;
        ScatterTerrainSampler sampler;
        AnsiString error;
        CHECK(sampler.Initialize(terrain, error));
        const auto violates = [&](const ScatterInstanceValue& instance, uint64_t (&outside)[4])
        {
            double height = 0;
            float normal[3], road = 0, wetness = 0;
            CHECK(sampler.SampleHeight(instance.PositionX, instance.PositionZ, height));
            CHECK(sampler.SampleNormal(instance.PositionX, instance.PositionZ, normal));
            CHECK(sampler.SampleLayerChannel(TerrainMaskLayerFourCC, 2, instance.PositionX, instance.PositionZ, road));
            CHECK(sampler.SampleLayerChannel(TerrainMaskLayerFourCC, 0, instance.PositionX, instance.PositionZ, wetness));
            const double slope = std::atan2(std::hypot(normal[0], normal[2]), normal[1]) * 180 / Pi;
            outside[0] += height < rule.HeightMinM || height > rule.HeightMaxM;
            outside[1] += slope < rule.SlopeMinDeg || slope > rule.SlopeMaxDeg;
            outside[2] += road >= rule.RoadThreshold;
            outside[3] += wetness < rule.WetnessMin || wetness > rule.WetnessMax;
        };
        const auto inspect = [&](const CookedScatterData& scatter, uint64_t (&outside)[4])
        {
            uint64_t count = 0;
            for (size_t c = 0; c < scatter.Chunks.size(); ++c)
            {
                const auto bytes = scatter.GetInstances(static_cast<uint32_t>(c), 0);
                for (size_t i = 0; i < bytes.size() / 16; ++i)
                {
                    ScatterInstanceValue instance;
                    CHECK(DecodeScatterInstance(Record(bytes, i), scatter.Chunks[c], scatter.Species[0],
                                                scatter.ChunkSizeMeters, scatter.OriginX, scatter.OriginZ, instance));
                    violates(instance, outside);
                    ++count;
                }
            }
            return count;
        };
        uint64_t excludedOutside[4] = {}, enabledOutside[4] = {};
        const uint64_t enabled = inspect(Parse(Bake(desc, terrain)), enabledOutside);
        rule.bHeightBandEnabled = rule.bSlopeBandEnabled = rule.bExcludeRoad = rule.bWetnessBandEnabled = false;
        const uint64_t unrestricted = inspect(Parse(Bake(desc, terrain)), excludedOutside);
        CHECK(enabled > 0 && unrestricted > enabled);
        for (size_t i = 0; i < 4; ++i)
        {
            CHECK(enabledOutside[i] == 0 && excludedOutside[i] > 0);
        }
        std::cout << Utf8(u8"(e) 除外あり=") << enabled << Utf8(u8" 除外なし=") << unrestricted
                  << Utf8(u8" 除外なしの範囲外: 標高=") << excludedOutside[0] << Utf8(u8" 傾斜=") << excludedOutside[1]
                  << Utf8(u8" 道=") << excludedOutside[2] << Utf8(u8" 湿り=") << excludedOutside[3] << "\n";
    }

    void CheckQuantization()
    {
        const auto terrain = MakeTerrain(32, 32, 16, true);
        auto desc = MakeDesc(terrain);
        auto& first = desc.Species[0];
        first.ScaleMin = 0.6;
        first.ScaleMax = 1.7;
        first.bUseDryGrass = true;
        first.BrightnessVariation = 0.7;
        first.SlopeFollow = 0.35;
        first.WindStiffness = 0.75;
        first.WindPhaseSeed = 0x123456789abcdef0ull;
        first.bCollision = true;
        ScatterSpeciesRule second = first;
        second.Name = "shrub";
        second.RenderPath = ScatterRenderPath::Foliage;
        second.MinSpacingM = 0;
        second.MeshRadiusM = 0.3;
        desc.Species.push_back(second);
        const auto scatter = Parse(Bake(desc, terrain));
        CHECK(scatter.TerrainCookedHash == desc.TerrainCookedHash && scatter.Seed == desc.Params.Seed);
        ScatterTerrainSampler sampler;
        AnsiString error;
        CHECK(sampler.Initialize(terrain, error));
        uint64_t checked = 0;
        for (size_t s = 0; s < desc.Species.size(); ++s)
        {
            const auto& source = desc.Species[s];
            const auto& cooked = scatter.Species[s];
            CHECK(cooked.Name == source.Name && cooked.RenderPath == source.RenderPath);
            CHECK(cooked.LodCount == source.LodMeshLogicalPaths.size());
            for (size_t lod = 0; lod < cooked.LodCount; ++lod)
            {
                CHECK(cooked.LodPaths[lod] == source.LodMeshLogicalPaths[lod]);
            }
            CHECK(cooked.MeshRadiusMeters == static_cast<float>(source.MeshRadiusM));
            CHECK(cooked.MinSpacingMeters == static_cast<float>(ResolveScatterMinSpacing(source)));
            CHECK(cooked.ScaleMin == static_cast<float>(source.ScaleMin) && cooked.ScaleMax == static_cast<float>(source.ScaleMax));
            CHECK(cooked.SlopeFollow == static_cast<float>(source.SlopeFollow));
            CHECK(cooked.BrightnessVariation == static_cast<float>(source.BrightnessVariation));
            CHECK(cooked.WindStiffness == static_cast<float>(source.WindStiffness));
            CHECK(cooked.WindPhaseSeed == static_cast<uint32_t>(source.WindPhaseSeed) && cooked.Flags == 1);
            for (size_t c = 0; c < scatter.Chunks.size(); ++c)
            {
                const auto& chunk = scatter.Chunks[c];
                VariableArray<ScatterCandidate> candidates;
                CHECK(PlaceScatterChunk(desc.Params, source, sampler, chunk.ChunkX, chunk.ChunkZ, candidates, nullptr, error));
                const auto bytes = scatter.GetInstances(static_cast<uint32_t>(c), static_cast<uint32_t>(s));
                CHECK(bytes.size() == candidates.size() * 16 && !candidates.empty());
                uint8_t previousRank = 0;
                for (size_t i = 0; i < candidates.size(); ++i)
                {
                    const auto record = Record(bytes, i);
                    const auto& candidate = candidates[i];
                    ScatterInstanceValue decoded;
                    CHECK(DecodeScatterInstance(record, chunk, cooked, scatter.ChunkSizeMeters, scatter.OriginX, scatter.OriginZ, decoded));
                    CHECK(std::abs(decoded.PositionX - candidate.X) <= desc.Params.ChunkSizeM / 65535);
                    CHECK(std::abs(decoded.PositionZ - candidate.Z) <= desc.Params.ChunkSizeM / 65535);
                    CHECK(std::abs(decoded.PositionY - candidate.Y) <= (static_cast<double>(chunk.MaxY) - chunk.MinY) / 65535);
                    const double yawError = std::abs(std::remainder(decoded.YawRadians - candidate.Yaw, 2 * Pi));
                    CHECK(yawError <= 0.6 * Pi / 180);
                    CHECK(record.Rank == candidate.Rank && record.Rank >= previousRank);
                    CHECK(record.Dry == candidate.Dry);
                    for (size_t axis = 0; axis < 3; ++axis)
                    {
                        CHECK(record.Tint[axis] == candidate.Tint[axis]);
                    }
                    previousRank = record.Rank;
                    ++checked;
                }
            }
        }
        // 平面のゼロ幅 Y と、候補が無いチャンクも有効な表として残る。
        const auto flat = MakeTerrain(16, 16, 16, false);
        auto flatDesc = MakeDesc(flat);
        auto flatScatter = Parse(Bake(flatDesc, flat));
        CHECK(flatScatter.Chunks[0].MinY == flatScatter.Chunks[0].MaxY && flatScatter.SpeciesRanges[0].Count > 0);
        const auto flatBytes = flatScatter.GetInstances(0, 0);
        for (size_t i = 0; i < flatBytes.size() / 16; ++i)
        {
            ScatterInstanceValue decoded;
            CHECK(DecodeScatterInstance(Record(flatBytes, i), flatScatter.Chunks[0], flatScatter.Species[0],
                                        flatScatter.ChunkSizeMeters, flatScatter.OriginX, flatScatter.OriginZ, decoded));
            CHECK(decoded.PositionY == flatScatter.Chunks[0].MinY);
        }
        flatDesc.Species[0].BaseDensityPerM2 = 0;
        flatScatter = Parse(Bake(flatDesc, flat));
        CHECK(flatScatter.Chunks.size() == 1 && flatScatter.SpeciesRanges[0].Count == 0);
        CHECK(flatScatter.Chunks[0].MinY == 0 && flatScatter.Chunks[0].MaxY == 0);
        for (size_t axis = 0; axis < 3; ++axis)
        {
            CHECK(flatScatter.Chunks[0].AabbMin[axis] == 0 && flatScatter.Chunks[0].AabbMax[axis] == 0);
        }
        std::cout << Utf8(u8"(f) 復号誤差・Rank・色と枯れ度の往復を確認: 本数=") << checked << "\n";
    }
} // namespace

int main()
{
    CheckDeterminism();
    CheckIndependentChunk();
    CheckSpacing();
    CheckDensity();
    CheckExclusion();
    CheckQuantization();
    std::cout << Utf8(u8"ScatterBakeTest 完了\n");
    return 0;
}
