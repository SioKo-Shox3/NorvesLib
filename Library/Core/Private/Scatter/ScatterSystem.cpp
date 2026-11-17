// NVSCT の mega と foliage を種ごとにまとめ、距離と共通の予算に従って常駐を切り替える。
#include "Scatter/ScatterSystem.h"

#include "Component/MegaGeometryScatterComponent.h"
#include "Logging/LogMacros.h"
#include "Object/World.h"
#include "Rendering/RenderWorld.h"
#include "Rendering/SceneView.h"

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

        uint32_t PackMegaTint(const float (&tint)[3])
        {
            // NVSCT の色は倍率の半分なので 2 倍し、1 で打ち切って 8 ビットに四捨五入する。
            uint32_t packed = 0xFF000000u;
            for (uint32_t channel = 0; channel < 3; ++channel)
            {
                const float multiplier = std::min(1.0f, 2.0f * tint[channel]);
                const uint32_t value = static_cast<uint32_t>(std::round(multiplier * 255.0f));
                packed |= value << (channel * 8);
            }
            return packed;
        }

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

        uint64_t SpeciesInstanceOffset(const Asset::CookedScatterData& data, uint32_t chunk, uint32_t species)
        {
            uint64_t offset = 0;
            for (uint32_t index = 0; index < species; ++index)
            {
                offset += data.GetInstances(chunk, index).size() / sizeof(Asset::InstanceRecord16);
            }
            return offset;
        }

        void WriteHiddenInteger(Container::VariableArray<uint8_t>& bytes, uint64_t value, uint32_t width)
        {
            for (uint32_t byte = 0; byte < width; ++byte)
            {
                bytes.push_back(static_cast<uint8_t>(value >> (byte * 8)));
            }
        }

        uint64_t ReadHiddenInteger(Container::Span<const uint8_t> bytes, size_t offset, uint32_t width)
        {
            uint64_t value = 0;
            for (uint32_t byte = 0; byte < width; ++byte)
            {
                value |= static_cast<uint64_t>(bytes[offset + byte]) << (byte * 8);
            }
            return value;
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
        if (m_RenderWorld != renderWorld)
        {
            m_bSkyOccludersDirty = true;
        }
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
        scatter.Chunks.resize(scatter.Data->Chunks.size());
        scatter.HiddenInstances.resize(scatter.Data->Chunks.size());
        scatter.Species.resize(scatter.Data->Species.size());
        // 取っ手に 24 ビット、格子の各軸に 20 ビットを割り当てる。
        scatter.bSkyOccluderKeysValid = scatter.Handle < (1u << 24);
        for (const auto& chunk : scatter.Data->Chunks)
        {
            scatter.bSkyOccluderKeysValid &= chunk.ChunkX < (1u << 20) && chunk.ChunkZ < (1u << 20);
        }
        if (!scatter.bSkyOccluderKeysValid)
        {
            LOG_WARNING("散布の取っ手か格子座標が鍵の範囲を超えるため遮蔽物を作りません handle=%u", scatter.Handle);
        }
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
            auto& occluder = species.SkyOccluderDesc;
            occluder.bEnabled = source.SkyOccluderShape == 1u || source.SkyOccluderShape == 2u;
            occluder.Shape = source.SkyOccluderShape == 2u
                ? Rendering::SkyOccluderShape::Box : Rendering::SkyOccluderShape::Ellipsoid;
            occluder.Class = source.bSkyOccluderCanopy
                ? Rendering::SkyOccluderClass::Canopy : Rendering::SkyOccluderClass::Solid;
            occluder.RadiusMeters = source.SkyOccluderRadiusMeters;
            occluder.HeightMeters = source.SkyOccluderHeightMeters;
            occluder.BaseRatio = source.SkyOccluderBaseRatio;
            occluder.Kappa = source.SkyOccluderKappa;
            if (source.RenderPath == Asset::ScatterRenderPath::Foliage)
            {
                species.FoliageTemplate = binding->FoliageTemplate;
                species.FoliageTemplate.Instances.reset();
                species.FoliageTemplate.Cells.reset();
                species.bCastShadow = binding->bCastShadow && (source.Flags & Asset::ScatterSpeciesFlagNoCastShadow) == 0;
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
                const auto variationSeeds = scatter.Data->GetVariationSeeds(chunkIndex, speciesIndex);
                const size_t count = bytes.size() / sizeof(Asset::InstanceRecord16);
                constexpr size_t seedSize = Asset::CookedScatterFormatV0::VariationSeedRecordSize;
                if (!variationSeeds.empty() && variationSeeds.size() != count * seedSize)
                {
                    return fail();
                }
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
                    instance.Tint = PackMegaTint(value.Tint);
                    if (!variationSeeds.empty() &&
                        !Asset::ReadScatterVariationSeed(variationSeeds.subspan(index * seedSize, seedSize),
                                                         instance.VariationSeed))
                    {
                        return fail();
                    }
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
            if ((source.Flags & Asset::ScatterSpeciesFlagNoRayTracing) != 0)
            {
                if (auto* sceneView = world.GetSceneView())
                {
                    sceneView->SetMegaGeometryRayTracing(species.ComponentId, false);
                    species.bRayTracingExcluded = true;
                }
            }
            component->SetMegaMeshHandle(binding->MegaMesh);
            component->SetCastShadow(binding->bCastShadow && (source.Flags & Asset::ScatterSpeciesFlagNoCastShadow) == 0);
            // 樹冠の種は空の遮蔽のマップの樹冠だけの組が RTGI・DDGI の拡散を補うので、TLAS では影と PT にだけ当てる。
            // TLAS に入れない種は樹冠の組にも入れない。
            component->SetSkyMapProxy(occluder.bEnabled && occluder.Class == Rendering::SkyOccluderClass::Canopy &&
                                      (source.Flags & Asset::ScatterSpeciesFlagNoRayTracing) == 0);
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

    ScatterSystem::ScatterState* ScatterSystem::Find(ScatterHandle handle)
    {
        for (auto& scatter : m_Scatters)
        {
            if (scatter.Handle == handle)
            {
                return &scatter;
            }
        }
        return nullptr;
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
            if (species.bRayTracingExcluded && scatter.WorldPtr != nullptr && scatter.WorldPtr->HasFlag(OF_Initialized))
            {
                if (auto* sceneView = scatter.WorldPtr->GetSceneView())
                {
                    sceneView->SetMegaGeometryRayTracing(species.ComponentId, true);
                }
            }
            if (Entity* owner = ResolveOwner(scatter, species))
            {
                scatter.WorldPtr->RemoveEntity(owner);
            }
            species.OwnerHandle = {};
            species.ObjectId = 0;
            species.ComponentId = 0;
            species.bRayTracingExcluded = false;
        }
    }

    void ScatterSystem::Unload(ScatterHandle handle)
    {
        for (auto it = m_Scatters.begin(); it != m_Scatters.end(); ++it)
        {
            if (it->Handle == handle)
            {
                ReleaseEntities(*it);
                m_bSkyOccludersDirty |= !it->SkyOccluderChunks.empty();
                m_Scatters.erase(it);
                RebuildFoliageScene();
                RecountStats();
                PublishSkyOccluders();
                return;
            }
        }
    }

    void ScatterSystem::RecordEvent(ScatterState& scatter, uint32_t chunk, uint32_t species, bool bResident)
    {
        if (scatter.Species[species].SkyOccluderDesc.bEnabled)
        {
            RebuildSkyOccluderChunk(scatter, chunk);
        }
        if (scatter.Log.size() < kMaxLogEvents)
        {
            scatter.Log.push_back({m_Frame, chunk, species, bResident});
        }
    }

    bool ScatterSystem::ReleaseChunkUnits(ScatterState& scatter, uint32_t chunk)
    {
        bool bFoliageChanged = false;
        for (uint32_t speciesIndex = 0; speciesIndex < scatter.Species.size(); ++speciesIndex)
        {
            auto& species = scatter.Species[speciesIndex];
            if (chunk >= species.Chunks.size())
            {
                continue;
            }
            auto& unit = species.Chunks[chunk];
            if (!unit.bResident)
            {
                continue;
            }
            if (auto* component = ResolveMega(scatter, species))
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
            RecordEvent(scatter, chunk, speciesIndex, false);
        }
        return bFoliageChanged;
    }

    bool ScatterSystem::SetScatterName(ScatterHandle handle, Container::AnsiStringView name)
    {
        auto* scatter = Find(handle);
        if (scatter == nullptr)
        {
            return false;
        }
        const ScatterHandle existing = FindScatter(name);
        if (existing != kInvalidScatterHandle && existing != handle)
        {
            return false;
        }
        Container::AnsiString ownedName(name.size(), '\0');
        for (size_t index = 0; index < name.size(); ++index)
        {
            ownedName[index] = name[index];
        }
        scatter->Name = std::move(ownedName);
        return true;
    }

    ScatterHandle ScatterSystem::FindScatter(Container::AnsiStringView name) const
    {
        if (name.empty())
        {
            return kInvalidScatterHandle;
        }
        for (const auto& scatter : m_Scatters)
        {
            if (scatter.Name.size() == name.size() && std::equal(scatter.Name.begin(), scatter.Name.end(), name.begin()))
            {
                return scatter.Handle;
            }
        }
        return kInvalidScatterHandle;
    }

    Container::TSharedPtr<const Asset::CookedScatterData> ScatterSystem::GetData(ScatterHandle handle) const
    {
        const auto* scatter = Find(handle);
        return scatter != nullptr ? scatter->Data : Container::TSharedPtr<const Asset::CookedScatterData>();
    }

    bool ScatterSystem::SetDistanceResidency(ScatterHandle handle, bool bEnabled)
    {
        auto* scatter = Find(handle);
        if (scatter == nullptr)
        {
            return false;
        }
        if (scatter->bDistanceResidency == bEnabled)
        {
            return true;
        }
        scatter->bDistanceResidency = bEnabled;
        if (!bEnabled)
        {
            for (auto& species : scatter->Species)
            {
                for (uint32_t chunk = 0; chunk < species.Chunks.size(); ++chunk)
                {
                    auto& unit = species.Chunks[chunk];
                    unit.bDistanceRequested = false;
                    unit.bRequested = unit.Bytes > 0 && scatter->Chunks[chunk].bPinned;
                }
            }
            bool bFoliageChanged = false;
            for (uint32_t chunk = 0; chunk < scatter->Chunks.size(); ++chunk)
            {
                if (!scatter->Chunks[chunk].bPinned)
                {
                    bFoliageChanged |= ReleaseChunkUnits(*scatter, chunk);
                }
            }
            RecountStats();
            if (bFoliageChanged)
            {
                RebuildFoliageScene();
            }
        }
        return true;
    }

    bool ScatterSystem::PinChunk(ScatterHandle handle, uint32_t chunk)
    {
        auto* scatter = Find(handle);
        if (scatter == nullptr || chunk >= scatter->Chunks.size())
        {
            return false;
        }
        auto& state = scatter->Chunks[chunk];
        if (state.bPinned)
        {
            return true;
        }
        state.bPinned = true;
        bool bFoliageChanged = false;
        for (uint32_t speciesIndex = 0; speciesIndex < scatter->Species.size(); ++speciesIndex)
        {
            auto& species = scatter->Species[speciesIndex];
            if (chunk >= species.Chunks.size())
            {
                continue;
            }
            auto& unit = species.Chunks[chunk];
            if (unit.Bytes == 0)
            {
                continue;
            }
            unit.bRequested = true;
            if (unit.bResident)
            {
                continue;
            }
            if (species.RenderPath == Asset::ScatterRenderPath::Foliage)
            {
                if (!MakeFoliageResident(*scatter, chunk, speciesIndex))
                {
                    continue;
                }
                bFoliageChanged = true;
            }
            else
            {
                auto* component = ResolveMega(*scatter, species);
                if (component == nullptr || unit.FirstRange > component->GetRangeCount() ||
                    unit.RangeCount > component->GetRangeCount() - unit.FirstRange)
                {
                    continue;
                }
                for (uint32_t range = 0; range < unit.RangeCount; ++range)
                {
                    component->SetRangeActive(unit.FirstRange + range, !state.bDormant);
                }
            }
            unit.bResident = true;
            ApplyMegaHiddenInstances(*scatter, chunk, speciesIndex);
            RecordEvent(*scatter, chunk, speciesIndex, true);
        }
        RecountStats();
        if (bFoliageChanged)
        {
            RebuildFoliageScene();
        }
        return true;
    }

    bool ScatterSystem::UnpinChunk(ScatterHandle handle, uint32_t chunk)
    {
        auto* scatter = Find(handle);
        if (scatter == nullptr || chunk >= scatter->Chunks.size())
        {
            return false;
        }
        auto& state = scatter->Chunks[chunk];
        state.bPinned = false;
        if (scatter->bDistanceResidency)
        {
            SetChunkDormant(handle, chunk, false);
        }
        else
        {
            state.bDormant = false;
        }
        for (auto& species : scatter->Species)
        {
            if (chunk < species.Chunks.size())
            {
                auto& unit = species.Chunks[chunk];
                unit.bRequested = scatter->bDistanceResidency && unit.bDistanceRequested;
            }
        }
        const bool bFoliageChanged = !scatter->bDistanceResidency && ReleaseChunkUnits(*scatter, chunk);
        RecountStats();
        if (bFoliageChanged)
        {
            RebuildFoliageScene();
        }
        return true;
    }

    bool ScatterSystem::IsChunkPinned(ScatterHandle handle, uint32_t chunk) const
    {
        const auto* scatter = Find(handle);
        return scatter != nullptr && chunk < scatter->Chunks.size() && scatter->Chunks[chunk].bPinned;
    }

    bool ScatterSystem::IsChunkResident(ScatterHandle handle, uint32_t chunk) const
    {
        const auto* scatter = Find(handle);
        if (scatter == nullptr || chunk >= scatter->Chunks.size())
        {
            return false;
        }
        for (const auto& species : scatter->Species)
        {
            if (chunk < species.Chunks.size())
            {
                const auto& unit = species.Chunks[chunk];
                if (unit.Bytes > 0 && !unit.bResident)
                {
                    return false;
                }
            }
        }
        return true;
    }

    bool ScatterSystem::SetChunkDormant(ScatterHandle handle, uint32_t chunk, bool bDormant)
    {
        auto* scatter = Find(handle);
        if (scatter == nullptr || chunk >= scatter->Chunks.size())
        {
            return false;
        }
        auto& state = scatter->Chunks[chunk];
        if (state.bDormant == bDormant)
        {
            return true;
        }
        state.bDormant = bDormant;
        bool bFoliageChanged = false;
        for (auto& species : scatter->Species)
        {
            if (chunk >= species.Chunks.size() || !species.Chunks[chunk].bResident)
            {
                continue;
            }
            const auto& unit = species.Chunks[chunk];
            if (species.RenderPath == Asset::ScatterRenderPath::Foliage)
            {
                bFoliageChanged = true;
            }
            else if (auto* component = ResolveMega(*scatter, species))
            {
                for (uint32_t range = 0; range < unit.RangeCount; ++range)
                {
                    component->SetRangeActive(unit.FirstRange + range, !bDormant);
                }
            }
        }
        if (bFoliageChanged)
        {
            RebuildFoliageScene();
        }
        return true;
    }

    bool ScatterSystem::IsChunkDormant(ScatterHandle handle, uint32_t chunk) const
    {
        const auto* scatter = Find(handle);
        return scatter != nullptr && chunk < scatter->Chunks.size() && scatter->Chunks[chunk].bDormant;
    }

    void ScatterSystem::CollectChunksInBounds(ScatterHandle handle, const Math::Vector3& boundsMin,
                                               const Math::Vector3& boundsMax,
                                               Container::VariableArray<uint32_t>& outChunks) const
    {
        outChunks.clear();
        const auto* scatter = Find(handle);
        if (scatter == nullptr)
        {
            return;
        }
        const auto& data = *scatter->Data;
        for (uint32_t chunk = 0; chunk < data.Chunks.size(); ++chunk)
        {
            const auto& source = data.Chunks[chunk];
            const double x = data.OriginX + (static_cast<double>(source.ChunkX) + 0.5) * data.ChunkSizeMeters;
            const double z = data.OriginZ + (static_cast<double>(source.ChunkZ) + 0.5) * data.ChunkSizeMeters;
            if (x >= boundsMin.x && x < boundsMax.x && z >= boundsMin.z && z < boundsMax.z)
            {
                outChunks.push_back(chunk);
            }
        }
    }

    uint64_t ScatterSystem::GetChunkBytes(ScatterHandle handle, uint32_t chunk) const
    {
        const auto* scatter = Find(handle);
        if (scatter == nullptr || chunk >= scatter->Chunks.size())
        {
            return 0;
        }
        uint64_t bytes = 0;
        for (const auto& species : scatter->Species)
        {
            if (chunk < species.Chunks.size())
            {
                bytes += species.Chunks[chunk].Bytes;
            }
        }
        return bytes;
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
                    unit.bDistanceRequested = scatter.bDistanceResidency && distance <= species.ResidencyDistance;
                    unit.bRequested = unit.bDistanceRequested || scatter.Chunks[chunk].bPinned;
                    if (scatter.bDistanceResidency && !scatter.Chunks[chunk].bPinned && unit.bResident &&
                        distance > species.ResidencyDistance + m_Budget.ReleaseHysteresisMeters)
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
                    component->SetRangeActive(unit.FirstRange + range, !scatter.Chunks[candidate.Chunk].bDormant);
                }
            }
            unit.bResident = true;
            ApplyMegaHiddenInstances(scatter, candidate.Chunk, candidate.Species);
            ++m_Stats.ResidentThisFrame;
            m_Stats.BytesThisFrame += unit.Bytes;
            RecordEvent(scatter, candidate.Chunk, candidate.Species, true);
        }
        RecountStats();
        if (bFoliageChanged)
        {
            RebuildFoliageScene();
        }
        PublishSkyOccluders();
    }

    void ScatterSystem::RebuildSkyOccluderChunk(ScatterState& scatter, uint32_t chunkIndex)
    {
        if (!scatter.bSkyOccluderKeysValid)
        {
            return;
        }
        SkyOccluderChunk rebuilt;
        const auto& chunk = scatter.Data->Chunks[chunkIndex];
        rebuilt.ChunkIndex = chunkIndex;
        rebuilt.Key = (static_cast<uint64_t>(scatter.Handle) << 40) |
                      (static_cast<uint64_t>(chunk.ChunkZ) << 20) | chunk.ChunkX;
        // 描画用の並べ替えには依存せず、元の種・インスタンスの順で作る。
        for (uint32_t speciesIndex = 0; speciesIndex < scatter.Species.size(); ++speciesIndex)
        {
            const auto& species = scatter.Species[speciesIndex];
            if (!species.SkyOccluderDesc.bEnabled || chunkIndex >= species.Chunks.size() ||
                !species.Chunks[chunkIndex].bResident)
            {
                continue;
            }
            const auto& source = scatter.Data->Species[speciesIndex];
            const auto bytes = scatter.Data->GetInstances(chunkIndex, speciesIndex);
            const size_t count = bytes.size() / sizeof(Asset::InstanceRecord16);
            // 隠した個体は描画と同じく遮蔽物にも入れない。
            const uint64_t firstIndex = SpeciesInstanceOffset(*scatter.Data, chunkIndex, speciesIndex);
            const auto& hidden = scatter.HiddenInstances[chunkIndex];
            for (size_t index = 0; index < count; ++index)
            {
                if (std::binary_search(hidden.begin(), hidden.end(), static_cast<uint32_t>(firstIndex + index)))
                {
                    continue;
                }
                Asset::InstanceRecord16 record;
                Asset::ScatterInstanceValue value;
                if (!Asset::ReadScatterInstance(bytes.subspan(index * sizeof(record), sizeof(record)), record) ||
                    !Asset::DecodeScatterInstance(record, chunk, source, scatter.Data->ChunkSizeMeters,
                                                  scatter.Data->OriginX, scatter.Data->OriginZ, value))
                {
                    continue;
                }
                Rendering::SkyOccluderProxy proxy;
                // 復号済みの角度はラジアン、尺度は ScaleMin〜ScaleMax の実寸。
                if (Rendering::MakeSkyOccluderFromInstance(species.SkyOccluderDesc,
                    static_cast<float>(value.PositionX), static_cast<float>(value.PositionY),
                    static_cast<float>(value.PositionZ), value.YawRadians, value.Scale, proxy))
                {
                    rebuilt.Occluders.push_back(proxy);
                }
            }
        }
        auto& chunks = scatter.SkyOccluderChunks;
        auto existing = std::find_if(chunks.begin(), chunks.end(), [chunkIndex](const SkyOccluderChunk& entry)
        {
            return entry.ChunkIndex == chunkIndex;
        });
        if (rebuilt.Occluders.empty())
        {
            if (existing != chunks.end())
            {
                chunks.erase(existing);
                m_bSkyOccludersDirty = true;
            }
            return;
        }
        // Clear や再常駐でも番号を戻さず、同じ鍵の古い版と混同させない。
        if (m_SkyOccluderRevision == std::numeric_limits<uint64_t>::max())
        {
            LOG_WARNING("散布の遮蔽物の版番号を使い切ったため更新できません");
            return;
        }
        rebuilt.Revision = ++m_SkyOccluderRevision;
        if (existing != chunks.end())
        {
            *existing = std::move(rebuilt);
        }
        else
        {
            chunks.push_back(std::move(rebuilt));
        }
        m_bSkyOccludersDirty = true;
    }

    void ScatterSystem::PublishSkyOccluders()
    {
        if (!m_bSkyOccludersDirty || m_RenderWorld == nullptr || !m_RenderWorld->IsInitialized())
        {
            return;
        }
        // 不正な配列は出さず、次に内容が変わるまで再公開を試みない。
        m_bSkyOccludersDirty = false;
        Container::VariableArray<const SkyOccluderChunk*> chunks;
        size_t totalCount = 0;
        for (const auto& scatter : m_Scatters)
        {
            for (const auto& chunk : scatter.SkyOccluderChunks)
            {
                if (chunk.Occluders.size() > std::numeric_limits<uint32_t>::max() - totalCount)
                {
                    LOG_WARNING("散布の遮蔽物が範囲の表の上限を超えるため公開しません");
                    return;
                }
                totalCount += chunk.Occluders.size();
                chunks.push_back(&chunk);
            }
        }
        if (totalCount == 0)
        {
            m_RenderWorld->RemoveExtensionData(Rendering::kSkyOcclusionChunkRangesKey.Name);
            m_RenderWorld->RemoveExtensionData(Rendering::kSkyOcclusionChunkOccludersKey.Name);
            return;
        }
        std::sort(chunks.begin(), chunks.end(), [](const SkyOccluderChunk* a, const SkyOccluderChunk* b)
        {
            return a->Key < b->Key;
        });
        Container::VariableArray<Rendering::SkyOccluderChunkRange> ranges;
        Container::VariableArray<Rendering::SkyOccluderProxy> occluders;
        ranges.reserve(chunks.size());
        occluders.reserve(totalCount);
        const auto roundOutward = [](double value, bool bUpper)
        {
            float rounded = static_cast<float>(value);
            if (bUpper ? static_cast<double>(rounded) < value : static_cast<double>(rounded) > value)
            {
                rounded = std::nextafter(rounded, bUpper ? std::numeric_limits<float>::infinity()
                                                       : -std::numeric_limits<float>::infinity());
            }
            return rounded;
        };
        for (const auto* chunk : chunks)
        {
            Rendering::SkyOccluderChunkRange range;
            range.ChunkKey = chunk->Key;
            range.Revision = chunk->Revision;
            range.Offset = static_cast<uint32_t>(occluders.size());
            range.Count = static_cast<uint32_t>(chunk->Occluders.size());
            double minX = std::numeric_limits<double>::infinity();
            double minZ = minX;
            double maxX = -minX;
            double maxZ = -minX;
            for (const auto& proxy : chunk->Occluders)
            {
                double proxyMinX, proxyMinZ, proxyMaxX, proxyMaxZ;
                Rendering::GetSkyOccluderHorizontalBounds(proxy, proxyMinX, proxyMinZ, proxyMaxX, proxyMaxZ);
                minX = std::min(minX, proxyMinX);
                minZ = std::min(minZ, proxyMinZ);
                maxX = std::max(maxX, proxyMaxX);
                maxZ = std::max(maxZ, proxyMaxZ);
            }
            range.MinX = roundOutward(minX, false);
            range.MinZ = roundOutward(minZ, false);
            range.MaxX = roundOutward(maxX, true);
            range.MaxZ = roundOutward(maxZ, true);
            ranges.push_back(range);
            occluders.insert(occluders.end(), chunk->Occluders.begin(), chunk->Occluders.end());
        }
        if (!Rendering::IsValidSkyOccluderChunkRanges({ranges.data(), ranges.size()}, occluders.size()))
        {
            LOG_WARNING("散布の遮蔽物の範囲の表が不正なため公開しません");
            return;
        }
        m_RenderWorld->SetExtensionArray(Rendering::kSkyOcclusionChunkRangesKey,
            Container::Span<const Rendering::SkyOccluderChunkRange>(ranges.data(), ranges.size()));
        m_RenderWorld->SetExtensionArray(Rendering::kSkyOcclusionChunkOccludersKey,
            Container::Span<const Rendering::SkyOccluderProxy>(occluders.data(), occluders.size()));
    }

    bool ScatterSystem::MakeFoliageResident(ScatterState& scatter, uint32_t chunkIndex, uint32_t speciesIndex)
    {
        return BuildFoliageUnit(scatter, chunkIndex, speciesIndex, scatter.HiddenInstances[chunkIndex],
                                scatter.Species[speciesIndex].Chunks[chunkIndex]);
    }

    bool ScatterSystem::BuildFoliageUnit(const ScatterState& scatter, uint32_t chunkIndex, uint32_t speciesIndex,
                                         const Container::VariableArray<uint32_t>& hidden, ChunkUnit& unit) const
    {
        const auto& species = scatter.Species[speciesIndex];
        const auto& source = scatter.Data->Species[speciesIndex];
        const auto& chunk = scatter.Data->Chunks[chunkIndex];
        const auto bytes = scatter.Data->GetInstances(chunkIndex, speciesIndex);
        const size_t count = bytes.size() / sizeof(Asset::InstanceRecord16);
        Container::VariableArray<Rendering::FoliageInstance> instances;
        instances.reserve(count);
        bool bSorted = true;
        uint8_t previousRank = 0;
        const uint64_t firstIndex = SpeciesInstanceOffset(*scatter.Data, chunkIndex, speciesIndex);
        auto hiddenIt = std::lower_bound(hidden.begin(), hidden.end(), firstIndex);
        for (size_t index = 0; index < count; ++index)
        {
            if (hiddenIt != hidden.end() && *hiddenIt == firstIndex + index)
            {
                ++hiddenIt;
                continue;
            }
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
                for (uint32_t chunk = 0; chunk < species.Chunks.size(); ++chunk)
                {
                    const auto& unit = species.Chunks[chunk];
                    if (unit.bResident && !scatter.Chunks[chunk].bDormant)
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
                for (uint32_t chunk = 0; chunk < species.Chunks.size(); ++chunk)
                {
                    const auto& unit = species.Chunks[chunk];
                    if (!unit.bResident || scatter.Chunks[chunk].bDormant)
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

    bool ScatterSystem::ResolveInstanceIndex(const ScatterState& scatter, uint32_t chunk, uint32_t index,
                                              uint32_t& outSpecies, uint32_t& outLocalIndex) const
    {
        if (chunk >= scatter.Chunks.size())
        {
            return false;
        }
        uint64_t remaining = index;
        for (uint32_t species = 0; species < scatter.Species.size(); ++species)
        {
            const size_t count = scatter.Data->GetInstances(chunk, species).size() / sizeof(Asset::InstanceRecord16);
            if (remaining < count)
            {
                outSpecies = species;
                outLocalIndex = static_cast<uint32_t>(remaining);
                return true;
            }
            remaining -= count;
        }
        return false;
    }

    void ScatterSystem::ApplyMegaHiddenInstances(const ScatterState& scatter, uint32_t chunk, uint32_t speciesIndex) const
    {
        const auto& species = scatter.Species[speciesIndex];
        auto* component = ResolveMega(scatter, species);
        if (component == nullptr || chunk >= species.Chunks.size())
        {
            return;
        }
        const auto& unit = species.Chunks[chunk];
        const uint64_t first = SpeciesInstanceOffset(*scatter.Data, chunk, speciesIndex);
        const uint64_t end = first + scatter.Data->GetInstances(chunk, speciesIndex).size() / sizeof(Asset::InstanceRecord16);
        const auto& hidden = scatter.HiddenInstances[chunk];
        for (auto it = std::lower_bound(hidden.begin(), hidden.end(), first); it != hidden.end() && *it < end; ++it)
        {
            component->SetInstanceHidden(unit.FirstRange * kRangeRows + static_cast<uint32_t>(*it - first), true);
        }
    }

    bool ScatterSystem::VisitInstancesInRadius(const Math::Vector3& center, float radius,
                                                const ScatterQueryFilter& filter, void* context,
                                                void (*visitor)(void*, const ScatterQueryHit&)) const
    {
        if (radius < 0.0f || !std::isfinite(radius))
        {
            return true;
        }
        bool bComplete = true;
        const double radiusSquared = static_cast<double>(radius) * radius;
        for (const auto& scatter : m_Scatters)
        {
            if (filter.Scatter != kInvalidScatterHandle && filter.Scatter != scatter.Handle)
            {
                continue;
            }
            const auto& data = *scatter.Data;
            for (uint32_t chunkIndex = 0; chunkIndex < data.Chunks.size(); ++chunkIndex)
            {
                const auto& chunk = data.Chunks[chunkIndex];
                // 箱との距離も double で計算し、大きい有限の半径での二乗の桁あふれを避ける。
                const double coordinates[] = {center.x, center.y, center.z};
                double chunkDistanceSquared = 0.0;
                for (uint32_t axis = 0; axis < 3; ++axis)
                {
                    const double delta = std::max(static_cast<double>(chunk.AabbMin[axis]) - coordinates[axis],
                                                  std::max(0.0, coordinates[axis] - chunk.AabbMax[axis]));
                    chunkDistanceSquared += delta * delta;
                }
                if (chunkDistanceSquared > radiusSquared)
                {
                    continue;
                }
                uint64_t firstIndex = 0;
                const auto& hidden = scatter.HiddenInstances[chunkIndex];
                for (uint32_t speciesIndex = 0; speciesIndex < scatter.Species.size(); ++speciesIndex)
                {
                    const auto bytes = data.GetInstances(chunkIndex, speciesIndex);
                    const size_t count = bytes.size() / sizeof(Asset::InstanceRecord16);
                    const uint64_t speciesFirst = firstIndex;
                    firstIndex += count;
                    const auto& species = scatter.Species[speciesIndex];
                    if (speciesIndex >= 64 || (filter.SpeciesMask & (1ull << speciesIndex)) == 0 ||
                        species.Chunks.empty() || count == 0)
                    {
                        continue;
                    }
                    if (!species.Chunks[chunkIndex].bResident)
                    {
                        bComplete = false;
                        continue;
                    }
                    auto hiddenIt = std::lower_bound(hidden.begin(), hidden.end(), speciesFirst);
                    for (size_t index = 0; index < count; ++index)
                    {
                        const uint64_t instanceIndex = speciesFirst + index;
                        if (hiddenIt != hidden.end() && *hiddenIt == instanceIndex)
                        {
                            ++hiddenIt;
                            continue;
                        }
                        Asset::InstanceRecord16 record;
                        Asset::ScatterInstanceValue value;
                        if (!Asset::ReadScatterInstance(bytes.subspan(index * sizeof(record), sizeof(record)), record) ||
                            !Asset::DecodeScatterInstance(record, chunk, data.Species[speciesIndex], data.ChunkSizeMeters,
                                                          data.OriginX, data.OriginZ, value))
                        {
                            continue;
                        }
                        const double dx = value.PositionX - center.x;
                        const double dy = value.PositionY - center.y;
                        const double dz = value.PositionZ - center.z;
                        if (dx * dx + dy * dy + dz * dz <= radiusSquared &&
                            instanceIndex <= std::numeric_limits<uint32_t>::max())
                        {
                            ScatterQueryHit hit;
                            hit.Id = {scatter.Handle, chunkIndex, static_cast<uint32_t>(instanceIndex)};
                            hit.Species = speciesIndex;
                            hit.Position = Math::Vector3(static_cast<float>(value.PositionX), static_cast<float>(value.PositionY),
                                                         static_cast<float>(value.PositionZ));
                            hit.YawRadians = value.YawRadians;
                            hit.Scale = value.Scale;
                            visitor(context, hit);
                        }
                    }
                }
            }
        }
        return bComplete;
    }

    bool ScatterSystem::SetInstanceHidden(const ScatterInstanceId& id, bool bHidden)
    {
        auto* scatter = Find(id.Scatter);
        uint32_t speciesIndex = 0;
        uint32_t localIndex = 0;
        if (scatter == nullptr || !ResolveInstanceIndex(*scatter, id.Chunk, id.Index, speciesIndex, localIndex))
        {
            return false;
        }
        auto& hidden = scatter->HiddenInstances[id.Chunk];
        const auto it = std::lower_bound(hidden.begin(), hidden.end(), id.Index);
        const bool bWasHidden = it != hidden.end() && *it == id.Index;
        if (bWasHidden == bHidden)
        {
            return true;
        }
        if (bHidden && GetHiddenInstanceCount(id.Scatter) == std::numeric_limits<uint32_t>::max())
        {
            return false;
        }
        auto replacement = hidden;
        const auto replacementIt = std::lower_bound(replacement.begin(), replacement.end(), id.Index);
        if (bHidden)
        {
            replacement.insert(replacementIt, id.Index);
        }
        else
        {
            replacement.erase(replacementIt);
        }
        auto& species = scatter->Species[speciesIndex];
        const bool bRebuildFoliage = species.RenderPath == Asset::ScatterRenderPath::Foliage &&
                                    !species.Chunks.empty() && species.Chunks[id.Chunk].bResident;
        ChunkUnit rebuilt;
        if (bRebuildFoliage)
        {
            rebuilt.bWarnedUnsortedRank = species.Chunks[id.Chunk].bWarnedUnsortedRank;
            if (!BuildFoliageUnit(*scatter, id.Chunk, speciesIndex, replacement, rebuilt))
            {
                return false;
            }
        }
        hidden = std::move(replacement);
        if (auto* component = ResolveMega(*scatter, species))
        {
            component->SetInstanceHidden(species.Chunks[id.Chunk].FirstRange * kRangeRows + localIndex, bHidden);
        }
        if (bRebuildFoliage)
        {
            auto& unit = species.Chunks[id.Chunk];
            unit.FoliageInstances = std::move(rebuilt.FoliageInstances);
            unit.FoliageBounds = rebuilt.FoliageBounds;
            unit.bWarnedUnsortedRank = rebuilt.bWarnedUnsortedRank;
            RebuildFoliageScene();
        }
        // 常駐中に切り替えた個体は、そのチャンクの遮蔽物の配列を作り直して外す・戻す。
        if (species.SkyOccluderDesc.bEnabled && id.Chunk < species.Chunks.size() && species.Chunks[id.Chunk].bResident)
        {
            RebuildSkyOccluderChunk(*scatter, id.Chunk);
            PublishSkyOccluders();
        }
        return true;
    }

    bool ScatterSystem::IsInstanceHidden(const ScatterInstanceId& id) const
    {
        const auto* scatter = Find(id.Scatter);
        uint32_t species = 0;
        uint32_t localIndex = 0;
        if (scatter == nullptr || !ResolveInstanceIndex(*scatter, id.Chunk, id.Index, species, localIndex))
        {
            return false;
        }
        const auto& hidden = scatter->HiddenInstances[id.Chunk];
        return std::binary_search(hidden.begin(), hidden.end(), id.Index);
    }

    uint32_t ScatterSystem::GetHiddenInstanceCount(ScatterHandle handle) const
    {
        const auto* scatter = Find(handle);
        uint32_t count = 0;
        if (scatter != nullptr)
        {
            for (const auto& hidden : scatter->HiddenInstances)
            {
                count += static_cast<uint32_t>(hidden.size());
            }
        }
        return count;
    }

    bool ScatterSystem::SerializeHiddenInstances(ScatterHandle handle, Container::VariableArray<uint8_t>& outBytes) const
    {
        const auto* scatter = Find(handle);
        if (scatter == nullptr)
        {
            return false;
        }
        Container::VariableArray<uint8_t> bytes;
        const uint32_t count = GetHiddenInstanceCount(handle);
        bytes.reserve(24ull + static_cast<uint64_t>(count) * 8);
        bytes.push_back('N');
        bytes.push_back('V');
        bytes.push_back('H');
        bytes.push_back('D');
        WriteHiddenInteger(bytes, 0, 4);
        WriteHiddenInteger(bytes, scatter->Data->PayloadHash, 8);
        WriteHiddenInteger(bytes, scatter->Chunks.size(), 4);
        WriteHiddenInteger(bytes, count, 4);
        for (uint32_t chunk = 0; chunk < scatter->HiddenInstances.size(); ++chunk)
        {
            for (uint32_t index : scatter->HiddenInstances[chunk])
            {
                WriteHiddenInteger(bytes, chunk, 4);
                WriteHiddenInteger(bytes, index, 4);
            }
        }
        outBytes = std::move(bytes);
        return true;
    }

    bool ScatterSystem::RestoreHiddenInstances(ScatterHandle handle, Container::Span<const uint8_t> bytes)
    {
        auto* scatter = Find(handle);
        if (scatter == nullptr || bytes.size() < 24 || bytes[0] != 'N' || bytes[1] != 'V' ||
            bytes[2] != 'H' || bytes[3] != 'D' || ReadHiddenInteger(bytes, 4, 4) != 0 ||
            ReadHiddenInteger(bytes, 8, 8) != scatter->Data->PayloadHash ||
            ReadHiddenInteger(bytes, 16, 4) != scatter->Chunks.size())
        {
            return false;
        }
        const uint64_t count = ReadHiddenInteger(bytes, 20, 4);
        if (bytes.size() != 24 + count * 8)
        {
            return false;
        }
        Container::VariableArray<Container::VariableArray<uint32_t>> replacement(scatter->Chunks.size());
        uint32_t previousChunk = 0;
        uint32_t previousIndex = 0;
        for (uint64_t entry = 0; entry < count; ++entry)
        {
            const size_t offset = static_cast<size_t>(24 + entry * 8);
            const uint32_t chunk = static_cast<uint32_t>(ReadHiddenInteger(bytes, offset, 4));
            const uint32_t index = static_cast<uint32_t>(ReadHiddenInteger(bytes, offset + 4, 4));
            uint32_t species = 0;
            uint32_t localIndex = 0;
            if ((entry != 0 && (chunk < previousChunk || (chunk == previousChunk && index <= previousIndex))) ||
                !ResolveInstanceIndex(*scatter, chunk, index, species, localIndex))
            {
                return false;
            }
            replacement[chunk].push_back(index);
            previousChunk = chunk;
            previousIndex = index;
        }
        // foliage の復号も置き換え前に済ませ、失敗時に集合や描画を変えない。
        struct FoliageReplacement
        {
            uint32_t Chunk = 0;
            uint32_t Species = 0;
            ChunkUnit Unit;
        };
        Container::VariableArray<FoliageReplacement> foliage;
        for (uint32_t chunk = 0; chunk < replacement.size(); ++chunk)
        {
            const auto& oldHidden = scatter->HiddenInstances[chunk];
            const auto& newHidden = replacement[chunk];
            for (uint32_t speciesIndex = 0; speciesIndex < scatter->Species.size(); ++speciesIndex)
            {
                const auto& species = scatter->Species[speciesIndex];
                if (species.RenderPath != Asset::ScatterRenderPath::Foliage || species.Chunks.empty() ||
                    !species.Chunks[chunk].bResident)
                {
                    continue;
                }
                const uint64_t first = SpeciesInstanceOffset(*scatter->Data, chunk, speciesIndex);
                const uint64_t end = first + scatter->Data->GetInstances(chunk, speciesIndex).size() / sizeof(Asset::InstanceRecord16);
                const auto oldBegin = std::lower_bound(oldHidden.begin(), oldHidden.end(), first);
                const auto oldEnd = std::lower_bound(oldBegin, oldHidden.end(), end);
                const auto newBegin = std::lower_bound(newHidden.begin(), newHidden.end(), first);
                const auto newEnd = std::lower_bound(newBegin, newHidden.end(), end);
                if (std::equal(oldBegin, oldEnd, newBegin, newEnd))
                {
                    continue;
                }
                foliage.emplace_back();
                auto& prepared = foliage.back();
                prepared.Chunk = chunk;
                prepared.Species = speciesIndex;
                prepared.Unit.bWarnedUnsortedRank = species.Chunks[chunk].bWarnedUnsortedRank;
                if (!BuildFoliageUnit(*scatter, chunk, speciesIndex, newHidden, prepared.Unit))
                {
                    return false;
                }
            }
        }
        auto previous = std::move(scatter->HiddenInstances);
        scatter->HiddenInstances = std::move(replacement);
        for (uint32_t chunk = 0; chunk < previous.size(); ++chunk)
        {
            const auto& hidden = scatter->HiddenInstances[chunk];
            for (uint32_t index : previous[chunk])
            {
                if (!std::binary_search(hidden.begin(), hidden.end(), index))
                {
                    uint32_t speciesIndex = 0;
                    uint32_t localIndex = 0;
                    if (ResolveInstanceIndex(*scatter, chunk, index, speciesIndex, localIndex))
                    {
                        const auto& species = scatter->Species[speciesIndex];
                        if (auto* component = ResolveMega(*scatter, species))
                        {
                            component->SetInstanceHidden(species.Chunks[chunk].FirstRange * kRangeRows + localIndex, false);
                        }
                    }
                }
            }
            bool bOccluderResident = false;
            for (uint32_t species = 0; species < scatter->Species.size(); ++species)
            {
                ApplyMegaHiddenInstances(*scatter, chunk, species);
                const auto& state = scatter->Species[species];
                bOccluderResident |= state.SkyOccluderDesc.bEnabled && chunk < state.Chunks.size() &&
                                     state.Chunks[chunk].bResident;
            }
            // 隠す集合が変わった常駐のチャンクは、遮蔽物の配列も同じ集合で作り直す。
            if (bOccluderResident && !std::equal(previous[chunk].begin(), previous[chunk].end(), hidden.begin(), hidden.end()))
            {
                RebuildSkyOccluderChunk(*scatter, chunk);
            }
        }
        for (auto& prepared : foliage)
        {
            auto& unit = scatter->Species[prepared.Species].Chunks[prepared.Chunk];
            unit.FoliageInstances = std::move(prepared.Unit.FoliageInstances);
            unit.FoliageBounds = prepared.Unit.FoliageBounds;
            unit.bWarnedUnsortedRank = prepared.Unit.bWarnedUnsortedRank;
        }
        if (!foliage.empty())
        {
            RebuildFoliageScene();
        }
        PublishSkyOccluders();
        return true;
    }

    void ScatterSystem::Clear()
    {
        for (auto& scatter : m_Scatters)
        {
            ReleaseEntities(scatter);
            m_bSkyOccludersDirty |= !scatter.SkyOccluderChunks.empty();
        }
        m_Scatters.clear();
        m_Stats = {};
        RebuildFoliageScene();
        PublishSkyOccluders();
    }
} // namespace NorvesLib::Core::Scatter
