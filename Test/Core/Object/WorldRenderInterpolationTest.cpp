#include "Component/CameraComponent.h"
#include "Component/MeshComponent.h"
#include "Component/SpringArmComponent.h"
#include "Game/CameraLateUpdate.h"
#include "Object/World.h"
#include "Rendering/SceneView.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <iostream>
using namespace NorvesLib;
using namespace NorvesLib::Core;
namespace
{
    bool Near(float a, float b)
    {
        return std::fabs(a - b) < 1e-4f;
    }
    const Rendering::MeshProxy& Proxy(const Rendering::SceneView& view, uint64_t id)
    {
        for (const auto& proxy : view.GetMeshProxies())
            if (proxy.ObjectId == id)
                return proxy;
        assert(false);
        return view.GetMeshProxies()[0];
    }
} // namespace
int main()
{
    World world;
    world.Initialize();
    Rendering::SceneView view;
    Rendering::SceneViewSettings settings;
    assert(view.Initialize(settings));
    world.SetSceneView(&view);
    auto* physical = world.SpawnEntity<Entity>();
    auto* visual = world.SpawnEntity<Entity>(physical);
    auto* child = world.SpawnEntity<Entity>(visual);
    assert(physical && visual && child);
    physical->SetLocalPosition(10, 0, 0);
    visual->SetLocalPosition(1, 0, 0);
    child->SetLocalPosition(0, 0, 2);
    visual->SetRenderInterpolationEnabled(true);
    auto* mesh = world.CreateComponent<Component::MeshComponent>(visual);
    assert(mesh);
    Rendering::MeshDataHandle handle;
    handle.Id = 100;
    mesh->SetMeshHandle(handle);
    world.SyncToSceneView();
    assert(Near(Proxy(view, visual->GetObjectId()).WorldTransform.GetTranslationRow().x, 11));
    world.PrepareRenderInterpolationStep();
    physical->SetLocalPosition(14, 0, 0);
    visual->SetLocalRotation(Math::Quaternion(Math::Vector3::UnitY, 1.570796326f));
    world.UpdateWorldTransforms();
    world.CaptureRenderInterpolationStep();
    const auto version = visual->GetTransformVersion();
    assert(world.SetRenderInterpolationAlpha(.5f));
    world.SyncToSceneView();
    assert(Near(visual->GetWorldTransform().position.x, 15));
    assert(Near(visual->GetRenderWorldTransform().position.x, 13));
    assert(Near(child->GetRenderWorldTransform().position.x, 13 + std::sqrt(2.f)));
    assert(Near(child->GetRenderWorldTransform().position.z, std::sqrt(2.f)));
    assert(Near(Proxy(view, visual->GetObjectId()).WorldTransform.GetTranslationRow().x, 13));
    assert(Near(Proxy(view, visual->GetObjectId()).PreviousWorldTransform.GetTranslationRow().x, 11));
    assert(world.SetRenderInterpolationAlpha(.75f));
    world.SyncToSceneView();
    assert(visual->GetTransformVersion() == version);
    assert(Near(Proxy(view, visual->GetObjectId()).WorldTransform.GetTranslationRow().x, 14));
    assert(Near(Proxy(view, visual->GetObjectId()).PreviousWorldTransform.GetTranslationRow().x, 13));
    // 固定0stepでも視点入力を反映し、後段の再refreshでも描画targetを維持する。
    auto* cameraOwner = world.SpawnEntity<Entity>();
    auto* arm = world.CreateComponent<Component::SpringArmComponent>(cameraOwner);
    auto* camera = world.CreateComponent<Component::CameraComponent>(cameraOwner);
    assert(arm && camera && arm->SetPivot(visual));
    arm->SetYaw(0);
    arm->SetPitch(0);
    arm->SetArmLength(4);
    camera->SetActiveCamera(true);
    Game::CameraLateUpdateState cameraState;
    cameraState.OwnerId = cameraOwner->GetObjectId();
    cameraState.SpringArmId = arm->GetComponentId();
    cameraState.CameraId = camera->GetComponentId();
    world.Tick(.005f);
    world.LateTick(.005f);
    Rendering::CameraProxy cameraProxy;
    assert(cameraState.BuildSnapshot(world, cameraProxy) && Near(cameraProxy.PositionX, 14));
    Component::SpringArmIntent look;
    look.YawDelta = 90;
    arm->ApplyIntent(look);
    assert(world.SetRenderInterpolationAlpha(.9f));
    world.Tick(.005f);
    world.LateTick(.005f);
    assert(cameraState.BuildSnapshot(world, cameraProxy) && Near(cameraProxy.PositionX, 14.6f + 4));
    // step外の親Teleportは0step frameでも履歴を切る。
    physical->SetLocalPosition(100, 0, 0);
    world.UpdateRenderTransforms();
    assert(Near(visual->GetRenderWorldTransform().position.x, 101));
    world.UpdateWorldTransforms();
    world.PrepareRenderInterpolationStep();
    physical->SetLocalPosition(200, 0, 0);
    physical->ResetRenderInterpolation();
    world.UpdateWorldTransforms();
    world.CaptureRenderInterpolationStep();
    assert(world.SetRenderInterpolationAlpha(0));
    world.UpdateRenderTransforms();
    assert(Near(visual->GetRenderWorldTransform().position.x, 201));
    // reparentでworldを保っても古い補間履歴は使わない。
    auto* other = world.SpawnEntity<Entity>();
    other->SetLocalPosition(-10, 0, 0);
    world.UpdateWorldTransforms();
    assert(world.ReparentEntity(visual, other));
    world.UpdateRenderTransforms();
    assert(Near(visual->GetRenderWorldTransform().position.x, 201));
    visual->SetRenderInterpolationEnabled(false);
    world.UpdateRenderTransforms();
    assert(visual->GetRenderWorldTransform() == visual->GetWorldTransform());
    visual->SetRenderInterpolationEnabled(true);
    world.SetRenderInterpolationAllowed(false);
    world.PrepareRenderInterpolationStep();
    visual->SetLocalPosition(4, 0, 0);
    world.UpdateWorldTransforms();
    world.CaptureRenderInterpolationStep();
    world.UpdateRenderTransforms();
    assert(visual->GetRenderWorldTransform() == visual->GetWorldTransform());
    world.SetRenderInterpolationAllowed(true);
    world.UpdateRenderTransforms();
    assert(visual->GetRenderWorldTransform() == visual->GetWorldTransform());
    world.Finalize();
    std::cout << "WorldRenderInterpolationTest PASS\n";
    return 0;
}
