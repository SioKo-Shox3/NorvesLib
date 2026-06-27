#include "Math/GeometryTypes.h"
#include "Math/GeometryIntersection.h"
#include "Math/MatrixUtils.h"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace NorvesLib::Math;

namespace
{
    static bool NearlyEqual(float a, float b, float tol = 1e-4f)
    {
        return std::fabs(a - b) <= tol;
    }

    static void AssertVectorNearlyEqual(const Vector3& actual, const Vector3& expected)
    {
        assert(NearlyEqual(actual.x, expected.x));
        assert(NearlyEqual(actual.y, expected.y));
        assert(NearlyEqual(actual.z, expected.z));
    }

    static bool FrustumContainsPoint(const Frustum& frustum, const Vector3& point)
    {
        for (int i = 0; i < 6; ++i)
        {
            if (frustum.Planes[i].SignedDistance(point) < -1e-4f)
            {
                return false;
            }
        }

        return true;
    }
}

int main()
{
    std::cout << "MathGeometryTest start\n";

    {
        const Ray ray(Vector3(1.0f, 2.0f, 3.0f), Vector3(0.0f, 0.0f, 2.0f));
        AssertVectorNearlyEqual(ray.PointAt(1.5f), Vector3(1.0f, 2.0f, 6.0f));
        std::cout << "Ray::PointAt passed\n";
    }

    {
        const Plane plane(Vector3(0.0f, 1.0f, 0.0f), Vector3(0.0f, 2.0f, 0.0f));
        assert(NearlyEqual(plane.SignedDistance(Vector3(0.0f, 3.0f, 0.0f)), 1.0f));
        assert(NearlyEqual(plane.SignedDistance(Vector3(0.0f, 1.0f, 0.0f)), -1.0f));
        assert(NearlyEqual(plane.SignedDistance(Vector3(5.0f, 2.0f, 1.0f)), 0.0f));

        const Plane trianglePlane(
            Vector3(0.0f, 0.0f, 0.0f),
            Vector3(1.0f, 0.0f, 0.0f),
            Vector3(0.0f, 1.0f, 0.0f));
        assert(trianglePlane.SignedDistance(Vector3(0.0f, 0.0f, 1.0f)) > 0.0f);
        std::cout << "Plane::SignedDistance passed\n";
    }

    {
        const Sphere sphere(Vector3(1.0f, 2.0f, 3.0f), 2.0f);
        assert(sphere.Contains(Vector3(1.0f, 2.0f, 3.0f)));
        assert(sphere.Contains(Vector3(3.0f, 2.0f, 3.0f)));
        assert(!sphere.Contains(Vector3(3.1f, 2.0f, 3.0f)));
        std::cout << "Sphere::Contains passed\n";
    }

    {
        AABB box(Vector3(-1.0f, -2.0f, -3.0f), Vector3(3.0f, 2.0f, 1.0f));
        AssertVectorNearlyEqual(box.Center(), Vector3(1.0f, 0.0f, -1.0f));
        AssertVectorNearlyEqual(box.Extents(), Vector3(4.0f, 4.0f, 4.0f));
        AssertVectorNearlyEqual(box.HalfExtents(), Vector3(2.0f, 2.0f, 2.0f));
        assert(box.Contains(Vector3(0.0f, 0.0f, 0.0f)));
        assert(!box.Contains(Vector3(4.0f, 0.0f, 0.0f)));

        box.Expand(Vector3(5.0f, -3.0f, 0.0f));
        AssertVectorNearlyEqual(box.Min, Vector3(-1.0f, -3.0f, -3.0f));
        AssertVectorNearlyEqual(box.Max, Vector3(5.0f, 2.0f, 1.0f));

        box.Merge(AABB(Vector3(-10.0f, 0.0f, -6.0f), Vector3(-1.0f, 8.0f, 1.0f)));
        AssertVectorNearlyEqual(box.Min, Vector3(-10.0f, -3.0f, -6.0f));
        AssertVectorNearlyEqual(box.Max, Vector3(5.0f, 8.0f, 1.0f));

        const AABB fromCenter = AABB::FromCenterExtents(
            Vector3(1.0f, 2.0f, 3.0f),
            Vector3(2.0f, 3.0f, 4.0f));
        AssertVectorNearlyEqual(fromCenter.Min, Vector3(-1.0f, -1.0f, -1.0f));
        AssertVectorNearlyEqual(fromCenter.Max, Vector3(3.0f, 5.0f, 7.0f));

        AABB invalid = AABB::CreateInvalid();
        invalid.Merge(fromCenter);
        AssertVectorNearlyEqual(invalid.Min, fromCenter.Min);
        AssertVectorNearlyEqual(invalid.Max, fromCenter.Max);
        std::cout << "AABB helpers passed\n";
    }

    {
        const Ray ray(Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f));
        const Plane plane(Vector3(0.0f, 1.0f, 0.0f), 5.0f);
        float t = 0.0f;
        assert(RayIntersectsPlane(ray, plane, t));
        assert(NearlyEqual(t, 5.0f));

        const Ray parallelRay(Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 0.0f, 0.0f));
        assert(!RayIntersectsPlane(parallelRay, plane, t));
        std::cout << "RayIntersectsPlane passed\n";
    }

    {
        float t = 0.0f;
        assert(RayIntersectsSphere(
            Ray(Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f)),
            Sphere(Vector3(0.0f, 0.0f, 5.0f), 1.0f),
            t));
        assert(NearlyEqual(t, 4.0f));

        assert(RayIntersectsSphere(
            Ray(Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f)),
            Sphere(Vector3(1.0f, 0.0f, 5.0f), 1.0f),
            t));
        assert(NearlyEqual(t, 5.0f));

        assert(!RayIntersectsSphere(
            Ray(Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f)),
            Sphere(Vector3(3.0f, 0.0f, 5.0f), 1.0f),
            t));
        std::cout << "RayIntersectsSphere passed\n";
    }

    {
        const AABB box(Vector3(-1.0f, -1.0f, 4.0f), Vector3(1.0f, 1.0f, 6.0f));
        float t = 0.0f;
        assert(RayIntersectsAABB(
            Ray(Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f)),
            box,
            t));
        assert(NearlyEqual(t, 4.0f));

        assert(!RayIntersectsAABB(
            Ray(Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f)),
            box,
            t));

        assert(RayIntersectsAABB(
            Ray(Vector3(0.0f, 0.0f, 5.0f), Vector3(0.0f, 0.0f, 1.0f)),
            box,
            t));
        assert(NearlyEqual(t, 0.0f));

        assert(RayIntersectsAABB(
            Ray(Vector3(0.5f, 0.5f, 0.0f), Vector3(0.0f, 0.0f, 1.0f)),
            box,
            t));
        assert(NearlyEqual(t, 4.0f));
        std::cout << "RayIntersectsAABB passed\n";
    }

    {
        const AABB box(Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f));
        assert(AABBIntersectsAABB(box, AABB(Vector3(0.5f, 0.5f, 0.5f), Vector3(2.0f, 2.0f, 2.0f))));
        assert(AABBIntersectsAABB(box, AABB(Vector3(1.0f, 1.0f, 1.0f), Vector3(2.0f, 2.0f, 2.0f))));
        assert(!AABBIntersectsAABB(box, AABB(Vector3(1.1f, 0.0f, 0.0f), Vector3(2.0f, 1.0f, 1.0f))));
        std::cout << "AABBIntersectsAABB passed\n";
    }

    {
        const Matrix4x4 projection = MatrixUtils::CreatePerspectiveFieldOfView(
            Constants::HALF_PI,
            1.0f,
            0.1f,
            10.0f);
        const Frustum frustum = Frustum::FromViewProjection(projection, ClipSpaceDepthRange::ZeroToOne);

        assert(FrustumContainsPoint(frustum, Vector3(0.0f, 0.0f, 1.0f)));
        assert(FrustumContainsPoint(frustum, Vector3(0.0f, 0.0f, 9.0f)));
        assert(!FrustumContainsPoint(frustum, Vector3(0.0f, 0.0f, 11.0f)));
        assert(!FrustumContainsPoint(frustum, Vector3(2.0f, 0.0f, 1.0f)));
        std::cout << "Frustum::FromViewProjection passed\n";
    }

    std::cout << "MathGeometryTest passed\n";
    return 0;
}
