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

    {
        uint32_t randomState = 0x7d39a5c1u;
        const auto nextRandom = [&randomState]()
        {
            randomState = randomState * 1664525u + 1013904223u;
            return static_cast<float>(randomState >> 8) / 16777215.0f;
        };

        for (int i = 0; i < 70; ++i)
        {
            const Vector3 v0(
                nextRandom() * 6.0f - 3.0f,
                nextRandom() * 6.0f - 3.0f,
                nextRandom() * 6.0f - 3.0f);
            const Vector3 v1 = v0 + Vector3(1.0f + nextRandom() * 2.0f, 0.0f, nextRandom() * 2.0f - 1.0f);
            const Vector3 v2 = v0 + Vector3(0.0f, 1.0f + nextRandom() * 2.0f, nextRandom() * 2.0f - 1.0f);
            const Vector3 edge1 = v1 - v0;
            const Vector3 edge2 = v2 - v0;
            const Vector3 normal = VectorUtils::Cross(edge1, edge2).Normalized();
            Vector3 direction = normal * (i % 2 == 0 ? 1.0f : -1.0f);
            float expectedU = 0.1f + nextRandom() * 0.3f;
            float expectedV = 0.1f + nextRandom() * 0.3f;
            float expectedT = 0.5f + nextRandom() * 1.5f;
            const int variant = i % 10;

            if (variant == 3)
            {
                expectedU = 0.0f;
                expectedV = 0.0f;
                expectedT = 0.0f;
            }
            else if (variant == 4)
            {
                direction = Vector3(0.0f, 0.0f, 0.0f);
            }
            else if (variant == 5)
            {
                expectedU = -0.2f;
            }
            else if (variant == 6)
            {
                expectedU = 1.2f;
            }
            else if (variant == 7)
            {
                expectedV = -0.2f;
            }
            else if (variant == 8)
            {
                expectedU = 0.7f;
                expectedV = 0.6f;
            }
            else if (variant == 9)
            {
                expectedT = -expectedT;
            }

            const Vector3 point = v0 + edge1 * expectedU + edge2 * expectedV;
            const Ray ray(point - direction * expectedT, direction);
            float referenceT = -1.0f;
            float t = -1.0f;
            float u = -2.0f;
            float v = -3.0f;
            const bool bReferenceHit = RayIntersectsTriangle(ray, v0, v1, v2, referenceT);
            const bool bHit = RayIntersectsTriangleBarycentric(ray, v0, v1, v2, t, u, v);
            assert(bHit == bReferenceHit);
            assert(bHit == (variant < 4));
            assert(t == referenceT);
            if (bHit)
            {
                assert(NearlyEqual(t, expectedT, 1e-5f));
                assert(NearlyEqual(u, expectedU, 1e-5f));
                assert(NearlyEqual(v, expectedV, 1e-5f));
                const Vector3 trianglePoint = v0 + edge1 * u + edge2 * v;
                const Vector3 rayPoint = ray.Origin + ray.Direction * t;
                assert((trianglePoint - rayPoint).Length() <= 1e-5f);
            }
            else
            {
                assert(t == -1.0f);
                assert(u == -2.0f);
                assert(v == -3.0f);
            }
        }
        std::cout << "RayIntersectsTriangleBarycentric passed\n";
    }

    std::cout << "GeometryIntersectionExtraTest passed\n";
    return 0;
}
