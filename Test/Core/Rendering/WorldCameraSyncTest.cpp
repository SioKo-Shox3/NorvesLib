#include "Component/CameraComponent.h"
#include "Component/SpringArmComponent.h"
#include "Component/SpringArmTypes.h"
#include "Object/World.h"
#include "Object/Entity.h"
#include "Math/Vector3.h"
#include "Math/VectorUtils.h"
#include "Rendering/SceneProxy.h"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Component;
using namespace NorvesLib::Core::Rendering;
using NorvesLib::Math::Vector3;
using NorvesLib::Math::VectorUtils;

// ========================================
// D1 ハイブリッド供給（Component ベース3層カメラ → RenderWorld::SetMainCamera）
// における SpringArmComponent と CameraComponent の結合検証。
//
// develop には rubin の F2（SceneView::SetMainCameraProxy 等）が存在しない
// ため、旧 WorldCameraSyncTest（SceneView 経由の選定・クリアを検証）は
// そのまま移植できない。本テストは D1 ハイブリッドの実際の供給経路
// （SpringArmComponent::RefreshOwnerTransform → CameraComponent::
// BuildCameraProxy → 呼び出し側が RenderWorld::SetMainCamera へ渡す）の
// うち、Core 単体で検証可能な「SpringArm 駆動 → CameraComponent 取得」の
// 結合部分を確認する。RenderWorld::SetMainCamera 自体は RHI/Vulkan
// 初期化を要するため対象外とする（Game 層の Rendering3DTest 起動スモークで
// 別途確認済み）。
// ========================================

namespace
{
    bool IsNearlyEqual(float lhs, float rhs, float tolerance = 1e-4f)
    {
        return std::abs(lhs - rhs) <= tolerance;
    }

    bool VecNearlyEqual(const Vector3 &a, const Vector3 &b, float tolerance = 1e-3f)
    {
        return IsNearlyEqual(a.x, b.x, tolerance) &&
               IsNearlyEqual(a.y, b.y, tolerance) &&
               IsNearlyEqual(a.z, b.z, tolerance);
    }

    // ========================================
    // ケース1: SpringArm 駆動後の owner Transform を CameraComponent が
    //          正しく BuildCameraProxy へ反映する
    // ========================================
    void TestCameraTracksSpringArmDrivenTransform()
    {
        World world;
        world.Initialize();

        Entity *pivot = world.SpawnObject<Entity>();
        assert(pivot);
        pivot->SetPosition(0.0f, 0.0f, 0.0f);

        Entity *cameraObject = world.SpawnObject<Entity>();
        assert(cameraObject);

        SpringArmComponent *arm = world.CreateComponent<SpringArmComponent>(cameraObject);
        assert(arm);
        arm->SetPivot(pivot);
        arm->SetArmLength(5.0f);
        arm->SetYaw(0.0f);
        arm->SetPitch(30.0f);

        CameraComponent *camera = world.CreateComponent<CameraComponent>(cameraObject);
        assert(camera);
        camera->SetActiveCamera(true);

        // Enter 直後の初期確定と同じ経路（Component::Tick を直呼びしない）。
        arm->RefreshOwnerTransform();

        CameraProxy proxy;
        assert(camera->BuildCameraProxy(proxy));

        // BuildCameraProxy の位置は owner（= SpringArm が駆動した cameraObject）の
        // ワールド位置と一致する。
        const Vector3 ownerPos = cameraObject->GetPosition();
        assert(IsNearlyEqual(proxy.PositionX, ownerPos.x));
        assert(IsNearlyEqual(proxy.PositionY, ownerPos.y));
        assert(IsNearlyEqual(proxy.PositionZ, ownerPos.z));

        // forward は正規化済みで、注視点（ピボット位置）へ向いている。
        const Vector3 forward(proxy.ForwardX, proxy.ForwardY, proxy.ForwardZ);
        const Vector3 expectedForward = (pivot->GetPosition() - ownerPos).Normalized();
        assert(IsNearlyEqual(VectorUtils::Length(forward), 1.0f, 1e-3f));
        assert(VecNearlyEqual(forward, expectedForward));

        world.Finalize();
    }

    // ========================================
    // ケース2: ピボット移動後、RefreshOwnerTransform → BuildCameraProxy が
    //          新しいピボット位置に追従する
    // ========================================
    void TestCameraTracksPivotMovement()
    {
        World world;
        world.Initialize();

        Entity *pivot = world.SpawnObject<Entity>();
        assert(pivot);
        pivot->SetPosition(0.0f, 0.0f, 0.0f);

        Entity *cameraObject = world.SpawnObject<Entity>();
        assert(cameraObject);

        SpringArmComponent *arm = world.CreateComponent<SpringArmComponent>(cameraObject);
        assert(arm);
        arm->SetPivot(pivot);
        arm->SetArmLength(4.0f);
        arm->SetYaw(20.0f);
        arm->SetPitch(15.0f);

        CameraComponent *camera = world.CreateComponent<CameraComponent>(cameraObject);
        assert(camera);
        camera->SetActiveCamera(true);

        arm->RefreshOwnerTransform();

        CameraProxy proxyBefore;
        assert(camera->BuildCameraProxy(proxyBefore));
        const Vector3 posBefore(proxyBefore.PositionX, proxyBefore.PositionY, proxyBefore.PositionZ);

        // ピボットを移動し、SpringArm を再確定する（GameMode の Tick 内
        // ApplyIntent 直後の RefreshOwnerTransform と同じ経路）。
        const Vector3 move(3.0f, 1.0f, -2.0f);
        pivot->SetPosition(pivot->GetPosition() + move);
        arm->RefreshOwnerTransform();

        CameraProxy proxyAfter;
        assert(camera->BuildCameraProxy(proxyAfter));
        const Vector3 posAfter(proxyAfter.PositionX, proxyAfter.PositionY, proxyAfter.PositionZ);

        // カメラは同じオフセットを保ったままピボット移動量だけ追従する。
        assert(VecNearlyEqual(posAfter, posBefore + move));

        world.Finalize();
    }

    // ========================================
    // ケース3: D1 の実フレーム経路（ApplyIntent → RefreshOwnerTransform →
    //          BuildCameraProxy）を通しで検証する
    // ========================================
    // ケース1/2 は setter でアーム状態を直接作って RefreshOwnerTransform を
    // 呼ぶため、GameMode の Tick が実際に辿る ApplyIntent 経由の経路を通らない。
    // 本ケースは非ゼロの SpringArmIntent を ApplyIntent し、その直後の
    // BuildCameraProxy が更新後の Yaw/Pitch/ArmLength を反映することを確認する。
    void TestCameraReflectsAppliedIntent()
    {
        World world;
        world.Initialize();

        Entity *pivot = world.SpawnObject<Entity>();
        assert(pivot);
        pivot->SetPosition(0.0f, 0.0f, 0.0f);

        Entity *cameraObject = world.SpawnObject<Entity>();
        assert(cameraObject);

        SpringArmComponent *arm = world.CreateComponent<SpringArmComponent>(cameraObject);
        assert(arm);
        arm->SetPivot(pivot);
        arm->SetArmLength(5.0f);
        arm->SetYaw(0.0f);
        arm->SetPitch(30.0f);

        CameraComponent *camera = world.CreateComponent<CameraComponent>(cameraObject);
        assert(camera);
        camera->SetActiveCamera(true);

        // Enter 直後の初期確定。
        arm->RefreshOwnerTransform();
        CameraProxy proxyBefore;
        assert(camera->BuildCameraProxy(proxyBefore));
        const Vector3 posBefore(proxyBefore.PositionX, proxyBefore.PositionY, proxyBefore.PositionZ);

        // GameMode の Tick と同じ経路: ApplyIntent → RefreshOwnerTransform →
        // BuildCameraProxy。
        SpringArmIntent intent;
        intent.YawDelta = 45.0f;
        intent.PitchDelta = -10.0f;
        intent.DollyDelta = 1.0f;
        arm->ApplyIntent(intent);
        arm->RefreshOwnerTransform();

        assert(IsNearlyEqual(arm->GetYaw(), 45.0f));
        assert(IsNearlyEqual(arm->GetPitch(), 20.0f));
        assert(IsNearlyEqual(arm->GetArmLength(), 4.0f));

        CameraProxy proxyAfter;
        assert(camera->BuildCameraProxy(proxyAfter));
        const Vector3 posAfter(proxyAfter.PositionX, proxyAfter.PositionY, proxyAfter.PositionZ);

        // ApplyIntent で Yaw/Pitch/ArmLength が変わったので、カメラ位置は
        // Enter 直後の位置から実際に変化している。
        assert(!VecNearlyEqual(posAfter, posBefore, 1e-3f));

        // 新しい位置は更新後のパラメーターから導かれる球面座標と一致する。
        const float yawRad = arm->GetYaw() * NorvesLib::Math::Constants::PI / 180.0f;
        const float pitchRad = arm->GetPitch() * NorvesLib::Math::Constants::PI / 180.0f;
        const float armLen = arm->GetArmLength();
        const Vector3 expectedOffset(
            armLen * std::cos(pitchRad) * std::sin(yawRad),
            armLen * std::sin(pitchRad),
            armLen * std::cos(pitchRad) * std::cos(yawRad));
        assert(VecNearlyEqual(posAfter, pivot->GetPosition() + expectedOffset));

        world.Finalize();
    }

    // ========================================
    // ケース4: BuildCameraProxy はピボット未解決でも owner Transform を維持し、
    //          クラッシュせず動作し続ける（SpringArm 側の安全設計との結合確認）
    // ========================================
    void TestCameraKeepsTransformWhenPivotDestroyed()
    {
        World world;
        world.Initialize();

        Entity *pivot = world.SpawnObject<Entity>();
        assert(pivot);
        pivot->SetPosition(1.0f, 1.0f, 1.0f);

        Entity *cameraObject = world.SpawnObject<Entity>();
        assert(cameraObject);

        SpringArmComponent *arm = world.CreateComponent<SpringArmComponent>(cameraObject);
        assert(arm);
        arm->SetPivot(pivot);
        arm->SetArmLength(5.0f);
        arm->SetYaw(0.0f);
        arm->SetPitch(30.0f);

        CameraComponent *camera = world.CreateComponent<CameraComponent>(cameraObject);
        assert(camera);
        camera->SetActiveCamera(true);

        arm->RefreshOwnerTransform();

        CameraProxy proxyBefore;
        assert(camera->BuildCameraProxy(proxyBefore));

        // ピボットを破棄（GameMode の Leave に相当）。
        world.RemoveObject(pivot);
        assert(arm->HasValidPivot() == false);

        // RefreshOwnerTransform はクラッシュせず、owner Transform を維持する。
        arm->RefreshOwnerTransform();

        CameraProxy proxyAfter;
        assert(camera->BuildCameraProxy(proxyAfter));
        assert(IsNearlyEqual(proxyAfter.PositionX, proxyBefore.PositionX));
        assert(IsNearlyEqual(proxyAfter.PositionY, proxyBefore.PositionY));
        assert(IsNearlyEqual(proxyAfter.PositionZ, proxyBefore.PositionZ));

        // 向き（Forward/Up/Right）も破棄前後で不変かつ finite であること。
        // 位置だけ維持して向きが壊れる退行は検出できないため。
        const Vector3 forwardBefore(proxyBefore.ForwardX, proxyBefore.ForwardY, proxyBefore.ForwardZ);
        const Vector3 upBefore(proxyBefore.UpX, proxyBefore.UpY, proxyBefore.UpZ);
        const Vector3 rightBefore(proxyBefore.RightX, proxyBefore.RightY, proxyBefore.RightZ);
        const Vector3 forwardAfter(proxyAfter.ForwardX, proxyAfter.ForwardY, proxyAfter.ForwardZ);
        const Vector3 upAfter(proxyAfter.UpX, proxyAfter.UpY, proxyAfter.UpZ);
        const Vector3 rightAfter(proxyAfter.RightX, proxyAfter.RightY, proxyAfter.RightZ);

        assert(std::isfinite(forwardAfter.x) && std::isfinite(forwardAfter.y) && std::isfinite(forwardAfter.z));
        assert(std::isfinite(upAfter.x) && std::isfinite(upAfter.y) && std::isfinite(upAfter.z));
        assert(std::isfinite(rightAfter.x) && std::isfinite(rightAfter.y) && std::isfinite(rightAfter.z));
        assert(VecNearlyEqual(forwardAfter, forwardBefore));
        assert(VecNearlyEqual(upAfter, upBefore));
        assert(VecNearlyEqual(rightAfter, rightBefore));

        world.Finalize();
    }
}

int main()
{
    std::cout << "WorldCameraSyncTest start\n";

    TestCameraTracksSpringArmDrivenTransform();
    TestCameraTracksPivotMovement();
    TestCameraReflectsAppliedIntent();
    TestCameraKeepsTransformWhenPivotDestroyed();

    std::cout << "WorldCameraSyncTest passed\n";
    return 0;
}
