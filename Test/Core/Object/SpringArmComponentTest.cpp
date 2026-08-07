#include "Component/SpringArmComponent.h"
#include "Component/SpringArmTypes.h"
#include "Input/InputState.h"
#include "Input/MayaCameraController.h"
#include "Math/Quaternion.h"
#include "Math/Vector3.h"
#include "Object/Entity.h"
#include "Object/World.h"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Component;
using namespace NorvesLib::Core::Input;

namespace
{
    bool IsNearlyEqual(float lhs, float rhs, float tolerance = 1.0e-4f)
    {
        return std::fabs(lhs - rhs) <= tolerance;
    }

    bool IsNearlyEqual(
        const NorvesLib::Math::Vector3& lhs,
        const NorvesLib::Math::Vector3& rhs,
        float tolerance = 1.0e-4f)
    {
        return IsNearlyEqual(lhs.x, rhs.x, tolerance) &&
               IsNearlyEqual(lhs.y, rhs.y, tolerance) &&
               IsNearlyEqual(lhs.z, rhs.z, tolerance);
    }

    bool IsNearlyEqual(
        const NorvesLib::Math::Quaternion& lhs,
        const NorvesLib::Math::Quaternion& rhs,
        float tolerance = 1.0e-4f)
    {
        return IsNearlyEqual(lhs.x, rhs.x, tolerance) &&
               IsNearlyEqual(lhs.y, rhs.y, tolerance) &&
               IsNearlyEqual(lhs.z, rhs.z, tolerance) &&
               IsNearlyEqual(lhs.w, rhs.w, tolerance);
    }

    void TestWorldLookupCoversHierarchyAndSkipsPendingDestroy()
    {
        World world;
        world.Initialize();

        Entity* root = world.SpawnEntity<Entity>();
        Entity* child = world.SpawnEntity<Entity>(root);
        assert(root != nullptr);
        assert(child != nullptr);

        assert(world.FindEntityByObjectId(0) == nullptr);
        assert(world.FindEntityByObjectId(root->GetObjectId()) == root);
        assert(world.FindEntityByObjectId(child->GetObjectId()) == child);
        assert(world.FindEntityByObjectId(child->GetObjectId() + 1000) == nullptr);

        child->MarkForDestroy();
        assert(world.FindEntityByObjectId(child->GetObjectId()) == nullptr);
        assert(world.FindEntityByObjectId(root->GetObjectId()) == root);

        world.Finalize();
    }

    void TestPivotRejectsForeignWorldAndOwner()
    {
        World world;
        World foreignWorld;
        world.Initialize();
        foreignWorld.Initialize();

        Entity* owner = world.SpawnEntity<Entity>();
        Entity* pivot = world.SpawnEntity<Entity>();
        Entity* foreignPivot = foreignWorld.SpawnEntity<Entity>();
        assert(owner != nullptr);
        assert(pivot != nullptr);
        assert(foreignPivot != nullptr);

        SpringArmComponent* arm = world.CreateComponent<SpringArmComponent>(owner);
        assert(arm != nullptr);
        assert(arm->GetOuter() == owner);

        assert(!arm->SetPivot(foreignPivot));
        assert(arm->GetPivotObjectId() == 0);
        assert(!arm->HasValidPivot());

        assert(arm->SetPivot(pivot));
        assert(arm->GetPivotObjectId() == pivot->GetObjectId());
        assert(arm->ResolvePivot() == pivot);

        assert(!arm->SetPivot(owner));
        assert(arm->ResolvePivot() == pivot);

        assert(arm->SetPivot(nullptr));
        assert(arm->GetPivotObjectId() == 0);
        assert(!arm->HasValidPivot());

        foreignWorld.Finalize();
        world.Finalize();
    }

    void TestOwnerWorldTransformAndIntentAreAppliedByValue()
    {
        World world;
        world.Initialize();

        Entity* pivotParent = world.SpawnEntity<Entity>();
        Entity* pivot = world.SpawnEntity<Entity>(pivotParent);
        Entity* ownerParent = world.SpawnEntity<Entity>();
        Entity* owner = world.SpawnEntity<Entity>(ownerParent);
        assert(pivotParent != nullptr);
        assert(pivot != nullptr);
        assert(ownerParent != nullptr);
        assert(owner != nullptr);

        pivotParent->SetPosition(10.0f, 2.0f, -4.0f);
        pivot->SetLocalPosition(1.0f, 3.0f, 2.0f);
        ownerParent->SetPosition(-7.0f, 5.0f, 8.0f);
        world.UpdateWorldTransforms();

        SpringArmComponent* arm = world.CreateComponent<SpringArmComponent>(owner);
        assert(arm != nullptr);
        assert(arm->SetPivot(pivot));
        arm->SetTargetOffset(NorvesLib::Math::Vector3(0.0f, 1.0f, 0.0f));
        arm->SetArmLength(8.0f);
        arm->SetYaw(90.0f);
        arm->SetPitch(0.0f);
        arm->RefreshOwnerTransform();
        world.UpdateWorldTransforms();

        const NorvesLib::Math::Vector3 target =
            pivot->GetPosition() + NorvesLib::Math::Vector3(0.0f, 1.0f, 0.0f);
        const NorvesLib::Math::Vector3 expectedPosition =
            target + NorvesLib::Math::Vector3(8.0f, 0.0f, 0.0f);
        assert(IsNearlyEqual(owner->GetPosition(), expectedPosition, 1.0e-3f));

        const NorvesLib::Math::Vector3 forward =
            owner->GetRotation() * NorvesLib::Math::Vector3::Forward;
        assert(IsNearlyEqual(forward, (target - expectedPosition).Normalized(), 1.0e-3f));

        const NorvesLib::Math::Vector3 pivotBeforePan = pivot->GetPosition();
        SpringArmIntent intent;
        intent.YawDelta = 15.0f;
        intent.PitchDelta = 10.0f;
        intent.DollyDelta = 2.0f;
        intent.PanDelta = NorvesLib::Math::Vector3(1.0f, -0.5f, 0.0f);
        intent.bHasInput = true;
        arm->ApplyIntent(intent);
        world.UpdateWorldTransforms();

        assert(IsNearlyEqual(arm->GetYaw(), 105.0f));
        assert(IsNearlyEqual(arm->GetPitch(), 10.0f));
        assert(IsNearlyEqual(arm->GetArmLength(), 6.0f));
        assert(!IsNearlyEqual(pivot->GetPosition(), pivotBeforePan));

        arm->Tick(0.016f);
        world.UpdateWorldTransforms();
        assert(std::isfinite(owner->GetPosition().x));
        assert(std::isfinite(owner->GetPosition().y));
        assert(std::isfinite(owner->GetPosition().z));

        world.Finalize();
    }

    void TestMissingPivotPreservesOwnerTransform()
    {
        World world;
        world.Initialize();

        Entity* pivot = world.SpawnEntity<Entity>();
        Entity* owner = world.SpawnEntity<Entity>();
        assert(pivot != nullptr);
        assert(owner != nullptr);

        SpringArmComponent* arm = world.CreateComponent<SpringArmComponent>(owner);
        assert(arm != nullptr);
        assert(arm->SetPivot(pivot));
        arm->RefreshOwnerTransform();

        const NorvesLib::Math::Vector3 positionBefore = owner->GetPosition();
        const NorvesLib::Math::Quaternion rotationBefore = owner->GetRotation();

        pivot->MarkForDestroy();
        assert(!arm->HasValidPivot());
        arm->Tick(0.016f);
        assert(IsNearlyEqual(owner->GetPosition(), positionBefore));
        assert(IsNearlyEqual(owner->GetRotation(), rotationBefore));

        world.Tick(0.016f);
        assert(!arm->HasValidPivot());
        arm->Tick(0.016f);
        assert(IsNearlyEqual(owner->GetPosition(), positionBefore));
        assert(IsNearlyEqual(owner->GetRotation(), rotationBefore));

        arm->SetPivotObjectId(0xFFFFFFFFu);
        assert(!arm->HasValidPivot());
        arm->RefreshOwnerTransform();
        assert(IsNearlyEqual(owner->GetPosition(), positionBefore));
        assert(IsNearlyEqual(owner->GetRotation(), rotationBefore));

        world.Finalize();
    }

    void TestLimitsAvoidSingularTransforms()
    {
        World world;
        world.Initialize();

        Entity* pivot = world.SpawnEntity<Entity>();
        Entity* owner = world.SpawnEntity<Entity>();
        assert(pivot != nullptr);
        assert(owner != nullptr);

        SpringArmComponent* arm = world.CreateComponent<SpringArmComponent>(owner);
        assert(arm != nullptr);
        assert(arm->SetPivot(pivot));

        arm->SetPitchLimits(100.0f, -100.0f);
        assert(arm->GetMinPitch() > -90.0f);
        assert(arm->GetMaxPitch() < 90.0f);
        assert(arm->GetMinPitch() <= arm->GetMaxPitch());

        arm->SetArmLengthLimits(-4.0f, 0.0f);
        arm->SetArmLength(0.0f);
        assert(arm->GetArmLength() > 0.0f);

        arm->SetPitch(90.0f);
        arm->RefreshOwnerTransform();
        assert(std::isfinite(owner->GetRotation().x));
        assert(std::isfinite(owner->GetRotation().y));
        assert(std::isfinite(owner->GetRotation().z));
        assert(std::isfinite(owner->GetRotation().w));

        world.Finalize();
    }

    void TestBuildIntentConvertsPolledInputByValue()
    {
        MayaCameraController controller;
        controller.Initialize(NorvesLib::Math::Vector3(2.0f, 3.0f, 4.0f), 100.0f, 20.0f, -10.0f);

        InputState input;
        input.SetKeyState(KeyCode::LeftAlt, true);
        input.SetMouseButtonState(MouseButton::Left, true);
        input.SetMouseButtonState(MouseButton::Middle, true);
        input.SetMouseButtonState(MouseButton::Right, true);
        input.SetMousePosition(0.0f, 0.0f);
        input.SetMousePosition(4.0f, -6.0f);
        input.AddMouseScroll(3.0f);

        const SpringArmIntent intent = controller.BuildIntent(input, 0.25f, 8.0f);

        assert(intent.bHasInput);
        assert(IsNearlyEqual(intent.YawDelta, -1.2f));
        assert(IsNearlyEqual(intent.PitchDelta, 1.8f));
        assert(IsNearlyEqual(intent.PanDelta, NorvesLib::Math::Vector3(-0.16f, -0.24f, 0.0f)));
        assert(IsNearlyEqual(intent.DollyDelta, 2.08f));
        assert(IsNearlyEqual(controller.GetTarget(), NorvesLib::Math::Vector3(2.0f, 3.0f, 4.0f)));
        assert(IsNearlyEqual(controller.GetDistance(), 100.0f));
        assert(IsNearlyEqual(controller.GetYaw(), 20.0f));
        assert(IsNearlyEqual(controller.GetPitch(), -10.0f));
    }

    void TestBuildIntentRequiresAltForDrag()
    {
        MayaCameraController controller;
        controller.Initialize(NorvesLib::Math::Vector3::Zero, 6.0f, 0.0f, 30.0f);

        InputState idleInput;
        const SpringArmIntent idleIntent = controller.BuildIntent(idleInput, 0.0f, 6.0f);
        assert(!idleIntent.bHasInput);
        assert(IsNearlyEqual(idleIntent.YawDelta, 0.0f));
        assert(IsNearlyEqual(idleIntent.PitchDelta, 0.0f));
        assert(IsNearlyEqual(idleIntent.DollyDelta, 0.0f));
        assert(IsNearlyEqual(idleIntent.PanDelta, NorvesLib::Math::Vector3::Zero));

        InputState dragWithoutAlt;
        dragWithoutAlt.SetMouseButtonState(MouseButton::Left, true);
        dragWithoutAlt.SetMouseButtonState(MouseButton::Middle, true);
        dragWithoutAlt.SetMouseButtonState(MouseButton::Right, true);
        dragWithoutAlt.SetMousePosition(0.0f, 0.0f);
        dragWithoutAlt.SetMousePosition(-5.0f, 2.0f);
        const SpringArmIntent dragIntent = controller.BuildIntent(dragWithoutAlt, 0.016f, 6.0f);
        assert(!dragIntent.bHasInput);
        assert(IsNearlyEqual(dragIntent.YawDelta, 0.0f));
        assert(IsNearlyEqual(dragIntent.PitchDelta, 0.0f));
        assert(IsNearlyEqual(dragIntent.DollyDelta, 0.0f));
        assert(IsNearlyEqual(dragIntent.PanDelta, NorvesLib::Math::Vector3::Zero));
    }

    void TestBuildIntentAcceptsRightAltAndIgnoresFrameDelta()
    {
        MayaCameraController controller;
        controller.Initialize(NorvesLib::Math::Vector3::Zero, 6.0f, 0.0f, 30.0f);

        InputState input;
        input.SetKeyState(KeyCode::RightAlt, true);
        input.SetMouseButtonState(MouseButton::Left, true);
        input.SetMousePosition(0.0f, 0.0f);
        input.SetMousePosition(-5.0f, 2.0f);

        const SpringArmIntent shortFrame = controller.BuildIntent(input, 0.001f, 6.0f);
        const SpringArmIntent longFrame = controller.BuildIntent(input, 1.0f, 6.0f);
        assert(shortFrame.bHasInput);
        assert(IsNearlyEqual(shortFrame.YawDelta, 1.5f));
        assert(IsNearlyEqual(shortFrame.PitchDelta, -0.6f));
        assert(IsNearlyEqual(shortFrame.YawDelta, longFrame.YawDelta));
        assert(IsNearlyEqual(shortFrame.PitchDelta, longFrame.PitchDelta));
    }

    void TestBuildIntentKeepsScrollIndependentFromAlt()
    {
        MayaCameraController controller;
        controller.Initialize(NorvesLib::Math::Vector3::Zero, 6.0f, 0.0f, 30.0f);

        InputState input;
        input.SetMouseButtonState(MouseButton::Left, true);
        input.SetMouseButtonState(MouseButton::Right, true);
        input.SetMousePosition(0.0f, 0.0f);
        input.SetMousePosition(4.0f, -2.0f);
        input.AddMouseScroll(2.0f);

        const SpringArmIntent intent = controller.BuildIntent(input, 0.016f, 6.0f);
        assert(intent.bHasInput);
        assert(IsNearlyEqual(intent.YawDelta, 0.0f));
        assert(IsNearlyEqual(intent.PitchDelta, 0.0f));
        assert(IsNearlyEqual(intent.PanDelta, NorvesLib::Math::Vector3::Zero));
        assert(IsNearlyEqual(intent.DollyDelta, 1.2f));
    }
}

int main()
{
    std::cout << "SpringArmComponentTest start\n";
    TestWorldLookupCoversHierarchyAndSkipsPendingDestroy();
    TestPivotRejectsForeignWorldAndOwner();
    TestOwnerWorldTransformAndIntentAreAppliedByValue();
    TestMissingPivotPreservesOwnerTransform();
    TestLimitsAvoidSingularTransforms();
    TestBuildIntentConvertsPolledInputByValue();
    TestBuildIntentRequiresAltForDrag();
    TestBuildIntentAcceptsRightAltAndIgnoresFrameDelta();
    TestBuildIntentKeepsScrollIndependentFromAlt();
    std::cout << "SpringArmComponentTest passed\n";
    return 0;
}
