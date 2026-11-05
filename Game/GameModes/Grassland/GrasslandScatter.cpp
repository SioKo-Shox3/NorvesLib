// 草原のクック済み散布と、開発用の合成散布を描画先へ結び付ける。
#include "GameModes/Grassland/GrasslandScatter.h"

#include "Core/Public/Asset/AssetSystem.h"
#include "Core/Public/Asset/CookedMeshFormat.h"
#include "Core/Public/Asset/CookedScatterFormat.h"
#include "Core/Public/Engine/Engine.h"
#include "Core/Public/GameMode/GameModeContext.h"
#include "Core/Public/GameMode/GameModeScope.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Rendering/MegaGeometry/CookedMeshMegaMeshAdapter.h"
#include "Core/Public/Rendering/MegaGeometry/CookedMeshPageSource.h"
#include "Core/Public/Rendering/RenderResources.h"
#include "Core/Public/Rendering/Terrain/TerrainSystem.h"

#include <algorithm>
#include <cmath>
#include <utility>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Container;
using namespace NorvesLib::Core::Rendering;
using namespace NorvesLib::Core::Rendering::MegaGeometry;

namespace Game::GameModes
{
    namespace
    {
        constexpr const char* kScatterPath = "Assets/Terrain/Grassland/GrasslandScatter.nvsct";

        // 株の形と色は仮の値。草原の撮影で見え方を確かめて調整する。
        TSharedPtr<const FoliageMeshData> MakePlantMesh(float height, float halfWidth)
        {
            auto mesh = MakeShared<FoliageMeshData>();
            mesh->Height = height;
            constexpr uint32_t kStripCount = 4;
            constexpr float kPi = 3.14159265358979323846f;
            for (uint32_t strip = 0; strip < kStripCount; ++strip)
            {
                const float angle = kPi * static_cast<float>(strip) / static_cast<float>(kStripCount);
                const float sx = std::cos(angle) * halfWidth;
                const float sz = std::sin(angle) * halfWidth;
                const float phase = static_cast<float>(strip) / static_cast<float>(kStripCount);
                const uint32_t first = static_cast<uint32_t>(mesh->Vertices.size());
                for (uint32_t row = 0; row < 2; ++row)
                {
                    for (uint32_t side = 0; side < 2; ++side)
                    {
                        const float sign = side == 0 ? -1.0f : 1.0f;
                        FoliageVertex vertex;
                        vertex.Position[0] = sign * sx;
                        vertex.Position[1] = row == 0 ? 0.0f : height;
                        vertex.Position[2] = sign * sz;
                        vertex.Normal[0] = -std::sin(angle);
                        vertex.Normal[1] = 0.0f;
                        vertex.Normal[2] = std::cos(angle);
                        vertex.Side[0] = sign * sx;
                        vertex.Side[2] = sign * sz;
                        vertex.Flex = row == 0 ? 0.0f : 1.0f;
                        vertex.Tint = row == 0 ? 0.15f : 0.85f;
                        vertex.Occlusion = row == 0 ? 0.55f : 1.0f;
                        vertex.Phase = phase;
                        mesh->Vertices.push_back(vertex);
                    }
                }
                mesh->Indices.push_back(first);
                mesh->Indices.push_back(first + 1);
                mesh->Indices.push_back(first + 2);
                mesh->Indices.push_back(first + 1);
                mesh->Indices.push_back(first + 3);
                mesh->Indices.push_back(first + 2);
            }
            return mesh;
        }

        FoliageBatch MakePlantBatch(bool bShrub)
        {
            FoliageBatch batch;
            batch.Mesh = MakePlantMesh(bShrub ? 0.9f : 0.45f, bShrub ? 0.16f : 0.09f);
            batch.FadeStartDistance = bShrub ? 60.0f : 35.0f;
            batch.FadeEndDistance = bShrub ? 110.0f : 70.0f;
            batch.ShadowDistance = bShrub ? 25.0f : 0.0f;
            batch.StemColor[0] = 0.10f;
            batch.StemColor[1] = 0.13f;
            batch.StemColor[2] = 0.045f;
            batch.TipColorA[0] = 0.16f;
            batch.TipColorA[1] = 0.24f;
            batch.TipColorA[2] = 0.065f;
            batch.TipColorB[0] = 0.28f;
            batch.TipColorB[1] = 0.23f;
            batch.TipColorB[2] = 0.10f;
            batch.DebugName = bShrub ? "grassland_shrub" : "grassland_forb_clump";
            return batch;
        }

        MegaMeshHandle LoadMegaMesh(GameMode::GameModeContext& ctx, const Asset::AssetSystem& assetSystem,
                                    const Asset::CookedScatterSpecies& species)
        {
            const String path(species.LodPaths[0].c_str());
            const auto resolved = assetSystem.ResolveAsset(path, Asset::AssetKind::Model);
            if (!resolved.UsedCooked())
            {
                return {};
            }
            const auto parsed = Asset::ParseCookedMesh(resolved.Blob);
            MegaMeshCreateInfo info;
            if (!parsed.Succeeded() || !BuildMegaMeshCreateInfoFromCookedMesh(parsed.Mesh, info))
            {
                return {};
            }
            Asset::AssetCookedRange pageRange;
            if (parsed.Mesh.Pages.size() >= 2 && Asset::AssetSystem::TryMakeCookedRange(resolved, pageRange))
            {
                info.PageSource = MakeCookedMeshPageSource(parsed.Mesh, assetSystem.GetCookedFileReader(),
                                                           pageRange.Request, pageRange.BaseOffset);
            }
            info.DebugName = path.c_str();
            auto& resources = ctx.RenderResourcesRef.MegaGeometry();
            const MegaMeshHandle mesh = resources.CreateMegaMesh(info);
            if (!mesh.IsValid())
            {
                return {};
            }
            const ModelHandle model = resources.RegisterModel(mesh, path, path);
            if (!model.IsValid())
            {
                resources.ReleaseMegaMesh(mesh);
                return {};
            }
            ctx.ScopeRef.TrackModel(model);
            return mesh;
        }

#if NORVES_BUILD_DEVELOPMENT
        uint32_t Hash(uint32_t value)
        {
            value ^= value >> 16;
            value *= 0x7feb352du;
            value ^= value >> 15;
            value *= 0x846ca68bu;
            return value ^ (value >> 16);
        }

        float HashUnit(uint32_t value)
        {
            return (static_cast<float>(Hash(value) >> 16) + 0.5f) / 65536.0f;
        }

        bool MakeStressScatter(const Asset::CookedScatterData& source, uint32_t count,
                               const Terrain::TerrainSystem& terrain, Asset::CookedScatterData& out)
        {
            const uint32_t speciesCount = static_cast<uint32_t>(source.Species.size());
            const uint32_t chunkCount = static_cast<uint32_t>(source.Chunks.size());
            if (speciesCount == 0 || chunkCount == 0)
            {
                LOG_WARNING("GRASSLAND_SCATTER_STRESS 種またはチャンクがないため合成できません");
                return false;
            }
            Asset::CookedScatterWriteDesc desc;
            desc.Seed = source.Seed;
            desc.TerrainCookedHash = source.TerrainCookedHash;
            desc.ChunkSizeMeters = source.ChunkSizeMeters;
            desc.OriginX = source.OriginX;
            desc.OriginZ = source.OriginZ;
            desc.GridCountX = source.GridCountX;
            desc.GridCountZ = source.GridCountZ;
            desc.Species = source.Species;
            desc.Chunks = source.Chunks;
            desc.Instances.reserve(count);
            desc.SpeciesRanges.reserve(static_cast<size_t>(chunkCount) * speciesCount);
            for (uint32_t chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex)
            {
                auto& chunk = desc.Chunks[chunkIndex];
                VariableArray<VariableArray<Asset::ScatterInstanceValue>> values;
                values.resize(speciesCount);
                for (uint32_t speciesIndex = 0; speciesIndex < speciesCount; ++speciesIndex)
                {
                    const uint32_t speciesTotal = count / speciesCount + (speciesIndex < count % speciesCount ? 1u : 0u);
                    const uint32_t remainder = speciesTotal % chunkCount;
                    const uint32_t rotated = (chunkIndex + speciesIndex * chunkCount / speciesCount) % chunkCount;
                    const uint32_t extra = static_cast<uint32_t>(
                        (static_cast<uint64_t>(rotated + 1) * remainder) / chunkCount -
                        (static_cast<uint64_t>(rotated) * remainder) / chunkCount);
                    const uint32_t chunkInstances = speciesTotal / chunkCount + extra;
                    values[speciesIndex].reserve(chunkInstances);
                    const auto& species = desc.Species[speciesIndex];
                    for (uint32_t index = 0; index < chunkInstances; ++index)
                    {
                        const uint32_t key = Hash(chunkIndex ^ Hash(speciesIndex + 1u) ^ Hash(index + 1u));
                        Asset::ScatterInstanceValue value;
                        value.PositionX = static_cast<float>(source.OriginX +
                            (static_cast<double>(chunk.ChunkX) + HashUnit(key)) * source.ChunkSizeMeters);
                        value.PositionZ = static_cast<float>(source.OriginZ +
                            (static_cast<double>(chunk.ChunkZ) + HashUnit(key ^ 0x9e3779b9u)) * source.ChunkSizeMeters);
                        Terrain::GroundSample sample;
                        if (!terrain.SampleGround(static_cast<float>(value.PositionX), static_cast<float>(value.PositionZ), sample))
                        {
                            LOG_WARNING_F("GRASSLAND_SCATTER_STRESS chunk=%u 地面の高さを取得できません", chunkIndex);
                            return false;
                        }
                        value.PositionY = sample.Height;
                        value.Normal[0] = sample.Normal.x;
                        value.Normal[1] = sample.Normal.y;
                        value.Normal[2] = sample.Normal.z;
                        value.YawRadians = HashUnit(key ^ 0x243f6a88u) * 6.28318530718f;
                        value.Scale = species.ScaleMin + (species.ScaleMax - species.ScaleMin) * HashUnit(key ^ 0xb7e15162u);
                        value.Rank = static_cast<uint8_t>(Hash(index ^ Hash(chunkIndex) ^ Hash(speciesIndex)) & 255u);
                        const float position[3] = {static_cast<float>(value.PositionX), sample.Height,
                                                   static_cast<float>(value.PositionZ)};
                        for (uint32_t axis = 0; axis < 3; ++axis)
                        {
                            // 水平位置は量子化で動く分も、常駐判定の箱に含める。
                            const float margin = axis == 1 ? 0.0f : source.ChunkSizeMeters / 65535.0f;
                            chunk.AabbMin[axis] = (std::min)(chunk.AabbMin[axis], position[axis] - margin);
                            chunk.AabbMax[axis] = (std::max)(chunk.AabbMax[axis], position[axis] + margin);
                        }
                        chunk.MinY = (std::min)(chunk.MinY, sample.Height);
                        chunk.MaxY = (std::max)(chunk.MaxY, sample.Height);
                        values[speciesIndex].push_back(value);
                    }
                }
                for (uint32_t speciesIndex = 0; speciesIndex < speciesCount; ++speciesIndex)
                {
                    auto& instances = values[speciesIndex];
                    std::sort(instances.begin(), instances.end(), [](const auto& a, const auto& b)
                    {
                        return a.Rank < b.Rank;
                    });
                    desc.SpeciesRanges.push_back({desc.Instances.size(), instances.size()});
                    for (const auto& value : instances)
                    {
                        Asset::InstanceRecord16 record;
                        if (!Asset::EncodeScatterInstance(value, chunk, desc.Species[speciesIndex], desc.ChunkSizeMeters,
                                                          desc.OriginX, desc.OriginZ, record))
                        {
                            LOG_WARNING_F("GRASSLAND_SCATTER_STRESS chunk=%u 散布を量子化できません", chunkIndex);
                            return false;
                        }
                        desc.Instances.push_back(record);
                    }
                }
            }
            VariableArray<uint8_t> bytes;
            const auto status = Asset::WriteCookedScatter(desc, bytes);
            if (status != Asset::CookedScatterWriteStatus::Success)
            {
                LOG_WARNING_F("GRASSLAND_SCATTER_STRESS status=%u 散布を書き出せません", static_cast<uint32_t>(status));
                return false;
            }
            auto parsed = Asset::ParseCookedScatter(Asset::AssetBlob::CopyBytes({bytes.data(), bytes.size()}));
            if (!parsed.Succeeded())
            {
                LOG_WARNING_F("GRASSLAND_SCATTER_STRESS status=%s 散布を解析できません",
                              Asset::GetCookedScatterParseStatusName(parsed.Status));
                return false;
            }
            out = std::move(parsed.Scatter);
            LOG_INFO_F("GRASSLAND_SCATTER_STRESS instances=%u chunks=%u 合成した散布を読み込みます", count, chunkCount);
            return true;
        }
#endif
    } // namespace

    Scatter::ScatterHandle LoadGrasslandScatter(GameMode::GameModeContext& ctx, uint32_t stressCount)
    {
        const auto assetSystem = ctx.EngineRef.GetTerrainSystem().GetAssetSystem();
        if (!assetSystem)
        {
            LOG_WARNING_F("GRASSLAND_SCATTER path=%s 資産システムがないため散布を置きません", kScatterPath);
            return Scatter::kInvalidScatterHandle;
        }
        const auto resolved = assetSystem->ResolveAsset(String(kScatterPath), Asset::AssetKind::World);
        if (!resolved.UsedCooked())
        {
            LOG_WARNING_F("GRASSLAND_SCATTER path=%s status=%u 散布を読めません", kScatterPath,
                          static_cast<uint32_t>(resolved.Status));
            return Scatter::kInvalidScatterHandle;
        }
        auto parsed = Asset::ParseCookedScatter(resolved.Blob);
        if (!parsed.Succeeded())
        {
            LOG_WARNING_F("GRASSLAND_SCATTER path=%s status=%s 散布を解析できません", kScatterPath,
                          Asset::GetCookedScatterParseStatusName(parsed.Status));
            return Scatter::kInvalidScatterHandle;
        }
#if NORVES_BUILD_DEVELOPMENT
        if (stressCount > 0)
        {
            Asset::CookedScatterData stress;
            if (!MakeStressScatter(parsed.Scatter, stressCount, ctx.EngineRef.GetTerrainSystem(), stress))
            {
                return Scatter::kInvalidScatterHandle;
            }
            parsed.Scatter = std::move(stress);
        }
#else
        (void)stressCount;
#endif
        VariableArray<Scatter::ScatterSpeciesBinding> bindings;
        for (const auto& species : parsed.Scatter.Species)
        {
            Scatter::ScatterSpeciesBinding binding;
            binding.Name = species.Name;
            if (species.RenderPath == Asset::ScatterRenderPath::Mega)
            {
                binding.MegaMesh = LoadMegaMesh(ctx, *assetSystem, species);
                if (!binding.MegaMesh.IsValid())
                {
                    // 未結合の種は ScatterSystem::Load が一度だけ警告する。
                    continue;
                }
                binding.bCastShadow = true;
                binding.ResidencyDistance = species.Name == "rock" ? 192.0f : 128.0f;
            }
            else if (species.RenderPath == Asset::ScatterRenderPath::Foliage &&
                     (species.Name == "shrub" || species.Name == "forb_clump"))
            {
                const bool bShrub = species.Name == "shrub";
                binding.FoliageTemplate = MakePlantBatch(bShrub);
                binding.ResidencyDistance = binding.FoliageTemplate.FadeEndDistance + 10.0f;
                binding.bCastShadow = bShrub;
            }
            else
            {
                continue;
            }
            bindings.push_back(std::move(binding));
        }
        auto scatter = MakeShared<Asset::CookedScatterData>(std::move(parsed.Scatter));
        const auto handle = ctx.EngineRef.GetScatterSystem().Load(ctx.WorldRef, scatter, {bindings.data(), bindings.size()});
        if (handle == Scatter::kInvalidScatterHandle)
        {
            LOG_WARNING("GRASSLAND_SCATTER 散布を登録できませんでした");
        }
        return handle;
    }
} // namespace Game::GameModes
