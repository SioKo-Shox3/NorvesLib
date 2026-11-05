// 配置の順序を保ち、候補と統計を NVSCT v0 の表にまとめる。
#include "ScatterBaker.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace NorvesLib::Tools::AssetCook
{
    using Core::Container::AnsiString;
    using Core::Container::Span;
    using Core::Container::VariableArray;
    namespace Asset = Core::Asset;

    namespace
    {
        bool Fail(AnsiString& error, const char8_t* message)
        {
            error = reinterpret_cast<const char*>(message);
            return false;
        }

        float Bound(double value, bool bUpper)
        {
            // float への丸めで元の候補を囲む範囲が縮まらないようにする。
            float result = static_cast<float>(value);
            if ((bUpper && result < value) || (!bUpper && result > value))
            {
                result = std::nextafter(result, bUpper ? std::numeric_limits<float>::infinity() :
                                                        -std::numeric_limits<float>::infinity());
            }
            return result;
        }
    } // namespace

    bool BakeScatter(const ScatterBakeDesc& desc, const Asset::CookedTerrainData& terrain,
                     Span<const ScatterChunkCoord> onlyChunks, VariableArray<uint8_t>& outBytes,
                     ScatterBakeStats* stats, AnsiString& error)
    {
        outBytes.clear();
        error.clear();
        if (stats)
        {
            *stats = {};
        }
        const auto& params = desc.Params;
        const double tileSize = static_cast<double>(terrain.TileCells) * terrain.CellSizeM;
        const double ratio = tileSize / params.ChunkSizeM;
        if (!std::isfinite(params.ChunkSizeM) || params.ChunkSizeM <= 0.0 ||
            !std::isfinite(ratio) || ratio < 1.0 - 1e-9 ||
            std::abs(ratio - std::round(ratio)) > 1e-9 * std::abs(ratio) ||
            params.OriginX != terrain.OriginX || params.OriginZ != terrain.OriginZ)
        {
            return Fail(error, u8"散布のチャンク寸法は地形のタイル寸法の約数、原点は地形と同じ値が必要です");
        }
        if (desc.Species.empty() || desc.Species.size() > Asset::CookedScatterFormatV0::MaxSpeciesCount)
        {
            return Fail(error, u8"散布の種数が形式の範囲外です");
        }
        const double gridX = std::ceil(static_cast<double>(terrain.CellCountX) * terrain.CellSizeM / params.ChunkSizeM);
        const double gridZ = std::ceil(static_cast<double>(terrain.CellCountZ) * terrain.CellSizeM / params.ChunkSizeM);
        if (!std::isfinite(gridX) || !std::isfinite(gridZ) || gridX < 1 || gridZ < 1 ||
            gridX > INT32_MAX || gridZ > INT32_MAX)
        {
            return Fail(error, u8"散布のチャンク格子が配置の座標範囲外です");
        }
        Asset::CookedScatterWriteDesc output;
        output.Seed = params.Seed;
        output.TerrainCookedHash = desc.TerrainCookedHash;
        output.ChunkSizeMeters = static_cast<float>(params.ChunkSizeM);
        output.OriginX = terrain.OriginX;
        output.OriginZ = terrain.OriginZ;
        output.GridCountX = static_cast<uint32_t>(gridX);
        output.GridCountZ = static_cast<uint32_t>(gridZ);
        if (!std::isfinite(output.ChunkSizeMeters) || output.ChunkSizeMeters <= 0.0f)
        {
            return Fail(error, u8"散布のチャンク寸法を形式の float で表せません");
        }
        for (const auto& rule : desc.Species)
        {
            if (rule.LodMeshLogicalPaths.empty() || rule.LodMeshLogicalPaths.size() > Asset::CookedScatterFormatV0::MaxLodCount ||
                !std::isfinite(rule.ScaleMin) || !std::isfinite(rule.ScaleMax) || rule.ScaleMin <= 0 || rule.ScaleMax < rule.ScaleMin)
            {
                return Fail(error, u8"散布の LOD 数または拡大範囲が不正です");
            }
            Asset::CookedScatterSpecies species;
            species.Name = rule.Name;
            species.RenderPath = rule.RenderPath;
            species.LodCount = static_cast<uint32_t>(rule.LodMeshLogicalPaths.size());
            for (uint32_t lod = 0; lod < species.LodCount; ++lod)
            {
                species.LodPaths[lod] = rule.LodMeshLogicalPaths[lod];
            }
            species.MeshRadiusMeters = static_cast<float>(rule.MeshRadiusM);
            species.MinSpacingMeters = static_cast<float>(ResolveScatterMinSpacing(rule));
            species.ScaleMin = static_cast<float>(rule.ScaleMin);
            species.ScaleMax = static_cast<float>(rule.ScaleMax);
            species.SlopeFollow = static_cast<float>(rule.SlopeFollow);
            species.BrightnessVariation = static_cast<float>(rule.BrightnessVariation);
            species.WindStiffness = static_cast<float>(rule.WindStiffness);
            species.WindPhaseSeed = static_cast<uint32_t>(rule.WindPhaseSeed);
            species.Flags = rule.bCollision ? 1u : 0u;
            output.Species.push_back(species);
        }
        VariableArray<ScatterChunkCoord> chunks;
        if (onlyChunks.empty())
        {
            if (gridX * gridZ > UINT32_MAX)
            {
                return Fail(error, u8"散布の全チャンク数が形式の範囲外です");
            }
            for (uint32_t z = 0; z < output.GridCountZ; ++z)
            {
                for (uint32_t x = 0; x < output.GridCountX; ++x)
                {
                    chunks.push_back({static_cast<int32_t>(x), static_cast<int32_t>(z)});
                }
            }
        }
        else
        {
            for (const auto& coord : onlyChunks)
            {
                if (coord.ChunkX < 0 || coord.ChunkZ < 0 || coord.ChunkX >= gridX || coord.ChunkZ >= gridZ)
                {
                    return Fail(error, u8"選択した散布チャンクが地形の格子外です");
                }
                chunks.push_back(coord);
            }
            std::sort(chunks.begin(), chunks.end(), [](const auto& a, const auto& b)
            {
                return a.ChunkZ != b.ChunkZ ? a.ChunkZ < b.ChunkZ : a.ChunkX < b.ChunkX;
            });
            const auto end = std::unique(chunks.begin(), chunks.end(), [](const auto& a, const auto& b)
            {
                return a.ChunkX == b.ChunkX && a.ChunkZ == b.ChunkZ;
            });
            chunks.resize(static_cast<size_t>(end - chunks.begin()));
        }
        ScatterTerrainSampler sampler;
        if (!sampler.Initialize(terrain, error))
        {
            return false;
        }
        ScatterBakeStats result;
        result.Species.resize(desc.Species.size());
        VariableArray<VariableArray<ScatterCandidate>> candidates;
        candidates.resize(desc.Species.size());
        for (const auto& coord : chunks)
        {
            Asset::CookedScatterChunk chunk;
            chunk.ChunkX = static_cast<uint32_t>(coord.ChunkX);
            chunk.ChunkZ = static_cast<uint32_t>(coord.ChunkZ);
            bool bHasBounds = false;
            for (size_t s = 0; s < desc.Species.size(); ++s)
            {
                ScatterPlacementStats placement;
                if (!PlaceScatterChunk(params, desc.Species[s], sampler, coord.ChunkX, coord.ChunkZ, candidates[s], &placement, error))
                {
                    return false;
                }
                auto& total = result.Species[s];
                total.InstanceCount += candidates[s].size();
                total.ExaminedCells += placement.ExaminedCells;
                total.ThinnedCount += placement.ThinnedCount;
                total.bDensitySaturated |= placement.bDensitySaturated;
                for (const auto& candidate : candidates[s])
                {
                    const double position[] = {candidate.X, candidate.Y, candidate.Z};
                    for (size_t axis = 0; axis < 3; ++axis)
                    {
                        const float lower = Bound(position[axis], false), upper = Bound(position[axis], true);
                        chunk.AabbMin[axis] = bHasBounds ? std::min(chunk.AabbMin[axis], lower) : lower;
                        chunk.AabbMax[axis] = bHasBounds ? std::max(chunk.AabbMax[axis], upper) : upper;
                    }
                    bHasBounds = true;
                }
            }
            chunk.MinY = chunk.AabbMin[1];
            chunk.MaxY = chunk.AabbMax[1];
            output.Chunks.push_back(chunk);
            for (size_t s = 0; s < desc.Species.size(); ++s)
            {
                output.SpeciesRanges.push_back({output.Instances.size(), candidates[s].size()});
                for (const auto& candidate : candidates[s])
                {
                    Asset::ScatterInstanceValue value;
                    value.PositionX = candidate.X;
                    value.PositionY = candidate.Y;
                    value.PositionZ = candidate.Z;
                    value.YawRadians = static_cast<float>(candidate.Yaw);
                    value.Scale = static_cast<float>(candidate.Scale);
                    value.Rank = candidate.Rank;
                    for (size_t axis = 0; axis < 3; ++axis)
                    {
                        value.Normal[axis] = candidate.Normal[axis];
                        value.Tint[axis] = candidate.Tint[axis] / 255.0f;
                    }
                    value.Dry = candidate.Dry / 255.0f;
                    Asset::InstanceRecord16 record;
                    if (!Asset::EncodeScatterInstance(value, chunk, output.Species[s], output.ChunkSizeMeters,
                                                       output.OriginX, output.OriginZ, record))
                    {
                        return Fail(error, u8"散布の候補を NVSCT のレコードへ量子化できません");
                    }
                    output.Instances.push_back(record);
                }
            }
        }
        if (Asset::WriteCookedScatter(output, outBytes) != Asset::CookedScatterWriteStatus::Success)
        {
            return Fail(error, u8"散布の表を NVSCT v0 へ書き出せません");
        }
        if (stats)
        {
            *stats = result;
        }
        return true;
    }
} // namespace NorvesLib::Tools::AssetCook
