// NVSCT の mega と foliage を種ごとにまとめ、距離と共通の予算に従って常駐を切り替える。
#include "Scatter/ScatterSystem.h"

#include "Component/MegaGeometryScatterComponent.h"
#include "Logging/LogMacros.h"
#include "Object/World.h"
#include "Rendering/RenderWorld.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <utility>

namespace NorvesLib::Core::Scatter
{
    namespace
    {
        constexpr uint32_t kRangeRows = Component::MegaGeometryScatterComponent::kRangeRows;
        constexpr size_t kMaxLogEvents = 1u << 20;

        float FoliageWindPhase(uint32_t seed, uint32_t chunk, uint32_t index)
        {
            // 各成分を順に混ぜ、上位 24 ビットを float で正確な 0〜1 に写す。
            const auto mix = [](uint32_t value)
            {
                value ^= value >> 16;
                value *= 0x7feb352du;
                value ^= value >> 15;
                value *= 0x846ca68bu;
                return value ^ (value >> 16);
            };
            const uint32_t hash = mix(mix(mix(seed) ^ chunk) ^ index);
            return static_cast<float>(hash >> 8) / 16777216.0f;
        }

        float DistanceToChunk(const Math::Vector3& position, const Asset::CookedScatterChunk& chunk)
        {
            const float coordinates[] = {position.x, position.y, position.z};
            float squared = 0.0f;
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                const float delta = std::max(chunk.AabbMin[axis] - coordinates[axis],
                                             std::max(0.0f, coordinates[axis] - chunk.AabbMax[axis]));
                squared += delta * delta;
            }
            return std::sqrt(squared);
        }

        Math::Quaternion InstanceRotation(const Asset::ScatterInstanceValue& value, float slopeFollow)
        {
            const Math::Vector3 up(0.0f, 1.0f, 0.0f);
            const Math::Vector3 normal = Math::Vector3(value.Normal[0], value.Normal[1], value.Normal[2]).Normalized();
            // 単位から Y→法線への最短回転の slerp は、軸角の角度に割合を掛けたものに等しい。
            const float angle = std::acos(std::clamp(normal.y, -1.0f, 1.0f));
            Math::Vector3 axis(normal.z, 0.0f, -normal.x);
            if (axis.x * axis.x + axis.z * axis.z < 1.0e-12f)
            {
                axis = Math::Vector3(1.0f, 0.0f, 0.0f);
            }
            const Math::Quaternion tilt(axis, angle * std::clamp(slopeFollow, 0.0f, 1.0f));
            return tilt * Math::Quaternion(up, value.YawRadians);
        }
    } // namespace

    void ScatterSystem::SetRenderWorld(Rendering::RenderWorld* renderWorld)
    {
        m_RenderWorld = renderWorld;
    }

    void ScatterSystem::SetBudget(const ScatterBudget& budget)
    {
        m_Budget = budget;
    }

    const ScatterBudget& ScatterSystem::GetBudget() const
    {
        return m_Budget;
    }

    ScatterHandle ScatterSystem::Load(World& world, Container::TSharedPtr<const Asset::CookedScatterData> data,
                                       Container::Span<const ScatterSpeciesBinding> bindings)
    {
        if (!world.HasFlag(OF_Initialized) || !data || data->Species.empty() || data->Chunks.empty() ||
            m_NextHandle == kInvalidScatterHandle || data->Species.size() > std::numeric_limits<uint32_t>::max() ||
            data->Chunks.size() > std::numeric_limits<uint32_t>::max())
        {
            return kInvalidScatterHandle;
        }
        uint64_t totalInstances = 0;
        for (uint32_t chunk = 0; chunk < data->Chunks.size(); ++chunk)
        {
            for (uint32_t species = 0; species < data->Species.size(); ++species)
            {
                const auto bytes = data->GetInstances(chunk, species);
                if (bytes.size() % sizeof(Asset::InstanceRecord16) != 0)
                {
                    return kInvalidScatterHandle;
                }
                totalInstances += bytes.size() / sizeof(Asset::InstanceRecord16);
            }
        }
        if (totalInstances == 0)
        {
            return kInvalidScatterHandle;
        }

        m_Scatters.emplace_back();
        ScatterState& scatter = m_Scatters.back();
        scatter.Handle = m_NextHandle++;
        scatter.WorldPtr = &world;
        scatter.Data = std::move(data);
        scatter.Species.resize(scatter.Data->Species.size());
        const auto fail = [&]()
        {
            ReleaseEntities(scatter);
            m_Scatters.pop_back();
            return kInvalidScatterHandle;
        };
        uint32_t megaCount = 0;
        for (uint32_t speciesIndex = 0; speciesIndex < scatter.Species.size(); ++speciesIndex)
        {
            const auto& source = scatter.Data->Species[speciesIndex];
            auto& species = scatter.Species[speciesIndex];
            species.RenderPath = source.RenderPath;
            const ScatterSpeciesBinding* binding = nullptr;
            for (const auto& candidate : bindings)
            {
                if (candidate.Name == source.Name)
                {
                    binding = &candidate;
                    break;
                }
            }
            if (binding == nullptr)
            {
                LOG_WARNING_F("散布の種に描画先がありません species=%s", source.Name.c_str());
                continue;
            }
            switch (source.RenderPath)
            {
            case Asset::ScatterRenderPath::Foliage:
                if (!binding->FoliageTemplate.Mesh)
                {
                    LOG_WARNING_F("散布の種の foliage メッシュが無効です species=%s", source.Name.c_str());
                    continue;
                }
                break;
            case Asset::ScatterRenderPath::Mega:
                if (!binding->MegaMesh.IsValid())
                {
                    LOG_WARNING_F("散布の種のメッシュが無効です species=%s", source.Name.c_str());
                    continue;
                }
                break;
            default:
                continue;
            }
            species.ResidencyDistance = binding->ResidencyDistance;
            species.Priority = binding->Priority;
            species.Chunks.resize(scatter.Data->Chunks.size());
            if (source.RenderPath == Asset::ScatterRenderPath::Foliage)
            {
                species.FoliageTemplate = binding->FoliageTemplate;
                species.FoliageTemplate.Instances.reset();
                species.FoliageTemplate.Cells.reset();
                species.bCastShadow = binding->bCastShadow;
                uint64_t speciesInstances = 0;
                for (uint32_t chunkIndex = 0; chunkIndex < species.Chunks.size(); ++chunkIndex)
                {
                    const auto bytes = scatter.Data->GetInstances(chunkIndex, speciesIndex);
                    species.Chunks[chunkIndex].Bytes = bytes.size();
                    speciesInstances += bytes.size() / sizeof(Asset::InstanceRecord16);
                    // セルの先頭と個数は uint32_t で表す。
                    if (speciesInstances > std::numeric_limits<uint32_t>::max())
                    {
                        return fail();
                    }
                }
                continue;
            }
            Container::VariableArray<Component::MegaScatterInstance> instances;
            for (uint32_t chunkIndex = 0; chunkIndex < species.Chunks.size(); ++chunkIndex)
            {
                const auto bytes = scatter.Data->GetInstances(chunkIndex, speciesIndex);
                const size_t count = bytes.size() / sizeof(Asset::InstanceRecord16);
                const size_t paddedCount = ((count + kRangeRows - 1) / kRangeRows) * kRangeRows;
                if (paddedCount > std::numeric_limits<uint32_t>::max() - instances.size())
                {
                    return fail();
                }
                auto& unit = species.Chunks[chunkIndex];
                unit.FirstRange = static_cast<uint32_t>(instances.size() / kRangeRows);
                unit.RangeCount = static_cast<uint32_t>(paddedCount / kRangeRows);
                unit.Bytes = bytes.size();
                for (size_t index = 0; index < count; ++index)
                {
                    Asset::InstanceRecord16 record;
                    Asset::ScatterInstanceValue value;
                    if (!Asset::ReadScatterInstance(bytes.subspan(index * sizeof(record), sizeof(record)), record) ||
                        !Asset::DecodeScatterInstance(record, scatter.Data->Chunks[chunkIndex], source,
                                                      scatter.Data->ChunkSizeMeters, scatter.Data->OriginX,
                                                      scatter.Data->OriginZ, value))
                    {
                        return fail();
                    }
                    Component::MegaScatterInstance instance;
                    instance.Position = Math::Vector3(static_cast<float>(value.PositionX), static_cast<float>(value.PositionY),
                                                     static_cast<float>(value.PositionZ));
                    instance.Rotation = InstanceRotation(value, source.SlopeFollow);
                    instance.Scale = Math::Vector3(value.Scale, value.Scale, value.Scale);
                    instance.MeshIndex = 0;
                    instances.push_back(instance);
                }
                for (size_t index = count; index < paddedCount; ++index)
                {
                    auto padding = instances.back();
                    padding.MeshIndex = Component::MegaGeometryScatterComponent::kInvalidIndex;
                    instances.push_back(padding);
                }
            }
            Entity* owner = world.SpawnEntity<Entity>();
            if (owner == nullptr)
            {
                return fail();
            }
            species.OwnerHandle = world.GetEntityHandle(*owner);
            species.ObjectId = owner->GetObjectId();
            // World の木へ付いた原点の Entity に、種全体の入力を一度だけ与える。
            auto* component = world.CreateComponent<Component::MegaGeometryScatterComponent>(owner);
            if (component == nullptr)
            {
                return fail();
            }
            species.ComponentId = component->GetComponentId();
            component->SetMegaMeshHandle(binding->MegaMesh);
            component->SetCastShadow(binding->bCastShadow);
            if (!component->SetInstances({instances.data(), instances.size()},
                                         {Component::MegaScatterRowOrder::Input, false}))
            {
                return fail();
            }
            ++megaCount;
        }
        LOG_INFO_F("SCATTER_LOAD handle=%u species=%zu mega=%u chunks=%zu instances=%llu 散布を読み込みました",
                   scatter.Handle, scatter.Species.size(), megaCount, scatter.Data->Chunks.size(),
                   static_cast<unsigned long long>(totalInstances));
        return scatter.Handle;
    }

    const ScatterSystem::ScatterState* ScatterSystem::Find(ScatterHandle handle) const
    {
        for (const auto& scatter : m_Scatters)
        {
            if (scatter.Handle == handle)
            {
                return &scatter;
            }
        }
        return nullptr;
    }

    Entity* ScatterSystem::ResolveOwner(const ScatterState& scatter, const SpeciesState& species) const
    {
        if (scatter.WorldPtr == nullptr || !scatter.WorldPtr->HasFlag(OF_Initialized) || species.ObjectId == 0)
        {
            return nullptr;
        }
        if (species.OwnerHandle.IsValid())
        {
            return scatter.WorldPtr->ResolveEntityHandle(species.OwnerHandle);
        }
        // ECS の作成予約がまだ再生されていない Entity は ObjectId で探す。
        Entity* owner = scatter.WorldPtr->FindEntityByObjectId(species.ObjectId);
        if (owner != nullptr && species.ComponentId != 0)
        {
            const auto* component = owner->GetComponent<Component::MegaGeometryScatterComponent>();
            if (component == nullptr || component->GetComponentId() != species.ComponentId)
            {
                return nullptr;
            }
        }
        return owner;
    }

    Component::MegaGeometryScatterComponent* ScatterSystem::ResolveMega(const ScatterState& scatter,
                                                                      const SpeciesState& species) const
    {
        if (species.RenderPath != Asset::ScatterRenderPath::Mega || species.ComponentId == 0)
        {
            return nullptr;
        }
        Entity* owner = ResolveOwner(scatter, species);
        auto* component = owner != nullptr ? owner->GetComponent<Component::MegaGeometryScatterComponent>() : nullptr;
        return component != nullptr && component->GetComponentId() == species.ComponentId ? component : nullptr;
    }

    void ScatterSystem::ReleaseEntities(ScatterState& scatter)
    {
        for (auto& species : scatter.Species)
        {
            if (Entity* owner = ResolveOwner(scatter, species))
            {
                scatter.WorldPtr->RemoveEntity(owner);
            }
            species.OwnerHandle = {};
            species.ObjectId = 0;
            species.ComponentId = 0;
        }
    }

    void ScatterSystem::Unload(ScatterHandle handle)
    {
        for (auto it = m_Scatters.begin(); it != m_Scatters.end(); ++it)
        {
            if (it->Handle == handle)
            {
                ReleaseEntities(*it);
                m_Scatters.erase(it);
                RebuildFoliageScene();
                RecountStats();
                return;
            }
        }
    }

    void ScatterSystem::RecordEvent(ScatterState& scatter, uint32_t chunk, uint32_t species, bool bResident)
    {
        if (scatter.Log.size() < kMaxLogEvents)
        {
            scatter.Log.push_back({m_Frame, chunk, species, bResident});
        }
    }

    void ScatterSystem::Update(const Math::Vector3& cameraPosition)
    {
        ++m_Frame;
        m_Stats = {};
        bool bFoliageChanged = false;
        struct Candidate
        {
            size_t ScatterIndex;
            uint32_t Chunk;
            uint32_t Species;
            uint32_t Priority;
            float Distance;
            ScatterHandle Handle;
        };
        Container::VariableArray<Candidate> candidates;
        for (size_t scatterIndex = 0; scatterIndex < m_Scatters.size(); ++scatterIndex)
        {
            auto& scatter = m_Scatters[scatterIndex];
            for (uint32_t speciesIndex = 0; speciesIndex < scatter.Species.size(); ++speciesIndex)
            {
                auto& species = scatter.Species[speciesIndex];
                auto* component = ResolveMega(scatter, species);
                for (uint32_t chunk = 0; chunk < species.Chunks.size(); ++chunk)
                {
                    auto& unit = species.Chunks[chunk];
                    if (unit.Bytes == 0)
                    {
                        continue;
                    }
                    const float distance = DistanceToChunk(cameraPosition, scatter.Data->Chunks[chunk]);
                    unit.bRequested = distance <= species.ResidencyDistance;
                    if (unit.bResident && distance > species.ResidencyDistance + m_Budget.ReleaseHysteresisMeters)
                    {
                        if (component != nullptr)
                        {
                            for (uint32_t range = 0; range < unit.RangeCount; ++range)
                            {
                                component->SetRangeActive(unit.FirstRange + range, false);
                            }
                        }
                        if (species.RenderPath == Asset::ScatterRenderPath::Foliage)
                        {
                            Container::VariableArray<Rendering::FoliageInstance>().swap(unit.FoliageInstances);
                            bFoliageChanged = true;
                        }
                        unit.bResident = false;
                        ++m_Stats.ReleasedThisFrame;
                        RecordEvent(scatter, chunk, speciesIndex, false);
                    }
                    if (!unit.bResident && unit.bRequested)
                    {
                        candidates.push_back({scatterIndex, chunk, speciesIndex, species.Priority, distance, scatter.Handle});
                    }
                }
            }
        }
        // 最後のハンドルまで比べるので、候補の列挙順によらず一意の順序になる。
        std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b)
        {
            if (a.Priority != b.Priority)
            {
                return a.Priority < b.Priority;
            }
            if (a.Distance != b.Distance)
            {
                return a.Distance < b.Distance;
            }
            if (a.Chunk != b.Chunk)
            {
                return a.Chunk < b.Chunk;
            }
            if (a.Species != b.Species)
            {
                return a.Species < b.Species;
            }
            return a.Handle < b.Handle;
        });
        const auto start = std::chrono::steady_clock::now();
        for (const auto& candidate : candidates)
        {
            auto& scatter = m_Scatters[candidate.ScatterIndex];
            auto& species = scatter.Species[candidate.Species];
            auto& unit = species.Chunks[candidate.Chunk];
            auto* component = ResolveMega(scatter, species);
            if (species.RenderPath == Asset::ScatterRenderPath::Mega &&
                (component == nullptr || unit.FirstRange > component->GetRangeCount() ||
                 unit.RangeCount > component->GetRangeCount() - unit.FirstRange))
            {
                // この候補の行が未準備でも、別の種や散布は進める。予算には数えない。
                continue;
            }
            if (m_Stats.ResidentThisFrame > 0)
            {
                const float elapsed = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
                if (m_Stats.ResidentThisFrame >= m_Budget.MaxResidentPerFrame ||
                    m_Stats.BytesThisFrame >= m_Budget.MaxBytesPerFrame ||
                    unit.Bytes > m_Budget.MaxBytesPerFrame - m_Stats.BytesThisFrame ||
                    (m_Budget.MaxMillisecondsPerFrame > 0.0f && elapsed >= m_Budget.MaxMillisecondsPerFrame))
                {
                    break;
                }
            }
            if (species.RenderPath == Asset::ScatterRenderPath::Foliage)
            {
                if (!MakeFoliageResident(scatter, candidate.Chunk, candidate.Species))
                {
                    continue;
                }
                bFoliageChanged = true;
            }
            else
            {
                for (uint32_t range = 0; range < unit.RangeCount; ++range)
                {
                    component->SetRangeActive(unit.FirstRange + range, true);
                }
            }
            unit.bResident = true;
            ++m_Stats.ResidentThisFrame;
            m_Stats.BytesThisFrame += unit.Bytes;
            RecordEvent(scatter, candidate.Chunk, candidate.Species, true);
        }
        RecountStats();
        if (bFoliageChanged)
        {
            RebuildFoliageScene();
        }
    }

    bool ScatterSystem::MakeFoliageResident(ScatterState& scatter, uint32_t chunkIndex, uint32_t speciesIndex)
    {
        auto& species = scatter.Species[speciesIndex];
        auto& unit = species.Chunks[chunkIndex];
        const auto& source = scatter.Data->Species[speciesIndex];
        const auto& chunk = scatter.Data->Chunks[chunkIndex];
        const auto bytes = scatter.Data->GetInstances(chunkIndex, speciesIndex);
        const size_t count = bytes.size() / sizeof(Asset::InstanceRecord16);
        Container::VariableArray<Rendering::FoliageInstance> instances;
        instances.reserve(count);
        bool bSorted = true;
        uint8_t previousRank = 0;
        for (size_t index = 0; index < count; ++index)
        {
            Asset::InstanceRecord16 record;
            Asset::ScatterInstanceValue value;
            if (!Asset::ReadScatterInstance(bytes.subspan(index * sizeof(record), sizeof(record)), record) ||
                !Asset::DecodeScatterInstance(record, chunk, source, scatter.Data->ChunkSizeMeters,
                                              scatter.Data->OriginX, scatter.Data->OriginZ, value))
            {
                return false;
            }
            Rendering::FoliageInstance instance;
            instance.Position[0] = static_cast<float>(value.PositionX);
            instance.Position[1] = static_cast<float>(value.PositionY);
            instance.Position[2] = static_cast<float>(value.PositionZ);
            instance.Yaw = value.YawRadians;
            instance.WidthScale = value.Scale;
            instance.HeightScale = value.Scale;
            instance.ColorVariation = value.Dry;
            instance.WindPhase = FoliageWindPhase(source.WindPhaseSeed, chunkIndex, static_cast<uint32_t>(index));
            instance.FadeThreshold = (static_cast<float>(value.Rank) + 0.5f) / 256.0f;
            instances.push_back(instance);
            bSorted = bSorted && value.Rank >= previousRank;
            previousRank = value.Rank;
        }
        if (!bSorted)
        {
            if (!unit.bWarnedUnsortedRank)
            {
                LOG_WARNING_F("散布の Rank が昇順ではないため foliage を安定に並べ替えます handle=%u chunk=%u species=%u",
                              scatter.Handle, chunkIndex, speciesIndex);
                unit.bWarnedUnsortedRank = true;
            }
            std::stable_sort(instances.begin(), instances.end(),
                             [](const Rendering::FoliageInstance& a, const Rendering::FoliageInstance& b)
            {
                return a.FadeThreshold < b.FadeThreshold;
            });
        }
        const float radius = source.MeshRadiusMeters * source.ScaleMax;
        const float height = species.FoliageTemplate.Mesh->Height * source.ScaleMax;
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            unit.FoliageBounds.BoundsMin[axis] = chunk.AabbMin[axis] - (axis == 1 ? 0.0f : radius);
            unit.FoliageBounds.BoundsMax[axis] = chunk.AabbMax[axis] + (axis == 1 ? height : radius);
        }
        unit.FoliageInstances = std::move(instances);
        return true;
    }

    void ScatterSystem::RebuildFoliageScene()
    {
        Rendering::FoliageScene scene;
        // ハンドルは単調増加で追加され、削除しても順序を保つ。種とチャンクも番号順に連結する。
        for (const auto& scatter : m_Scatters)
        {
            for (const auto& species : scatter.Species)
            {
                if (species.RenderPath != Asset::ScatterRenderPath::Foliage || !species.FoliageTemplate.Mesh)
                {
                    continue;
                }
                size_t instanceCount = 0;
                size_t cellCount = 0;
                for (const auto& unit : species.Chunks)
                {
                    if (unit.bResident)
                    {
                        instanceCount += unit.FoliageInstances.size();
                        ++cellCount;
                    }
                }
                if (cellCount == 0)
                {
                    continue;
                }
                auto instances = Container::MakeShared<Container::VariableArray<Rendering::FoliageInstance>>();
                auto cells = Container::MakeShared<Container::VariableArray<Rendering::FoliageCell>>();
                instances->reserve(instanceCount);
                cells->reserve(cellCount);
                for (const auto& unit : species.Chunks)
                {
                    if (!unit.bResident)
                    {
                        continue;
                    }
                    auto cell = unit.FoliageBounds;
                    cell.FirstInstance = static_cast<uint32_t>(instances->size());
                    cell.InstanceCount = static_cast<uint32_t>(unit.FoliageInstances.size());
                    cells->push_back(cell);
                    instances->insert(instances->end(), unit.FoliageInstances.begin(), unit.FoliageInstances.end());
                }
                auto batch = species.FoliageTemplate;
                batch.Instances = std::move(instances);
                batch.Cells = std::move(cells);
                batch.ShadowDistance = species.bCastShadow ? batch.ShadowDistance : 0.0f;
                scene.Batches.push_back(std::move(batch));
            }
        }
        m_FoliageScene = std::move(scene);
        ++m_FoliageVersion;
        if (m_RenderWorld != nullptr && m_RenderWorld->IsInitialized())
        {
            m_RenderWorld->SetFoliageSource(Rendering::FoliageSourceSlot::Scatter, m_FoliageScene);
        }
    }

    void ScatterSystem::RecountStats()
    {
        m_Stats.ResidentUnits = 0;
        m_Stats.ResidentBytes = 0;
        m_Stats.PendingUnits = 0;
        for (const auto& scatter : m_Scatters)
        {
            for (const auto& species : scatter.Species)
            {
                for (const auto& unit : species.Chunks)
                {
                    if (unit.bResident)
                    {
                        ++m_Stats.ResidentUnits;
                        m_Stats.ResidentBytes += unit.Bytes;
                    }
                    else if (unit.bRequested)
                    {
                        ++m_Stats.PendingUnits;
                    }
                }
            }
        }
    }

    bool ScatterSystem::IsSettled(ScatterHandle handle) const
    {
        const auto* scatter = Find(handle);
        if (scatter == nullptr || !scatter->WorldPtr->HasFlag(OF_Initialized))
        {
            return false;
        }
        for (const auto& species : scatter->Species)
        {
            if (species.RenderPath == Asset::ScatterRenderPath::Mega)
            {
                if (species.ComponentId == 0)
                {
                    continue;
                }
                const auto* component = ResolveMega(*scatter, species);
                if (component == nullptr || component->HasPendingWork())
                {
                    return false;
                }
            }
            for (const auto& unit : species.Chunks)
            {
                if (unit.bRequested && !unit.bResident)
                {
                    return false;
                }
            }
        }
        return true;
    }

    const ScatterResidencyStats& ScatterSystem::GetStats() const
    {
        return m_Stats;
    }

    const Rendering::FoliageScene& ScatterSystem::GetFoliageScene() const
    {
        return m_FoliageScene;
    }

    uint64_t ScatterSystem::GetFoliageVersion() const
    {
        return m_FoliageVersion;
    }

    Container::Span<const ScatterResidencyEvent> ScatterSystem::GetResidencyLog(ScatterHandle handle) const
    {
        const auto* scatter = Find(handle);
        return scatter != nullptr ? Container::Span<const ScatterResidencyEvent>(scatter->Log.data(), scatter->Log.size())
                                  : Container::Span<const ScatterResidencyEvent>();
    }

    Component::MegaGeometryScatterComponent* ScatterSystem::GetMegaComponent(ScatterHandle handle, uint32_t speciesIndex) const
    {
        const auto* scatter = Find(handle);
        return scatter != nullptr && speciesIndex < scatter->Species.size()
                   ? ResolveMega(*scatter, scatter->Species[speciesIndex]) : nullptr;
    }

    void ScatterSystem::GetChunkRanges(ScatterHandle handle, uint32_t chunk, uint32_t species,
                                       uint32_t& outFirst, uint32_t& outCount) const
    {
        outFirst = 0;
        outCount = 0;
        const auto* scatter = Find(handle);
        if (scatter != nullptr && species < scatter->Species.size() && chunk < scatter->Species[species].Chunks.size())
        {
            const auto& unit = scatter->Species[species].Chunks[chunk];
            outFirst = unit.FirstRange;
            outCount = unit.RangeCount;
        }
    }

    bool ScatterSystem::IsResident(ScatterHandle handle, uint32_t chunk, uint32_t species) const
    {
        const auto* scatter = Find(handle);
        return scatter != nullptr && species < scatter->Species.size() && chunk < scatter->Species[species].Chunks.size() &&
               scatter->Species[species].Chunks[chunk].bResident;
    }

    void ScatterSystem::Clear()
    {
        for (auto& scatter : m_Scatters)
        {
            ReleaseEntities(scatter);
        }
        m_Scatters.clear();
        m_Stats = {};
        RebuildFoliageScene();
    }
} // namespace NorvesLib::Core::Scatter
