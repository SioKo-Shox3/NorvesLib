#include "Component/CameraComponent.h"
#include "Object/World.h"
#include "Object/WorldObject.h"
#include "Math/Vector3.h"
#include "Rendering/SceneView.h"
#include "Rendering/SceneProxy.h"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Component;
using namespace NorvesLib::Core::Rendering;

namespace
{
    bool IsNearlyEqual(float lhs, float rhs, float tolerance = 1e-4f)
    {
        return std::abs(lhs - rhs) <= tolerance;
    }

    // ========================================
    // ケース1: アクティブカメラがメインカメラProxyに反映される
    // ========================================
    void TestActiveCameraReflected()
    {
        World world;
        world.Initialize();

        SceneView view;
        SceneViewSettings settings;
        assert(view.Initialize(settings));
        world.SetSceneView(&view);

        WorldObject *object = world.SpawnObject<WorldObject>();
        assert(object);

        CameraComponent *camera = world.CreateComponent<CameraComponent>(object);
        assert(camera);
        camera->SetActiveCamera(true);
        object->SetPosition(1.0f, 2.0f, 3.0f);

        // 同期前はメインカメラProxyを持たない
        assert(!view.HasMainCameraProxy());

        world.SyncToSceneView();

        assert(view.HasMainCameraProxy());
        const CameraProxy &proxy = view.GetMainCameraProxy();
        assert(proxy.CameraId == camera->GetComponentId());
        assert(IsNearlyEqual(proxy.PositionX, 1.0f));
        assert(IsNearlyEqual(proxy.PositionY, 2.0f));
        assert(IsNearlyEqual(proxy.PositionZ, 3.0f));

        world.Finalize();
    }

    // ========================================
    // ケース2: オーナーのTransform変更に追従する
    // ========================================
    void TestCameraUpdateFollowsTransform()
    {
        World world;
        world.Initialize();

        SceneView view;
        SceneViewSettings settings;
        assert(view.Initialize(settings));
        world.SetSceneView(&view);

        WorldObject *object = world.SpawnObject<WorldObject>();
        assert(object);

        CameraComponent *camera = world.CreateComponent<CameraComponent>(object);
        assert(camera);
        camera->SetActiveCamera(true);
        object->SetPosition(1.0f, 2.0f, 3.0f);

        world.SyncToSceneView();
        assert(view.HasMainCameraProxy());
        assert(IsNearlyEqual(view.GetMainCameraProxy().PositionX, 1.0f));

        // 位置を変更して再同期するとProxyが更新される
        object->SetPosition(10.0f, 20.0f, 30.0f);
        world.SyncToSceneView();

        assert(view.HasMainCameraProxy());
        const CameraProxy &proxy = view.GetMainCameraProxy();
        assert(proxy.CameraId == camera->GetComponentId());
        assert(IsNearlyEqual(proxy.PositionX, 10.0f));
        assert(IsNearlyEqual(proxy.PositionY, 20.0f));
        assert(IsNearlyEqual(proxy.PositionZ, 30.0f));

        world.Finalize();
    }

    // ========================================
    // ケース3: 非アクティブ化でメインカメラProxyがクリアされる
    // ========================================
    void TestDeactivateClearsCamera()
    {
        World world;
        world.Initialize();

        SceneView view;
        SceneViewSettings settings;
        assert(view.Initialize(settings));
        world.SetSceneView(&view);

        WorldObject *object = world.SpawnObject<WorldObject>();
        assert(object);

        CameraComponent *camera = world.CreateComponent<CameraComponent>(object);
        assert(camera);
        camera->SetActiveCamera(true);

        world.SyncToSceneView();
        assert(view.HasMainCameraProxy());

        camera->SetActiveCamera(false);
        world.SyncToSceneView();
        assert(!view.HasMainCameraProxy());

        world.Finalize();
    }

    // ========================================
    // ケース4: 決定的選定（RenderOrder最小、同値ならComponentId最小）
    // ========================================
    void TestDeterministicSelection()
    {
        World world;
        world.Initialize();

        SceneView view;
        SceneViewSettings settings;
        assert(view.Initialize(settings));
        world.SetSceneView(&view);

        // 2つのアクティブカメラ。RenderOrderが小さい方が選ばれる。
        WorldObject *objectA = world.SpawnObject<WorldObject>();
        assert(objectA);
        CameraComponent *cameraA = world.CreateComponent<CameraComponent>(objectA);
        assert(cameraA);
        cameraA->SetActiveCamera(true);
        cameraA->SetRenderOrder(5);

        WorldObject *objectB = world.SpawnObject<WorldObject>();
        assert(objectB);
        CameraComponent *cameraB = world.CreateComponent<CameraComponent>(objectB);
        assert(cameraB);
        cameraB->SetActiveCamera(true);
        cameraB->SetRenderOrder(2);

        world.SyncToSceneView();
        assert(view.HasMainCameraProxy());
        // RenderOrderが小さいcameraB(2 < 5)が選ばれる
        assert(view.GetMainCameraProxy().CameraId == cameraB->GetComponentId());

        // RenderOrderを同値にすると、ComponentIdが小さい方が選ばれる
        cameraA->SetRenderOrder(2);
        world.SyncToSceneView();
        assert(view.HasMainCameraProxy());
        const uint64_t expectedId = (cameraA->GetComponentId() < cameraB->GetComponentId())
                                        ? cameraA->GetComponentId()
                                        : cameraB->GetComponentId();
        assert(view.GetMainCameraProxy().CameraId == expectedId);

        world.Finalize();
    }

    // ========================================
    // ケース5: アクティブカメラ所有オブジェクトの破棄でクリアされる（即時経路）
    // ========================================
    void TestRemoveObjectClearsCamera()
    {
        World world;
        world.Initialize();

        SceneView view;
        SceneViewSettings settings;
        assert(view.Initialize(settings));
        world.SetSceneView(&view);

        WorldObject *object = world.SpawnObject<WorldObject>();
        assert(object);

        CameraComponent *camera = world.CreateComponent<CameraComponent>(object);
        assert(camera);
        camera->SetActiveCamera(true);

        world.SyncToSceneView();
        assert(view.HasMainCameraProxy());

        // RemoveObject（即時破棄経路C）でメインカメラProxyがクリアされる
        world.RemoveObject(object);
        assert(!view.HasMainCameraProxy());

        world.Finalize();
    }
}

int main()
{
    std::cout << "WorldCameraSyncTest start\n";

    TestActiveCameraReflected();
    TestCameraUpdateFollowsTransform();
    TestDeactivateClearsCamera();
    TestDeterministicSelection();
    TestRemoveObjectClearsCamera();

    std::cout << "WorldCameraSyncTest passed\n";
    return 0;
}
