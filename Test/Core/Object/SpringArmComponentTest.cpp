#include "Component/SpringArmComponent.h"
#include "Component/SpringArmTypes.h"
#include "Component/CameraComponent.h"
#include "Input/MayaCameraController.h"
#include "Object/World.h"
#include "Object/WorldObject.h"
#include "Math/Vector3.h"
#include "Math/Quaternion.h"
#include "Math/QuaternionUtils.h"
#include "Math/VectorUtils.h"
#include "Rendering/SceneProxy.h"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Component;
using namespace NorvesLib::Core::Input;
using namespace NorvesLib::Core::Rendering;
using NorvesLib::Math::Vector3;
using NorvesLib::Math::Quaternion;
using NorvesLib::Math::QuaternionUtils;
using NorvesLib::Math::VectorUtils;

namespace
{
    bool IsNearlyEqual(float lhs, float rhs, float tolerance = 1e-4f)
    {
        return std::abs(lhs - rhs) <= tolerance;
    }

    bool IsFinite3(const Vector3 &v)
    {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }

    bool VecNearlyEqual(const Vector3 &a, const Vector3 &b, float tolerance = 1e-3f)
    {
        return IsNearlyEqual(a.x, b.x, tolerance) &&
               IsNearlyEqual(a.y, b.y, tolerance) &&
               IsNearlyEqual(a.z, b.z, tolerance);
    }

    // 球面座標式（MayaCameraController::RecalculatePosition と同一）。
    Vector3 SphericalCameraPos(const Vector3 &target, float distance, float yawDeg, float pitchDeg)
    {
        const float yawRad = yawDeg * NorvesLib::Math::Constants::PI / 180.0f;
        const float pitchRad = pitchDeg * NorvesLib::Math::Constants::PI / 180.0f;
        const float cosP = std::cos(pitchRad);
        const float sinP = std::sin(pitchRad);
        const float cosY = std::cos(yawRad);
        const float sinY = std::sin(yawRad);
        return Vector3(
            target.x + distance * cosP * sinY,
            target.y + distance * sinP,
            target.z + distance * cosP * cosY);
    }

    // ========================================
    // ケース1: 駆動（球面位置 + forward が注視点方向）
    // ========================================
    void TestDrive()
    {
        World world;
        world.Initialize();

        WorldObject *pivot = world.SpawnObject<WorldObject>();
        assert(pivot);
        pivot->SetPosition(2.0f, 1.0f, -3.0f);

        WorldObject *cameraObj = world.SpawnObject<WorldObject>();
        assert(cameraObj);

        SpringArmComponent *arm = world.CreateComponent<SpringArmComponent>(cameraObj);
        assert(arm);

        arm->SetPivot(pivot);
        assert(arm->GetPivotObjectId() == pivot->GetObjectId());
        assert(arm->HasValidPivot());

        arm->SetArmLength(7.0f);
        arm->SetYaw(35.0f);
        arm->SetPitch(25.0f);

        arm->Tick(0.016f);

        const Vector3 target = pivot->GetPosition(); // TargetOffset=0（既定）
        const Vector3 expectedPos = SphericalCameraPos(target, 7.0f, 35.0f, 25.0f);
        assert(VecNearlyEqual(cameraObj->GetPosition(), expectedPos));

        // forward = rotation * +Z は注視点方向に一致
        const Vector3 forward = cameraObj->GetRotation() * Vector3::Forward;
        const Vector3 expectedForward = (target - cameraObj->GetPosition()).Normalized();
        assert(VecNearlyEqual(forward, expectedForward));
        assert(IsFinite3(forward));

        world.Finalize();
    }

    // ========================================
    // ケース2: 追従（ピボット移動でカメラ位置が追従）
    // ========================================
    void TestFollow()
    {
        World world;
        world.Initialize();

        WorldObject *pivot = world.SpawnObject<WorldObject>();
        assert(pivot);
        pivot->SetPosition(0.0f, 0.0f, 0.0f);

        WorldObject *cameraObj = world.SpawnObject<WorldObject>();
        assert(cameraObj);

        SpringArmComponent *arm = world.CreateComponent<SpringArmComponent>(cameraObj);
        assert(arm);
        arm->SetPivot(pivot);
        arm->SetArmLength(5.0f);
        arm->SetYaw(0.0f);
        arm->SetPitch(30.0f);

        arm->Tick(0.016f);
        const Vector3 posBefore = cameraObj->GetPosition();

        // ピボットを移動 → 再Tick → カメラ位置が同じオフセットで追従
        const Vector3 move(10.0f, 4.0f, -6.0f);
        pivot->SetPosition(pivot->GetPosition() + move);
        arm->Tick(0.016f);
        const Vector3 posAfter = cameraObj->GetPosition();

        assert(VecNearlyEqual(posAfter, posBefore + move));

        // 期待値（球面式）とも一致
        const Vector3 expectedPos = SphericalCameraPos(pivot->GetPosition(), 5.0f, 0.0f, 30.0f);
        assert(VecNearlyEqual(posAfter, expectedPos));

        world.Finalize();
    }

    // ========================================
    // ケース3: 寿命（ピボット破棄で use-after-free を避け、Transform 維持）
    // ========================================
    void TestLifetime()
    {
        World world;
        world.Initialize();

        WorldObject *pivot = world.SpawnObject<WorldObject>();
        assert(pivot);
        pivot->SetPosition(1.0f, 2.0f, 3.0f);

        WorldObject *cameraObj = world.SpawnObject<WorldObject>();
        assert(cameraObj);

        SpringArmComponent *arm = world.CreateComponent<SpringArmComponent>(cameraObj);
        assert(arm);
        arm->SetPivot(pivot);
        arm->SetArmLength(5.0f);
        arm->SetYaw(10.0f);
        arm->SetPitch(20.0f);

        arm->Tick(0.016f);
        const Vector3 posBefore = cameraObj->GetPosition();
        const Quaternion rotBefore = cameraObj->GetRotation();

        // ピボットを即時破棄
        world.RemoveObject(pivot);

        // ピボットは解決不能になる
        assert(arm->HasValidPivot() == false);
        assert(arm->ResolvePivot() == nullptr);

        // Tick はクラッシュせず（use-after-free 回避）、オーナー Transform を維持
        arm->Tick(0.016f);
        assert(VecNearlyEqual(cameraObj->GetPosition(), posBefore));
        assert(IsNearlyEqual(cameraObj->GetRotation().x, rotBefore.x));
        assert(IsNearlyEqual(cameraObj->GetRotation().y, rotBefore.y));
        assert(IsNearlyEqual(cameraObj->GetRotation().z, rotBefore.z));
        assert(IsNearlyEqual(cameraObj->GetRotation().w, rotBefore.w));

        world.Finalize();
    }

    // ========================================
    // ケース4: ApplyIntent（Yaw/Pitch/Dolly/Pan）
    // ========================================
    void TestApplyIntent()
    {
        World world;
        world.Initialize();

        WorldObject *pivot = world.SpawnObject<WorldObject>();
        assert(pivot);
        pivot->SetPosition(0.0f, 0.0f, 0.0f);

        WorldObject *cameraObj = world.SpawnObject<WorldObject>();
        assert(cameraObj);

        SpringArmComponent *arm = world.CreateComponent<SpringArmComponent>(cameraObj);
        assert(arm);
        arm->SetPivot(pivot);
        arm->SetArmLength(5.0f);
        arm->SetYaw(0.0f);
        arm->SetPitch(30.0f);

        // Yaw +10
        {
            SpringArmIntent intent;
            intent.YawDelta = 10.0f;
            arm->ApplyIntent(intent);
            assert(IsNearlyEqual(arm->GetYaw(), 10.0f));
        }

        // Dolly +2 → ArmLength 5 - 2 = 3
        {
            SpringArmIntent intent;
            intent.DollyDelta = 2.0f;
            arm->ApplyIntent(intent);
            assert(IsNearlyEqual(arm->GetArmLength(), 3.0f));
        }

        // Pitch クランプ（+200 しても MaxPitch=89 を超えない）
        {
            SpringArmIntent intent;
            intent.PitchDelta = 200.0f;
            arm->ApplyIntent(intent);
            assert(IsNearlyEqual(arm->GetPitch(), arm->GetMaxPitch()));
            assert(arm->GetPitch() <= 89.0f + 1e-4f);
        }
        // 下方向クランプ（-400 しても MinPitch=-89 を下回らない）
        {
            SpringArmIntent intent;
            intent.PitchDelta = -400.0f;
            arm->ApplyIntent(intent);
            assert(IsNearlyEqual(arm->GetPitch(), arm->GetMinPitch()));
            assert(arm->GetPitch() >= -89.0f - 1e-4f);
        }

        // Pan: ピボットの Position が移動する（焦点移動）
        {
            const Vector3 pivotBefore = pivot->GetPosition();
            SpringArmIntent intent;
            intent.PanDelta = Vector3(1.5f, -0.5f, 0.0f);
            arm->ApplyIntent(intent);
            const Vector3 pivotAfter = pivot->GetPosition();
            // 何らかの移動があったこと（basis 投影で非ゼロ）
            assert(!VecNearlyEqual(pivotAfter, pivotBefore, 1e-5f));
            assert(IsFinite3(pivotAfter));
        }

        world.Finalize();
    }

    // ========================================
    // ケース5【最重要】: ゴールデン一致（旧 ApplyTo との等価）
    // ========================================
    // 旧経路: MayaCameraController::ApplyTo
    // 新経路: SpringArmComponent::Tick → CameraComponent::BuildCameraProxy
    //
    // CameraViewConstants の view 行列は Position/Forward/Up のみを使う
    //（CreateLookAt(pos, pos+forward, up)）。LookRotation(forward, Vector3::Up) は
    // Maya の forward/up を完全再現するため、描画結果は一致する。
    // Right は LookRotation が右手系（Cross(up, forward)）、Maya の ApplyTo は
    // 左手系（Cross(forward, up)）で導くため符号が反転するが、view 行列では未使用。
    // そこで Position/Forward/Up を厳密に一致確認し、Right は正規直交＆右手系を確認する。
    void CheckGolden(const Vector3 &target, float distance, float yaw, float pitch,
                     bool bSingular)
    {
        // --- 旧経路 ---
        MayaCameraController ctrl;
        ctrl.Initialize(target, distance, yaw, pitch);
        CameraProxy proxyOld;
        ctrl.ApplyTo(proxyOld);

        // --- 新経路 ---
        World world;
        world.Initialize();

        WorldObject *pivot = world.SpawnObject<WorldObject>();
        assert(pivot);
        pivot->SetPosition(target);

        WorldObject *cameraObj = world.SpawnObject<WorldObject>();
        assert(cameraObj);

        SpringArmComponent *arm = world.CreateComponent<SpringArmComponent>(cameraObj);
        assert(arm);
        arm->SetPivot(pivot);
        arm->SetTargetOffset(Vector3::Zero);
        // 制限内に収まるよう ArmLength 制限は既定（0.1〜10000）のまま、Pitch 制限も既定。
        arm->SetArmLength(distance);
        arm->SetYaw(yaw);
        arm->SetPitch(pitch);

        CameraComponent *cam = world.CreateComponent<CameraComponent>(cameraObj);
        assert(cam);

        arm->Tick(0.016f);

        CameraProxy proxyNew;
        assert(cam->BuildCameraProxy(proxyNew));

        const Vector3 oldPos(proxyOld.PositionX, proxyOld.PositionY, proxyOld.PositionZ);
        const Vector3 newPos(proxyNew.PositionX, proxyNew.PositionY, proxyNew.PositionZ);
        const Vector3 oldFwd(proxyOld.ForwardX, proxyOld.ForwardY, proxyOld.ForwardZ);
        const Vector3 newFwd(proxyNew.ForwardX, proxyNew.ForwardY, proxyNew.ForwardZ);
        const Vector3 oldUp(proxyOld.UpX, proxyOld.UpY, proxyOld.UpZ);
        const Vector3 newUp(proxyNew.UpX, proxyNew.UpY, proxyNew.UpZ);
        const Vector3 newRight(proxyNew.RightX, proxyNew.RightY, proxyNew.RightZ);

        // finite であること
        assert(IsFinite3(newPos) && IsFinite3(newFwd) && IsFinite3(newUp) && IsFinite3(newRight));

        // Position と Forward は常に一致（描画位置・視線方向）
        assert(VecNearlyEqual(newPos, oldPos));
        assert(VecNearlyEqual(newFwd, oldFwd));

        if (!bSingular)
        {
            // 真上/真下から離れていれば Up も一致（描画結果を決める）
            assert(VecNearlyEqual(newUp, oldUp));
        }

        // Right は新経路の規約（右手系: Cross(Up, Forward)）と整合
        const Vector3 expectedRight = VectorUtils::Cross(newUp, newFwd).Normalized();
        assert(VecNearlyEqual(newRight, expectedRight));

        // 正規直交基底
        assert(IsNearlyEqual(VectorUtils::Length(newFwd), 1.0f, 1e-3f));
        assert(IsNearlyEqual(VectorUtils::Length(newUp), 1.0f, 1e-3f));
        assert(IsNearlyEqual(VectorUtils::Length(newRight), 1.0f, 1e-3f));
        assert(IsNearlyEqual(VectorUtils::Dot(newFwd, newUp), 0.0f, 1e-3f));
        assert(IsNearlyEqual(VectorUtils::Dot(newFwd, newRight), 0.0f, 1e-3f));
        assert(IsNearlyEqual(VectorUtils::Dot(newUp, newRight), 0.0f, 1e-3f));

        world.Finalize();
    }

    // ========================================
    // ケース6: Pan 方向の旧 Maya 一致（符号パリティ）
    // ========================================
    // 同一入力（MMB ドラッグ）に対し、
    //   旧: MayaCameraController::Update 後の GetTarget() の移動量
    //   新: BuildIntent → SpringArmComponent::ApplyIntent 後の pivot 移動量
    // が一致することを確認する。これは Pan-X の符号反転バグ（LookRotation 由来 right が
    // 旧 GetRight() と逆符号）に対する回帰テスト。
    //
    // InputState の合成: 公開 API のみで MMB 押下とマウス delta を作る。
    //  - SetMouseButtonState(MouseButton::Middle, true) で MMB を押下状態に。
    //  - SetMousePosition(0,0) で初回更新（delta=0、prev=0,0 に確定）。
    //  - SetMousePosition(dx,dy) で delta=(dx,dy) を生成（prev は 0,0）。
    InputState MakeMmbDragInput(float dx, float dy)
    {
        InputState input;
        input.SetMouseButtonState(MouseButton::Middle, true);
        input.SetMousePosition(0.0f, 0.0f); // 初回更新でデルタ 0、prev=(0,0)
        input.SetMousePosition(dx, dy);     // delta=(dx,dy)
        return input;
    }

    void CheckPanParity(float dx, float dy)
    {
        const Vector3 target(2.0f, 1.0f, -3.0f);
        const float distance = 6.0f;
        const float yaw = 35.0f;
        const float pitch = 25.0f; // 特異点から離れた姿勢
        const float dt = 0.016f;

        const InputState input = MakeMmbDragInput(dx, dy);

        // --- 旧経路: Update(MMB) で m_Target が移動する ---
        MayaCameraController ctrlOld;
        ctrlOld.Initialize(target, distance, yaw, pitch);
        ctrlOld.Update(input, dt);
        const Vector3 oldTargetDelta = ctrlOld.GetTarget() - target;

        // 横・縦ともに非ゼロな delta であること（方向検証として意味を持たせる）
        assert(std::abs(oldTargetDelta.x) > 1e-5f || std::abs(oldTargetDelta.z) > 1e-5f);
        assert(IsFinite3(oldTargetDelta));

        // --- 新経路: BuildIntent → ApplyIntent で pivot が移動する ---
        World world;
        world.Initialize();

        WorldObject *pivot = world.SpawnObject<WorldObject>();
        assert(pivot);
        pivot->SetPosition(target);

        WorldObject *cameraObj = world.SpawnObject<WorldObject>();
        assert(cameraObj);

        SpringArmComponent *arm = world.CreateComponent<SpringArmComponent>(cameraObj);
        assert(arm);
        arm->SetPivot(pivot);
        arm->SetTargetOffset(Vector3::Zero);
        arm->SetArmLength(distance);
        arm->SetYaw(yaw);
        arm->SetPitch(pitch);

        // BuildIntent には現在のアーム長を渡す（distance と一致させ旧 panAmount と揃える）。
        MayaCameraController ctrlNew;
        ctrlNew.Initialize(target, distance, yaw, pitch);
        const SpringArmIntent intent = ctrlNew.BuildIntent(input, dt, distance);

        // MMB のみの入力では Pan だけが立ち、Orbit/Dolly は混ざらない。
        assert(IsNearlyEqual(intent.YawDelta, 0.0f));
        assert(IsNearlyEqual(intent.PitchDelta, 0.0f));
        assert(IsNearlyEqual(intent.DollyDelta, 0.0f));

        const Vector3 pivotBefore = pivot->GetPosition();
        arm->ApplyIntent(intent);
        const Vector3 pivotDelta = pivot->GetPosition() - pivotBefore;

        // 横・縦両方向で旧 Maya の焦点移動量と一致する。
        assert(VecNearlyEqual(pivotDelta, oldTargetDelta));

        world.Finalize();
    }

    void TestPanParity()
    {
        CheckPanParity(10.0f, 6.0f);  // 右下ドラッグ
        CheckPanParity(-8.0f, 4.0f);  // 左下ドラッグ（X 符号反転の検出に重要）
    }

    void TestGolden()
    {
        const Vector3 target(1.0f, 2.0f, 3.0f);
        const float distance = 5.0f;

        // 代表姿勢（特異点から離れている）
        CheckGolden(target, distance, 30.0f, 20.0f, /*bSingular*/ false);
        CheckGolden(target, distance, -45.0f, 10.0f, false);
        CheckGolden(target, distance, 120.0f, -30.0f, false);

        // 真上/真下付近（特異点）: forward 一致＋全成分 finite を確認
        CheckGolden(target, distance, 0.0f, 89.0f, /*bSingular*/ true);
        CheckGolden(target, distance, 60.0f, -89.0f, true);
    }
}

int main()
{
    std::cout << "SpringArmComponentTest start\n";

    TestDrive();
    TestFollow();
    TestLifetime();
    TestApplyIntent();
    TestPanParity();
    TestGolden();

    std::cout << "SpringArmComponentTest passed\n";
    return 0;
}
