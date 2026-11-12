// 実際のワールドでセルの予算・休眠・復帰・解放と、その再現性を確かめる。
#include "Animation/SkeletalAssetResource.h"
#include "Component/BoardComponent.h"
#include "Component/MegaGeometryComponent.h"
#include "Component/MegaGeometryScatterComponent.h"
#include "Component/MeshComponent.h"
#include "Component/PointLightComponent.h"
#include "Component/SkinnedMeshComponent.h"
#include "Ecs/EcsWorld.h"
#include "Object/ResourceRegistry.h"
#include "Object/World.h"
#include "Rendering/RenderResources.h"
#include "Rendering/SceneView.h"
#include "Rendering/Terrain/TerrainSystem.h"
#include "Scatter/ScatterSystem.h"
#include "Scene/SceneQuery.h"
#include "Streaming/StreamingSystem.h"
#include "Streaming/WorldPayloadHandlers.h"

#include <cstring>
#include <iostream>
#include <utility>

using namespace NorvesLib;
using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Asset;
using namespace NorvesLib::Core::Rendering;
using namespace NorvesLib::Core::Scatter;
using namespace NorvesLib::Core::Streaming;

namespace
{
    constexpr uint32_t CellCount = 4;
    constexpr uint32_t ItemsPerCell = 6;
    constexpr uint32_t SpeciesCount = 2;
    constexpr uint32_t HoldFrames = 16;
    constexpr float FarFocus = -1024.0f;
    constexpr float DeltaTime = 1.0f / 60.0f;
    int g_Failures = 0;

    const char* Utf8(const char8_t* text)
    {
        return reinterpret_cast<const char*>(text);
    }

    bool Check(bool bCondition, uint32_t contract, uint64_t frame, int cell, const char8_t* message)
    {
        if (!bCondition)
        {
            ++g_Failures;
            std::cerr << Utf8(u8"検査失敗: 契約=") << contract << Utf8(u8" フレーム=") << frame
                      << Utf8(u8" セル=") << cell << " " << Utf8(message) << "\n";
        }
        return bCondition;
    }

    class FakeClock final : public IStreamingClock
    {
    public:
        double NowMs() const override
        {
            return TimeMs;
        }

        double TimeMs = 0.0;
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

    using ProbeBase = Component::Component;

    class StreamingPayloadTickProbe final : public ProbeBase
    {
        REFLECTION_CLASS(StreamingPayloadTickProbe, ProbeBase)

    public:
        uint64_t Calls = 0;

        static void RunTick(ProbeBase& component, float)
        {
            ++static_cast<StreamingPayloadTickProbe&>(component).Calls;
        }
    };
    IMPLEMENT_CLASS(StreamingPayloadTickProbe, ProbeBase)

    MeshDataHandle MakeMeshHandle(uint64_t id)
    {
        MeshDataHandle handle;
        handle.Id = id;
        return handle;
    }

    Container::TSharedPtr<SkeletalAssetResource> MakeSkeletalAsset(ResourceRegistry& resources)
    {
        auto mesh = resources.CreateTransient<SkinnedMeshResource>("StreamingPayload.Mesh");
        auto skeleton = resources.CreateTransient<SkeletonResource>("StreamingPayload.Skeleton");
        auto clip = resources.CreateTransient<AnimationClipResource>("StreamingPayload.Clip");
        auto asset = resources.CreateTransient<SkeletalAssetResource>("StreamingPayload.Asset");
        if (!Check(mesh && skeleton && clip && asset, 0, 0, -1, u8"骨格アセットの資源を作れません"))
        {
            return {};
        }
        // 一つの骨に結び付いた三角形と静止クリップを、CPU のデータだけで組む。
        Container::VariableArray<Skeletal::SkeletalVertex> vertices(3);
        vertices[1].Position.X = 1.0f;
        vertices[2].Position.Y = 1.0f;
        for (auto& vertex : vertices)
        {
            vertex.Normal.Z = 1.0f;
            vertex.JointWeights[0] = 1.0f;
        }
        mesh->SetVertices(std::move(vertices));
        mesh->SetIndices(Container::VariableArray<uint32_t>{0, 1, 2});
        Container::VariableArray<Skeletal::SkeletalJoint> joints(1);
        joints[0].Name = "Root";
        joints[0].ParentIndex = -1;
        for (uint32_t index = 0; index < 16; index += 5)
        {
            joints[0].InverseBindMatrix[index] = 1.0f;
        }
        skeleton->SetJoints(std::move(joints));
        Skeletal::SkeletalAnimationClip animation;
        animation.Name = "Idle";
        animation.DurationSeconds = 1.0f;
        clip->SetClip(std::move(animation));
        if (!Check(mesh->Load() && skeleton->Load() && clip->Load(), 0, 0, -1,
                   u8"骨格アセットのメッシュ・骨・クリップを読み込めません"))
        {
            return {};
        }
        asset->SetResources(mesh, skeleton, clip);
        if (!Check(asset->Load(), 0, 0, -1, u8"骨格アセットを読み込めません"))
        {
            return {};
        }
        return asset;
    }

    template <typename T>
    size_t CountComponentProxies(const Container::VariableArray<T>& proxies, uint64_t componentId)
    {
        size_t count = 0;
        for (const auto& proxy : proxies)
        {
            count += proxy.ComponentId == componentId ? 1u : 0u;
        }
        return count;
    }

    struct PlacementProxyIds
    {
        uint64_t Mesh = 0;
        uint64_t Light = 0;
        uint64_t MegaGeometry = 0;
        uint64_t Board = 0;
        uint64_t SkinnedMesh = 0;
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

    class TerrainSite final : public ITerrainTileSite
    {
    public:
        explicit TerrainSite(World& world) : m_World(world)
        {
        }

        ETerrainTileSiteState GetSiteState() const override
        {
            return ETerrainTileSiteState::Ready;
        }

        void CollectTilesInBounds(const StreamingBounds& bounds,
                                 Container::VariableArray<TerrainTileCoordinate>& outTiles) const override
        {
            outTiles.clear();
            for (uint32_t x = 0; x < CellCount; ++x)
            {
                if (ContainsHorizontalPoint(bounds, 32.0f + 64.0f * x, 32.0f))
                {
                    outTiles.push_back({x, 0});
                }
            }
        }

        uint32_t GetTileId(const TerrainTileCoordinate& tile) const override
        {
            return tile.Z == 0 && tile.X < CellCount ? tile.X : Terrain::kInvalidTerrainTileId;
        }

        bool PlaceTile(const TerrainTileCoordinate& tile) override
        {
            if (GetTileId(tile) == Terrain::kInvalidTerrainTileId)
            {
                return false;
            }
            if (IsTilePlaced(tile))
            {
                return true;
            }
            auto* entity = m_World.SpawnEntity<Entity>();
            if (!entity)
            {
                return false;
            }
            EntityIds[tile.X] = entity->GetObjectId();
            entity->SetLocalPosition(32.0f + 64.0f * tile.X, 0.0f, 32.0f);
            auto* mega = m_World.CreateComponent<Component::MegaGeometryComponent>(entity);
            if (!mega)
            {
                return false;
            }
            mega->SetMegaMeshHandle(MegaGeometry::MegaMeshHandle{100});
            ComponentIds[tile.X] = mega->GetComponentId();
            return true;
        }

        void RemoveTile(const TerrainTileCoordinate& tile) override
        {
            if (GetTileId(tile) != Terrain::kInvalidTerrainTileId)
            {
                if (auto* entity = m_World.FindEntityByObjectId(EntityIds[tile.X]))
                {
                    m_World.RemoveEntity(entity);
                }
                EntityIds[tile.X] = 0;
            }
        }

        bool IsTilePlaced(const TerrainTileCoordinate& tile) const override
        {
            return GetTileId(tile) != Terrain::kInvalidTerrainTileId &&
                m_World.FindEntityByObjectId(EntityIds[tile.X]) != nullptr;
        }

        uint64_t EntityIds[CellCount] = {};
        uint64_t ComponentIds[CellCount] = {};

    private:
        World& m_World;
    };

    Container::TSharedPtr<const CookedScatterData> MakeData()
    {
        CookedScatterWriteDesc desc;
        desc.Seed = 9031;
        desc.ChunkSizeMeters = 32.0f;
        desc.GridCountX = 8;
        desc.GridCountZ = 1;
        desc.Species.resize(SpeciesCount);
        for (uint32_t species = 0; species < SpeciesCount; ++species)
        {
            auto& value = desc.Species[species];
            value.Name = species == 0 ? "rock" : "shrub";
            value.RenderPath = ScatterRenderPath::Mega;
            value.LodCount = 1;
            value.LodPaths[0] = "Meshes/ScatterTriangle";
            value.MeshRadiusMeters = 0.5f;
            value.MinSpacingMeters = 1.0f;
        }
        desc.Chunks.resize(8);
        desc.SpeciesRanges.resize(8 * SpeciesCount);
        for (uint32_t chunk = 0; chunk < 8; ++chunk)
        {
            auto& bounds = desc.Chunks[chunk];
            bounds.ChunkX = chunk;
            bounds.ChunkZ = 0;
            bounds.MinY = bounds.MaxY = 0.0f;
            bounds.AabbMin[0] = chunk * 32.0f;
            bounds.AabbMax[0] = bounds.AabbMin[0] + 32.0f;
            bounds.AabbMax[2] = 32.0f;
            for (uint32_t species = 0; species < SpeciesCount; ++species)
            {
                desc.SpeciesRanges[chunk * SpeciesCount + species] = {desc.Instances.size(), 1};
                ScatterInstanceValue value;
                value.PositionX = bounds.AabbMin[0] + 16.0;
                value.PositionZ = 16.0;
                InstanceRecord16 record;
                if (!Check(EncodeScatterInstance(value, bounds, desc.Species[species], 32.0f, 0.0, 0.0, record),
                           0, 0, chunk / 2, u8"散布のレコードを符号化できません"))
                {
                    return {};
                }
                desc.Instances.push_back(record);
            }
        }
        Container::VariableArray<uint8_t> bytes;
        if (!Check(WriteCookedScatter(desc, bytes) == CookedScatterWriteStatus::Success,
                   0, 0, -1, u8"散布を書き出せません"))
        {
            return {};
        }
        auto parsed = ParseCookedScatter(AssetBlob::CopyBytes({bytes.data(), bytes.size()}));
        if (!Check(parsed.Succeeded(), 0, 0, -1, u8"散布を読み戻せません"))
        {
            return {};
        }
        return Container::MakeShared<CookedScatterData>(std::move(parsed.Scatter));
    }

    Container::String MakeManifest()
    {
        const char* cells[] = {
            R"({"id":0,"bounds":{"min":[0,-100,0],"max":[64,100,64]},"payloads":)",
            R"({"id":1,"bounds":{"min":[64,-100,0],"max":[128,100,64]},"payloads":)",
            R"({"id":2,"bounds":{"min":[128,-100,0],"max":[192,100,64]},"payloads":)",
            R"({"id":3,"bounds":{"min":[192,-100,0],"max":[256,100,64]},"payloads":)"
        };
        Container::String text = R"({"format":"norves.cell-manifest.v1","origin":[0,0,0],"cellSize":64,"cells":[)";
        for (uint32_t cell = 0; cell < CellCount; ++cell)
        {
            if (cell != 0)
            {
                text += ",";
            }
            text += cells[cell];
            text += R"([{"kind":"terrain-tile","logicalPath":"test"},{"kind":"scatter-chunk","logicalPath":"test"},{"kind":"placement","logicalPath":"props"}]})";
        }
        text += "]}";
        return text;
    }

    struct FrameRecord
    {
        uint32_t Items[CellCount] = {};
        uint32_t ResidentUnits = 0;
        uint32_t TileMask = 0;
    };

    struct RunResult
    {
        Container::VariableArray<FrameRecord> Frames;
        Container::VariableArray<StreamingTransition> Transitions;
    };

    struct TestRig
    {
        ResourceRegistry Resources;
        Container::TSharedPtr<SkeletalAssetResource> SkeletalAsset;
        SceneView View;
        World WorldRef;
        Scene::SceneQuery Query;
        FakeClock Clock;
        ManualExecutor Executor;
        Terrain::TerrainSystem Terrain;
        ScatterSystem Scatter;
        TerrainSite Site{WorldRef};
        TerrainTilePayloadHandler TerrainHandler{Terrain};
        ScatterChunkPayloadHandler ScatterHandler{Scatter};
        PlacementPayloadHandler PlacementHandler{WorldRef};
        StreamingSystem System;
        ScatterHandle Handle = kInvalidScatterHandle;
        uint32_t Spawned = 0;
        uint32_t SpawnFrames[CellCount] = {};
        PlacementProxyIds ItemProxyIds[CellCount][ItemsPerCell] = {};
        size_t BaselineEntities = 0;
        uint32_t BaselineUnits = 0;
        uint32_t BaselineTiles = 0;
        uint32_t BaselineCells = 0;
        RunResult Result;

        TestRig()
        {
            WorldRef.Initialize();
        }

        ~TestRig()
        {
            Executor.RunAll();
            System.Shutdown();
            Scatter.Unload(Handle);
            Query.Clear();
            WorldRef.Finalize();
            View.Shutdown();
            SkeletalAsset.reset();
            Resources.Shutdown();
        }

        bool Expect(bool bCondition, uint32_t contract, int cell, const char8_t* message) const
        {
            return Check(bCondition, contract, System.GetLastFrameStats().Frame, cell, message);
        }

        uint32_t ItemCount(uint32_t cell) const
        {
            const auto* root = PlacementHandler.FindCellRoot(cell, 2);
            return root ? static_cast<uint32_t>(root->GetChildEntities().size()) : 0;
        }

        uint32_t TileMask() const
        {
            uint32_t mask = 0;
            for (uint32_t cell = 0; cell < CellCount; ++cell)
            {
                mask |= Terrain.IsTileResident(cell) ? 1u << cell : 0;
            }
            return mask;
        }

        bool Initialize(const Container::TSharedPtr<const CookedScatterData>& data)
        {
            if (!Expect(Resources.Initialize(), 0, -1, u8"資源レジストリを初期化できません"))
            {
                return false;
            }
            SkeletalAsset = MakeSkeletalAsset(Resources);
            if (!SkeletalAsset)
            {
                return false;
            }
            if (!Expect(View.Initialize(SceneViewSettings{}), 0, -1, u8"SceneView を初期化できません"))
            {
                return false;
            }
            WorldRef.SetSceneView(&View);
            WorldRef.GetEcsWorld().GetMassWork().SetBudget({8192, 0.0f});
            Component::ComponentTickRegistration registration;
            registration.Name = "Test.StreamingPayload.Tick";
            registration.Run = &StreamingPayloadTickProbe::RunTick;
            WorldRef.RegisterComponentTick(StreamingPayloadTickProbe::StaticClass(), registration);
            for (uint32_t cell = 0; cell < CellCount; ++cell)
            {
                Terrain.SetTileResident(cell, false);
            }
            ScatterSpeciesBinding bindings[SpeciesCount];
            for (uint32_t species = 0; species < SpeciesCount; ++species)
            {
                bindings[species].Name = species == 0 ? "rock" : "shrub";
                bindings[species].MegaMesh = MegaGeometry::MegaMeshHandle{species + 1};
            }
            ScatterBudget scatterBudget;
            scatterBudget.MaxMillisecondsPerFrame = 0.0f;
            Scatter.SetBudget(scatterBudget);
            Handle = Scatter.Load(WorldRef, data, {bindings, SpeciesCount});
            if (!Expect(Handle != kInvalidScatterHandle && Scatter.SetScatterName(Handle, "test") &&
                        Scatter.SetDistanceResidency(Handle, false), 0, -1, u8"散布を束ねられません"))
            {
                return false;
            }
            // 行の作成は LateTick の仕事を進めて完了させる。
            for (uint32_t frame = 0; frame < HoldFrames; ++frame)
            {
                Scatter.Update(Math::Vector3(FarFocus, 0.0f, 32.0f));
                WorldRef.Tick(DeltaTime);
                WorldRef.LateTick(DeltaTime);
            }
            if (!Expect(Scatter.IsSettled(Handle), 0, -1, u8"散布の行の準備が終わりません"))
            {
                return false;
            }
            PlacementBuilder builder;
            builder.CountItems = [](const PlacementSpawnContext&)
            {
                return ItemsPerCell;
            };
            builder.SpawnItem = [this](const PlacementSpawnContext& context, uint32_t index)
            {
                auto* entity = context.WorldRef.SpawnEntity<Entity>(&context.Root);
                if (!entity)
                {
                    return false;
                }
                entity->SetLocalPosition(context.Bounds.Min.x + 8.0f + index * 8.0f, 20.0f, 48.0f);
                auto* probe = context.WorldRef.CreateComponent<StreamingPayloadTickProbe>(entity);
                auto* mega = context.WorldRef.CreateComponent<Component::MegaGeometryComponent>(entity);
                auto* mesh = context.WorldRef.CreateComponent<Component::MeshComponent>(entity);
                auto* light = context.WorldRef.CreateComponent<Component::PointLightComponent>(entity);
                auto* board = context.WorldRef.CreateComponent<Component::BoardComponent>(entity);
                auto* skinned = context.WorldRef.CreateComponent<Component::SkinnedMeshComponent>(entity);
                if (!probe || !mega || !mesh || !light || !board || !skinned)
                {
                    return false;
                }
                probe->SetTickEnabled(true);
                mega->SetMegaMeshHandle(MegaGeometry::MegaMeshHandle{200});
                mesh->SetMeshHandle(MakeMeshHandle(300));
                board->SetTextureHandle(TextureHandle{400});
                board->SetBoardSpace(BoardSpace::WorldSpace);
                skinned->SetSkeletalAsset(SkeletalAsset);
                skinned->SetPlaying(false);
                // 基底型の検索で別のメッシュを拾わないよう、生成した各 Component の ID を控える。
                ItemProxyIds[context.Cell][index] = {mesh->GetComponentId(), light->GetComponentId(),
                    mega->GetComponentId(), board->GetComponentId(), skinned->GetComponentId()};
                ++Spawned;
                Clock.TimeMs += 1.0;
                return true;
            };
            PlacementHandler.SetItemsPerStep(1);
            System.SetEnvironment(&Clock, &Executor);
            StreamingSystemSettings settings;
            settings.Ring.ActiveRadius = 40.0f;
            settings.Ring.DormantRadius = 100.0f;
            settings.Ring.UnloadRadius = 160.0f;
            settings.Ring.DirectionWeight = 0.0f;
            settings.Ring.LatencySeconds = 0.0f;
            settings.Budget.PerFrameCommitMs = 2.0;
            if (!Expect(TerrainHandler.BindSite("test", &Site) &&
                        PlacementHandler.RegisterBuilder("props", std::move(builder)) && System.SetSettings(settings) &&
                        System.RegisterHandler(&TerrainHandler) && System.RegisterHandler(&ScatterHandler) &&
                        System.RegisterHandler(&PlacementHandler), 0, -1, u8"ハンドラを登録できません"))
            {
                return false;
            }
            System.SetRecordTransitions(true);
            CellManifestError error;
            if (!Expect(System.LoadManifestFromText(MakeManifest(), &error), 0, -1, u8"マニフェストを読めません"))
            {
                std::cerr << ToString(error.Code) << "\n";
                return false;
            }
            Advance(FarFocus);
            BaselineEntities = CountEntities(WorldRef);
            BaselineUnits = Scatter.GetStats().ResidentUnits;
            BaselineTiles = TileMask();
            BaselineCells = System.GetLastFrameStats().ResidentCells;
            return true;
        }

        void Advance(float x)
        {
            uint32_t before[CellCount];
            for (uint32_t cell = 0; cell < CellCount; ++cell)
            {
                before[cell] = ItemCount(cell);
            }
            const uint32_t spawnedBefore = Spawned;
            Executor.RunAll();
            System.SetFocus({Math::Vector3(x, 0.0f, 32.0f), Math::Vector3::Zero});
            System.Update();
            Executor.RunAll();
            Scatter.Update(Math::Vector3(FarFocus, 0.0f, 32.0f));
            WorldRef.Tick(DeltaTime);
            WorldRef.LateTick(DeltaTime);
            // 解放との相殺で予算超過を隠さないよう、新規生成の総数も数える。
            Expect(Spawned - spawnedBefore <= 3, 1, -1, u8"配置の生成が時間予算と一歩分を超えました");
            FrameRecord record;
            for (uint32_t cell = 0; cell < CellCount; ++cell)
            {
                record.Items[cell] = ItemCount(cell);
                Expect(record.Items[cell] <= before[cell] + 3, 1, cell, u8"配置の増分が三個を超えました");
                if (record.Items[cell] > before[cell])
                {
                    ++SpawnFrames[cell];
                }
            }
            record.ResidentUnits = Scatter.GetStats().ResidentUnits;
            record.TileMask = TileMask();
            Result.Frames.push_back(record);
        }

        void Hold(float x)
        {
            for (uint32_t frame = 0; frame < HoldFrames; ++frame)
            {
                Advance(x);
            }
        }

        size_t ProxyCount(uint64_t componentId) const
        {
            return CountComponentProxies(View.GetMegaGeometryProxies(), componentId);
        }

        void CheckPlacementProxies(const PlacementProxyIds& ids, size_t expected, uint32_t contract, uint32_t cell)
        {
            Expect(CountComponentProxies(View.GetMeshProxies(), ids.Mesh) == expected,
                   contract, cell, u8"配置の Mesh proxy の公開数が違います");
            size_t lights = 0;
            for (const auto& proxy : View.GetLightProxies())
            {
                lights += proxy.LightId == ids.Light ? 1u : 0u;
            }
            Expect(lights == expected, contract, cell, u8"配置の Light proxy の公開数が違います");
            Expect(ProxyCount(ids.MegaGeometry) == expected,
                   contract, cell, u8"配置の MegaGeometry proxy の公開数が違います");
            Expect(CountComponentProxies(View.GetBoardProxies(), ids.Board) == expected,
                   contract, cell, u8"配置の Board proxy の公開数が違います");
            Expect(CountComponentProxies(View.GetSkinnedMeshProxies(), ids.SkinnedMesh) == expected,
                   contract, cell, u8"配置の SkinnedMesh proxy の公開数が違います");
        }

        void CheckScatter(uint32_t cell, bool bActive, bool bUnloaded, uint32_t contract)
        {
            for (uint32_t chunk = cell * 2; chunk < cell * 2 + 2; ++chunk)
            {
                Expect(Scatter.IsChunkPinned(Handle, chunk) == !bUnloaded &&
                       Scatter.IsChunkResident(Handle, chunk) == !bUnloaded &&
                       Scatter.IsChunkDormant(Handle, chunk) == (!bActive && !bUnloaded),
                       contract, cell, u8"散布チャンクの固定・常駐・休眠が違います");
                for (uint32_t species = 0; species < SpeciesCount; ++species)
                {
                    auto* component = Scatter.GetMegaComponent(Handle, species);
                    uint32_t first = 0;
                    uint32_t count = 0;
                    Scatter.GetChunkRanges(Handle, chunk, species, first, count);
                    if (!Expect(component && count > 0 && first + count <= component->GetRangeCount(),
                                contract, cell, u8"散布チャンクの範囲がありません"))
                    {
                        continue;
                    }
                    for (uint32_t range = first; range < first + count; ++range)
                    {
                        Expect(component->IsRangeActive(range) == bActive, contract, cell,
                               u8"散布チャンクの範囲の有効状態が違います");
                        if (bUnloaded)
                        {
                            Expect(!component->IsRangePublished(range), contract, cell,
                                   u8"解放後に散布チャンクの範囲が公開されています");
                        }
                    }
                }
            }
        }

        void CheckPublication(uint32_t cell, bool bActive, float focus, uint32_t contract)
        {
            auto* root = PlacementHandler.FindCellRoot(cell, 2);
            if (!Expect(root && ItemCount(cell) == ItemsPerCell, contract, cell, u8"配置の六個が揃いません"))
            {
                return;
            }
            uint64_t before[ItemsPerCell] = {};
            uint32_t index = 0;
            for (const Entity* entity : root->GetChildEntities())
            {
                const auto* probe = entity->GetComponent<StreamingPayloadTickProbe>();
                if (!Expect(probe != nullptr, contract, cell, u8"配置の Tick probe がありません"))
                {
                    return;
                }
                before[index++] = probe->Calls;
            }
            Advance(focus);
            WorldRef.SyncToSceneView();
            Query.Rebuild(WorldRef);
            index = 0;
            for (const Entity* entity : root->GetChildEntities())
            {
                const auto* probe = entity->GetComponent<StreamingPayloadTickProbe>();
                const auto* mega = entity->GetComponent<Component::MegaGeometryComponent>();
                Expect(bActive ? probe->Calls > before[index] : probe->Calls == before[index],
                       contract, cell, u8"配置の Tick の増え方が有効状態と合いません");
                Expect(mega && ProxyCount(mega->GetComponentId()) == (bActive ? 1u : 0u),
                       contract, cell, u8"配置の MegaGeometry Component の proxy の公開数が違います");
                CheckPlacementProxies(ItemProxyIds[cell][index], bActive ? 1u : 0u, contract, cell);
                // 隣の配置物や地形の境界に触れない高さから、個々の配置物を狙う。
                const Math::Ray ray(Math::Vector3(cell * 64.0f + 8.0f + index * 8.0f, 30.0f, 48.0f),
                                    Math::Vector3(0.0f, -1.0f, 0.0f));
                Scene::RaycastHit hit;
                const bool bHit = Query.Raycast(ray, hit);
                Expect(bHit == bActive && (!bActive || hit.HitEntity == entity), contract, cell,
                       u8"配置の Raycast の結果が有効状態と合いません");
                ++index;
            }
            CheckScatter(cell, bActive, false, contract);
            Expect(Terrain.IsTileResident(cell) == bActive, contract, cell, u8"地形区画の常駐の印が違います");
            Expect(WorldRef.FindEntityByObjectId(Site.EntityIds[cell]) && ProxyCount(Site.ComponentIds[cell]) == 1,
                   contract, cell, u8"地形区画の Entity または遠景の描画 proxy が消えました");
        }

        void CheckBaseline(uint32_t contract)
        {
            Expect(CountEntities(WorldRef) == BaselineEntities, contract, -1, u8"解放後の World の Entity 数が基準へ戻りません");
            Expect(Scatter.GetStats().ResidentUnits == BaselineUnits, contract, -1, u8"散布の常駐単位数が基準へ戻りません");
            Expect(TileMask() == BaselineTiles, contract, -1, u8"地形区画の常駐の印が基準へ戻りません");
            Expect(System.GetLastFrameStats().ResidentCells == BaselineCells, contract, -1,
                   u8"常駐セル数が基準へ戻りません");
            for (uint32_t cell = 0; cell < CellCount; ++cell)
            {
                Expect(System.GetCellState(cell) == EStreamingCellState::Unloaded, contract, cell,
                       u8"セルが Unloaded へ戻りません");
            }
        }
    };

    RunResult Run(const Container::TSharedPtr<const CookedScatterData>& data)
    {
        TestRig rig;
        if (!rig.Initialize(data))
        {
            return {};
        }
        // この焦点では隣のセルは先読みの輪の外にあり、セル 0 だけが読まれる。
        rig.Hold(-40.0f);
        rig.Expect(rig.System.GetCellState(0) == EStreamingCellState::Active && rig.SpawnFrames[0] > 1,
                   1, 0, u8"配置の構築が複数フレームに分かれて Active になりません");
        rig.CheckPublication(0, true, -40.0f, 2);
        rig.Hold(-120.0f);
        rig.Expect(rig.System.GetCellState(0) == EStreamingCellState::Dormant, 3, 0, u8"セルが休眠しません");
        rig.CheckPublication(0, false, -120.0f, 3);
        rig.Hold(-40.0f);
        rig.Expect(rig.System.GetCellState(0) == EStreamingCellState::Active, 4, 0, u8"セルが復帰しません");
        rig.CheckPublication(0, true, -40.0f, 4);

        Container::VariableArray<uint64_t> removedIds;
        Container::VariableArray<PlacementProxyIds> removedProxyIds;
        if (const auto* root = rig.PlacementHandler.FindCellRoot(0, 2))
        {
            removedIds.push_back(root->GetObjectId());
            uint32_t index = 0;
            for (const Entity* child : root->GetChildEntities())
            {
                removedIds.push_back(child->GetObjectId());
                removedProxyIds.push_back(rig.ItemProxyIds[0][index++]);
            }
        }
        removedIds.push_back(rig.Site.EntityIds[0]);
        rig.Hold(FarFocus);
        rig.WorldRef.SyncToSceneView();
        for (const auto& ids : removedProxyIds)
        {
            rig.CheckPlacementProxies(ids, 0, 5, 0);
        }
        rig.CheckBaseline(5);
        for (uint64_t id : removedIds)
        {
            rig.Expect(rig.WorldRef.FindEntityByObjectId(id) == nullptr, 5, 0,
                       u8"解放した配置または地形の Entity が World に残っています");
        }
        rig.CheckScatter(0, false, true, 5);

        for (uint32_t cycle = 0; cycle < 20; ++cycle)
        {
            for (uint32_t cell = 0; cell < CellCount; ++cell)
            {
                const float focus = 32.0f + cell * 64.0f;
                rig.Hold(focus);
                rig.Expect(rig.System.GetCellState(cell) == EStreamingCellState::Active,
                           6, cell, u8"往路でセルを読み終えません");
                rig.CheckPublication(cell, true, focus, 6);
            }
            for (int cell = CellCount - 2; cell >= 0; --cell)
            {
                rig.Hold(32.0f + cell * 64.0f);
            }
            rig.Hold(FarFocus);
            const int failuresBefore = g_Failures;
            rig.CheckBaseline(6);
            if (g_Failures != failuresBefore)
            {
                std::cerr << Utf8(u8"  出し入れの周回=") << cycle + 1 << "\n";
            }
        }
        for (const auto& transition : rig.System.GetTransitions())
        {
            rig.Result.Transitions.push_back(transition);
        }
        return std::move(rig.Result);
    }

    void EncodeBits(uint64_t value, uint8_t* bytes, uint32_t size)
    {
        for (uint32_t byte = 0; byte < size; ++byte)
        {
            bytes[byte] = static_cast<uint8_t>(value >> (byte * 8));
        }
    }

    void EncodeTransition(const StreamingTransition& value, uint8_t (&bytes)[14])
    {
        // 未規定のパディングを比較せず、公開された全フィールドのビットを比較する。
        EncodeBits(value.Frame, bytes, 8);
        EncodeBits(value.Cell, bytes + 8, 4);
        bytes[12] = static_cast<uint8_t>(value.From);
        bytes[13] = static_cast<uint8_t>(value.To);
    }

    void EncodeFrame(const FrameRecord& value, uint8_t (&bytes)[24])
    {
        for (uint32_t cell = 0; cell < CellCount; ++cell)
        {
            EncodeBits(value.Items[cell], bytes + cell * 4, 4);
        }
        EncodeBits(value.ResidentUnits, bytes + 16, 4);
        EncodeBits(value.TileMask, bytes + 20, 4);
    }
} // namespace

int main()
{
    const auto data = MakeData();
    if (!data)
    {
        return 1;
    }
    const auto first = Run(data);
    const auto second = Run(data);
    Check(!first.Frames.empty() && first.Frames.size() == second.Frames.size(), 7, 0, -1,
          u8"二回のフレーム記録の長さが一致しません");
    for (size_t index = 0; index < first.Frames.size() && index < second.Frames.size(); ++index)
    {
        uint8_t firstBytes[24];
        uint8_t secondBytes[24];
        EncodeFrame(first.Frames[index], firstBytes);
        EncodeFrame(second.Frames[index], secondBytes);
        Check(std::memcmp(firstBytes, secondBytes, sizeof(firstBytes)) == 0, 7, index + 1, -1,
              u8"配置数・常駐単位数・地形の印のビットが一致しません");
    }
    Check(first.Transitions.size() == second.Transitions.size(), 7, 0, -1, u8"二回の遷移記録の長さが一致しません");
    for (size_t index = 0; index < first.Transitions.size() && index < second.Transitions.size(); ++index)
    {
        uint8_t firstBytes[14];
        uint8_t secondBytes[14];
        EncodeTransition(first.Transitions[index], firstBytes);
        EncodeTransition(second.Transitions[index], secondBytes);
        Check(std::memcmp(firstBytes, secondBytes, sizeof(firstBytes)) == 0, 7,
              first.Transitions[index].Frame, first.Transitions[index].Cell, u8"遷移記録のビットが一致しません");
    }
    if (g_Failures != 0)
    {
        std::cerr << Utf8(u8"StreamingPayloadTest 不合格: ") << g_Failures << "\n";
        return 1;
    }
    std::cout << Utf8(u8"StreamingPayloadTest 合格: 予算・有効・休眠・復帰・解放・二十周・二回の決定性を確認。フレーム=")
              << first.Frames.size() << Utf8(u8" 遷移=") << first.Transitions.size() << "\n";
    return 0;
}
