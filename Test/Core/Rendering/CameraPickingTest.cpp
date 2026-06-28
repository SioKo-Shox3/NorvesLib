#include "Rendering/CameraPicking.h"
#include <cassert>

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

    void AssertRayOrigin(const NorvesLib::Math::Ray& ray, float x, float y, float z)
    {
        AssertVectorNear(ray.Origin, x, y, z);
    }

    void AssertRayDirection(const NorvesLib::Math::Ray& ray, float x, float y, float z)
    {
        AssertVectorNear(ray.Direction, x, y, z);
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

    return 0;
}
