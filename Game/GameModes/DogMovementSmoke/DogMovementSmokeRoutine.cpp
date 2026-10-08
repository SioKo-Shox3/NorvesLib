#include "Component/CameraComponent.h"
#include "Component/DirectionalLightComponent.h"
#include "Component/MeshComponent.h"
#include "DogMovementSmokeGeometry.h"
#include "DogMovementSmokeMode.h"
#include "Engine/Engine.h"
#include "Game/Input/GameInputActions.h"
#include "GameMode/GameModeContext.h"
#include "GameMode/GameModeScope.h"
#include "Gameplay/Camera/FollowCameraComponent.h"
#include "Gameplay/Locomotion/QuadrupedLocomotionComponent.h"
#include "Logging/LogMacros.h"
#include "Math/QuaternionUtils.h"
#include "Physics/ColliderComponent.h"
#include "Physics/RigidBodyComponent.h"
#include "Rendering/ProceduralMeshGenerator.h"
#include "Rendering/RenderResources.h"
#include "Rendering/RenderWorld.h"
#include "Terrain/TerrainMesher.h"
#include <algorithm>
#include <cmath>
namespace Game::GameModes
{
    using namespace NorvesLib::Core;
    using namespace NorvesLib::Core::Rendering;
    using namespace NorvesLib::Core::GameMode;
    namespace M = NorvesLib::Math;
    namespace P = NorvesLib::Modules::Physics;
    namespace
    {
        bool RegisterMesh(GameModeContext& ctx, const Container::VariableArray<Mesh3DVertex>& vertices,
                          const Container::VariableArray<uint32_t>& indices, MeshDataHandle& handle)
        {
            if (!ctx.RenderResourcesRef.Meshes().Register(handle, vertices.data(),
                                                          static_cast<uint32_t>(vertices.size() * sizeof(Mesh3DVertex)),
                                                          indices.data(), static_cast<uint32_t>(indices.size())))
                return false;
            ctx.ScopeRef.TrackMesh(handle);
            return true;
        }
        bool AttachMesh(GameModeContext& ctx, Entity* owner, MeshDataHandle mesh, MaterialHandle material)
        {
            auto* component = ctx.WorldRef.CreateComponent<Component::MeshComponent>(owner);
            if (!component)
                return false;
            component->SetMeshHandle(mesh);
            component->SetMaterial(0, material);
            return true;
        }
    } // namespace
    void DogMovementSmokeRoutine::Stop(GameModeContext& ctx, DogMovementSmokeData& data)
    {
        data.LateState.reset();
        if (data.HitStop.IsValid())
            (void)ctx.EngineRef.GetTimeSystem().RemoveScale(data.HitStop);
        data.HitStop = {};
        if (auto slot = data.LateSlot.lock())
            slot->Reset();
        if (auto* owner = ctx.WorldRef.FindEntityByObjectId(data.CharacterId))
            if (auto* driver = owner->GetComponent<Gameplay::QuadrupedLocomotionComponent>())
                driver->BindInput(nullptr);
        if (auto* owner = ctx.WorldRef.FindEntityByObjectId(data.CameraOwnerId))
            if (auto* camera = owner->GetComponent<Gameplay::FollowCameraComponent>())
            {
                camera->BindInput(nullptr);
                camera->BindSceneQuery(nullptr);
            }
        auto& mapper = ctx.EngineRef.GetInputMapper();
        if (data.PushedContext)
            (void)mapper.RemoveContext(InputActions::GameplayContext);
        data.PushedContext = false;
        ctx.ScopeRef.Cleanup();
        for (auto& material : data.Materials)
        {
            if (material.IsValid())
                ctx.RenderResourcesRef.Materials().Release(material);
            material = {};
        }
        data.CharacterId = data.CameraOwnerId = 0;
        data.ReportedReady = false;
    }
    GameModeEnterResult DogMovementSmokeRoutine::Enter(GameModeContext& ctx, DogMovementSmokeData& data)
    {
        bool succeeded = false;
        struct Guard
        {
            GameModeContext& Context;
            DogMovementSmokeData& Data;
            bool& Succeeded;
            ~Guard()
            {
                if (!Succeeded)
                    DogMovementSmokeRoutine::Stop(Context, Data);
            }
        } guard{ctx, data, succeeded};
        auto& mapper = ctx.EngineRef.GetInputMapper();
        if (mapper.GetActiveContext() != InputActions::GameplayContext)
        {
            if (!mapper.PushContext(InputActions::GameplayContext))
                return GameModeEnterResult::Failed;
            data.PushedContext = true;
        }
        const float colors[3][3] = {{.35f, .45f, .35f}, {.65f, .25f, .1f}, {.1f, .2f, .7f}};
        for (unsigned i = 0; i < 3; ++i)
        {
            MaterialCreateData material;
            material.DebugName = "DogMovementSmoke";
            material.Metallic = 0;
            material.Roughness = .75f;
            material.bTwoSided = true;
            for (unsigned c = 0; c < 3; ++c)
                material.BaseColor[c] = colors[i][c];
            data.Materials[i] = ctx.RenderResourcesRef.Materials().Create(material);
            if (!data.Materials[i].IsValid())
                return GameModeEnterResult::Failed;
        }
        Container::VariableArray<Mesh3DVertex> vertices;
        Container::VariableArray<uint32_t> indices;
        MeshDataHandle capsuleMesh, planeMesh, sphereMesh, wallMesh;
        Container::TSharedPtr<const Terrain::HeightField> heightField;
        DogMovementSmokeGeometry::Capsule(vertices, indices);
        if (!RegisterMesh(ctx, vertices, indices, capsuleMesh))
            return GameModeEnterResult::Failed;
        if (data.Terrain)
        {
            // 中央は開始用の平地。外側へ進むと緩い丘と谷へつながる。
            constexpr uint32_t side = 129;
            Container::VariableArray<float> heights(side * side);
            for (uint32_t z = 0; z < side; ++z)
                for (uint32_t x = 0; x < side; ++x)
                {
                    const float px = float(x) * .5f - 32, pz = float(z) * .5f - 32;
                    const float radius = std::sqrt(px * px + pz * pz);
                    const float blend = std::clamp((radius - 4.f) / 6.f, 0.f, 1.f);
                    heights[size_t(z) * side + x] =
                        blend * blend * (3 - 2 * blend) *
                        (1.5f * std::sin(px * .18f) * std::cos(pz * .15f) + .4f * std::sin(pz * .5f));
                }
            heightField = Terrain::HeightField::Create(side, side, .5f, {heights.data(), heights.size()});
            if (!heightField || !Terrain::BuildTerrainMesh(*heightField, vertices, indices))
                return GameModeEnterResult::Failed;
        }
        else
            ProceduralMeshGenerator::GeneratePlane(1, 1, 1, 1, vertices, indices);
        if (!RegisterMesh(ctx, vertices, indices, planeMesh))
            return GameModeEnterResult::Failed;
        ProceduralMeshGenerator::GenerateUVSphere(.08f, 12, 6, vertices, indices);
        if (!RegisterMesh(ctx, vertices, indices, sphereMesh))
            return GameModeEnterResult::Failed;
        DogMovementSmokeGeometry::WallBox(vertices, indices);
        if (!RegisterMesh(ctx, vertices, indices, wallMesh))
            return GameModeEnterResult::Failed;
        auto* floor = ctx.ScopeRef.SpawnObject<Entity>();
        auto* wall = ctx.ScopeRef.SpawnObject<Entity>();
        auto* physical = ctx.ScopeRef.SpawnObject<Entity>();
        auto* cameraOwner = ctx.ScopeRef.SpawnObject<Entity>();
        auto* view = ctx.ScopeRef.SpawnObject<Entity>();
        auto* lightOwner = ctx.ScopeRef.SpawnObject<Entity>();
        if (!floor || !wall || !physical || !cameraOwner || !view || !lightOwner)
            return GameModeEnterResult::Failed;
        data.CharacterId = physical->GetObjectId();
        data.CameraOwnerId = cameraOwner->GetObjectId();
        auto* visual = ctx.WorldRef.SpawnEntity<Entity>(physical);
        auto* pose = visual ? ctx.WorldRef.SpawnEntity<Entity>(visual) : nullptr;
        auto* nose = pose ? ctx.WorldRef.SpawnEntity<Entity>(pose) : nullptr;
        auto* floorVisual = ctx.WorldRef.SpawnEntity<Entity>(floor);
        auto* wallVisual = ctx.WorldRef.SpawnEntity<Entity>(wall);
        if (!visual || !pose || !nose || !floorVisual || !wallVisual)
            return GameModeEnterResult::Failed;
        if (data.Terrain)
            floor->SetPosition({-32, 0, -32});
        else
        {
            floor->SetPosition({0, -.5f, 0});
            floorVisual->SetLocalPosition({0, .5f, 0});
            floorVisual->SetLocalScale({30, 1, 30});
        }
        wall->SetPosition({0, 1.5f, -5});
        nose->SetLocalPosition({0, .5f, .34f});
        if (!AttachMesh(ctx, floorVisual, planeMesh, data.Materials[0]) ||
            !AttachMesh(ctx, wallVisual, wallMesh, data.Materials[2]) ||
            !AttachMesh(ctx, pose, capsuleMesh, data.Materials[1]) ||
            !AttachMesh(ctx, nose, sphereMesh, data.Materials[2]))
            return GameModeEnterResult::Failed;
        auto* floorCollider = ctx.WorldRef.CreateComponent<P::ColliderComponent>(floor);
        auto* wallCollider = ctx.WorldRef.CreateComponent<P::ColliderComponent>(wall);
        auto* collider = ctx.WorldRef.CreateComponent<P::ColliderComponent>(physical);
        auto* body = ctx.WorldRef.CreateComponent<P::RigidBodyComponent>(physical);
        auto* character = ctx.WorldRef.CreateComponent<P::CharacterBodyComponent>(physical);
        auto* driver = ctx.WorldRef.CreateComponent<Gameplay::QuadrupedLocomotionComponent>(physical);
        auto* follow = ctx.WorldRef.CreateComponent<Gameplay::FollowCameraComponent>(cameraOwner);
        auto* camera = ctx.WorldRef.CreateComponent<Component::CameraComponent>(cameraOwner);
        auto* light = ctx.WorldRef.CreateComponent<Component::DirectionalLightComponent>(lightOwner);
        if (!floorCollider || !wallCollider || !collider || !body || !character || !driver || !follow || !camera ||
            !light)
            return GameModeEnterResult::Failed;
        const auto floorResult =
            data.Terrain ? floorCollider->SetHeightField(heightField) : floorCollider->SetBox({15, .5f, 15});
        if (floorResult != P::EPhysicsResult::Success ||
            wallCollider->SetBox({4, 1.5f, .1f}) != P::EPhysicsResult::Success ||
            collider->SetCapsule(.3f, .1f) != P::EPhysicsResult::Success ||
            collider->SetLocalPose(M::Transform(M::Vector3(0, .4f, 0))) != P::EPhysicsResult::Success ||
            body->SetBodyType(P::EPhysicsBodyType::Kinematic) != P::EPhysicsResult::Success ||
            !driver->SetVisualRoots(visual, pose) || !driver->SetDriveMode(data.Drive) ||
            !driver->SetViewSource(view) || !follow->SetSubject(visual, physical) || !follow->SetViewSource(view))
            return GameModeEnterResult::Failed;
        driver->BindInput(&mapper);
        follow->BindInput(&mapper);
        follow->BindSceneQuery(&ctx.EngineRef.GetSceneQuery());
        follow->SetTargetOffset({0, .5f, 0});
        follow->SetArmLength(3);
        follow->SetPitch(15);
        follow->SetYaw(180);
        cameraOwner->SetPosition({0, 1.276457f, -2.897777f});
        cameraOwner->SetRotation(
            M::QuaternionUtils::LookRotation(M::Vector3(0, -.776457f, 2.897777f), M::Vector3::UnitY));
        camera->SetActiveCamera(true);
        camera->SetFieldOfView(60);
        camera->SetNearPlane(.05f);
        camera->SetAperture(16);
        camera->SetShutterSpeed(.01f);
        camera->SetISO(100);
        camera->SetExposureCompensation(0);
        lightOwner->SetRotation(M::QuaternionUtils::LookRotation(M::Vector3(-.4f, -1, -.3f), M::Vector3::UnitY));
        light->SetLightColor(1, .95f, .85f);
        light->SetIntensity(60000);
        data.LateState = Container::MakeShared<DogMovementSmokeLateState>();
        data.LateState->CharacterId = physical->GetObjectId();
        data.LateState->OwnerId = cameraOwner->GetObjectId();
        data.LateState->SpringArmId = follow->GetComponentId();
        data.LateState->CameraId = camera->GetComponentId();
        const Container::TWeakPtr<DogMovementSmokeLateState> weak = data.LateState;
        auto* world = &ctx.WorldRef;
        auto* render = &ctx.EngineRef.GetRenderWorld();
        auto* engine = &ctx.EngineRef;
        character->OnLanded.Add(Delegate<void, const P::CharacterBodyState&>([weak, world](const P::CharacterBodyState&) {
            auto state = weak.lock();
            if (!state)
                return;
            auto* owner = world->FindEntityByObjectId(state->OwnerId);
            auto* camera = owner ? owner->GetComponent<Gameplay::FollowCameraComponent>() : nullptr;
            if (camera && camera->GetCameraTickCount())
                camera->AddTrauma(.15f);
        }));

        data.LateState->Callback = Delegate<void, float>([weak, world, render, engine](float) {
            auto state = weak.lock();
            CameraProxy proxy;
            if (!state || !state->BuildSnapshot(*world, proxy))
                return;
            render->SetMainCamera(proxy);
            auto* characterOwner = world->FindEntityByObjectId(state->CharacterId);
            auto* cameraOwner = world->FindEntityByObjectId(state->OwnerId);
            auto* body = characterOwner ? characterOwner->GetComponent<P::CharacterBodyComponent>() : nullptr;
            auto* follow = cameraOwner ? cameraOwner->GetComponent<Gameplay::FollowCameraComponent>() : nullptr;
            if (!body || !body->GetState().bReady || !follow)
                return;
            const auto& times = engine->GetTimeSystem().GetFrameTimes();
            const auto step = body->GetState().StepSerial, cameraTick = follow->GetCameraTickCount();
            const bool stopped =
                times.World == 0 && times.Animation == 0 && times.PhysicsDeltaNanoseconds == 0 && times.Unscaled > 0;
            const bool cameraAdvanced = state->HasPrevious && cameraTick > state->PreviousCameraTick;
            if (stopped && state->HasPrevious && !state->ObservedStop)
            {
                NORVES_LOG_INFO("DogMovementSmoke",
                                "DOG_MOVEMENT_SMOKE stage=hitstop_active world_dt=%g animation_dt=%g physics_ns=%lld "
                                "unscaled_dt=%g body_step_held=%d camera_tick_advanced=%d camera_result=%u",
                                double(times.World), double(times.Animation),
                                static_cast<long long>(times.PhysicsDeltaNanoseconds), double(times.Unscaled),
                                int(step == state->PreviousBodyStep), int(cameraAdvanced),
                                static_cast<unsigned>(follow->GetLastCollisionResult()));
                state->ObservedStop = true;
            }
            else if (!stopped && state->ObservedStop && step > state->PreviousBodyStep)
            {
                NORVES_LOG_INFO("DogMovementSmoke",
                                "DOG_MOVEMENT_SMOKE stage=hitstop_resumed body_step_advanced=1 camera_tick_advanced=%d "
                                "physics_ns=%lld",
                                int(cameraAdvanced), static_cast<long long>(times.PhysicsDeltaNanoseconds));
                state->ObservedStop = false;
            }
            state->PreviousBodyStep = step;
            state->PreviousCameraTick = cameraTick;
            state->HasPrevious = true;
        });
        ctx.WorldRef.UpdateWorldTransforms();
        CameraProxy initial;
        if (!camera->BuildCameraProxy(initial))
            return GameModeEnterResult::Failed;
        ctx.EngineRef.GetRenderWorld().SetMainCamera(initial);
        succeeded = true;
        return GameModeEnterResult::Succeeded;
    }
    void DogMovementSmokeRoutine::Tick(GameModeContext& ctx, DogMovementSmokeData& data, float)
    {
        if (auto slot = data.LateSlot.lock())
            slot->Arm(data.LateState);
        // 比較用にBiteの押下で100ms停止する。噛みつきの本実装ではなく、Camera/Input継続の確認用。
        const auto bite = ctx.EngineRef.GetInputMapper().GetAction(InputActions::GameplayContext, InputActions::Bite);
        if (bite.Active && bite.Button.Pressed)
        {
            Engine::TimeScaleRequest request;
            request.Channels = Engine::TimeChannelBit(Engine::TimeChannel::World) |
                               Engine::TimeChannelBit(Engine::TimeChannel::Animation) |
                               Engine::TimeChannelBit(Engine::TimeChannel::Physics);
            request.Scale = 0;
            request.DurationSeconds = .1;
            request.Tag = InputActions::Bite;
            Engine::TimeScaleHandle next;
            if (ctx.EngineRef.GetTimeSystem().PushScale(request, next) == Engine::TimeSystemResult::Success)
            {
                if (data.HitStop.IsValid())
                    (void)ctx.EngineRef.GetTimeSystem().RemoveScale(data.HitStop);
                data.HitStop = next;
                NORVES_LOG_INFO("DogMovementSmoke", "DOG_MOVEMENT_SMOKE stage=hitstop duration_ms=100");
            }
        }

        auto* owner = ctx.WorldRef.FindEntityByObjectId(data.CharacterId);
        auto* cameraOwner = ctx.WorldRef.FindEntityByObjectId(data.CameraOwnerId);
        auto* body = owner ? owner->GetComponent<P::CharacterBodyComponent>() : nullptr;
        auto* follow = cameraOwner ? cameraOwner->GetComponent<Gameplay::FollowCameraComponent>() : nullptr;
        if (!data.ReportedReady && body && body->GetState().bReady && follow &&
            follow->GetLastCollisionResult() == Camera::CameraCollisionResult::Success)
        {
            data.ReportedReady = true;
            NORVES_LOG_INFO("DogMovementSmoke", "DOG_MOVEMENT_SMOKE stage=ready drive=%s",
                            data.Drive == P::CharacterDriveMode::Fixed ? "fixed" : "variable");
        }
    }
    void DogMovementSmokeRoutine::Leave(GameModeContext& ctx, DogMovementSmokeData& data, GameModeExitReason)
    {
        Stop(ctx, data);
    }
} // namespace Game::GameModes
