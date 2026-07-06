#include "Component/SpringArmComponent.h"
#include "Component/SpringArmTypes.h"
#include "Component/CameraComponent.h"
#include "Input/MayaCameraController.h"
#include "Object/World.h"
#include "Object/Entity.h"
#include "Math/Vector3.h"
#include "Math/Quaternion.h"
#include "Math/QuaternionUtils.h"
#include "Math/VectorUtils.h"
#include "Rendering/SceneProxy.h"
#include "Logging/LogMacros.h"
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

        Entity *pivot = world.SpawnObject<Entity>();
        assert(pivot);
        pivot->SetPosition(2.0f, 1.0f, -3.0f);

        Entity *cameraObj = world.SpawnObject<Entity>();
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

        Entity *pivot = world.SpawnObject<Entity>();
        assert(pivot);
        pivot->SetPosition(0.0f, 0.0f, 0.0f);

        Entity *cameraObj = world.SpawnObject<Entity>();
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

        Entity *pivot = world.SpawnObject<Entity>();
        assert(pivot);
        pivot->SetPosition(1.0f, 2.0f, 3.0f);

        Entity *cameraObj = world.SpawnObject<Entity>();
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
    // ケース3b: 寿命（MarkForDestroy 直後、m_Inners に残ったまま
    //           IsPendingDestroy() で除外されることを確認）
    // ========================================
    void TestLifetimePendingDestroy()
    {
        World world;
        world.Initialize();

        Entity *pivot = world.SpawnObject<Entity>();
        assert(pivot);
        pivot->SetPosition(4.0f, 5.0f, 6.0f);

        Entity *cameraObj = world.SpawnObject<Entity>();
        assert(cameraObj);

        SpringArmComponent *arm = world.CreateComponent<SpringArmComponent>(cameraObj);
        assert(arm);
        arm->SetPivot(pivot);
        arm->SetArmLength(5.0f);

        arm->Tick(0.016f);
        assert(arm->HasValidPivot());

        // 即時 RemoveObject ではなく MarkForDestroy() で遅延破棄を予約する。
        // この時点で pivot はまだ World::m_Inners に残っているが、
        // World::FindEntityByObjectId は IsPendingDestroy() の Entity を除外するため、
        // ResolvePivot() は nullptr を返さなければならない（use-after-free 予防の
        // 早期検出。CleanupDestroyedObjects で実際に破棄される前でも安全側に倒す）。
        pivot->MarkForDestroy();
        assert(arm->HasValidPivot() == false);
        assert(arm->ResolvePivot() == nullptr);

        world.Finalize();
    }

    // ========================================
    // ケース4: ApplyIntent（Yaw/Pitch/Dolly/Pan）
    // ========================================
    void TestApplyIntent()
    {
        World world;
        world.Initialize();

        Entity *pivot = world.SpawnObject<Entity>();
        assert(pivot);
        pivot->SetPosition(0.0f, 0.0f, 0.0f);

        Entity *cameraObj = world.SpawnObject<Entity>();
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

        // Pan: ピボットの Position が移動する（焦点移動）。
        // ApplyIntent 内部の式（ComputeArmOffset → forward → LookRotation → right/up →
        // worldDelta = -right*PanDelta.x + up*PanDelta.y）をここで独立に再現し、
        // 実際のピボット移動量と数値的に一致することを確認する。「何か動いた」という
        // 緩い検証では Pan-X の符号反転回帰（旧 GetRight との符号差）を検出できないため。
        {
            const float yawNow = arm->GetYaw();
            const float pitchNow = arm->GetPitch();
            const float armLenNow = arm->GetArmLength();

            const float yawRad = yawNow * NorvesLib::Math::Constants::PI / 180.0f;
            const float pitchRad = pitchNow * NorvesLib::Math::Constants::PI / 180.0f;
            const Vector3 armOffset(
                armLenNow * std::cos(pitchRad) * std::sin(yawRad),
                armLenNow * std::sin(pitchRad),
                armLenNow * std::cos(pitchRad) * std::cos(yawRad));
            const Vector3 expectedForward = (armOffset * -1.0f).Normalized();
            const Quaternion expectedRotation =
                QuaternionUtils::LookRotation(expectedForward, Vector3::Up);
            const Vector3 expectedRight = expectedRotation * Vector3::Right;
            const Vector3 expectedUp = expectedRotation * Vector3::Up;

            const Vector3 panDelta(1.5f, -0.5f, 0.0f);
            const Vector3 expectedWorldDelta =
                (expectedRight * -1.0f) * panDelta.x + expectedUp * panDelta.y;

            const Vector3 pivotBefore = pivot->GetPosition();
            SpringArmIntent intent;
            intent.PanDelta = panDelta;
            arm->ApplyIntent(intent);
            const Vector3 pivotAfter = pivot->GetPosition();
            const Vector3 actualWorldDelta = pivotAfter - pivotBefore;

            assert(IsFinite3(actualWorldDelta));
            // 非ゼロであること（basis 投影で退化していないこと）
            assert(!VecNearlyEqual(actualWorldDelta, Vector3::Zero, 1e-5f));
            // ApplyIntent 内部式と数値的に厳密一致（符号・投影の回帰検出）
            assert(VecNearlyEqual(actualWorldDelta, expectedWorldDelta, 1e-3f));
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

        Entity *pivot = world.SpawnObject<Entity>();
        assert(pivot);
        pivot->SetPosition(target);

        Entity *cameraObj = world.SpawnObject<Entity>();
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

    // ========================================
    // ケース6: Pan 方向の旧 Maya 一致（符号パリティ）
    // ========================================
    // 同一入力（MMB ドラッグ）に対し、
    //   旧: MayaCameraController のイベント駆動（OnMouseButton→OnMouseMove）後の
    //       GetTarget() の移動量
    //   新: BuildIntent（poll 版） → SpringArmComponent::ApplyIntent 後の
    //       pivot 移動量
    // が一致することを確認する。develop の MayaCameraController はイベント駆動
    // （OnMouseButton/OnMouseMove）と poll 版 BuildIntent の両方を持つが、
    // 単発ドラッグ（同一フレーム内1イベント）については同一の感度換算式を
    // 使うため、同一入力に対する移動量は一致するはずである。これは Pan-X の
    // 符号反転バグ（LookRotation 由来 right が旧 GetRight() と逆符号）に対する
    // 回帰テスト。
    void CheckPanParity(float dx, float dy)
    {
        const Vector3 target(2.0f, 1.0f, -3.0f);
        const float distance = 6.0f;
        const float yaw = 35.0f;
        const float pitch = 25.0f; // 特異点から離れた姿勢
        const float dt = 0.016f;

        // --- 旧経路: イベント駆動（OnMouseButton→OnMouseMove）で m_Target が移動する ---
        MayaCameraController ctrlOld;
        ctrlOld.Initialize(target, distance, yaw, pitch);
        {
            MouseButtonEvent buttonEvent;
            buttonEvent.Button = MouseButton::Middle;
            buttonEvent.Action = InputAction::Pressed;
            ctrlOld.OnMouseButton(buttonEvent);

            MouseMoveEvent moveEvent;
            moveEvent.DeltaX = dx;
            moveEvent.DeltaY = dy;
            ctrlOld.OnMouseMove(moveEvent);
        }
        const Vector3 oldTargetDelta = ctrlOld.GetTarget() - target;

        // 横・縦ともに非ゼロな delta であること（方向検証として意味を持たせる）
        assert(std::abs(oldTargetDelta.x) > 1e-5f || std::abs(oldTargetDelta.z) > 1e-5f);
        assert(IsFinite3(oldTargetDelta));

        // --- 新経路: BuildIntent（poll 版） → ApplyIntent で pivot が移動する ---
        World world;
        world.Initialize();

        Entity *pivot = world.SpawnObject<Entity>();
        assert(pivot);
        pivot->SetPosition(target);

        Entity *cameraObj = world.SpawnObject<Entity>();
        assert(cameraObj);

        SpringArmComponent *arm = world.CreateComponent<SpringArmComponent>(cameraObj);
        assert(arm);
        arm->SetPivot(pivot);
        arm->SetTargetOffset(Vector3::Zero);
        arm->SetArmLength(distance);
        arm->SetYaw(yaw);
        arm->SetPitch(pitch);

        // InputState を MMB 押下 + delta(dx,dy) で合成する（公開 API のみ使用）。
        //  - SetMouseButtonState(MouseButton::Middle, true) で MMB を押下状態に。
        //  - SetMousePosition(0,0) で初回更新（delta=0、prev=0,0 に確定）。
        //  - SetMousePosition(dx,dy) で delta=(dx,dy) を生成（prev は 0,0）。
        InputState input;
        input.SetMouseButtonState(MouseButton::Middle, true);
        input.SetMousePosition(0.0f, 0.0f);
        input.SetMousePosition(dx, dy);

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

    // ========================================
    // ケース6b: Orbit / Dolly（ドラッグ・スクロール）の符号パリティ、
    //           および無入力時に bHasInput が立たないことの確認
    // ========================================
    // TestPanParity（MMB=Pan）と同じ手法で、LMB=Orbit・RMB=Dolly・スクロール=Dolly
    // についてもイベント駆動側と poll版 BuildIntent 側の一致を確認する。
    void CheckOrbitParity(float dx, float dy)
    {
        const Vector3 target(1.0f, -2.0f, 3.0f);
        const float distance = 4.0f;
        const float yaw = 20.0f;
        const float pitch = -10.0f;
        const float dt = 0.016f;

        // --- 旧経路: OnMouseButton(Left)→OnMouseMove ---
        MayaCameraController ctrlOld;
        ctrlOld.Initialize(target, distance, yaw, pitch);
        {
            MouseButtonEvent buttonEvent;
            buttonEvent.Button = MouseButton::Left;
            buttonEvent.Action = InputAction::Pressed;
            ctrlOld.OnMouseButton(buttonEvent);

            MouseMoveEvent moveEvent;
            moveEvent.DeltaX = dx;
            moveEvent.DeltaY = dy;
            ctrlOld.OnMouseMove(moveEvent);
        }
        const float oldYawDelta = ctrlOld.GetYaw() - yaw;
        const float oldPitchDelta = ctrlOld.GetPitch() - pitch;
        assert(std::abs(oldYawDelta) > 1e-5f || std::abs(oldPitchDelta) > 1e-5f);

        // --- 新経路: BuildIntent（poll 版） ---
        InputState input;
        input.SetMouseButtonState(MouseButton::Left, true);
        input.SetMousePosition(0.0f, 0.0f);
        input.SetMousePosition(dx, dy);

        MayaCameraController ctrlNew;
        ctrlNew.Initialize(target, distance, yaw, pitch);
        const SpringArmIntent intent = ctrlNew.BuildIntent(input, dt, distance);

        assert(intent.bHasInput);
        assert(IsNearlyEqual(intent.DollyDelta, 0.0f));
        assert(IsNearlyEqual(intent.PanDelta.x, 0.0f));
        assert(IsNearlyEqual(intent.PanDelta.y, 0.0f));
        assert(IsNearlyEqual(intent.YawDelta, oldYawDelta));
        assert(IsNearlyEqual(intent.PitchDelta, oldPitchDelta));
    }

    void CheckRmbDollyParity(float dragDx)
    {
        const Vector3 target(0.0f, 0.0f, 0.0f);
        const float distance = 8.0f;
        const float yaw = 0.0f;
        const float pitch = 30.0f;
        const float dt = 0.016f;

        // RMB ドラッグによる Dolly。
        MayaCameraController ctrlOld;
        ctrlOld.Initialize(target, distance, yaw, pitch);
        {
            MouseButtonEvent buttonEvent;
            buttonEvent.Button = MouseButton::Right;
            buttonEvent.Action = InputAction::Pressed;
            ctrlOld.OnMouseButton(buttonEvent);

            MouseMoveEvent moveEvent;
            moveEvent.DeltaX = dragDx;
            moveEvent.DeltaY = 0.0f;
            ctrlOld.OnMouseMove(moveEvent);
        }
        const float oldDistanceDelta = ctrlOld.GetDistance() - distance;
        assert(std::abs(oldDistanceDelta) > 1e-5f);

        InputState input;
        input.SetMouseButtonState(MouseButton::Right, true);
        input.SetMousePosition(0.0f, 0.0f);
        input.SetMousePosition(dragDx, 0.0f);

        MayaCameraController ctrlNew;
        ctrlNew.Initialize(target, distance, yaw, pitch);
        const SpringArmIntent intent = ctrlNew.BuildIntent(input, dt, distance);

        assert(intent.bHasInput);
        assert(IsNearlyEqual(intent.YawDelta, 0.0f));
        assert(IsNearlyEqual(intent.PitchDelta, 0.0f));
        // ApplyIntent は ArmLength -= DollyDelta。旧経路は m_Distance += oldDistanceDelta。
        // 両者が同じ ArmLength/Distance 変化になるには DollyDelta == -oldDistanceDelta。
        assert(IsNearlyEqual(intent.DollyDelta, -oldDistanceDelta));
    }

    void CheckScrollDollyParity(float scrollDelta)
    {
        const Vector3 target(0.0f, 0.0f, 0.0f);
        const float distance = 8.0f;
        const float yaw = 0.0f;
        const float pitch = 30.0f;
        const float dt = 0.016f;

        // スクロールによる Dolly。
        MayaCameraController ctrlOld;
        ctrlOld.Initialize(target, distance, yaw, pitch);
        {
            MouseScrollEvent scrollEvent;
            scrollEvent.Delta = scrollDelta;
            ctrlOld.OnMouseScroll(scrollEvent);
        }
        const float oldDistanceDelta = ctrlOld.GetDistance() - distance;
        assert(std::abs(oldDistanceDelta) > 1e-5f);

        InputState input;
        input.AddMouseScroll(scrollDelta);

        MayaCameraController ctrlNew;
        ctrlNew.Initialize(target, distance, yaw, pitch);
        const SpringArmIntent intent = ctrlNew.BuildIntent(input, dt, distance);

        assert(intent.bHasInput);
        assert(IsNearlyEqual(intent.YawDelta, 0.0f));
        assert(IsNearlyEqual(intent.PitchDelta, 0.0f));
        assert(IsNearlyEqual(intent.DollyDelta, -oldDistanceDelta));
    }

    void TestOrbitAndDollyParity()
    {
        CheckOrbitParity(12.0f, -5.0f);
        CheckOrbitParity(-7.0f, 9.0f);
        CheckRmbDollyParity(6.0f);
        CheckRmbDollyParity(-4.0f);
        CheckScrollDollyParity(-3.0f);
        CheckScrollDollyParity(2.0f);
    }

    // ========================================
    // ケース6c: 無入力時は bHasInput が立たず全 Delta が 0 のままであること
    // ========================================
    void TestBuildIntentNoInput()
    {
        const Vector3 target(2.0f, 2.0f, 2.0f);
        const float distance = 5.0f;

        MayaCameraController ctrl;
        ctrl.Initialize(target, distance, 0.0f, 30.0f);

        // ボタン非押下・移動なし・スクロールなしの InputState。
        InputState input;

        const SpringArmIntent intent = ctrl.BuildIntent(input, 0.016f, distance);

        assert(intent.bHasInput == false);
        assert(IsNearlyEqual(intent.YawDelta, 0.0f));
        assert(IsNearlyEqual(intent.PitchDelta, 0.0f));
        assert(IsNearlyEqual(intent.DollyDelta, 0.0f));
        assert(IsNearlyEqual(intent.PanDelta.x, 0.0f));
        assert(IsNearlyEqual(intent.PanDelta.y, 0.0f));
        assert(IsNearlyEqual(intent.PanDelta.z, 0.0f));
    }

    // ========================================
    // ケース7: ピボットが child Entity の場合は警告を出しつつ処理を継続する
    // ========================================
    void TestChildPivotWarns()
    {
        World world;
        world.Initialize();

        Entity *parent = world.SpawnObject<Entity>();
        assert(parent);
        parent->SetPosition(0.0f, 0.0f, 0.0f);

        Entity *childPivot = world.SpawnEntity<Entity>(parent);
        assert(childPivot);

        Entity *cameraObj = world.SpawnObject<Entity>();
        assert(cameraObj);

        SpringArmComponent *arm = world.CreateComponent<SpringArmComponent>(cameraObj);
        assert(arm);

        // child Entity をピボットに設定すると警告ログが出るが、処理は継続する
        // （SetPivot 自体はクラッシュしない）。PivotObjectId は設定される。
        arm->SetPivot(childPivot);
        assert(arm->GetPivotObjectId() == childPivot->GetObjectId());

        // World::FindEntityByObjectId は root Entity のみを走査するため、
        // child Entity をピボットに設定した場合は解決不能（HasValidPivot()==false）
        // となる。これは「ピボットは root Entity 限定」という契約どおりの挙動であり、
        // 解決不能時と同じ安全な扱い（owner Transform を維持し use-after-free しない）
        // になることを確認する。
        assert(arm->HasValidPivot() == false);
        assert(arm->ResolvePivot() == nullptr);

        // クラッシュせず Tick できることを確認（owner Transform は変更されず維持される）。
        const Vector3 ownerPosBefore = cameraObj->GetPosition();
        arm->Tick(0.016f);
        assert(VecNearlyEqual(cameraObj->GetPosition(), ownerPosBefore));

        world.Finalize();
    }
}

int main()
{
    std::cout << "SpringArmComponentTest start\n";

    TestDrive();
    TestFollow();
    TestLifetime();
    TestLifetimePendingDestroy();
    TestApplyIntent();
    TestGolden();
    TestPanParity();
    TestOrbitAndDollyParity();
    TestBuildIntentNoInput();
    TestChildPivotWarns();

    std::cout << "SpringArmComponentTest passed\n";
    return 0;
}
