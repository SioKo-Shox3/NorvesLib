#include "Component/CameraComponent.h"
#include "Component/SpringArmComponent.h"
#include "Component/SpringArmTypes.h"
#include "Input/CameraInputCollector.h"
#include "Input/InputRouter.h"
#include "Input/MayaCameraController.h"
#include "Math/Vector3.h"
#include "Math/VectorUtils.h"
#include "Object/Entity.h"
#include "Object/World.h"
#include "Rendering/SceneProxy.h"

#include <cassert>
#include <cmath>
#include <iostream>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Component;
using namespace NorvesLib::Core::Input;
using namespace NorvesLib::Core::Rendering;
using NorvesLib::Math::Vector3;
using NorvesLib::Math::VectorUtils;

namespace
{
    bool IsNearlyEqual(float lhs, float rhs, float tolerance = 1e-4f)
    {
        return std::abs(lhs - rhs) <= tolerance;
    }

    bool VecNearlyEqual(const Vector3& lhs, const Vector3& rhs, float tolerance = 1e-3f)
    {
        return IsNearlyEqual(lhs.x, rhs.x, tolerance) &&
               IsNearlyEqual(lhs.y, rhs.y, tolerance) &&
               IsNearlyEqual(lhs.z, rhs.z, tolerance);
    }

    class ConsumingController final : public IInputController
    {
    public:
        bool OnMouseButton(const MouseButtonEvent&) override { return true; }
        bool OnMouseMove(const MouseMoveEvent&) override { return true; }
        bool OnMouseScroll(const MouseScrollEvent&) override { return true; }
        bool OnKey(const KeyEvent&) override { return true; }
        const char* DebugName() const override { return "ConsumingController"; }
    };

    void TestCameraTracksSpringArmDrivenTransform()
    {
        World world;
        world.Initialize();

        Entity* pivot = world.SpawnObject<Entity>();
        Entity* cameraEntity = world.SpawnObject<Entity>();
        assert(pivot != nullptr);
        assert(cameraEntity != nullptr);
        pivot->SetPosition(0.0f, 0.0f, 0.0f);

        SpringArmComponent* arm = world.CreateComponent<SpringArmComponent>(cameraEntity);
        CameraComponent* camera = world.CreateComponent<CameraComponent>(cameraEntity);
        assert(arm != nullptr);
        assert(camera != nullptr);
        assert(arm->SetPivot(pivot));
        arm->SetArmLength(5.0f);
        arm->SetYaw(0.0f);
        arm->SetPitch(30.0f);
        camera->SetActiveCamera(true);

        arm->RefreshOwnerTransform();

        CameraProxy proxy;
        assert(camera->BuildCameraProxy(proxy));
        const Vector3 ownerPosition = cameraEntity->GetPosition();
        assert(IsNearlyEqual(proxy.PositionX, ownerPosition.x));
        assert(IsNearlyEqual(proxy.PositionY, ownerPosition.y));
        assert(IsNearlyEqual(proxy.PositionZ, ownerPosition.z));

        const Vector3 forward(proxy.ForwardX, proxy.ForwardY, proxy.ForwardZ);
        const Vector3 expectedForward = (pivot->GetPosition() - ownerPosition).Normalized();
        assert(IsNearlyEqual(VectorUtils::Length(forward), 1.0f, 1e-3f));
        assert(VecNearlyEqual(forward, expectedForward));

        world.Finalize();
    }

    void TestCameraTracksAppliedIntentAndPivotMovement()
    {
        World world;
        world.Initialize();

        Entity* pivot = world.SpawnObject<Entity>();
        Entity* cameraEntity = world.SpawnObject<Entity>();
        assert(pivot != nullptr);
        assert(cameraEntity != nullptr);

        SpringArmComponent* arm = world.CreateComponent<SpringArmComponent>(cameraEntity);
        CameraComponent* camera = world.CreateComponent<CameraComponent>(cameraEntity);
        assert(arm != nullptr);
        assert(camera != nullptr);
        assert(arm->SetPivot(pivot));
        arm->SetArmLength(5.0f);
        arm->SetYaw(0.0f);
        arm->SetPitch(30.0f);

        arm->RefreshOwnerTransform();
        CameraProxy before;
        assert(camera->BuildCameraProxy(before));
        const Vector3 beforePosition(before.PositionX, before.PositionY, before.PositionZ);

        SpringArmIntent intent;
        intent.YawDelta = 45.0f;
        intent.PitchDelta = -10.0f;
        intent.DollyDelta = 1.0f;
        arm->ApplyIntent(intent);
        arm->RefreshOwnerTransform();

        assert(IsNearlyEqual(arm->GetYaw(), 45.0f));
        assert(IsNearlyEqual(arm->GetPitch(), 20.0f));
        assert(IsNearlyEqual(arm->GetArmLength(), 4.0f));

        CameraProxy afterIntent;
        assert(camera->BuildCameraProxy(afterIntent));
        const Vector3 afterIntentPosition(afterIntent.PositionX, afterIntent.PositionY, afterIntent.PositionZ);
        assert(!VecNearlyEqual(afterIntentPosition, beforePosition));

        const Vector3 pivotMove(3.0f, 1.0f, -2.0f);
        pivot->SetPosition(pivotMove);
        arm->RefreshOwnerTransform();

        CameraProxy afterPivotMove;
        assert(camera->BuildCameraProxy(afterPivotMove));
        const Vector3 afterPivotMovePosition(
            afterPivotMove.PositionX,
            afterPivotMove.PositionY,
            afterPivotMove.PositionZ);
        assert(VecNearlyEqual(afterPivotMovePosition, afterIntentPosition + pivotMove));

        world.Finalize();
    }

    void TestCameraKeepsSnapshotWhenPivotIsDestroyed()
    {
        World world;
        world.Initialize();

        Entity* pivot = world.SpawnObject<Entity>();
        Entity* cameraEntity = world.SpawnObject<Entity>();
        assert(pivot != nullptr);
        assert(cameraEntity != nullptr);

        SpringArmComponent* arm = world.CreateComponent<SpringArmComponent>(cameraEntity);
        CameraComponent* camera = world.CreateComponent<CameraComponent>(cameraEntity);
        assert(arm != nullptr);
        assert(camera != nullptr);
        assert(arm->SetPivot(pivot));
        arm->SetArmLength(5.0f);
        arm->SetPitch(30.0f);
        arm->RefreshOwnerTransform();

        CameraProxy before;
        assert(camera->BuildCameraProxy(before));
        world.RemoveObject(pivot);
        assert(!arm->HasValidPivot());

        arm->RefreshOwnerTransform();
        CameraProxy after;
        assert(camera->BuildCameraProxy(after));
        assert(IsNearlyEqual(after.PositionX, before.PositionX));
        assert(IsNearlyEqual(after.PositionY, before.PositionY));
        assert(IsNearlyEqual(after.PositionZ, before.PositionZ));
        assert(std::isfinite(after.ForwardX));
        assert(std::isfinite(after.ForwardY));
        assert(std::isfinite(after.ForwardZ));

        world.Finalize();
    }

    void TestCollectorPreservesRouterConsumeOrder()
    {
        InputRouter router;
        ConsumingController blocker;
        Game::Input::CameraInputCollector collector;
        router.RegisterController(&blocker, InputRouter::PriorityOverlay);
        router.RegisterController(&collector, InputRouter::PriorityGame);

        router.DispatchKey({KeyCode::LeftAlt, InputAction::Pressed});
        router.DispatchMouseButton({MouseButton::Left, InputAction::Pressed, 10.0f, 20.0f});
        router.DispatchMouseMove({14.0f, 26.0f, 4.0f, 6.0f});
        router.DispatchMouseScroll({2.0f});

        const InputState blocked = collector.BuildFrameInputState();
        assert(!blocked.IsAltDown());
        assert(!blocked.IsMouseButtonDown(MouseButton::Left));
        assert(IsNearlyEqual(blocked.GetMouseState().DeltaX, 0.0f));
        assert(IsNearlyEqual(blocked.GetMouseState().DeltaY, 0.0f));
        assert(IsNearlyEqual(blocked.GetMouseState().ScrollDelta, 0.0f));
    }

    void TestCollectorBuildsAltDragIntentAndAltIndependentScroll()
    {
        InputRouter router;
        Game::Input::CameraInputCollector collector;
        MayaCameraController maya;
        maya.Initialize(Vector3(0.0f, 0.0f, 0.0f), 5.0f, 0.0f, 30.0f);
        router.RegisterController(&collector, InputRouter::PriorityGame);

        router.DispatchKey({KeyCode::RightAlt, InputAction::Pressed});
        router.DispatchMouseButton({MouseButton::Left, InputAction::Pressed, 0.0f, 0.0f});
        router.DispatchMouseMove({8.0f, -4.0f, 8.0f, -4.0f});
        const InputState altDrag = collector.BuildFrameInputState();
        assert(altDrag.IsKeyDown(KeyCode::RightAlt));
        const SpringArmIntent altIntent = maya.BuildIntent(altDrag, 1.0f / 60.0f, 5.0f);
        assert(!IsNearlyEqual(altIntent.YawDelta, 0.0f));
        assert(!IsNearlyEqual(altIntent.PitchDelta, 0.0f));

        collector.ResetFrame();
        router.DispatchKey({KeyCode::RightAlt, InputAction::Released});
        router.DispatchMouseMove({11.0f, 2.0f, 3.0f, 6.0f});
        const InputState plainDrag = collector.BuildFrameInputState();
        const SpringArmIntent plainIntent = maya.BuildIntent(plainDrag, 1.0f / 60.0f, 5.0f);
        assert(IsNearlyEqual(plainIntent.YawDelta, 0.0f));
        assert(IsNearlyEqual(plainIntent.PitchDelta, 0.0f));
        assert(IsNearlyEqual(plainIntent.PanDelta.x, 0.0f));
        assert(IsNearlyEqual(plainIntent.PanDelta.y, 0.0f));
        assert(IsNearlyEqual(plainIntent.PanDelta.z, 0.0f));

        collector.ResetFrame();
        router.DispatchMouseScroll({1.0f});
        const InputState scroll = collector.BuildFrameInputState();
        assert(!scroll.IsAltDown());
        const SpringArmIntent scrollIntent = maya.BuildIntent(scroll, 1.0f / 60.0f, 5.0f);
        assert(!IsNearlyEqual(scrollIntent.DollyDelta, 0.0f));
    }

    void TestCollectorBuildsLeftAltDragIntent()
    {
        InputRouter router;
        Game::Input::CameraInputCollector collector;
        MayaCameraController maya;
        maya.Initialize(Vector3(0.0f, 0.0f, 0.0f), 5.0f, 0.0f, 30.0f);
        router.RegisterController(&collector, InputRouter::PriorityGame);

        router.DispatchKey({KeyCode::LeftAlt, InputAction::Pressed});
        router.DispatchMouseButton({MouseButton::Left, InputAction::Pressed, 0.0f, 0.0f});
        router.DispatchMouseMove({-6.0f, 5.0f, -6.0f, 5.0f});

        const InputState leftAltDrag = collector.BuildFrameInputState();
        assert(leftAltDrag.IsKeyDown(KeyCode::LeftAlt));
        const SpringArmIntent leftAltIntent = maya.BuildIntent(leftAltDrag, 1.0f / 60.0f, 5.0f);
        assert(!IsNearlyEqual(leftAltIntent.YawDelta, 0.0f));
        assert(!IsNearlyEqual(leftAltIntent.PitchDelta, 0.0f));
    }
}

int main()
{
    std::cout << "WorldCameraSyncTest start\n";
    TestCameraTracksSpringArmDrivenTransform();
    TestCameraTracksAppliedIntentAndPivotMovement();
    TestCameraKeepsSnapshotWhenPivotIsDestroyed();
    TestCollectorPreservesRouterConsumeOrder();
    TestCollectorBuildsAltDragIntentAndAltIndependentScroll();
    TestCollectorBuildsLeftAltDragIntent();
    std::cout << "WorldCameraSyncTest passed\n";
    return 0;
}
