#include "Component/CameraComponent.h"
#include "Component/MeshComponent.h"
#include "Component/SpringArmComponent.h"
#include "Ecs/EcsWorld.h"
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

    // 型 T の列を持つ行の数
    template <typename T>
    uint32_t CountRowsWith(World& world)
    {
        Ecs::EcsQuery query;
        query.All.push_back(Ecs::EcsColumnInfoOf<T>().Type);
        Container::VariableArray<Ecs::EcsArchetypeId> archetypes;
        world.GetEcsWorld().MatchArchetypes(query, archetypes);
        uint32_t rows = 0;
        for (const Ecs::EcsArchetypeId archetype : archetypes)
            rows += world.GetEcsWorld().GetArchetypeRowCount(archetype);
        return rows;
    }

    // 更新して、描画の変換を旧の式で求め直した値と全行照合する。照合した Entity の数を返す
    uint32_t UpdateAndVerify(World& world)
    {
        world.UpdateRenderTransforms();
        uint32_t count = 0;
        assert(world.VerifyEcsRenderTransform(&count));
        return count;
    }

    // 補間の列は、補間する行とその子孫にだけ付き、処理するのも変わった行だけ。付け外しのあとも旧の式と一致する
    void TestRenderRowsAreIncremental()
    {
        World world;
        world.Initialize();
        Container::VariableArray<Entity*> statics;
        auto* staticRoot = world.SpawnEntity<Entity>();
        for (int i = 0; i < 200; ++i)
        {
            auto* entity = world.SpawnEntity<Entity>(staticRoot);
            entity->SetLocalPosition(static_cast<float>(i), 0, 0);
            statics.push_back(entity);
        }
        auto* body = world.SpawnEntity<Entity>();
        auto* visual = world.SpawnEntity<Entity>(body);
        auto* child = world.SpawnEntity<Entity>(visual);
        auto* leaf = world.SpawnEntity<Entity>(child);
        visual->SetRenderInterpolationEnabled(true);
        const uint32_t total = static_cast<uint32_t>(world.GetEcsWorld().GetEntityCount());
        assert(UpdateAndVerify(world) == total);
        // 描画の変換の列は visual とその子孫（child・leaf）だけ。タグと履歴は visual だけ
        assert(CountRowsWith<Ecs::EcsRenderTransform>(world) == 3);
        assert(CountRowsWith<Ecs::EcsRenderInterpolated>(world) == 1);
        assert(CountRowsWith<Ecs::EcsTransformHistory>(world) == 1);

        // 動かない行は、更新を重ねても描画の版が進まない。動いた行だけ 1 回進む
        const uint64_t before = statics[10]->GetRenderTransformVersion();
        const uint64_t movedBefore = statics[20]->GetRenderTransformVersion();
        for (int i = 0; i < 3; ++i)
            assert(UpdateAndVerify(world) == total);
        assert(statics[10]->GetRenderTransformVersion() == before);
        statics[20]->SetLocalPosition(-5, 0, 0);
        assert(UpdateAndVerify(world) == total);
        assert(statics[10]->GetRenderTransformVersion() == before);
        assert(statics[20]->GetRenderTransformVersion() == movedBefore + 1);

        // 固定ステップを重ねて補間を動かす。履歴は visual の行だけが持つ
        for (int step = 0; step < 4; ++step)
        {
            world.PrepareRenderInterpolationStep();
            body->SetLocalPosition(static_cast<float>(step) * 2, 0, 0);
            world.UpdateWorldTransforms();
            world.CaptureRenderInterpolationStep();
            assert(world.SetRenderInterpolationAlpha(.25f * static_cast<float>(step)));
            assert(UpdateAndVerify(world) == total);
        }

        // 許可を外すと描画の変換・履歴の列が外れ、タグだけが残る。戻すと付け直される
        world.SetRenderInterpolationAllowed(false);
        assert(UpdateAndVerify(world) == total);
        assert(CountRowsWith<Ecs::EcsRenderTransform>(world) == 0);
        assert(CountRowsWith<Ecs::EcsTransformHistory>(world) == 0);
        assert(CountRowsWith<Ecs::EcsRenderInterpolated>(world) == 1);
        assert(leaf->GetRenderWorldTransform() == leaf->GetWorldTransform());
        world.SetRenderInterpolationAllowed(true);
        assert(UpdateAndVerify(world) == total);
        assert(CountRowsWith<Ecs::EcsRenderTransform>(world) == 3);
        assert(CountRowsWith<Ecs::EcsTransformHistory>(world) == 1);

        // 補間しない親の下へ付け替えると、子孫も外れる。付け戻すと戻る
        auto* other = world.SpawnEntity<Entity>();
        assert(UpdateAndVerify(world) == total + 1);
        assert(world.ReparentEntity(child, other));
        assert(UpdateAndVerify(world) == total + 1);
        assert(CountRowsWith<Ecs::EcsRenderTransform>(world) == 1);
        assert(world.ReparentEntity(child, visual));
        assert(UpdateAndVerify(world) == total + 1);
        assert(CountRowsWith<Ecs::EcsRenderTransform>(world) == 3);

        // 要求を外すと列が外れ、補間する親の下の新しい子は継承で付く
        visual->SetRenderInterpolationEnabled(false);
        assert(UpdateAndVerify(world) == total + 1);
        assert(CountRowsWith<Ecs::EcsRenderTransform>(world) == 0);
        child->SetRenderInterpolationEnabled(true);
        auto* grand = world.SpawnEntity<Entity>(leaf);
        assert(UpdateAndVerify(world) == total + 2);
        assert(CountRowsWith<Ecs::EcsRenderTransform>(world) == 3);
        assert(CountRowsWith<Ecs::EcsRenderInterpolated>(world) == 1);
        assert(grand->GetParentEntity() == leaf);

        // 補間する行の部分木を消しても、残りの照合が通る
        child->MarkForDestroy();
        world.Tick(.01f);
        world.LateTick(.01f);
        assert(UpdateAndVerify(world) == static_cast<uint32_t>(world.GetEcsWorld().GetEntityCount()));
        assert(CountRowsWith<Ecs::EcsRenderTransform>(world) == 0);
        world.Finalize();
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
    TestRenderRowsAreIncremental();
    std::cout << "WorldRenderInterpolationTest PASS\n";
    return 0;
}
