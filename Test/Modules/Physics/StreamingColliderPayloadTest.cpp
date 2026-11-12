// セルの静的コライダーと配置コライダーの出し入れを、実際の物理照会で確かめる。
#include "Engine/Engine.h"
#include "Module/ModuleRegistry.h"
#include "Physics/ColliderComponent.h"
#include "Physics/IPhysicsModule.h"
#include "PhysicsModuleTestAccess.h"
#include "Scatter/ScatterSystem.h"
#include "Streaming/WorldPayloadHandlers.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <utility>
#include <Windows.h>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

using namespace NorvesLib;
using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Asset;
using namespace NorvesLib::Core::Scatter;
using namespace NorvesLib::Core::Scene;
using namespace NorvesLib::Core::Streaming;
using namespace NorvesLib::Modules::Physics;

namespace
{
    constexpr float FixedDelta = 1.0f / 60.0f;
    constexpr uint32_t CellCount = 3;
    constexpr size_t TransitionBytes = 14;
    constexpr const wchar_t* TraceEnvironment = L"NORVES_STREAMING_COLLIDER_TRACE";

    const char* Utf8(const char8_t* text)
    {
        return reinterpret_cast<const char*>(text);
    }

    struct TestFailure {};

    void Check(bool condition, uint32_t contract, uint64_t frame, int cell, const char8_t* message)
    {
        if (!condition)
        {
            std::cerr << Utf8(u8"検査失敗: 契約=") << contract << Utf8(u8" フレーム=") << frame
                      << Utf8(u8" セル=") << cell << " " << Utf8(message) << "\n";
            throw TestFailure{};
        }
    }

    class TempDirectory
    {
    public:
        TempDirectory()
        {
            wchar_t base[MAX_PATH] = {};
            const DWORD length = GetTempPathW(MAX_PATH, base);
            Check(length > 0 && length < MAX_PATH, 0, 0, -1, u8"一時ディレクトリを取得できません");
            Check(GetTempFileNameW(base, L"SCP", 0, Root) != 0, 0, 0, -1, u8"一時名を作れません");
            Check(DeleteFileW(Root) && CreateDirectoryW(Root, nullptr), 0, 0, -1, u8"一時ディレクトリを作れません");
            swprintf_s(Cells, L"%ls\\cells", Root);
            swprintf_s(Explicit, L"%ls\\explicit.colliders.json", Cells);
            swprintf_s(Rocks, L"%ls\\rocks.colliders.json", Cells);
            swprintf_s(Trace, L"%ls\\trace.bin", Root);
            Check(CreateDirectoryW(Cells, nullptr) != 0, 0, 0, -1, u8"セル用ディレクトリを作れません");
        }

        ~TempDirectory()
        {
            DeleteFileW(Explicit);
            DeleteFileW(Rocks);
            DeleteFileW(Trace);
            RemoveDirectoryW(Cells);
            RemoveDirectoryW(Root);
        }

        Container::AnsiString AssetRoot() const
        {
            const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, Root, -1, nullptr, 0, nullptr, nullptr);
            Check(length > 1, 0, 0, -1, u8"一時パスを UTF-8 に変換できません");
            Container::VariableArray<char> result(static_cast<size_t>(length));
            Check(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, Root, -1, result.data(), length, nullptr, nullptr) == length,
                  0, 0, -1, u8"一時パスを UTF-8 に変換できません");
            return Container::AnsiString(result.data());
        }

        static void Write(const wchar_t* path, const void* data, DWORD size)
        {
            const HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            Check(file != INVALID_HANDLE_VALUE, 0, 0, -1, u8"一時ファイルを作れません");
            DWORD written = 0;
            const bool ok = WriteFile(file, data, size, &written, nullptr) && written == size;
            CloseHandle(file);
            Check(ok, 0, 0, -1, u8"一時ファイルを書き込めません");
        }

        void WritePayloads() const
        {
            constexpr char explicitJson[] = R"({"format":"norves.static-colliders.v1","colliders":[
                {"shape":"box","position":[8,0,10],"halfExtents":[1,1,1]},
                {"shape":"sphere","position":[20,0,10],"radius":1},
                {"shape":"capsule","position":[32,0,10],"radius":1,"halfHeight":1}]})";
            constexpr char rocksJson[] = R"({"format":"norves.static-colliders.v1","scatter":[
                {"name":"rocks","species":"rock","shape":"sphere","radius":0.5,"centerHeight":0}]})";
            Write(Explicit, explicitJson, sizeof(explicitJson) - 1);
            Write(Rocks, rocksJson, sizeof(rocksJson) - 1);
        }

        wchar_t Root[MAX_PATH] = {};
        wchar_t Cells[MAX_PATH] = {};
        wchar_t Explicit[MAX_PATH] = {};
        wchar_t Rocks[MAX_PATH] = {};
        wchar_t Trace[MAX_PATH] = {};
    };

    class FakeClock final : public IStreamingClock
    {
    public:
        double NowMs() const override { return 0.0; }
    };

    class ManualExecutor final : public IStreamingJobExecutor
    {
    public:
        bool Submit(Thread::TaskFunction job) override
        {
            Jobs.push_back(std::move(job));
            return true;
        }

        void RunAll()
        {
            while (!Jobs.empty())
            {
                auto job = std::move(Jobs.front());
                Jobs.erase(Jobs.begin());
                job();
            }
        }

    private:
        Container::VariableArray<Thread::TaskFunction> Jobs;
    };

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

    Container::TSharedPtr<const CookedScatterData> MakeScatterData()
    {
        constexpr float chunkSize = 32.0f;
        constexpr uint32_t gridX = 6;
        constexpr uint32_t gridZ = 3;
        constexpr uint32_t chunkCount = gridX * gridZ;
        const double positions[][2] = {{62.0, 15.0}, {64.0, 15.0}, {80.0, 20.0}, {102.0, 15.0},
                                       {115.0, 25.0}, {128.0, 15.0}, {145.0, 15.0},
                                       {80.0, 0.0}, {80.0, 64.0}, {90.0, 80.0}};
        CookedScatterWriteDesc desc;
        desc.Seed = 47;
        desc.ChunkSizeMeters = chunkSize;
        desc.GridCountX = gridX;
        desc.GridCountZ = gridZ;
        desc.Species.resize(1);
        auto& species = desc.Species[0];
        species.Name = "rock";
        species.RenderPath = ScatterRenderPath::Mega;
        species.LodCount = 1;
        species.LodPaths[0] = "Meshes/ScatterTriangle";
        species.MeshRadiusMeters = 0.5f;
        species.MinSpacingMeters = 1.0f;
        species.ScaleMin = 0.5f;
        species.ScaleMax = 1.5f;
        desc.Chunks.resize(chunkCount);
        desc.SpeciesRanges.resize(chunkCount);
        for (uint32_t chunk = 0; chunk < chunkCount; ++chunk)
        {
            auto& bounds = desc.Chunks[chunk];
            bounds.ChunkX = chunk % gridX;
            bounds.ChunkZ = chunk / gridX;
            bounds.AabbMin[0] = bounds.ChunkX * chunkSize;
            bounds.AabbMax[0] = (bounds.ChunkX + 1) * chunkSize;
            bounds.AabbMin[2] = bounds.ChunkZ * chunkSize;
            bounds.AabbMax[2] = (bounds.ChunkZ + 1) * chunkSize;
            const uint64_t first = desc.Instances.size();
            for (size_t index = 0; index < sizeof(positions) / sizeof(positions[0]); ++index)
            {
                if (positions[index][0] < bounds.AabbMin[0] || positions[index][0] >= bounds.AabbMax[0] ||
                    positions[index][1] < bounds.AabbMin[2] || positions[index][1] >= bounds.AabbMax[2])
                {
                    continue;
                }
                ScatterInstanceValue value;
                value.PositionX = positions[index][0];
                value.PositionZ = positions[index][1];
                value.Scale = index % 2 == 0 ? 0.75f : 1.25f;
                InstanceRecord16 record;
                Check(EncodeScatterInstance(value, bounds, species, chunkSize, 0.0, 0.0, record),
                      0, 0, 1, u8"散布の個体を符号化できません");
                desc.Instances.push_back(record);
            }
            desc.SpeciesRanges[chunk] = {first, desc.Instances.size() - first};
        }
        Container::VariableArray<uint8_t> bytes;
        Check(WriteCookedScatter(desc, bytes) == CookedScatterWriteStatus::Success,
              0, 0, 1, u8"散布データを書き出せません");
        auto parsed = ParseCookedScatter(AssetBlob::CopyBytes({bytes.data(), bytes.size()}));
        Check(parsed.Succeeded(), 0, 0, 1, u8"散布データを読み戻せません");
        return Container::MakeShared<CookedScatterData>(std::move(parsed.Scatter));
    }

    struct Probe
    {
        Math::Vector3 Position;
        int Cell;
        bool bInside;
    };

    struct Trace
    {
        Container::VariableArray<uint8_t> Hits;
        Container::VariableArray<uint8_t> Transitions;
        Container::VariableArray<int> ProbeCells;
    };

    void AppendInteger(Container::VariableArray<uint8_t>& bytes, uint64_t value, uint32_t count)
    {
        for (uint32_t i = 0; i < count; ++i)
        {
            bytes.push_back(static_cast<uint8_t>(value >> (i * 8)));
        }
    }

    uint64_t ReadInteger(const uint8_t* bytes, uint32_t count)
    {
        uint64_t value = 0;
        for (uint32_t i = 0; i < count; ++i)
        {
            value |= uint64_t(bytes[i]) << (i * 8);
        }
        return value;
    }

    struct Fixture
    {
        FakeClock Clock;
        ManualExecutor Executor;
        Container::TUniquePtr<Engine::Engine> EnginePtr = Container::MakeUnique<Engine::Engine>();
        Module::ModuleRegistry& Registry = Module::GetModuleRegistry();
        PlacementPayloadHandler Placement{EnginePtr->GetWorld()};
        IPhysicsModule* Physics = nullptr;
        ScatterHandle ScatterId = kInvalidScatterHandle;
        ColliderHandle PlacedCollider;
        uint64_t PlacedEntityId = 0;
        uint64_t PlacedRootId = 0;
        bool bInstalled = false;
        bool bPlacementRegistered = false;
        Container::VariableArray<Probe> Probes;
        Container::VariableArray<uint8_t> LastHits;
        Trace Result;
        size_t BaseProxies = 0;
        uint64_t BaseTreeItems = 0;
        size_t BaseEntities = 0;

        ~Fixture()
        {
            Executor.RunAll();
            auto& streaming = EnginePtr->GetStreamingSystem();
            streaming.Shutdown();
            if (bPlacementRegistered)
            {
                streaming.UnregisterHandler(&Placement);
            }
            if (ScatterId != kInvalidScatterHandle)
            {
                EnginePtr->GetScatterSystem().Unload(ScatterId);
            }
            EnginePtr->GetWorld().Finalize();
            streaming.SetEnvironment(nullptr, nullptr);
            if (bInstalled)
            {
                Registry.ShutdownAll(*EnginePtr);
            }
        }

        uint64_t FrameNumber() const { return EnginePtr->GetStreamingSystem().GetLastFrameStats().Frame; }

        void Initialize(const TempDirectory& temp)
        {
            Physics = RegisterPhysicsModule(Registry);
            Check(Physics != nullptr, 0, 0, -1, u8"物理モジュールを登録できません");
            bInstalled = Registry.InstallAll(*EnginePtr);
            Check(bInstalled, 0, 0, -1, u8"物理モジュールを導入できません");
            World& world = EnginePtr->GetWorld();
            world.Initialize();
            auto& streaming = EnginePtr->GetStreamingSystem();
            streaming.SetEnvironment(&Clock, &Executor);
            PhysicsModuleTestAccess::GetStaticColliderPayloadHandler(*Physics).SetAssetRoot(temp.AssetRoot());

            PlacementBuilder builder;
            builder.CountItems = Delegate<uint32_t, const PlacementSpawnContext&>(
                [](const PlacementSpawnContext&) { return 1u; });
            builder.SpawnItem = Delegate<bool, const PlacementSpawnContext&, uint32_t>(
                [this](const PlacementSpawnContext& context, uint32_t index)
                {
                    if (index != 0)
                    {
                        return false;
                    }
                    Entity* entity = context.WorldRef.SpawnEntity<Entity>(&context.Root);
                    if (entity == nullptr)
                    {
                        return false;
                    }
                    entity->SetLocalPosition(144.0f, 0.0f, 40.0f);
                    auto* collider = context.WorldRef.CreateComponent<ColliderComponent>(entity);
                    if (collider == nullptr || collider->SetBox(Math::Vector3(1.0f, 1.0f, 1.0f)) != EPhysicsResult::Success)
                    {
                        return false;
                    }
                    PlacedRootId = context.Root.GetObjectId();
                    PlacedEntityId = entity->GetObjectId();
                    PlacedCollider = collider->GetColliderHandle();
                    return PlacedCollider.IsValid();
                });
            Check(Placement.RegisterBuilder("collider-props", std::move(builder)), 0, 0, 2, u8"配置の作り手を登録できません");
            bPlacementRegistered = streaming.RegisterHandler(&Placement);
            Check(bPlacementRegistered, 0, 0, 2, u8"配置ハンドラーを登録できません");

            ScatterSpeciesBinding binding;
            binding.Name = "rock";
            binding.MegaMesh.Id = 1;
            auto& scatter = EnginePtr->GetScatterSystem();
            ScatterId = scatter.Load(world, MakeScatterData(), {&binding, 1});
            Check(ScatterId != kInvalidScatterHandle && scatter.SetScatterName(ScatterId, "rocks"),
                  0, 0, 1, u8"散布を読み込めません");
            const auto data = scatter.GetData(ScatterId);
            Check(Container::IsValid(data), 0, 0, 1, u8"読み込んだ散布データがありません");
            Probes.push_back({Math::Vector3(8.0f, 0.0f, 10.0f), 0, true});
            Probes.push_back({Math::Vector3(20.0f, 0.0f, 10.0f), 0, true});
            Probes.push_back({Math::Vector3(32.0f, 0.0f, 10.0f), 0, true});
            uint32_t inside = 0;
            uint32_t outside = 0;
            bool minX = false, maxX = false, minZ = false, maxZ = false;
            for (uint32_t chunk = 0; chunk < data->Chunks.size(); ++chunk)
            {
                const auto records = data->GetInstances(chunk, 0);
                for (size_t offset = 0; offset < records.size(); offset += sizeof(InstanceRecord16))
                {
                    InstanceRecord16 record;
                    ScatterInstanceValue value;
                    Check(ReadScatterInstance(records.subspan(offset, sizeof(record)), record) &&
                          DecodeScatterInstance(record, data->Chunks[chunk], data->Species[0],
                                                data->ChunkSizeMeters, data->OriginX, data->OriginZ, value),
                          0, 0, 1, u8"散布の個体を復号できません");
                    const float x = static_cast<float>(value.PositionX);
                    const float z = static_cast<float>(value.PositionZ);
                    // 本体の境界判定関数に依らず、半開区間の期待値を作る。
                    const bool included = 64.0f <= x && x < 128.0f && 0.0f <= z && z < 64.0f;
                    minX = minX || x == 64.0f;
                    maxX = maxX || x == 128.0f;
                    minZ = minZ || z == 0.0f;
                    maxZ = maxZ || z == 64.0f;
                    included ? ++inside : ++outside;
                    Probes.push_back({Math::Vector3(x, static_cast<float>(value.PositionY), z), 1, included});
                }
            }
            Check(inside == 5 && outside == 5 && minX && maxX && minZ && maxZ,
                  0, 0, 1, u8"散布の内外の個体数または境界上の個体が違います");
            Probes.push_back({Math::Vector3(144.0f, 0.0f, 40.0f), 2, true});
            for (const auto& probe : Probes)
            {
                Result.ProbeCells.push_back(probe.Cell);
            }

            Physics->StepPreFixed(FixedDelta);
            Physics->StepFixed(FixedDelta);
            BaseProxies = PhysicsModuleTestAccess::GetWorkingProxies(*Physics).size();
            BaseTreeItems = PhysicsModuleTestAccess::GetWorkingBroadphaseStats(*Physics).StaticTreeItemCount;
            BaseEntities = CountEntities(world);

            StreamingSystemSettings settings;
            settings.Ring.ActiveRadius = 40.0f;
            settings.Ring.DormantRadius = 55.0f;
            settings.Ring.UnloadRadius = 100.0f;
            settings.Ring.DirectionWeight = 0.0f;
            settings.Ring.LatencySeconds = 0.0f;
            Check(streaming.SetSettings(settings), 0, 0, -1, u8"セルの輪を設定できません");
            streaming.SetRecordTransitions(true);
            const Container::String manifest = R"({"format":"norves.cell-manifest.v1","origin":[0,0,0],"cellSize":64,"cells":[
                {"id":0,"bounds":{"min":[0,-10,0],"max":[64,10,64]},"payloads":[{"kind":"static-colliders","logicalPath":"cells/explicit.colliders.json"}]},
                {"id":1,"bounds":{"min":[64,-10,0],"max":[128,10,64]},"payloads":[{"kind":"static-colliders","logicalPath":"cells/rocks.colliders.json"}]},
                {"id":2,"bounds":{"min":[128,-10,0],"max":[192,10,64]},"payloads":[{"kind":"placement","logicalPath":"collider-props"}]}]})";
            CellManifestError error;
            Check(streaming.LoadManifestFromText(manifest, &error), 0, 0, -1, u8"マニフェストを読み込めません");
        }

        void Advance(float z, uint32_t frames)
        {
            auto& streaming = EnginePtr->GetStreamingSystem();
            for (uint32_t frame = 0; frame < frames; ++frame)
            {
                streaming.SetFocus({Math::Vector3(96.0f, 0.0f, z), Math::Vector3()});
                streaming.Update();
                Executor.RunAll();
                Physics->StepPreFixed(FixedDelta);
                Physics->StepFixed(FixedDelta);
                EnginePtr->GetWorld().Tick(FixedDelta);
                LastHits.clear();
                for (const Probe& probe : Probes)
                {
                    PhysicsRaycastHit hit;
                    const Math::Ray ray(probe.Position + Math::Vector3(0.0f, 5.0f, 0.0f), Math::Vector3(0.0f, -1.0f, 0.0f));
                    const auto result = EnginePtr->GetSceneQuery().Raycast(ray, 10.0f, hit);
                    Check(result == EPhysicsSceneQueryResult::Success || result == EPhysicsSceneQueryResult::NoHit,
                          6, FrameNumber(), probe.Cell, u8"物理 Raycast を実行できません");
                    const uint8_t hitBit = result == EPhysicsSceneQueryResult::Success ? 1 : 0;
                    LastHits.push_back(hitBit);
                    Result.Hits.push_back(hitBit);
                }
            }
        }

        void CheckState(EStreamingCellState state, uint32_t contract)
        {
            auto& streaming = EnginePtr->GetStreamingSystem();
            for (uint32_t cell = 0; cell < CellCount; ++cell)
            {
                Check(streaming.GetCellState(cell) == state, contract, FrameNumber(), cell, u8"セルが期待した状態ではありません");
            }
            const bool active = state == EStreamingCellState::Active;
            for (size_t index = 0; index < Probes.size(); ++index)
            {
                const uint8_t expected = static_cast<uint8_t>(active && Probes[index].bInside);
                if (LastHits[index] != expected)
                {
                    std::cerr << Utf8(u8"照会点=") << index << " x=" << Probes[index].Position.x
                              << " z=" << Probes[index].Position.z << Utf8(u8" 期待=") << unsigned(expected)
                              << Utf8(u8" 実際=") << unsigned(LastHits[index]) << "\n";
                }
                Check(LastHits[index] == expected, contract, FrameNumber(), Probes[index].Cell,
                      u8"位置での当たりの有無が違います");
            }
            for (uint32_t cell = 0; cell < 2; ++cell)
            {
                const auto key = StaticColliderPayloadHandler::MakeChunkKey(cell, 0);
                const auto handles = PhysicsModuleTestAccess::GetDetachedColliderHandles(*Physics, key);
                const size_t expected = active ? (cell == 0 ? 3 : 5) : 0;
                Check(handles.size() == expected, contract, FrameNumber(), cell, u8"静的コライダーチャンクの個数が違います");
            }
            PhysicsShapeProxy proxy;
            Check(PhysicsModuleTestAccess::CopyPublishedProxy(*Physics, PlacedCollider, proxy) == active,
                  contract, FrameNumber(), 2, u8"配置コライダーの公開状態が違います");
            if (active)
            {
                PhysicsRaycastHit hit;
                const Math::Ray ray(Math::Vector3(144.0f, 5.0f, 40.0f), Math::Vector3(0.0f, -1.0f, 0.0f));
                Check(EnginePtr->GetSceneQuery().Raycast(ray, 10.0f, hit) == EPhysicsSceneQueryResult::Success &&
                      hit.Collider == PlacedCollider && hit.bHasEntity,
                      contract, FrameNumber(), 2, u8"配置コライダー自身に当たりません");
                Check(EnginePtr->GetWorld().FindEntityByObjectId(PlacedEntityId) != nullptr,
                      contract, FrameNumber(), 2, u8"配置された Entity がありません");
            }
            else if (state == EStreamingCellState::Unloaded)
            {
                Check(EnginePtr->GetWorld().FindEntityByObjectId(PlacedEntityId) == nullptr &&
                      EnginePtr->GetWorld().FindEntityByObjectId(PlacedRootId) == nullptr,
                      contract, FrameNumber(), 2, u8"配置された Entity またはルートが残っています");
            }
        }

        void CheckBaseline()
        {
            Check(PhysicsModuleTestAccess::GetWorkingProxies(*Physics).size() == BaseProxies,
                  5, FrameNumber(), -1, u8"物理プロキシ数が基準に戻りません");
            Check(PhysicsModuleTestAccess::GetWorkingBroadphaseStats(*Physics).StaticTreeItemCount == BaseTreeItems,
                  5, FrameNumber(), -1, u8"静的木の項目数が基準に戻りません");
            Check(CountEntities(EnginePtr->GetWorld()) == BaseEntities,
                  5, FrameNumber(), -1, u8"World の Entity 数が基準に戻りません");
        }

        Trace Run()
        {
            Advance(32.0f, 3);
            CheckState(EStreamingCellState::Active, 1);
            Advance(120.0f, 1);
            CheckState(EStreamingCellState::Dormant, 2);
            Advance(32.0f, 1);
            CheckState(EStreamingCellState::Active, 3);
            Advance(300.0f, 2);
            CheckState(EStreamingCellState::Unloaded, 4);
            for (uint32_t cycle = 0; cycle < 20; ++cycle)
            {
                Advance(32.0f, 3);
                CheckState(EStreamingCellState::Active, 5);
                Advance(300.0f, 2);
                CheckState(EStreamingCellState::Unloaded, 5);
                CheckBaseline();
            }
            for (const auto& transition : EnginePtr->GetStreamingSystem().GetTransitions())
            {
                // パディングを含めず、全フィールドのビット列を記録する。
                AppendInteger(Result.Transitions, transition.Frame, 8);
                AppendInteger(Result.Transitions, transition.Cell, 4);
                AppendInteger(Result.Transitions, static_cast<uint8_t>(transition.From), 1);
                AppendInteger(Result.Transitions, static_cast<uint8_t>(transition.To), 1);
            }
            return std::move(Result);
        }
    };

    Trace Execute(const TempDirectory& temp)
    {
        Fixture fixture;
        fixture.Initialize(temp);
        return fixture.Run();
    }

    Container::VariableArray<uint8_t> Serialize(const Trace& trace)
    {
        Container::VariableArray<uint8_t> bytes;
        AppendInteger(bytes, trace.Hits.size(), 8);
        AppendInteger(bytes, trace.Transitions.size(), 8);
        for (uint8_t value : trace.Hits) { bytes.push_back(value); }
        for (uint8_t value : trace.Transitions) { bytes.push_back(value); }
        return bytes;
    }

    Container::VariableArray<uint8_t> ReadTrace(const wchar_t* path)
    {
        const HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        Check(file != INVALID_HANDLE_VALUE, 6, 0, -1, u8"再実行の記録を開けません");
        LARGE_INTEGER size;
        const bool valid = GetFileSizeEx(file, &size) && size.QuadPart >= 16 && size.QuadPart < 1024 * 1024;
        if (!valid)
        {
            CloseHandle(file);
            Check(false, 6, 0, -1, u8"再実行の記録の大きさが不正です");
        }
        Container::VariableArray<uint8_t> bytes(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        const bool ok = ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) && read == bytes.size();
        CloseHandle(file);
        Check(ok, 6, 0, -1, u8"再実行の記録を読めません");
        return bytes;
    }

    void CompareWithFreshProcess(const TempDirectory& temp, const Trace& first)
    {
        wchar_t executable[MAX_PATH] = {};
        const DWORD length = GetModuleFileNameW(nullptr, executable, MAX_PATH);
        Check(length > 0 && length < MAX_PATH, 6, 0, -1, u8"試験の実行ファイルを取得できません");
        wchar_t command[MAX_PATH + 128] = {};
        swprintf_s(command, L"\"%ls\" --test=StreamingColliderPayloadTest --streaming-replay", executable);
        Check(SetEnvironmentVariableW(TraceEnvironment, temp.Trace) != 0, 6, 0, -1, u8"再実行の記録先を設定できません");
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        const bool started = CreateProcessW(executable, command, nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &process) != 0;
        SetEnvironmentVariableW(TraceEnvironment, nullptr);
        Check(started, 6, 0, -1, u8"新しいプロセスで再実行できません");
        constexpr DWORD replayTimeoutMs = 240000;
        constexpr DWORD terminationTimeoutMs = 5000;
        const DWORD wait = WaitForSingleObject(process.hProcess, replayTimeoutMs);
        if (wait != WAIT_OBJECT_0)
        {
            TerminateProcess(process.hProcess, 1);
            const DWORD stopped = WaitForSingleObject(process.hProcess, terminationTimeoutMs);
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            Check(stopped == WAIT_OBJECT_0, 6, 0, -1, u8"再実行の子プロセスの終了を確認できません");
            Check(false, 6, 0, -1, wait == WAIT_TIMEOUT
                ? u8"決定性の再実行が240秒を超えたため、子プロセスを終了しました"
                : u8"決定性の再実行の待機に失敗したため、子プロセスを終了しました");
        }
        DWORD exitCode = 1;
        const bool gotExit = GetExitCodeProcess(process.hProcess, &exitCode) != 0;
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        Check(wait == WAIT_OBJECT_0 && gotExit && exitCode == 0, 6, 0, -1, u8"新しいプロセスの再実行が失敗しました");
        const auto second = ReadTrace(temp.Trace);
        const uint64_t hitSize = ReadInteger(second.data(), 8);
        const uint64_t transitionSize = ReadInteger(second.data() + 8, 8);
        Check(hitSize == first.Hits.size() && transitionSize == first.Transitions.size() &&
              16 + hitSize + transitionSize == second.size(), 6, 0, -1, u8"二回の記録の長さが違います");
        for (size_t index = 0; index < first.Hits.size(); ++index)
        {
            Check(first.Hits[index] == second[16 + index], 6, index / first.ProbeCells.size() + 1,
                  first.ProbeCells[index % first.ProbeCells.size()], u8"二回の当たりの記録が一致しません");
        }
        for (size_t index = 0; index < first.Transitions.size(); ++index)
        {
            const size_t start = (index / TransitionBytes) * TransitionBytes;
            Check(first.Transitions[index] == second[16 + hitSize + index], 6,
                  ReadInteger(first.Transitions.data() + start, 8),
                  static_cast<int>(ReadInteger(first.Transitions.data() + start + 8, 4)),
                  u8"二回のセル遷移のビット列が一致しません");
        }
    }
}

int main(int argc, char** argv)
{
#ifdef _MSC_VER
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    try
    {
        bool replay = false;
        for (int index = 1; index < argc; ++index)
        {
            replay = replay || std::strcmp(argv[index], "--streaming-replay") == 0;
        }
        SetLastError(ERROR_SUCCESS);
        const DWORD traceLength = GetEnvironmentVariableW(TraceEnvironment, nullptr, 0);
        replay = replay || traceLength != 0 || GetLastError() != ERROR_ENVVAR_NOT_FOUND;
        TempDirectory temp;
        temp.WritePayloads();
        const Trace trace = Execute(temp);
        if (replay)
        {
            wchar_t path[MAX_PATH] = {};
            const DWORD length = GetEnvironmentVariableW(TraceEnvironment, path, MAX_PATH);
            Check(length > 0 && length < MAX_PATH, 6, 0, -1, u8"再実行の記録先がありません");
            const auto bytes = Serialize(trace);
            TempDirectory::Write(path, bytes.data(), static_cast<DWORD>(bytes.size()));
        }
        else
        {
            CompareWithFreshProcess(temp, trace);
            std::cout << Utf8(u8"StreamingColliderPayloadTest 合格: セルの出し入れと二回の記録を確認\n");
        }
        return 0;
    }
    catch (const TestFailure&)
    {
        return 1;
    }
}
