#include "Component/CameraComponent.h"
#include "Math/QuaternionUtils.h"
#include "Math/Vector3.h"
#include "Object/Entity.h"
#include "Object/World.h"
#include "Rendering/FramePacket.h"
#include "Rendering/SceneProxy.h"
#include "Rendering/ViewRenderPlan.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Component;
using namespace NorvesLib::Core::Rendering;

namespace
{
    bool IsNearlyEqual(float lhs, float rhs, float tolerance = 1.0e-4f)
    {
        return std::fabs(lhs - rhs) <= tolerance;
    }

    void ExpectVector(
        float actualX,
        float actualY,
        float actualZ,
        const NorvesLib::Math::Vector3& expected)
    {
        assert(IsNearlyEqual(actualX, expected.x));
        assert(IsNearlyEqual(actualY, expected.y));
        assert(IsNearlyEqual(actualZ, expected.z));
    }

    void TestDetachedComponentDoesNotProduceProxy()
    {
        CameraComponent camera;
        CameraProxy proxy;
        proxy.CameraId = 91;
        proxy.PositionX = 17.0f;

        assert(!camera.BuildCameraProxy(proxy));
        assert(proxy.CameraId == 91);
        assert(IsNearlyEqual(proxy.PositionX, 17.0f));
    }

    void TestOwnerWorldTransformBecomesValueSnapshot()
    {
        World world;
        world.Initialize();

        Entity* parent = world.SpawnEntity<Entity>();
        Entity* owner = world.SpawnEntity<Entity>(parent);
        assert(parent != nullptr);
        assert(owner != nullptr);

        CameraComponent* camera = world.CreateComponent<CameraComponent>(owner);
        assert(camera != nullptr);
        assert(camera->GetOwner() == owner);
        assert(camera->GetOuter() == owner);
        assert(owner->GetComponent<CameraComponent>() == camera);

        parent->SetLocalPosition(10.0f, 2.0f, -3.0f);
        parent->SetLocalRotation(NorvesLib::Math::QuaternionUtils::LookRotation(
            NorvesLib::Math::Vector3::Right,
            NorvesLib::Math::Vector3::Up));
        owner->SetLocalPosition(0.0f, 0.0f, 2.0f);
        world.UpdateWorldTransforms();

        const NorvesLib::Math::Transform ownerWorld = owner->GetWorldTransform();
        const NorvesLib::Math::Vector3 expectedForward =
            ownerWorld.rotation * NorvesLib::Math::Vector3::Forward;
        const NorvesLib::Math::Vector3 expectedUp =
            ownerWorld.rotation * NorvesLib::Math::Vector3::Up;
        const NorvesLib::Math::Vector3 expectedRight =
            ownerWorld.rotation * NorvesLib::Math::Vector3::Right;

        CameraProxy proxy;
        assert(camera->BuildCameraProxy(proxy));
        assert(proxy.CameraId == camera->GetComponentId());
        ExpectVector(proxy.PositionX, proxy.PositionY, proxy.PositionZ, ownerWorld.position);
        ExpectVector(proxy.ForwardX, proxy.ForwardY, proxy.ForwardZ, expectedForward);
        ExpectVector(proxy.UpX, proxy.UpY, proxy.UpZ, expectedUp);
        ExpectVector(proxy.RightX, proxy.RightY, proxy.RightZ, expectedRight);

        owner->SetLocalPosition(4.0f, 5.0f, 6.0f);
        world.UpdateWorldTransforms();
        ExpectVector(proxy.PositionX, proxy.PositionY, proxy.PositionZ, ownerWorld.position);

        world.Finalize();
    }

    void TestLensValuesAndDirtyState()
    {
        World world;
        world.Initialize();

        Entity* owner = world.SpawnEntity<Entity>();
        CameraComponent* camera = world.CreateComponent<CameraComponent>(owner);
        assert(camera != nullptr);

        CameraProxy defaultProxy;
        CameraProxy builtDefaults;
        assert(camera->BuildCameraProxy(builtDefaults));
        assert(builtDefaults.Projection == defaultProxy.Projection);
        assert(IsNearlyEqual(builtDefaults.FieldOfView, defaultProxy.FieldOfView));
        assert(IsNearlyEqual(builtDefaults.NearPlane, defaultProxy.NearPlane));
        assert(IsNearlyEqual(builtDefaults.FarPlane, defaultProxy.FarPlane));
        assert(IsNearlyEqual(builtDefaults.OrthoWidth, defaultProxy.OrthoWidth));
        assert(IsNearlyEqual(builtDefaults.OrthoHeight, defaultProxy.OrthoHeight));
        assert(builtDefaults.CullingMask == defaultProxy.CullingMask);
        assert(builtDefaults.RenderOrder == defaultProxy.RenderOrder);
        assert(!camera->IsActiveCamera());

        camera->ClearRenderStateDirty();
        camera->SetProjectionType(ProjectionType::Orthographic);
        assert(camera->IsRenderStateDirty());
        camera->SetFieldOfView(47.0f);
        camera->SetNearPlane(0.25f);
        camera->SetFarPlane(750.0f);
        camera->SetOrthoSize(24.0f, 14.0f);
        camera->SetCullingMask(RenderLayer::Default);
        camera->SetRenderOrder(4);
        camera->SetActiveCamera(true);

        ViewportRect viewport;
        viewport.X = 11.0f;
        viewport.Y = 12.0f;
        viewport.Width = 1280.0f;
        viewport.Height = 720.0f;
        viewport.MinDepth = 0.1f;
        viewport.MaxDepth = 0.9f;
        camera->SetViewport(viewport);

        CameraProxy proxy;
        proxy.AspectRatio = 1.25f;
        assert(camera->BuildCameraProxy(proxy));
        assert(proxy.Projection == ProjectionType::Orthographic);
        assert(IsNearlyEqual(proxy.FieldOfView, 47.0f));
        assert(IsNearlyEqual(proxy.NearPlane, 0.25f));
        assert(IsNearlyEqual(proxy.FarPlane, 750.0f));
        assert(IsNearlyEqual(proxy.OrthoWidth, 24.0f));
        assert(IsNearlyEqual(proxy.OrthoHeight, 14.0f));
        assert(IsNearlyEqual(proxy.AspectRatio, 1.25f));
        assert(proxy.CullingMask == RenderLayer::Default);
        assert(proxy.RenderOrder == 4);
        assert(IsNearlyEqual(proxy.Viewport.X, 11.0f));
        assert(IsNearlyEqual(proxy.Viewport.Y, 12.0f));
        assert(IsNearlyEqual(proxy.Viewport.Width, 1280.0f));
        assert(IsNearlyEqual(proxy.Viewport.Height, 720.0f));
        assert(IsNearlyEqual(proxy.Viewport.MinDepth, 0.1f));
        assert(IsNearlyEqual(proxy.Viewport.MaxDepth, 0.9f));
        assert(camera->IsActiveCamera());

        camera->Disable();
        CameraProxy disabledProxy;
        assert(camera->BuildCameraProxy(disabledProxy));

        world.Finalize();
    }

    void TestPhysicalExposureSnapshotContract()
    {
        World world;
        world.Initialize();

        Entity* owner = world.SpawnEntity<Entity>();
        CameraComponent* camera = world.CreateComponent<CameraComponent>(owner);
        assert(camera != nullptr);
        assert(IsNearlyEqual(camera->GetAperture(), 4.0f));
        assert(IsNearlyEqual(camera->GetShutterSpeed(), 1.0f / 60.0f));
        assert(IsNearlyEqual(camera->GetISO(), 100.0f));
        assert(IsNearlyEqual(camera->GetExposureCompensation(), 0.0f));

        const float oldAperture = camera->GetAperture();
        const float oldShutterSpeed = camera->GetShutterSpeed();
        const float oldISO = camera->GetISO();
        const float oldExposureCompensation = camera->GetExposureCompensation();
        assert(!camera->SetAperture(0.0f));
        assert(!camera->SetShutterSpeed(-1.0f));
        assert(!camera->SetISO(std::numeric_limits<float>::infinity()));
        assert(!camera->SetExposureCompensation(std::numeric_limits<float>::quiet_NaN()));
        assert(IsNearlyEqual(camera->GetAperture(), oldAperture));
        assert(IsNearlyEqual(camera->GetShutterSpeed(), oldShutterSpeed));
        assert(IsNearlyEqual(camera->GetISO(), oldISO));
        assert(IsNearlyEqual(camera->GetExposureCompensation(), oldExposureCompensation));

        enum class ExposureParameter
        {
            Aperture,
            ShutterSpeed,
            ISO,
            Compensation
        };
        struct FiniteOverflowCase
        {
            ExposureParameter Parameter;
            float Candidate;
        };
        const FiniteOverflowCase overflowCases[] = {
            {ExposureParameter::Aperture, std::numeric_limits<float>::max()},
            {ExposureParameter::ShutterSpeed, std::numeric_limits<float>::denorm_min()},
            {ExposureParameter::ISO, std::numeric_limits<float>::denorm_min()},
            {ExposureParameter::Compensation, std::numeric_limits<float>::max()},
        };
        for (const FiniteOverflowCase& overflowCase : overflowCases)
        {
            bool accepted = false;
            switch (overflowCase.Parameter)
            {
            case ExposureParameter::Aperture:
                accepted = camera->SetAperture(overflowCase.Candidate);
                break;
            case ExposureParameter::ShutterSpeed:
                accepted = camera->SetShutterSpeed(overflowCase.Candidate);
                break;
            case ExposureParameter::ISO:
                accepted = camera->SetISO(overflowCase.Candidate);
                break;
            case ExposureParameter::Compensation:
                accepted = camera->SetExposureCompensation(overflowCase.Candidate);
                break;
            }
            assert(!accepted);
            assert(IsNearlyEqual(camera->GetAperture(), oldAperture));
            assert(IsNearlyEqual(camera->GetShutterSpeed(), oldShutterSpeed));
            assert(IsNearlyEqual(camera->GetISO(), oldISO));
            assert(IsNearlyEqual(camera->GetExposureCompensation(), oldExposureCompensation));
        }
        camera->ClearRenderStateDirty();
        assert(camera->SetAperture(8.0f));
        assert(camera->IsRenderStateDirty());

        camera->SetAperture(4.0f);
        camera->SetShutterSpeed(1.0f / 60.0f);
        camera->SetISO(100.0f);
        camera->SetExposureCompensation(0.0f);

        CameraProxy proxy;
        assert(camera->BuildCameraProxy(proxy));
        assert(IsNearlyEqual(proxy.Aperture, 4.0f));
        assert(IsNearlyEqual(proxy.ShutterSpeed, 1.0f / 60.0f));
        assert(IsNearlyEqual(proxy.ISO, 100.0f));
        assert(IsNearlyEqual(proxy.ExposureCompensation, 0.0f));
        assert(IsNearlyEqual(proxy.EV100, 9.9068906f, 1.0e-5f));
        assert(IsNearlyEqual(proxy.Exposure, 1.0f / 1152.0f, 1.0e-7f));
        assert(IsNearlyEqual(proxy.PreExposure, 1.0f / 1152.0f, 1.0e-7f));
        assert(IsNearlyEqual(proxy.InvPreExposure, 1152.0f, 1.0e-3f));

        ViewRenderPlan view;
        ViewportRenderPlan viewport;
        viewport.Camera = proxy;
        viewport.bHasCamera = true;
        view.Viewports.push_back(viewport);

        FramePacket packet;
        packet.Views.push_back(view);
        assert(IsNearlyEqual(packet.Views[0].Viewports[0].Camera.EV100, proxy.EV100, 1.0e-5f));
        assert(IsNearlyEqual(packet.Views[0].Viewports[0].Camera.Exposure, proxy.Exposure, 1.0e-7f));
        assert(IsNearlyEqual(packet.Views[0].Viewports[0].Camera.InvPreExposure, proxy.InvPreExposure, 1.0e-3f));

        world.Finalize();
    }
}

int main()
{
    std::cout << "CameraComponentProxyTest start\n";
    TestDetachedComponentDoesNotProduceProxy();
    TestOwnerWorldTransformBecomesValueSnapshot();
    TestLensValuesAndDirtyState();
    TestPhysicalExposureSnapshotContract();
    std::cout << "CameraComponentProxyTest passed\n";
    return 0;
}
