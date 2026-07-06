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

    // 注: Pan 方向の旧 Maya 一致（旧 rubin ケース6, MayaCameraController::BuildIntent
    // 依存）は F3（develop の MayaCameraController に BuildIntent を追加するフェーズ）
    // 完了後に追加する。develop の MayaCameraController はまだイベント駆動のみで
    // BuildIntent を持たないため、本フェーズ（F2）では移植しない。

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
    TestChildPivotWarns();

    std::cout << "SpringArmComponentTest passed\n";
    return 0;
}
