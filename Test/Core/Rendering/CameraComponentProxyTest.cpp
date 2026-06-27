#include "Component/CameraComponent.h"
#include "Object/World.h"
#include "Object/WorldObject.h"
#include "Math/Quaternion.h"
#include "Math/QuaternionUtils.h"
#include "Math/Vector3.h"
#include "Rendering/SceneProxy.h"
#include "Rendering/RenderTypes.h"
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

    bool IsFinite3(float x, float y, float z)
    {
        return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
    }

    // 単位ベクトルかどうか
    bool IsUnitVector(float x, float y, float z, float tolerance = 1e-3f)
    {
        const float lenSq = x * x + y * y + z * z;
        return IsNearlyEqual(lenSq, 1.0f, tolerance);
    }

    float Dot3(float ax, float ay, float az, float bx, float by, float bz)
    {
        return ax * bx + ay * by + az * bz;
    }

    // ========================================
    // ケース1: 既定姿勢（単位回転）
    // ========================================
    void TestDefaultPose()
    {
        World world;
        world.Initialize();

        WorldObject *object = world.SpawnObject<WorldObject>();
        assert(object);

        CameraComponent *camera = world.CreateComponent<CameraComponent>(object);
        assert(camera);

        object->SetPosition(1.0f, 2.0f, 3.0f);
        object->SetRotation(NorvesLib::Math::Quaternion::Identity);

        CameraProxy proxy;
        assert(camera->BuildCameraProxy(proxy));

        // CameraId = ComponentId
        assert(proxy.CameraId == camera->GetComponentId());

        // 位置
        assert(IsNearlyEqual(proxy.PositionX, 1.0f));
        assert(IsNearlyEqual(proxy.PositionY, 2.0f));
        assert(IsNearlyEqual(proxy.PositionZ, 3.0f));

        // 単位回転では forward=+Z, up=+Y, right=+X（ローカル +Z が forward の規約）
        assert(IsNearlyEqual(proxy.ForwardX, 0.0f));
        assert(IsNearlyEqual(proxy.ForwardY, 0.0f));
        assert(IsNearlyEqual(proxy.ForwardZ, 1.0f));

        assert(IsNearlyEqual(proxy.UpX, 0.0f));
        assert(IsNearlyEqual(proxy.UpY, 1.0f));
        assert(IsNearlyEqual(proxy.UpZ, 0.0f));

        assert(IsNearlyEqual(proxy.RightX, 1.0f));
        assert(IsNearlyEqual(proxy.RightY, 0.0f));
        assert(IsNearlyEqual(proxy.RightZ, 0.0f));

        // owner がある場合に true を返すことの確認（2回呼んでも true）
        assert(camera->BuildCameraProxy(proxy));

        world.Finalize();
    }

    // ========================================
    // ケース2: round-trip 自己整合
    // ========================================
    void CheckRoundTrip(const NorvesLib::Math::Vector3 &forwardIn,
                        const NorvesLib::Math::Vector3 &upIn)
    {
        World world;
        world.Initialize();

        WorldObject *object = world.SpawnObject<WorldObject>();
        assert(object);

        CameraComponent *camera = world.CreateComponent<CameraComponent>(object);
        assert(camera);

        const NorvesLib::Math::Quaternion rotation =
            NorvesLib::Math::QuaternionUtils::LookRotation(forwardIn, upIn);
        object->SetRotation(rotation);

        CameraProxy proxy;
        assert(camera->BuildCameraProxy(proxy));

        // 特異点でも NaN/Inf が出ないこと
        assert(IsFinite3(proxy.ForwardX, proxy.ForwardY, proxy.ForwardZ));
        assert(IsFinite3(proxy.UpX, proxy.UpY, proxy.UpZ));
        assert(IsFinite3(proxy.RightX, proxy.RightY, proxy.RightZ));

        // forward は入力 forward（正規化後）に一致（LookRotation の規約: ローカル +Z = forward）
        const NorvesLib::Math::Vector3 expectedForward = forwardIn.Normalized();
        assert(IsNearlyEqual(proxy.ForwardX, expectedForward.x));
        assert(IsNearlyEqual(proxy.ForwardY, expectedForward.y));
        assert(IsNearlyEqual(proxy.ForwardZ, expectedForward.z));

        // forward/up/right は正規直交基底
        assert(IsUnitVector(proxy.ForwardX, proxy.ForwardY, proxy.ForwardZ));
        assert(IsUnitVector(proxy.UpX, proxy.UpY, proxy.UpZ));
        assert(IsUnitVector(proxy.RightX, proxy.RightY, proxy.RightZ));

        assert(IsNearlyEqual(
            Dot3(proxy.ForwardX, proxy.ForwardY, proxy.ForwardZ,
                 proxy.UpX, proxy.UpY, proxy.UpZ),
            0.0f, 1e-3f));
        assert(IsNearlyEqual(
            Dot3(proxy.ForwardX, proxy.ForwardY, proxy.ForwardZ,
                 proxy.RightX, proxy.RightY, proxy.RightZ),
            0.0f, 1e-3f));
        assert(IsNearlyEqual(
            Dot3(proxy.UpX, proxy.UpY, proxy.UpZ,
                 proxy.RightX, proxy.RightY, proxy.RightZ),
            0.0f, 1e-3f));

        world.Finalize();
    }

    void TestRoundTrip()
    {
        // 代表的な斜め方向
        CheckRoundTrip(NorvesLib::Math::Vector3(1.0f, 0.0f, -1.0f),
                       NorvesLib::Math::Vector3(0.0f, 1.0f, 0.0f));

        // 真上付近（特異点）: up を別軸にして NaN が出ないことを確認
        CheckRoundTrip(NorvesLib::Math::Vector3(0.0f, 1.0f, 0.0f),
                       NorvesLib::Math::Vector3(0.0f, 0.0f, 1.0f));
    }

    // ========================================
    // ケース3: レンズ値の反映
    // ========================================
    void TestLensValues()
    {
        World world;
        world.Initialize();

        WorldObject *object = world.SpawnObject<WorldObject>();
        assert(object);

        CameraComponent *camera = world.CreateComponent<CameraComponent>(object);
        assert(camera);

        camera->SetFieldOfView(45.0f);
        camera->SetNearPlane(0.5f);
        camera->SetFarPlane(500.0f);
        camera->SetProjectionType(ProjectionType::Orthographic);
        camera->SetOrthoSize(20.0f, 12.0f);
        camera->SetRenderOrder(3);
        camera->SetCullingMask(RenderLayer::Default);

        ViewportRect viewport;
        viewport.X = 10.0f;
        viewport.Y = 20.0f;
        viewport.Width = 640.0f;
        viewport.Height = 480.0f;
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        camera->SetViewport(viewport);

        CameraProxy proxy;
        assert(camera->BuildCameraProxy(proxy));

        assert(proxy.Projection == ProjectionType::Orthographic);
        assert(IsNearlyEqual(proxy.FieldOfView, 45.0f));
        assert(IsNearlyEqual(proxy.NearPlane, 0.5f));
        assert(IsNearlyEqual(proxy.FarPlane, 500.0f));
        assert(IsNearlyEqual(proxy.OrthoWidth, 20.0f));
        assert(IsNearlyEqual(proxy.OrthoHeight, 12.0f));
        assert(proxy.RenderOrder == 3);
        assert(proxy.CullingMask == RenderLayer::Default);

        assert(IsNearlyEqual(proxy.Viewport.X, 10.0f));
        assert(IsNearlyEqual(proxy.Viewport.Y, 20.0f));
        assert(IsNearlyEqual(proxy.Viewport.Width, 640.0f));
        assert(IsNearlyEqual(proxy.Viewport.Height, 480.0f));

        // getter/setter の往復確認
        camera->SetActiveCamera(true);
        assert(camera->IsActiveCamera());
        assert(IsNearlyEqual(camera->GetFieldOfView(), 45.0f));
        assert(camera->GetProjectionType() == ProjectionType::Orthographic);
        assert(camera->GetCullingMask() == RenderLayer::Default);
        assert(camera->GetRenderOrder() == 3);
        assert(IsNearlyEqual(camera->GetViewport().Width, 640.0f));

        world.Finalize();
    }

    // ========================================
    // ケース4: CameraComponent の既定値検証
    // ========================================
    void TestDefaultValues()
    {
        World world;
        world.Initialize();

        WorldObject *object = world.SpawnObject<WorldObject>();
        assert(object);

        CameraComponent *camera = world.CreateComponent<CameraComponent>(object);
        assert(camera);

        // setter を一切呼ばずに BuildCameraProxy する
        CameraProxy proxy;
        assert(camera->BuildCameraProxy(proxy));

        // 投影: Perspective（CameraComponent 既定）
        assert(proxy.Projection == ProjectionType::Perspective);

        // レンズ値（CameraComponent 既定）
        assert(IsNearlyEqual(proxy.FieldOfView, 60.0f));
        assert(IsNearlyEqual(proxy.NearPlane, 0.1f));
        assert(IsNearlyEqual(proxy.FarPlane, 1000.0f));
        assert(IsNearlyEqual(proxy.OrthoWidth, 10.0f));
        assert(IsNearlyEqual(proxy.OrthoHeight, 10.0f));

        // 描画設定（CameraComponent 既定）
        assert(proxy.RenderOrder == 0);
        assert(proxy.CullingMask == RenderLayer::All);

        // アクティブカメラ既定は false
        assert(camera->IsActiveCamera() == false);

        world.Finalize();
    }

    // ========================================
    // ケース4b: CameraComponent 既定 == CameraProxy 既定 の一致回帰
    // ========================================
    void TestDefaultsMatchProxyDefaults()
    {
        // 素の CameraProxy 既定値（何も触らない）。
        // BuildCameraProxy が書き込むレンズ系フィールドの既定の基準とする。
        CameraProxy defaultProxy;

        World world;
        world.Initialize();

        WorldObject *object = world.SpawnObject<WorldObject>();
        assert(object);

        CameraComponent *camera = world.CreateComponent<CameraComponent>(object);
        assert(camera);

        // setter を一切呼ばず（CameraComponent の素の既定値のまま）Build する。
        CameraProxy built;
        assert(camera->BuildCameraProxy(built));

        // CameraComponent の既定レンズ値が CameraProxy の既定値と一致すること
        // （どちらかが変わると画角が静かにズレるのを検出）。
        assert(built.Projection == defaultProxy.Projection);
        assert(IsNearlyEqual(built.FieldOfView, defaultProxy.FieldOfView));
        assert(IsNearlyEqual(built.NearPlane, defaultProxy.NearPlane));
        assert(IsNearlyEqual(built.FarPlane, defaultProxy.FarPlane));
        // Ortho も含めて比較する（Perspective 既定でも Ortho 既定がズレると
        // 投影切替時に静かに画角がズレるため）。
        assert(IsNearlyEqual(built.OrthoWidth, defaultProxy.OrthoWidth));
        assert(IsNearlyEqual(built.OrthoHeight, defaultProxy.OrthoHeight));

        world.Finalize();
    }

    // ========================================
    // ケース5: AspectRatio は BuildCameraProxy が触らない
    // ========================================
    void TestAspectRatioUntouched()
    {
        World world;
        world.Initialize();

        WorldObject *object = world.SpawnObject<WorldObject>();
        assert(object);

        CameraComponent *camera = world.CreateComponent<CameraComponent>(object);
        assert(camera);

        // CameraProxy の既定値（16/9）のまま Build → 触らないことを確認
        {
            CameraProxy proxy;
            assert(camera->BuildCameraProxy(proxy));
            assert(IsNearlyEqual(proxy.AspectRatio, 16.0f / 9.0f));
        }

        // 事前に AspectRatio を書き換えても Build 後に上書きされないことを確認
        {
            CameraProxy proxy;
            proxy.AspectRatio = 1.234f;
            assert(camera->BuildCameraProxy(proxy));
            assert(IsNearlyEqual(proxy.AspectRatio, 1.234f));
        }

        world.Finalize();
    }

    // ========================================
    // ケース6: BuildCameraProxy の冪等性
    // ========================================
    void TestBuildIdempotency()
    {
        World world;
        world.Initialize();

        WorldObject *object = world.SpawnObject<WorldObject>();
        assert(object);

        CameraComponent *camera = world.CreateComponent<CameraComponent>(object);
        assert(camera);

        object->SetPosition(5.0f, 3.0f, -2.0f);
        object->SetRotation(NorvesLib::Math::QuaternionUtils::LookRotation(
            NorvesLib::Math::Vector3(1.0f, -0.5f, 0.5f),
            NorvesLib::Math::Vector3(0.0f, 1.0f, 0.0f)));

        camera->SetFieldOfView(75.0f);
        camera->SetNearPlane(0.2f);
        camera->SetFarPlane(800.0f);
        camera->SetProjectionType(ProjectionType::Perspective);
        camera->SetRenderOrder(1);
        camera->SetCullingMask(RenderLayer::Default);

        ViewportRect vp;
        vp.X = 0.0f;
        vp.Y = 0.0f;
        vp.Width = 1920.0f;
        vp.Height = 1080.0f;
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        camera->SetViewport(vp);

        // 同一 camera で2回 Build し、結果が一致することを確認（冪等性）
        CameraProxy proxyA;
        CameraProxy proxyB;
        assert(camera->BuildCameraProxy(proxyA));
        assert(camera->BuildCameraProxy(proxyB));

        assert(IsNearlyEqual(proxyA.PositionX, proxyB.PositionX));
        assert(IsNearlyEqual(proxyA.PositionY, proxyB.PositionY));
        assert(IsNearlyEqual(proxyA.PositionZ, proxyB.PositionZ));

        assert(IsNearlyEqual(proxyA.ForwardX, proxyB.ForwardX));
        assert(IsNearlyEqual(proxyA.ForwardY, proxyB.ForwardY));
        assert(IsNearlyEqual(proxyA.ForwardZ, proxyB.ForwardZ));

        assert(IsNearlyEqual(proxyA.UpX, proxyB.UpX));
        assert(IsNearlyEqual(proxyA.UpY, proxyB.UpY));
        assert(IsNearlyEqual(proxyA.UpZ, proxyB.UpZ));

        assert(IsNearlyEqual(proxyA.RightX, proxyB.RightX));
        assert(IsNearlyEqual(proxyA.RightY, proxyB.RightY));
        assert(IsNearlyEqual(proxyA.RightZ, proxyB.RightZ));

        assert(IsNearlyEqual(proxyA.FieldOfView, proxyB.FieldOfView));
        assert(IsNearlyEqual(proxyA.NearPlane, proxyB.NearPlane));
        assert(IsNearlyEqual(proxyA.FarPlane, proxyB.FarPlane));
        assert(proxyA.Projection == proxyB.Projection);
        assert(proxyA.RenderOrder == proxyB.RenderOrder);
        assert(proxyA.CullingMask == proxyB.CullingMask);

        assert(IsNearlyEqual(proxyA.Viewport.X, proxyB.Viewport.X));
        assert(IsNearlyEqual(proxyA.Viewport.Y, proxyB.Viewport.Y));
        assert(IsNearlyEqual(proxyA.Viewport.Width, proxyB.Viewport.Width));
        assert(IsNearlyEqual(proxyA.Viewport.Height, proxyB.Viewport.Height));

        world.Finalize();
    }
}

int main()
{
    std::cout << "CameraComponentProxyTest start\n";

    TestDefaultPose();
    TestRoundTrip();
    TestLensValues();
    TestDefaultValues();
    TestDefaultsMatchProxyDefaults();
    TestAspectRatioUntouched();
    TestBuildIdempotency();

    std::cout << "CameraComponentProxyTest passed\n";
    return 0;
}
