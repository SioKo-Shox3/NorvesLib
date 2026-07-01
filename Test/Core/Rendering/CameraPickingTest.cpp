#include "Rendering/CameraPicking.h"
#include "Math/GeometryIntersection.h"
#include <cassert>
#include <cmath>

namespace
{
    constexpr float EPSILON = 0.0001f;
    constexpr float DIAGONAL = 0.70710678f;

    NorvesLib::Core::Rendering::ViewportRect MakeViewport(float x, float y, float width, float height)
    {
        NorvesLib::Core::Rendering::ViewportRect viewport;
        viewport.X = x;
        viewport.Y = y;
        viewport.Width = width;
        viewport.Height = height;
        return viewport;
    }

    NorvesLib::Core::Rendering::CameraProxy MakeCamera(
        NorvesLib::Core::Rendering::ProjectionType projection,
        const NorvesLib::Core::Rendering::ViewportRect& viewport)
    {
        NorvesLib::Core::Rendering::CameraProxy camera;
        camera.PositionX = 0.0f;
        camera.PositionY = 0.0f;
        camera.PositionZ = 0.0f;
        camera.ForwardX = 0.0f;
        camera.ForwardY = 0.0f;
        camera.ForwardZ = -1.0f;
        camera.UpX = 0.0f;
        camera.UpY = 1.0f;
        camera.UpZ = 0.0f;
        camera.RightX = 1.0f;
        camera.RightY = 0.0f;
        camera.RightZ = 0.0f;
        camera.Projection = projection;
        camera.FieldOfView = 90.0f;
        camera.AspectRatio = 1.0f;
        camera.NearPlane = 0.1f;
        camera.FarPlane = 1000.0f;
        camera.OrthoWidth = 20.0f;
        camera.OrthoHeight = 20.0f;
        camera.Viewport = viewport;
        return camera;
    }

    bool IsNearlyEqual(float lhs, float rhs, float tolerance = EPSILON)
    {
        float difference = lhs - rhs;
        if (difference < 0.0f)
        {
            difference = -difference;
        }
        return difference <= tolerance;
    }

    void AssertVectorNear(const NorvesLib::Math::Vector3& actual, float x, float y, float z)
    {
        assert(IsNearlyEqual(actual.x, x));
        assert(IsNearlyEqual(actual.y, y));
        assert(IsNearlyEqual(actual.z, z));
    }

    void AssertSphereUnchanged(
        const NorvesLib::Math::Sphere& actual,
        const NorvesLib::Math::Sphere& expected)
    {
        AssertVectorNear(actual.Center, expected.Center.x, expected.Center.y, expected.Center.z);
        assert(IsNearlyEqual(actual.Radius, expected.Radius));
    }

    void AssertSelectionSphereFailureDoesNotMutate(
        const NorvesLib::Core::Rendering::CameraProxy& camera,
        float centerX,
        float centerY,
        float edgeX,
        float edgeY,
        float centerAlongRayDistance)
    {
        const NorvesLib::Math::Sphere sentinel(NorvesLib::Math::Vector3(12.0f, -3.0f, 8.0f), 2.5f);
        NorvesLib::Math::Sphere sphere = sentinel;

        const bool bBuilt = NorvesLib::Core::Rendering::BuildSelectionSphere(
            camera,
            centerX,
            centerY,
            edgeX,
            edgeY,
            centerAlongRayDistance,
            sphere);
        assert(!bBuilt);
        AssertSphereUnchanged(sphere, sentinel);
    }

    void AssertRayOrigin(const NorvesLib::Math::Ray& ray, float x, float y, float z)
    {
        AssertVectorNear(ray.Origin, x, y, z);
    }

    void AssertRayDirection(const NorvesLib::Math::Ray& ray, float x, float y, float z)
    {
        AssertVectorNear(ray.Direction, x, y, z);
    }

    bool FrustumIntersectsBox(
        const NorvesLib::Math::Frustum& frustum,
        float x,
        float y,
        float z,
        float halfExtent = 0.25f)
    {
        return NorvesLib::Math::FrustumIntersectsAABB(
            frustum,
            NorvesLib::Math::AABB::FromCenterExtents(
                NorvesLib::Math::Vector3(x, y, z),
                NorvesLib::Math::Vector3(halfExtent, halfExtent, halfExtent)));
    }
}

int main()
{
    {
        const NorvesLib::Core::Rendering::CameraProxy camera = MakeCamera(
            NorvesLib::Core::Rendering::ProjectionType::Perspective,
            MakeViewport(0.0f, 0.0f, 100.0f, 100.0f));
        NorvesLib::Math::Ray ray;

        bool bBuilt = NorvesLib::Core::Rendering::BuildPickingRay(camera, 50.0f, 50.0f, ray);
        assert(bBuilt);
        AssertRayOrigin(ray, 0.0f, 0.0f, 0.0f);
        AssertRayDirection(ray, 0.0f, 0.0f, -1.0f);

        bBuilt = NorvesLib::Core::Rendering::BuildPickingRay(camera, 100.0f, 50.0f, ray);
        assert(bBuilt);
        AssertRayOrigin(ray, 0.0f, 0.0f, 0.0f);
        AssertRayDirection(ray, DIAGONAL, 0.0f, -DIAGONAL);

        bBuilt = NorvesLib::Core::Rendering::BuildPickingRay(camera, 0.0f, 50.0f, ray);
        assert(bBuilt);
        AssertRayOrigin(ray, 0.0f, 0.0f, 0.0f);
        AssertRayDirection(ray, -DIAGONAL, 0.0f, -DIAGONAL);

        bBuilt = NorvesLib::Core::Rendering::BuildPickingRay(camera, 50.0f, 0.0f, ray);
        assert(bBuilt);
        AssertRayOrigin(ray, 0.0f, 0.0f, 0.0f);
        AssertRayDirection(ray, 0.0f, DIAGONAL, -DIAGONAL);

        bBuilt = NorvesLib::Core::Rendering::BuildPickingRay(camera, 50.0f, 100.0f, ray);
        assert(bBuilt);
        AssertRayOrigin(ray, 0.0f, 0.0f, 0.0f);
        AssertRayDirection(ray, 0.0f, -DIAGONAL, -DIAGONAL);
    }

    {
        const NorvesLib::Core::Rendering::CameraProxy camera = MakeCamera(
            NorvesLib::Core::Rendering::ProjectionType::Perspective,
            MakeViewport(10.0f, 20.0f, 100.0f, 100.0f));
        NorvesLib::Math::Ray ray;

        const bool bBuilt = NorvesLib::Core::Rendering::BuildPickingRay(camera, 60.0f, 70.0f, ray);
        assert(bBuilt);
        AssertRayOrigin(ray, 0.0f, 0.0f, 0.0f);
        AssertRayDirection(ray, 0.0f, 0.0f, -1.0f);
    }

    {
        const NorvesLib::Core::Rendering::CameraProxy invalidCamera = MakeCamera(
            NorvesLib::Core::Rendering::ProjectionType::Perspective,
            MakeViewport(0.0f, 0.0f, 0.0f, 100.0f));
        NorvesLib::Math::Ray ray;
        assert(!NorvesLib::Core::Rendering::BuildPickingRay(invalidCamera, 0.0f, 50.0f, ray));

        const NorvesLib::Core::Rendering::CameraProxy camera = MakeCamera(
            NorvesLib::Core::Rendering::ProjectionType::Perspective,
            MakeViewport(0.0f, 0.0f, 100.0f, 100.0f));
        assert(!NorvesLib::Core::Rendering::BuildPickingRay(camera, -5.0f, 50.0f, ray));
        assert(!NorvesLib::Core::Rendering::BuildPickingRay(camera, 50.0f, 150.0f, ray));
    }

    {
        const NorvesLib::Core::Rendering::CameraProxy camera = MakeCamera(
            NorvesLib::Core::Rendering::ProjectionType::Orthographic,
            MakeViewport(0.0f, 0.0f, 100.0f, 100.0f));
        NorvesLib::Math::Ray ray;

        bool bBuilt = NorvesLib::Core::Rendering::BuildPickingRay(camera, 50.0f, 50.0f, ray);
        assert(bBuilt);
        AssertRayOrigin(ray, 0.0f, 0.0f, 0.0f);
        AssertRayDirection(ray, 0.0f, 0.0f, -1.0f);

        bBuilt = NorvesLib::Core::Rendering::BuildPickingRay(camera, 100.0f, 50.0f, ray);
        assert(bBuilt);
        AssertRayOrigin(ray, 10.0f, 0.0f, 0.0f);
        AssertRayDirection(ray, 0.0f, 0.0f, -1.0f);

        bBuilt = NorvesLib::Core::Rendering::BuildPickingRay(camera, 50.0f, 0.0f, ray);
        assert(bBuilt);
        AssertRayOrigin(ray, 0.0f, 10.0f, 0.0f);
        AssertRayDirection(ray, 0.0f, 0.0f, -1.0f);
    }

    {
        const NorvesLib::Core::Rendering::CameraProxy camera = MakeCamera(
            NorvesLib::Core::Rendering::ProjectionType::Perspective,
            MakeViewport(0.0f, 0.0f, 100.0f, 100.0f));
        NorvesLib::Math::Sphere sphere;

        const float centerAlongRayDistance = 10.0f;
        const bool bBuilt = NorvesLib::Core::Rendering::BuildSelectionSphere(
            camera,
            50.0f,
            50.0f,
            100.0f,
            50.0f,
            centerAlongRayDistance,
            sphere);
        assert(bBuilt);
        AssertVectorNear(sphere.Center, 0.0f, 0.0f, -10.0f);

        const float edgeNdcX = 1.0f;
        const float edgeNdcY = 0.0f;
        const float fieldOfViewRadians = camera.FieldOfView * (NorvesLib::Math::Constants::PI / 180.0f);
        // 期待半径は depth * tan(fov/2) * sqrt((ndcX * aspect)^2 + ndcY^2)。
        // edge=(100,50) では ndcX=1, ndcY=0, fov=90, aspect=1 なので式から 10 に定まる。
        const float expectedRadius = centerAlongRayDistance
            * std::tan(fieldOfViewRadians * 0.5f)
            * std::sqrt((edgeNdcX * camera.AspectRatio) * (edgeNdcX * camera.AspectRatio) + edgeNdcY * edgeNdcY);
        assert(IsNearlyEqual(expectedRadius, 10.0f));
        assert(IsNearlyEqual(sphere.Radius, expectedRadius));
    }

    {
        const NorvesLib::Core::Rendering::CameraProxy camera = MakeCamera(
            NorvesLib::Core::Rendering::ProjectionType::Orthographic,
            MakeViewport(0.0f, 0.0f, 100.0f, 100.0f));
        NorvesLib::Math::Sphere sphere;

        const bool bBuilt = NorvesLib::Core::Rendering::BuildSelectionSphere(
            camera,
            50.0f,
            50.0f,
            100.0f,
            50.0f,
            5.0f,
            sphere);
        assert(bBuilt);
        AssertVectorNear(sphere.Center, 0.0f, 0.0f, -5.0f);
        assert(IsNearlyEqual(sphere.Radius, camera.OrthoWidth * 0.5f));
        assert(IsNearlyEqual(sphere.Radius, 10.0f));
    }

    {
        const NorvesLib::Core::Rendering::CameraProxy camera = MakeCamera(
            NorvesLib::Core::Rendering::ProjectionType::Perspective,
            MakeViewport(0.0f, 0.0f, 100.0f, 100.0f));

        AssertSelectionSphereFailureDoesNotMutate(camera, 50.0f, 50.0f, 100.0f, 50.0f, 0.0f);
        AssertSelectionSphereFailureDoesNotMutate(camera, 50.0f, 50.0f, 100.0f, 50.0f, -1.0f);
        AssertSelectionSphereFailureDoesNotMutate(camera, -5.0f, 50.0f, 100.0f, 50.0f, 10.0f);
        AssertSelectionSphereFailureDoesNotMutate(camera, 50.0f, 50.0f, 50.0f, 150.0f, 10.0f);
        AssertSelectionSphereFailureDoesNotMutate(camera, 50.0f, 50.0f, 50.0f, 50.0f, 10.0f);
        AssertSelectionSphereFailureDoesNotMutate(camera, 50.0f, 50.0f, 100.0f, 50.0f, std::nanf(""));
        AssertSelectionSphereFailureDoesNotMutate(camera, INFINITY, 50.0f, 100.0f, 50.0f, 10.0f);
    }

    {
        const NorvesLib::Core::Rendering::CameraProxy camera = MakeCamera(
            NorvesLib::Core::Rendering::ProjectionType::Perspective,
            MakeViewport(0.0f, 0.0f, 100.0f, 100.0f));
        NorvesLib::Math::Frustum frustum;

        bool bBuilt = NorvesLib::Core::Rendering::BuildScreenRectFrustum(
            camera,
            40.0f,
            40.0f,
            60.0f,
            60.0f,
            frustum);
        assert(bBuilt);
        assert(FrustumIntersectsBox(frustum, 0.0f, 0.0f, -10.0f));
        assert(!FrustumIntersectsBox(frustum, -5.0f, 0.0f, -10.0f));
        assert(!FrustumIntersectsBox(frustum, 5.0f, 0.0f, -10.0f));
        assert(!FrustumIntersectsBox(frustum, 0.0f, 5.0f, -10.0f));
        assert(!FrustumIntersectsBox(frustum, 0.0f, -5.0f, -10.0f));
        assert(!FrustumIntersectsBox(frustum, 0.0f, 0.0f, 1.0f));
        assert(!FrustumIntersectsBox(frustum, 0.0f, 0.0f, -2000.0f, 1.0f));

        bBuilt = NorvesLib::Core::Rendering::BuildScreenRectFrustum(
            camera,
            0.0f,
            0.0f,
            100.0f,
            100.0f,
            frustum);
        assert(bBuilt);
        assert(FrustumIntersectsBox(frustum, 0.0f, 0.0f, -10.0f));
        assert(FrustumIntersectsBox(frustum, -5.0f, 0.0f, -10.0f));
        assert(FrustumIntersectsBox(frustum, 5.0f, 0.0f, -10.0f));
        assert(FrustumIntersectsBox(frustum, 0.0f, 5.0f, -10.0f));
        assert(FrustumIntersectsBox(frustum, 0.0f, -5.0f, -10.0f));
    }

    {
        NorvesLib::Core::Rendering::CameraProxy camera = MakeCamera(
            NorvesLib::Core::Rendering::ProjectionType::Perspective,
            MakeViewport(0.0f, 0.0f, 100.0f, 100.0f));
        camera.PositionX = 10.0f;
        camera.PositionY = 0.0f;
        camera.PositionZ = 0.0f;
        camera.ForwardX = -1.0f;
        camera.ForwardY = 0.0f;
        camera.ForwardZ = 0.0f;
        camera.RightX = 0.0f;
        camera.RightY = 0.0f;
        camera.RightZ = -1.0f;
        NorvesLib::Math::Frustum frustum;

        const bool bBuilt = NorvesLib::Core::Rendering::BuildScreenRectFrustum(
            camera,
            40.0f,
            40.0f,
            60.0f,
            60.0f,
            frustum);
        assert(bBuilt);
        assert(FrustumIntersectsBox(frustum, 0.0f, 0.0f, 0.0f));
        // 側方(depth10。near/farでは落ちず side plane が outside にする=平面向き補正の検証)
        assert(!FrustumIntersectsBox(frustum, 0.0f, 0.0f, 10.0f));
        assert(!FrustumIntersectsBox(frustum, 0.0f, 0.0f, -10.0f));
        assert(!FrustumIntersectsBox(frustum, 0.0f, 10.0f, 0.0f));
        assert(!FrustumIntersectsBox(frustum, 0.0f, -10.0f, 0.0f));
    }

    {
        const NorvesLib::Core::Rendering::CameraProxy camera = MakeCamera(
            NorvesLib::Core::Rendering::ProjectionType::Perspective,
            MakeViewport(0.0f, 0.0f, 100.0f, 100.0f));
        NorvesLib::Math::Frustum normalFrustum;
        NorvesLib::Math::Frustum reversedFrustum;

        bool bBuilt = NorvesLib::Core::Rendering::BuildScreenRectFrustum(
            camera,
            40.0f,
            40.0f,
            60.0f,
            60.0f,
            normalFrustum);
        assert(bBuilt);
        bBuilt = NorvesLib::Core::Rendering::BuildScreenRectFrustum(
            camera,
            60.0f,
            60.0f,
            40.0f,
            40.0f,
            reversedFrustum);
        assert(bBuilt);
        assert(FrustumIntersectsBox(normalFrustum, 0.0f, 0.0f, -10.0f)
            == FrustumIntersectsBox(reversedFrustum, 0.0f, 0.0f, -10.0f));
        assert(FrustumIntersectsBox(normalFrustum, -5.0f, 0.0f, -10.0f)
            == FrustumIntersectsBox(reversedFrustum, -5.0f, 0.0f, -10.0f));
        assert(FrustumIntersectsBox(normalFrustum, 0.0f, 5.0f, -10.0f)
            == FrustumIntersectsBox(reversedFrustum, 0.0f, 5.0f, -10.0f));
    }

    {
        const NorvesLib::Core::Rendering::CameraProxy camera = MakeCamera(
            NorvesLib::Core::Rendering::ProjectionType::Perspective,
            MakeViewport(0.0f, 0.0f, 100.0f, 100.0f));
        NorvesLib::Math::Frustum frustum;

        const bool bBuilt = NorvesLib::Core::Rendering::BuildScreenRectFrustum(
            camera,
            -20.0f,
            -20.0f,
            50.0f,
            50.0f,
            frustum);
        assert(bBuilt);
        assert(FrustumIntersectsBox(frustum, -5.0f, 5.0f, -10.0f));
    }

    {
        NorvesLib::Core::Rendering::CameraProxy camera = MakeCamera(
            NorvesLib::Core::Rendering::ProjectionType::Perspective,
            MakeViewport(0.0f, 0.0f, 100.0f, 100.0f));
        NorvesLib::Math::Frustum frustum;

        assert(!NorvesLib::Core::Rendering::BuildScreenRectFrustum(
            camera,
            50.0f,
            50.0f,
            50.0f,
            50.0f,
            frustum));
        assert(!NorvesLib::Core::Rendering::BuildScreenRectFrustum(
            camera,
            50.0f,
            50.0f,
            50.0005f,
            50.0005f,
            frustum));

        camera.NearPlane = 0.0f;
        assert(!NorvesLib::Core::Rendering::BuildScreenRectFrustum(
            camera,
            40.0f,
            40.0f,
            60.0f,
            60.0f,
            frustum));

        camera.NearPlane = 0.1f;
        camera.FarPlane = 0.1f;
        assert(!NorvesLib::Core::Rendering::BuildScreenRectFrustum(
            camera,
            40.0f,
            40.0f,
            60.0f,
            60.0f,
            frustum));

        const NorvesLib::Core::Rendering::CameraProxy orthoCamera = MakeCamera(
            NorvesLib::Core::Rendering::ProjectionType::Orthographic,
            MakeViewport(0.0f, 0.0f, 100.0f, 100.0f));
        assert(!NorvesLib::Core::Rendering::BuildScreenRectFrustum(
            orthoCamera,
            0.0f,
            0.0f,
            100.0f,
            100.0f,
            frustum));
    }

    return 0;
}
