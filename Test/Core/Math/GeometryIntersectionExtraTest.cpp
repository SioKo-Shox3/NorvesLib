#include "Math/GeometryTypes.h"
#include "Math/GeometryIntersection.h"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>

using namespace NorvesLib::Math;

namespace
{
    static bool NearlyEqual(float a, float b, float tol = 1e-4f)
    {
        return std::fabs(a - b) <= tol;
    }
}

int main()
{
    std::cout << "GeometryIntersectionExtraTest start\n";

    {
        const OBB obb(
            Vector3(0.0f, 0.0f, 5.0f),
            Vector3(1.0f, 1.0f, 1.0f),
            Vector3(1.0f, 0.0f, 0.0f),
            Vector3(0.0f, 1.0f, 0.0f),
            Vector3(0.0f, 0.0f, 1.0f));
        float t = 0.0f;
        assert(RayIntersectsOBB(
            Ray(Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f)),
            obb,
            t));
        assert(NearlyEqual(t, 4.0f));

        constexpr float sqrtHalf = 0.7071067811865475f;
        const OBB rotatedOBB(
            Vector3(0.0f, 0.0f, 5.0f),
            Vector3(1.0f, 1.0f, 1.0f),
            Vector3(sqrtHalf, 0.0f, sqrtHalf),
            Vector3(0.0f, 1.0f, 0.0f),
            Vector3(-sqrtHalf, 0.0f, sqrtHalf));
        assert(RayIntersectsOBB(
            Ray(Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f)),
            rotatedOBB,
            t));
        assert(NearlyEqual(t, 5.0f - 1.4142135623730951f));

        assert(!RayIntersectsOBB(
            Ray(Vector3(3.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f)),
            obb,
            t));

        assert(RayIntersectsOBB(
            Ray(Vector3(0.0f, 0.0f, 5.0f), Vector3(0.0f, 0.0f, 1.0f)),
            obb,
            t));
        assert(NearlyEqual(t, 0.0f));

        assert(RayIntersectsOBB(
            Ray(Vector3(0.0f, 0.0f, 5.0f), Vector3(0.0f, 0.0f, 0.0f)),
            obb,
            t));
        assert(NearlyEqual(t, 0.0f));

        assert(!RayIntersectsOBB(
            Ray(Vector3(2.0f, 0.0f, 5.0f), Vector3(0.0f, 0.0f, 0.0f)),
            obb,
            t));
        std::cout << "RayIntersectsOBB passed\n";
    }

    {
        assert(SphereIntersectsSphere(
            Sphere(Vector3(0.0f, 0.0f, 0.0f), 1.0f),
            Sphere(Vector3(1.5f, 0.0f, 0.0f), 1.0f)));
        assert(SphereIntersectsSphere(
            Sphere(Vector3(0.0f, 0.0f, 0.0f), 1.0f),
            Sphere(Vector3(2.0f, 0.0f, 0.0f), 1.0f)));
        assert(!SphereIntersectsSphere(
            Sphere(Vector3(0.0f, 0.0f, 0.0f), 1.0f),
            Sphere(Vector3(2.01f, 0.0f, 0.0f), 1.0f)));
        assert(SphereIntersectsSphere(
            Sphere(Vector3(1.0f, 2.0f, 3.0f), 0.0f),
            Sphere(Vector3(1.0f, 2.0f, 3.0f), 0.0f)));
        assert(!SphereIntersectsSphere(
            Sphere(Vector3(1.0f, 2.0f, 3.0f), 0.0f),
            Sphere(Vector3(1.001f, 2.0f, 3.0f), 0.0f)));
        std::cout << "SphereIntersectsSphere passed\n";
    }

    {
        const AABB aabb(Vector3(-1.0f, -1.0f, -1.0f), Vector3(1.0f, 1.0f, 1.0f));
        assert(CapsuleIntersectsAABB(
            Capsule(Vector3(0.0f, 3.0f, 0.0f), Vector3(0.0f, 5.0f, 0.0f), 2.5f),
            aabb));
        assert(!CapsuleIntersectsAABB(
            Capsule(Vector3(0.0f, 3.0f, 0.0f), Vector3(0.0f, 5.0f, 0.0f), 1.5f),
            aabb));
        assert(CapsuleIntersectsAABB(
            Capsule(Vector3(-3.0f, 0.0f, 0.0f), Vector3(3.0f, 0.0f, 0.0f), 0.1f),
            aabb));
        assert(CapsuleIntersectsAABB(
            Capsule(Vector3(0.0f, 3.0f, 0.0f), Vector3(0.0f, 3.0f, 0.0f), 2.5f),
            aabb));
        assert(CapsuleIntersectsAABB(
            Capsule(Vector3(0.0f, 3.0f, 0.0f), Vector3(0.0f, 5.0f, 0.0f), 2.0f),
            aabb));
        std::cout << "CapsuleIntersectsAABB passed\n";
    }

    {
        const Plane plane(Vector3(0.0f, 1.0f, 0.0f), Vector3(0.0f, 2.0f, 0.0f));
        assert(SphereIntersectsPlane(Sphere(Vector3(0.0f, 3.0f, 0.0f), 1.5f), plane));
        assert(!SphereIntersectsPlane(Sphere(Vector3(0.0f, 5.0f, 0.0f), 1.5f), plane));
        assert(SphereIntersectsPlane(Sphere(Vector3(0.0f, 4.0f, 0.0f), 2.0f), plane));
        assert(SphereIntersectsPlane(Sphere(Vector3(0.0f, 2.0f, 0.0f), 0.0f), plane));
        assert(!SphereIntersectsPlane(Sphere(Vector3(0.0f, 2.001f, 0.0f), 0.0f), plane));
        std::cout << "SphereIntersectsPlane passed\n";
    }

    {
        const Vector3 v0(0.0f, 0.0f, 0.0f);
        const Vector3 v1(1.0f, 0.0f, 0.0f);
        const Vector3 v2(0.0f, 1.0f, 0.0f);
        float t = 0.0f;
        assert(RayIntersectsTriangle(
            Ray(Vector3(0.25f, 0.25f, -1.0f), Vector3(0.0f, 0.0f, 1.0f)),
            v0,
            v1,
            v2,
            t));
        assert(NearlyEqual(t, 1.0f));

        assert(!RayIntersectsTriangle(
            Ray(Vector3(0.9f, 0.9f, -1.0f), Vector3(0.0f, 0.0f, 1.0f)),
            v0,
            v1,
            v2,
            t));
        assert(!RayIntersectsTriangle(
            Ray(Vector3(0.25f, 0.25f, -1.0f), Vector3(1.0f, 0.0f, 0.0f)),
            v0,
            v1,
            v2,
            t));
        assert(RayIntersectsTriangle(
            Ray(Vector3(0.25f, 0.25f, 1.0f), Vector3(0.0f, 0.0f, -1.0f)),
            v0,
            v1,
            v2,
            t));
        assert(NearlyEqual(t, 1.0f));
        assert(!RayIntersectsTriangle(
            Ray(Vector3(0.25f, 0.25f, 1.0f), Vector3(0.0f, 0.0f, 1.0f)),
            v0,
            v1,
            v2,
            t));
        assert(!RayIntersectsTriangle(
            Ray(Vector3(0.25f, 0.25f, -1.0f), Vector3(0.0f, 0.0f, 1.0f)),
            v0,
            v1,
            v1,
            t));
        std::cout << "RayIntersectsTriangle passed\n";
    }

    {
        const AABB aabb(Vector3(-1.0f, -1.0f, -1.0f), Vector3(1.0f, 1.0f, 1.0f));
        const OBB axisAlignedHit(
            Vector3(1.5f, 0.0f, 0.0f),
            Vector3(1.0f, 1.0f, 1.0f),
            Vector3(1.0f, 0.0f, 0.0f),
            Vector3(0.0f, 1.0f, 0.0f),
            Vector3(0.0f, 0.0f, 1.0f));
        const OBB axisAlignedBoundary(
            Vector3(2.0f, 0.0f, 0.0f),
            Vector3(1.0f, 1.0f, 1.0f),
            Vector3(1.0f, 0.0f, 0.0f),
            Vector3(0.0f, 1.0f, 0.0f),
            Vector3(0.0f, 0.0f, 1.0f));
        const OBB axisAlignedMiss(
            Vector3(2.01f, 0.0f, 0.0f),
            Vector3(1.0f, 1.0f, 1.0f),
            Vector3(1.0f, 0.0f, 0.0f),
            Vector3(0.0f, 1.0f, 0.0f),
            Vector3(0.0f, 0.0f, 1.0f));
        assert(OBBIntersectsAABB(axisAlignedHit, aabb));
        assert(OBBIntersectsAABB(axisAlignedBoundary, aabb));
        assert(!OBBIntersectsAABB(axisAlignedMiss, aabb));

        constexpr float sqrtHalf = 0.7071067811865475f;
        const OBB rotatedHit(
            Vector3(2.2f, 0.0f, 0.0f),
            Vector3(1.0f, 1.0f, 1.0f),
            Vector3(sqrtHalf, sqrtHalf, 0.0f),
            Vector3(-sqrtHalf, sqrtHalf, 0.0f),
            Vector3(0.0f, 0.0f, 1.0f));
        const OBB rotatedMiss(
            Vector3(2.5f, 0.0f, 0.0f),
            Vector3(1.0f, 1.0f, 1.0f),
            Vector3(sqrtHalf, sqrtHalf, 0.0f),
            Vector3(-sqrtHalf, sqrtHalf, 0.0f),
            Vector3(0.0f, 0.0f, 1.0f));
        const OBB contained(
            Vector3(0.0f, 0.0f, 0.0f),
            Vector3(0.5f, 0.5f, 0.5f),
            Vector3(1.0f, 0.0f, 0.0f),
            Vector3(0.0f, 1.0f, 0.0f),
            Vector3(0.0f, 0.0f, 1.0f));
        assert(OBBIntersectsAABB(rotatedHit, aabb));
        assert(!OBBIntersectsAABB(rotatedMiss, aabb));
        assert(OBBIntersectsAABB(contained, aabb));
        std::cout << "OBBIntersectsAABB passed\n";
    }

    {
        const Capsule capsule(Vector3(-2.0f, 0.0f, 0.0f), Vector3(2.0f, 0.0f, 0.0f), 1.0f);
        assert(!CapsuleIntersectsSphere(capsule, Sphere(Vector3(0.0f, 1.5f, 0.0f), 0.4f)));
        assert(CapsuleIntersectsSphere(capsule, Sphere(Vector3(0.0f, 1.5f, 0.0f), 0.6f)));
        assert(CapsuleIntersectsSphere(capsule, Sphere(Vector3(3.0f, 0.0f, 0.0f), 0.5f)));
        assert(!CapsuleIntersectsSphere(capsule, Sphere(Vector3(4.0f, 0.0f, 0.0f), 0.5f)));
        assert(CapsuleIntersectsSphere(
            Capsule(Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 0.0f), 1.0f),
            Sphere(Vector3(0.0f, 1.5f, 0.0f), 0.5f)));
        assert(!CapsuleIntersectsSphere(
            Capsule(Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 0.0f), 1.0f),
            Sphere(Vector3(0.0f, 1.6f, 0.0f), 0.5f)));
        std::cout << "CapsuleIntersectsSphere passed\n";
    }

    std::cout << "GeometryIntersectionExtraTest passed\n";
    return 0;
}
