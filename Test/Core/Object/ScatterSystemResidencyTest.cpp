// 散布の Entity 数、常駐予算、解放後の描画一覧と常駐記録の再現性を確かめる。
#include "Component/MegaGeometryScatterComponent.h"
#include "Ecs/EcsWorld.h"
#include "Object/World.h"
#include "Scatter/ScatterSystem.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <utility>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Asset;
using namespace NorvesLib::Core::Rendering;
using namespace NorvesLib::Core::Scatter;

namespace
{
    constexpr uint32_t GridSize = 16;
    constexpr uint32_t ChunkCount = GridSize * GridSize;
    constexpr uint32_t SpeciesCount = 4;
    constexpr uint32_t MegaCount = 2;
    constexpr uint32_t UnitCount = ChunkCount * SpeciesCount;
    constexpr uint32_t InstanceCount = 100000;
    constexpr float ChunkSize = 32.0f;
    constexpr float ResidencyDistance = 64.0f;
    constexpr const char* SpeciesNames[] = {"rock", "log", "shrub", "herb"};

    const char* Utf8(const char8_t* text)
    {
        return reinterpret_cast<const char*>(text);
    }

    void Check(bool bCondition, uint32_t point, uint32_t frame, int chunk, int species, const char* message)
    {
        if (!bCondition)
        {
            std::cerr << Utf8(u8"検査失敗: 点=") << point << Utf8(u8" フレーム=") << frame
                      << Utf8(u8" チャンク=") << chunk << Utf8(u8" 種=") << species << " " << message << "\n";
            std::exit(1);
        }
    }

    MegaGeometry::MegaMeshHandle MakeMegaMeshHandle(uint64_t id)
    {
        MegaGeometry::MegaMeshHandle handle;
        handle.Id = id;
        return handle;
    }

    size_t CountSubtree(const Entity& entity)
    {
        size_t count = 1;
        for (const Entity* child : entity.GetChildEntities())
        {
            count += CountSubtree(*child);
        }
        return count;
    }

    size_t CountEntities(const World& world)
    {
        size_t count = 0;
        for (const Entity* root : world.GetRootEntities())
        {
            count += CountSubtree(*root);
        }
        return count;
    }

    Container::TSharedPtr<const CookedScatterData> MakeData()
    {
        CookedScatterWriteDesc desc;
        desc.Seed = 9031;
        desc.ChunkSizeMeters = ChunkSize;
        desc.GridCountX = GridSize;
        desc.GridCountZ = GridSize;
        desc.Species.resize(SpeciesCount);
        for (uint32_t species = 0; species < SpeciesCount; ++species)
        {
            auto& value = desc.Species[species];
            value.Name = SpeciesNames[species];
            value.RenderPath = species < MegaCount ? ScatterRenderPath::Mega : ScatterRenderPath::Foliage;
            value.LodCount = 1;
            value.LodPaths[0] = "Meshes/ScatterTriangle";
            value.MeshRadiusMeters = 0.5f;
            value.MinSpacingMeters = 1.0f;
        }
        desc.Chunks.resize(ChunkCount);
        desc.SpeciesRanges.resize(UnitCount);
        desc.Instances.reserve(InstanceCount);
        for (uint32_t chunk = 0; chunk < ChunkCount; ++chunk)
        {
            auto& bounds = desc.Chunks[chunk];
            bounds.ChunkX = chunk % GridSize;
            bounds.ChunkZ = chunk / GridSize;
            bounds.MinY = bounds.MaxY = 0.0f;
            bounds.AabbMin[0] = bounds.ChunkX * ChunkSize;
            bounds.AabbMin[2] = bounds.ChunkZ * ChunkSize;
            bounds.AabbMax[0] = bounds.AabbMin[0] + ChunkSize;
            bounds.AabbMax[2] = bounds.AabbMin[2] + ChunkSize;
            for (uint32_t species = 0; species < SpeciesCount; ++species)
            {
                const uint32_t unit = chunk * SpeciesCount + species;
                const uint32_t count = InstanceCount / UnitCount + (unit < InstanceCount % UnitCount ? 1 : 0);
                desc.SpeciesRanges[unit] = {desc.Instances.size(), count};
                Check(count * sizeof(InstanceRecord16) <= 8192, 2, 0, chunk, species,
                      Utf8(u8"単独の常駐単位がバイト予算を超えています"));
                for (uint32_t index = 0; index < count; ++index)
                {
                    ScatterInstanceValue value;
                    value.PositionX = bounds.AabbMin[0] + 1.0 + (index % 10) * 3.0;
                    value.PositionZ = bounds.AabbMin[2] + 1.0 + (index / 10) * 3.0;
                    value.Rank = static_cast<uint8_t>(index * 255 / (count - 1));
                    InstanceRecord16 record;
                    Check(EncodeScatterInstance(value, bounds, desc.Species[species], ChunkSize, 0.0, 0.0, record),
                          1, 0, chunk, species, Utf8(u8"レコードを符号化できません"));
                    desc.Instances.push_back(record);
                }
            }
        }
        Check(desc.Instances.size() == InstanceCount, 1, 0, -1, -1, Utf8(u8"入力が十万本ではありません"));
        Container::VariableArray<uint8_t> bytes;
        Check(WriteCookedScatter(desc, bytes) == CookedScatterWriteStatus::Success,
              1, 0, -1, -1, Utf8(u8"散布を書き出せません"));
        auto parsed = ParseCookedScatter(AssetBlob::CopyBytes({bytes.data(), bytes.size()}));
        Check(parsed.Succeeded(), 1, 0, -1, -1, Utf8(u8"散布を読み戻せません"));
        return Container::MakeShared<CookedScatterData>(std::move(parsed.Scatter));
    }

    Container::TSharedPtr<const FoliageMeshData> MakeTriangle()
    {
        auto mesh = Container::MakeShared<FoliageMeshData>();
        mesh->Height = 1.0f;
        mesh->Vertices.resize(3);
        mesh->Vertices[1].Position[0] = 0.5f;
        mesh->Vertices[2].Position[1] = 1.0f;
        mesh->Indices = {0, 1, 2};
        return mesh;
    }

    void CheckFoliage(const ScatterSystem& system, const CookedScatterData& data,
                      const Container::VariableArray<ScatterSpeciesBinding>& bindings, const bool* residents, uint32_t frame)
    {
        bool seen[UnitCount] = {};
        uint64_t actualInstances = 0;
        for (const auto& batch : system.GetFoliageScene().Batches)
        {
            uint32_t species = MegaCount;
            while (species < SpeciesCount && batch.Mesh != bindings[species].FoliageTemplate.Mesh)
            {
                ++species;
            }
            Check(species < SpeciesCount, 3, frame, -1, species, Utf8(u8"未知の種の束があります"));
            Check(batch.Cells && batch.Instances, 3, frame, -1, species, Utf8(u8"束のセルまたは実体がありません"));
            uint64_t first = 0;
            for (const auto& cell : *batch.Cells)
            {
                // 各種の半径を除いたセルの左下は、元のチャンクの左下と一致する。
                const float radius = data.Species[species].MeshRadiusMeters * data.Species[species].ScaleMax;
                int chunk = -1;
                for (uint32_t candidate = 0; candidate < ChunkCount; ++candidate)
                {
                    const auto& bounds = data.Chunks[candidate];
                    if (cell.BoundsMin[0] + radius == bounds.AabbMin[0] &&
                        cell.BoundsMin[2] + radius == bounds.AabbMin[2])
                    {
                        chunk = static_cast<int>(candidate);
                        break;
                    }
                }
                Check(chunk >= 0, 3, frame, chunk, species, Utf8(u8"セルが元のチャンクに対応しません"));
                const uint32_t unit = chunk * SpeciesCount + species;
                Check(residents[unit] && !seen[unit], 3, frame, chunk, species,
                      Utf8(u8"解放済みまたは重複したチャンクのセルがあります"));
                seen[unit] = true;
                Check(cell.InstanceCount == data.SpeciesRanges[unit].Count && cell.FirstInstance == first,
                      3, frame, chunk, species, Utf8(u8"セルの実体の範囲が常駐チャンクの本数と違います"));
                first += cell.InstanceCount;
                Check(first <= batch.Instances->size(), 3, frame, chunk, species, Utf8(u8"セルが実体配列を越えています"));
                const auto& bounds = data.Chunks[chunk];
                for (uint64_t index = cell.FirstInstance; index < first; ++index)
                {
                    const auto& instance = (*batch.Instances)[index];
                    Check(instance.Position[0] >= bounds.AabbMin[0] && instance.Position[0] < bounds.AabbMax[0] &&
                          instance.Position[2] >= bounds.AabbMin[2] && instance.Position[2] < bounds.AabbMax[2],
                          3, frame, chunk, species, Utf8(u8"別のチャンクの実体がセルに残っています"));
                }
            }
            Check(first == batch.Instances->size(), 3, frame, -1, species, Utf8(u8"セルに属さない実体が残っています"));
            actualInstances += batch.Instances->size();
        }
        uint64_t expectedInstances = 0;
        for (uint32_t chunk = 0; chunk < ChunkCount; ++chunk)
        {
            for (uint32_t species = MegaCount; species < SpeciesCount; ++species)
            {
                const uint32_t unit = chunk * SpeciesCount + species;
                Check(seen[unit] == residents[unit], 3, frame, chunk, species, Utf8(u8"常駐状態とセルの有無が違います"));
                if (residents[unit])
                {
                    expectedInstances += data.SpeciesRanges[unit].Count;
                }
            }
        }
        Check(actualInstances == expectedInstances, 3, frame, -1, -1, Utf8(u8"束の実体総数が常駐本数の和と違います"));
    }

    Container::VariableArray<ScatterResidencyEvent> Run(const Container::TSharedPtr<const CookedScatterData>& data)
    {
        World world;
        world.Initialize();
        Entity* root = world.SpawnEntity<Entity>();
        Check(root != nullptr && world.SpawnEntity<Entity>(root) != nullptr,
              1, 0, -1, -1, Utf8(u8"基準となる Entity の木を作れません"));
        const size_t initialEntities = CountEntities(world);
        // 行の作成も時計に依存させず、LateTick の仕事の列で進める。
        world.GetEcsWorld().GetMassWork().SetBudget({8192, 0.0f});
        ScatterSystem system;
        ScatterBudget budget;
        budget.MaxResidentPerFrame = 4;
        budget.MaxBytesPerFrame = 8192;
        budget.MaxMillisecondsPerFrame = 0.0f;
        system.SetBudget(budget);
        Container::VariableArray<ScatterSpeciesBinding> bindings;
        bindings.resize(SpeciesCount);
        for (uint32_t species = 0; species < SpeciesCount; ++species)
        {
            bindings[species].Name = SpeciesNames[species];
            bindings[species].ResidencyDistance = ResidencyDistance;
            if (species < MegaCount)
            {
                bindings[species].MegaMesh = MakeMegaMeshHandle(species + 1);
            }
            else
            {
                bindings[species].FoliageTemplate.Mesh = MakeTriangle();
            }
        }
        const ScatterHandle handle = system.Load(world, data, {bindings.data(), bindings.size()});
        Check(handle != kInvalidScatterHandle, 1, 0, -1, -1, Utf8(u8"散布を Load できません"));
        Check(CountEntities(world) == initialEntities + MegaCount, 1, 0, -1, -1,
              Utf8(u8"Load 後の Entity の増分が mega の二種と違います"));

        bool residents[UnitCount] = {};
        uint32_t admitted[SpeciesCount] = {};
        uint32_t released[SpeciesCount] = {};
        bool bCountLimited = false;
        bool bBytesLimited = false;
        size_t logSize = 0;
        constexpr uint32_t HoldFrames = 32;
        constexpr uint32_t WalkFrames = 425;
        constexpr uint32_t LastFrame = HoldFrames * 2 + WalkFrames * 2 + 2;
        for (uint32_t frame = 1; frame <= LastFrame; ++frame)
        {
            if (frame == 2)
            {
                budget.MaxResidentPerFrame = 6;
                system.SetBudget(budget);
            }
            const uint32_t step = frame <= HoldFrames ? 0 :
                frame <= HoldFrames + WalkFrames ? frame - HoldFrames :
                frame <= HoldFrames + WalkFrames * 2 ? HoldFrames + WalkFrames * 2 - frame : 0;
            const float coordinate = static_cast<float>(step) * (2.0f / std::sqrt(2.0f));
            const bool bFar = frame > LastFrame - 2;
            const NorvesLib::Math::Vector3 camera = bFar ? NorvesLib::Math::Vector3(-2048.0f, 0.0f, -2048.0f) :
                NorvesLib::Math::Vector3(coordinate, 0.0f, coordinate);
            const uint64_t previousVersion = system.GetFoliageVersion();
            system.Update(camera);
            world.Tick(1.0f / 60.0f);
            world.LateTick(1.0f / 60.0f);
            Check(CountEntities(world) == initialEntities + MegaCount, 1, frame, -1, -1,
                  Utf8(u8"フレームを進めると Entity の数が変わりました"));
            const auto& stats = system.GetStats();
            Check(stats.ResidentThisFrame <= budget.MaxResidentPerFrame, 2, frame, -1, -1, Utf8(u8"個数予算を超えました"));
            Check(stats.BytesThisFrame <= budget.MaxBytesPerFrame, 2, frame, -1, -1, Utf8(u8"バイト予算を超えました"));
            bCountLimited = bCountLimited || (stats.PendingUnits > 0 && stats.ResidentThisFrame == budget.MaxResidentPerFrame);
            bBytesLimited = bBytesLimited || (stats.PendingUnits > 0 && stats.ResidentThisFrame < budget.MaxResidentPerFrame &&
                stats.BytesThisFrame + (InstanceCount / UnitCount) * sizeof(InstanceRecord16) > budget.MaxBytesPerFrame);
            const auto log = system.GetResidencyLog(handle);
            Check(log.size() >= logSize, 4, frame, -1, -1, Utf8(u8"記録が途中で短くなりました"));
            uint32_t frameAdmissions = 0;
            uint32_t frameReleases = 0;
            uint64_t frameBytes = 0;
            for (size_t index = logSize; index < log.size(); ++index)
            {
                const auto& event = log[index];
                Check(event.Frame == frame && event.Chunk < ChunkCount && event.Species < SpeciesCount,
                      4, frame, event.Chunk, event.Species, Utf8(u8"記録のフレームまたは対象が不正です"));
                const uint32_t unit = event.Chunk * SpeciesCount + event.Species;
                Check(residents[unit] != event.bResident, 3, frame, event.Chunk, event.Species, Utf8(u8"常駐の遷移が重複しています"));
                residents[unit] = event.bResident;
                if (event.bResident)
                {
                    ++admitted[event.Species];
                    ++frameAdmissions;
                    frameBytes += data->SpeciesRanges[unit].Count * sizeof(InstanceRecord16);
                }
                else
                {
                    ++released[event.Species];
                    ++frameReleases;
                }
            }
            logSize = log.size();
            Check(frameAdmissions == stats.ResidentThisFrame && frameBytes == stats.BytesThisFrame &&
                  frameReleases == stats.ReleasedThisFrame, 2, frame, -1, -1, Utf8(u8"記録とフレームの集計が違います"));
            uint32_t residentUnits = 0;
            uint64_t residentBytes = 0;
            for (uint32_t chunk = 0; chunk < ChunkCount; ++chunk)
            {
                const auto& bounds = data->Chunks[chunk];
                const float dx = camera.x < bounds.AabbMin[0] ? bounds.AabbMin[0] - camera.x :
                    camera.x > bounds.AabbMax[0] ? camera.x - bounds.AabbMax[0] : 0.0f;
                const float dz = camera.z < bounds.AabbMin[2] ? bounds.AabbMin[2] - camera.z :
                    camera.z > bounds.AabbMax[2] ? camera.z - bounds.AabbMax[2] : 0.0f;
                const float releaseDistance = ResidencyDistance + budget.ReleaseHysteresisMeters;
                for (uint32_t species = 0; species < SpeciesCount; ++species)
                {
                    const uint32_t unit = chunk * SpeciesCount + species;
                    Check(system.IsResident(handle, chunk, species) == residents[unit], 3, frame, chunk, species,
                          Utf8(u8"常駐の記録と現在の状態が違います"));
                    if (dx * dx + dz * dz > releaseDistance * releaseDistance)
                    {
                        Check(!residents[unit], 3, frame, chunk, species, Utf8(u8"解放距離を越えたチャンクが常駐しています"));
                    }
                    if (frame == HoldFrames && dx * dx + dz * dz <= ResidencyDistance * ResidencyDistance)
                    {
                        Check(residents[unit], 3, frame, chunk, species, Utf8(u8"静止中の近いチャンクが常駐していません"));
                    }
                    if (residents[unit])
                    {
                        ++residentUnits;
                        residentBytes += data->SpeciesRanges[unit].Count * sizeof(InstanceRecord16);
                    }
                    if (species < MegaCount)
                    {
                        const auto* component = system.GetMegaComponent(handle, species);
                        Check(component != nullptr, 3, frame, chunk, species, Utf8(u8"mega の Component がありません"));
                        uint32_t first = 0;
                        uint32_t count = 0;
                        system.GetChunkRanges(handle, chunk, species, first, count);
                        Check(count > 0 && first + count <= component->GetRangeCount(), 3, frame, chunk, species,
                              Utf8(u8"チャンクの範囲がありません"));
                        for (uint32_t range = first; range < first + count; ++range)
                        {
                            Check(component->IsRangeActive(range) == residents[unit], 3, frame, chunk, species,
                                  Utf8(u8"常駐の印と範囲の Active が違います"));
                            if (!residents[unit])
                            {
                                Check(!component->IsRangePublished(range), 3, frame, chunk, species, Utf8(u8"解放後の範囲が公開されています"));
                            }
                        }
                    }
                }
            }
            Check(stats.ResidentUnits == residentUnits && stats.ResidentBytes == residentBytes,
                  3, frame, -1, -1, Utf8(u8"常駐資源の集計が記録と違います"));
            CheckFoliage(system, *data, bindings, residents, frame);
            if (frame == HoldFrames)
            {
                Check(stats.ResidentUnits > 0 && stats.PendingUnits == 0 && frameAdmissions == 0 && frameReleases == 0,
                      3, frame, -1, -1, Utf8(u8"静止中の常駐が収束しません"));
                Check(system.GetFoliageVersion() == previousVersion, 3, frame, -1, -1,
                      Utf8(u8"常駐の組が変わらないのに foliage の版が増えました"));
                for (uint32_t species = 0; species < MegaCount; ++species)
                {
                    const auto* component = system.GetMegaComponent(handle, species);
                    Check(component->GetRowCount() == component->GetInstanceCount(), 3, frame, -1, species,
                          Utf8(u8"LateTick で mega の行の作成が完了しません"));
                }
            }
            if (bFar)
            {
                Check(stats.ResidentUnits == 0 && stats.ResidentBytes == 0 && system.GetFoliageScene().IsEmpty(),
                      3, frame, -1, -1, Utf8(u8"全チャンクを遠ざけても資源または foliage が残っています"));
            }
        }
        Check(bCountLimited && bBytesLimited, 2, LastFrame, -1, -1, Utf8(u8"個数またはバイトの上限に達する負荷がありません"));
        for (uint32_t species = 0; species < SpeciesCount; ++species)
        {
            Check(admitted[species] > 0 && released[species] == admitted[species], 3, LastFrame, -1, species,
                  Utf8(u8"種の常駐と全解放を通っていません"));
        }
        const auto log = system.GetResidencyLog(handle);
        Container::VariableArray<ScatterResidencyEvent> result(log.begin(), log.end());
        system.Unload(handle);
        Check(CountEntities(world) == initialEntities, 1, LastFrame, -1, -1, Utf8(u8"Unload で元の Entity 数に戻りません"));
        world.Tick(1.0f / 60.0f);
        world.LateTick(1.0f / 60.0f);
        Check(CountEntities(world) == initialEntities, 1, LastFrame, -1, -1, Utf8(u8"Unload 後の Tick で Entity 数が変わりました"));
        world.Finalize();
        return result;
    }

    void EncodeEvent(const ScatterResidencyEvent& event, uint8_t (&bytes)[13])
    {
        // 構造体の未規定のパディングを除き、全フィールドのビットを比較する。
        const uint32_t fields[] = {event.Frame, event.Chunk, event.Species};
        for (uint32_t field = 0; field < 3; ++field)
        {
            for (uint32_t byte = 0; byte < 4; ++byte)
            {
                bytes[field * 4 + byte] = static_cast<uint8_t>(fields[field] >> (byte * 8));
            }
        }
        bytes[12] = event.bResident ? 1 : 0;
    }
}

int main()
{
    const auto data = MakeData();
    const auto first = Run(data);
    const auto second = Run(data);
    Check(first.size() == second.size(), 4, 0, -1, -1, Utf8(u8"二回の常駐記録の長さが違います"));
    for (size_t index = 0; index < first.size(); ++index)
    {
        uint8_t firstBytes[13];
        uint8_t secondBytes[13];
        EncodeEvent(first[index], firstBytes);
        EncodeEvent(second[index], secondBytes);
        Check(std::memcmp(firstBytes, secondBytes, sizeof(firstBytes)) == 0, 4,
              first[index].Frame, first[index].Chunk, first[index].Species, Utf8(u8"二回の常駐記録のビットが一致しません"));
    }
    std::cout << Utf8(u8"ScatterSystemResidencyTest 合格: 十万本、二回の常駐記録 ") << first.size() << Utf8(u8" 件が一致\n");
    return 0;
}
