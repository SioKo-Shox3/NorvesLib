#include "AINavSmokeMode.h"

#include "AI/IAIModule.h"
#include "AI/Navigation/NavGridBaker.h"
#include "AI/Navigation/NavObstacleComponent.h"
#include "AI/Steering/SteeringAgentComponent.h"
#include "Component/CameraComponent.h"
#include "Component/DirectionalLightComponent.h"
#include "Component/MeshComponent.h"
#include "Engine/Engine.h"
#include "GameMode/GameModeContext.h"
#include "GameMode/GameModeScope.h"
#include "Logging/LogMacros.h"
#include "Math/QuaternionUtils.h"
#include "Physics/CharacterBodyComponent.h"
#include "Physics/ColliderComponent.h"
#include "Physics/RigidBodyComponent.h"
#include "Rendering/RenderResources.h"
#include "Rendering/RenderWorld.h"
#include "Resource/MeshResource.h"

#include <cmath>
#include <iterator>

namespace Game::GameModes
{
    using namespace NorvesLib::Core;
    using namespace NorvesLib::Core::GameMode;
    using namespace NorvesLib::Core::Rendering;
    namespace AI = NorvesLib::Modules::AI;
    namespace P = NorvesLib::Modules::Physics;
    namespace M = NorvesLib::Math;

    namespace
    {
        struct Box
        {
            M::Vector3 Center;
            M::Vector3 HalfExtent;
        };

        const Box Floor{{16, -.5f, 16}, {15, .5f, 15}};
        const Box Obstacles[] = {
            {{10.5f, 1, 14.5f}, {1.7f, 1, 3.4f}},
            {{21.5f, 1, 18.5f}, {1.5f, 1, 3.4f}}
        };
        const Box MovingBox{{16.5f, .75f, 10.5f}, {.75f, .75f, .75f}};
        constexpr float MovingBoxTravel = 13;
        constexpr float MovingBoxSpeed = .5f;
        const M::Vector3 Start{4.5f, 0, 16.5f};
        const M::Vector3 Goal{27.5f, 0, 16.5f};
        constexpr float ArrivalRadius = .6f;
        constexpr double TimeoutSeconds = 90;
        constexpr AI::NavTileCoord CourseTile{0, 0};
        using namespace NorvesLib::Core::literals;
        constexpr MeshDataHandle BoxMesh{"AINavSmoke.Box"_id.GetHash()};

        MaterialHandle CreateMaterial(GameModeContext& ctx, const M::Vector3& color)
        {
            MaterialCreateData info;
            info.DebugName = "AINavSmoke";
            info.BaseColor[0] = color.x;
            info.BaseColor[1] = color.y;
            info.BaseColor[2] = color.z;
            info.Metallic = 0;
            info.Roughness = .8f;
            info.bTwoSided = true;
            const auto material = ctx.RenderResourcesRef.Materials().Create(info);
            if (material.IsValid())
                ctx.ScopeRef.TrackMaterial(material);
            return material;
        }

        // 表示用の子だけを拡縮し、親の物理形状にはメートル単位の寸法を渡す。
        bool AttachBoxVisual(GameModeContext& ctx, Entity* owner, MaterialHandle material,
                             const M::Vector3& halfExtent, const M::Vector3& offset = {})
        {
            auto* visual = ctx.WorldRef.SpawnEntity<Entity>(owner);
            auto* mesh = ctx.WorldRef.CreateComponent<Component::MeshComponent>(visual);
            if (!mesh)
                return false;
            visual->SetLocalPosition(offset);
            visual->SetLocalScale(halfExtent * 2);
            mesh->SetMeshHandle(BoxMesh);
            mesh->SetMaterial(0, material);
            return true;
        }

        Entity* SpawnBox(GameModeContext& ctx, const Box& box, MaterialHandle material)
        {
            auto* entity = ctx.ScopeRef.SpawnObject<Entity>();
            auto* collider = ctx.WorldRef.CreateComponent<P::ColliderComponent>(entity);
            if (!collider || collider->SetBox(box.HalfExtent) != P::EPhysicsResult::Success ||
                !AttachBoxVisual(ctx, entity, material, box.HalfExtent))
                return nullptr;
            entity->SetPosition(box.Center);
            return entity;
        }

        bool CreateAgent(GameModeContext& ctx, AINavSmokeData& data, MaterialHandle material)
        {
            auto* entity = ctx.ScopeRef.SpawnObject<Entity>();
            if (!entity)
                return false;
            entity->SetPosition(Start);
            entity->SetRotation(M::QuaternionUtils::LookRotation(M::Vector3::UnitX, M::Vector3::UnitY));
            const M::Vector3 center{0, .45f, 0};
            auto* collider = ctx.WorldRef.CreateComponent<P::ColliderComponent>(entity);
            auto* rigid = ctx.WorldRef.CreateComponent<P::RigidBodyComponent>(entity);
            if (!collider || collider->SetCapsule(.32f, .13f) != P::EPhysicsResult::Success ||
                collider->SetLocalPose(M::Transform(center)) != P::EPhysicsResult::Success ||
                !rigid || rigid->SetBodyType(P::EPhysicsBodyType::Kinematic) != P::EPhysicsResult::Success ||
                !AttachBoxVisual(ctx, entity, material, {.32f, .45f, .46f}, center))
                return false;

            // モーターが同じ Entity の固定歩を購読するので、体を先に取り付ける。
            auto* body = ctx.WorldRef.CreateComponent<P::CharacterBodyComponent>(entity);
            if (!body || body->SetDriveMode(P::CharacterDriveMode::Fixed) != P::EPhysicsResult::Success)
                return false;
            auto* agent = ctx.WorldRef.CreateComponent<AI::SteeringAgentComponent>(entity);
            if (!agent || !agent->IsRegisteredToModule())
                return false;
            agent->getDimension() = AI::ESteeringDimension::Ground;
            agent->getNavProfileName() = "Medium";
            agent->getMotorProfilePath() = "AI/Locomotion/Wolf.json";
            agent->getMaxSpeed() = 2.0f;
            data.AgentId = entity->GetObjectId();
            data.PreviousPosition = Start;
            return true;
        }

        bool CreateView(GameModeContext& ctx)
        {
            auto* eye = ctx.ScopeRef.SpawnObject<Entity>();
            auto* sun = ctx.ScopeRef.SpawnObject<Entity>();
            auto* camera = ctx.WorldRef.CreateComponent<Component::CameraComponent>(eye);
            auto* light = ctx.WorldRef.CreateComponent<Component::DirectionalLightComponent>(sun);
            if (!camera || !light)
                return false;
            const M::Vector3 position{16, 35, 4};
            eye->SetPosition(position);
            eye->SetRotation(M::QuaternionUtils::LookRotation(M::Vector3(16, 0, 16) - position, M::Vector3::UnitY));
            camera->SetActiveCamera(true);
            camera->SetFieldOfView(60);
            camera->SetAperture(16);
            camera->SetShutterSpeed(.01f);
            camera->SetISO(100);
            sun->SetRotation(M::QuaternionUtils::LookRotation(M::Vector3(-.4f, -1, -.3f), M::Vector3::UnitY));
            light->SetIntensity(60000);
            ctx.WorldRef.UpdateWorldTransforms();
            CameraProxy proxy;
            if (!camera->BuildCameraProxy(proxy))
                return false;
            ctx.EngineRef.GetRenderWorld().SetMainCamera(proxy);
            return true;
        }

        bool CreateCourse(GameModeContext& ctx, AINavSmokeData& data, AI::INavigationService& nav)
        {
            AI::NavBakeInput input;
            const auto& grid = nav.GetGrid();
            input.Grid = grid.GetDesc();
            input.Tile = CourseTile;
            const auto floorMin = Floor.Center - Floor.HalfExtent;
            const auto floorMax = Floor.Center + Floor.HalfExtent;
            if (grid.FindTile(CourseTile) || grid.CellToTile(grid.WorldToCell(floorMin.x, floorMin.z)) != CourseTile ||
                grid.CellToTile(grid.WorldToCell(floorMax.x, floorMax.z)) != CourseTile)
                return false;
            const auto* profile = nav.FindProfile("Medium");
            if (!profile)
                return false;
            auto boxMesh = MeshResource::CreateBox();
            if (!boxMesh || !ctx.RenderResourcesRef.Meshes().Register(BoxMesh, boxMesh->GetVertexData().data(),
                    boxMesh->GetVertexData().size(), boxMesh->GetIndexData().data(), boxMesh->GetIndexCount()))
                return false;
            ctx.ScopeRef.TrackMesh(BoxMesh);

            const auto floorMaterial = CreateMaterial(ctx, {.27f, .36f, .29f});
            const auto obstacleMaterial = CreateMaterial(ctx, {.58f, .21f, .09f});
            const auto movingMaterial = CreateMaterial(ctx, {.08f, .26f, .73f});
            const auto agentMaterial = CreateMaterial(ctx, {.94f, .72f, .12f});
            if (!floorMaterial || !obstacleMaterial || !movingMaterial || !agentMaterial ||
                !SpawnBox(ctx, Floor, floorMaterial))
                return false;

            AI::NavObstacleFootprint footprints[std::size(Obstacles)];
            for (size_t i = 0; i < std::size(Obstacles); ++i)
            {
                const auto& box = Obstacles[i];
                if (!SpawnBox(ctx, box, obstacleMaterial))
                    return false;
                footprints[i] = {box.Center.x, box.Center.z, box.HalfExtent.x, box.HalfExtent.z, 0};
            }
            auto* moving = SpawnBox(ctx, MovingBox, movingMaterial);
            auto* rigid = ctx.WorldRef.CreateComponent<P::RigidBodyComponent>(moving);
            if (!rigid || rigid->SetBodyType(P::EPhysicsBodyType::Kinematic) != P::EPhysicsResult::Success)
                return false;
            auto* obstacle = ctx.WorldRef.CreateComponent<AI::NavObstacleComponent>(moving);
            if (!obstacle || !obstacle->IsRegisteredToModule())
                return false;
            obstacle->getShape() = AI::ENavObstacleShape::Obb;
            obstacle->getHalfExtents() = {MovingBox.HalfExtent.x, MovingBox.HalfExtent.z};
            obstacle->getMinHeight() = -MovingBox.HalfExtent.y;
            obstacle->getMaxHeight() = MovingBox.HalfExtent.y;
            obstacle->getMode() = AI::ENavObstacleMode::Block;
            data.MovingBoxId = moving->GetObjectId();
            if (!CreateAgent(ctx, data, agentMaterial) || !CreateView(ctx))
                return false;

            input.StaticObstacles = footprints;
            AI::NavTile tile;
            if (!AI::NavGridBaker::Bake(input, tile))
                return false;
            const auto threshold = AI::NavClearanceThresholdQ(profile->Radius, input.Grid.CellSize);
            for (uint32_t i = 0; i < tile.GetCellCount(); ++i)
            {
                if (AI::IsNavCellPassable(tile.Flags[i], tile.ClearanceQ[i], threshold))
                    ++data.WalkableCells;
            }
            if (nav.LoadTile(std::move(tile)) != AI::ENavTileResult::Ok)
                return false;
            data.TileLoaded = true;
            return true;
        }

        void Fail(GameModeContext& ctx, AINavSmokeData& data, const char* reason)
        {
            data.Finished = true;
            NORVES_LOG_ERROR("AINavSmoke", "AI_SMOKE mode=nav result=fail reason=%s", reason);
            ctx.EngineRef.RequestExit(1);
        }
    }

    GameModeEnterResult AINavSmokeRoutine::Enter(GameModeContext& ctx, AINavSmokeData& data)
    {
        auto* ai = AI::FindAIModule(Module::GetModuleRegistry());
        if (!ai || !CreateCourse(ctx, data, ai->GetNavigation()))
        {
            Fail(ctx, data, "setup");
            return GameModeEnterResult::Failed;
        }
        data.PreviousNavDebug = ai->IsNavDebugDrawEnabled();
        data.PreviousSteeringDebug = ai->IsSteeringDebugDrawEnabled();
        ai->SetNavDebugDrawEnabled(true);
        ai->SetSteeringDebugDrawEnabled(true);
        return GameModeEnterResult::Succeeded;
    }

    void AINavSmokeRoutine::Tick(GameModeContext& ctx, AINavSmokeData& data, float dt)
    {
        if (data.Finished)
            return;
        data.Elapsed += dt;
        auto* moving = ctx.WorldRef.FindEntityByObjectId(data.MovingBoxId);
        auto* obstacle = moving ? moving->GetComponent<AI::NavObstacleComponent>() : nullptr;
        auto* owner = ctx.WorldRef.FindEntityByObjectId(data.AgentId);
        auto* agent = owner ? owner->GetComponent<AI::SteeringAgentComponent>() : nullptr;
        auto* body = owner ? owner->GetComponent<P::CharacterBodyComponent>() : nullptr;
        if (!obstacle || !agent || !body)
        {
            Fail(ctx, data, "setup");
            return;
        }
        const uint32_t obstacleCells = obstacle->GetStampedCellCount();
        if (obstacleCells > data.MaxObstacleCells)
            data.MaxObstacleCells = obstacleCells;

        const double phase = std::fmod(data.Elapsed * MovingBoxSpeed, 2.0 * MovingBoxTravel);
        const float offset = static_cast<float>(MovingBoxTravel - std::abs(phase - MovingBoxTravel));
        moving->SetPosition(MovingBox.Center + M::Vector3(0, 0, offset));

        if (!data.Ready && body->GetState().bReady && agent->IsMotorBound())
        {
            if (!agent->RequestMoveTo(Goal, .3f))
            {
                Fail(ctx, data, "setup");
                return;
            }
            data.Ready = true;
            NORVES_LOG_INFO("AINavSmoke", "AI_SMOKE mode=nav stage=ready cells=%u start=%.1f,%.1f goal=%.1f,%.1f obstacle_cells=%u",
                            data.WalkableCells, Start.x, Start.z, Goal.x, Goal.z, obstacleCells);
        }
        const auto position = owner->GetWorldTransform().position;
        data.Distance += std::hypot(position.x - data.PreviousPosition.x, position.z - data.PreviousPosition.z);
        data.PreviousPosition = position;
        if (data.Ready && std::hypot(position.x - Goal.x, position.z - Goal.z) <= ArrivalRadius)
        {
            if (data.MaxObstacleCells == 0)
            {
                Fail(ctx, data, "obstacle");
                return;
            }
            data.Finished = true;
            NORVES_LOG_INFO("AINavSmoke", "AI_SMOKE mode=nav result=pass time=%.3f distance=%.3f obstacle_cells=%u obstacle_cells_max=%u",
                            data.Elapsed, data.Distance, obstacleCells, data.MaxObstacleCells);
            ctx.EngineRef.RequestExit(0);
        }
        else if (data.Elapsed >= TimeoutSeconds)
        {
            Fail(ctx, data, data.Ready ? "timeout" : "setup");
        }
    }

    void AINavSmokeRoutine::Leave(GameModeContext&, AINavSmokeData& data, GameModeExitReason)
    {
        if (data.TileLoaded)
        {
            if (auto* ai = AI::FindAIModule(Module::GetModuleRegistry()))
            {
                ai->GetNavigation().UnloadTile(CourseTile);
                ai->SetNavDebugDrawEnabled(data.PreviousNavDebug);
                ai->SetSteeringDebugDrawEnabled(data.PreviousSteeringDebug);
            }
        }
        data = {};
    }
}
