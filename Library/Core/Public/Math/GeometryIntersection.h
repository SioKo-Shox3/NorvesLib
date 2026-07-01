#pragma once

#include "GeometryTypes.h"
#include "MathTypes.h"
#include "VectorUtils.h"
#include <cmath>
#include <cfloat>

namespace NorvesLib::Math
{
namespace Detail
{
    inline bool RayIntersectsSlab(
        float origin,
        float direction,
        float minValue,
        float maxValue,
        float& inOutTMin,
        float& inOutTMax)
    {
        if (std::fabs(direction) <= Constants::EPSILON)
        {
            return origin >= minValue && origin <= maxValue;
        }

        const float invDirection = 1.0f / direction;
        float t0 = (minValue - origin) * invDirection;
        float t1 = (maxValue - origin) * invDirection;

        if (t0 > t1)
        {
            const float temp = t0;
            t0 = t1;
            t1 = temp;
        }

        if (t0 > inOutTMin)
        {
            inOutTMin = t0;
        }
        if (t1 < inOutTMax)
        {
            inOutTMax = t1;
        }

        return inOutTMin <= inOutTMax;
    }

    inline float ClampToRange(float value, float minValue, float maxValue)
    {
        return std::fmaxf(minValue, std::fminf(value, maxValue));
    }

    inline float Clamp01(float value)
    {
        return ClampToRange(value, 0.0f, 1.0f);
    }

    inline float GetComponent(const Vector3& value, int axis)
    {
        if (axis == 0)
        {
            return value.x;
        }
        if (axis == 1)
        {
            return value.y;
        }

        return value.z;
    }

    inline void AddUniqueCandidate(float* candidates, int& candidateCount, int capacity, float value)
    {
        if (candidateCount >= capacity)
        {
            return;
        }

        for (int i = 0; i < candidateCount; ++i)
        {
            if (std::fabs(candidates[i] - value) <= Constants::EPSILON)
            {
                return;
            }
        }

        candidates[candidateCount] = value;
        ++candidateCount;
    }

    inline void SortCandidates(float* candidates, int candidateCount)
    {
        for (int i = 1; i < candidateCount; ++i)
        {
            const float value = candidates[i];
            int j = i - 1;
            while (j >= 0 && candidates[j] > value)
            {
                candidates[j + 1] = candidates[j];
                --j;
            }

            candidates[j + 1] = value;
        }
    }

    inline float PointAABBDistanceSquared(const Vector3& point, const AABB& aabb)
    {
        const Vector3 closestPoint(
            ClampToRange(point.x, aabb.Min.x, aabb.Max.x),
            ClampToRange(point.y, aabb.Min.y, aabb.Max.y),
            ClampToRange(point.z, aabb.Min.z, aabb.Max.z));
        return VectorUtils::DistanceSquared(point, closestPoint);
    }

    inline float SegmentPointDistanceSquared(const Vector3& point, const Vector3& a, const Vector3& b)
    {
        const Vector3 ab = b - a;
        const float abLenSq = VectorUtils::Dot(ab, ab);
        float t = 0.0f;
        if (abLenSq > Constants::EPSILON)
        {
            t = Clamp01(VectorUtils::Dot(point - a, ab) / abLenSq);
        }

        const Vector3 closestPoint = a + ab * t;
        return VectorUtils::DistanceSquared(point, closestPoint);
    }

    inline float SegmentAABBDistanceSquared(const Vector3& a, const Vector3& b, const AABB& aabb)
    {
        const Vector3 direction = b - a;
        constexpr int candidateCapacity = 8;
        float candidates[candidateCapacity];
        int candidateCount = 0;
        AddUniqueCandidate(candidates, candidateCount, candidateCapacity, 0.0f);
        AddUniqueCandidate(candidates, candidateCount, candidateCapacity, 1.0f);

        for (int axis = 0; axis < 3; ++axis)
        {
            const float directionComponent = GetComponent(direction, axis);
            if (std::fabs(directionComponent) <= Constants::EPSILON)
            {
                continue;
            }

            const float startComponent = GetComponent(a, axis);
            AddUniqueCandidate(
                candidates,
                candidateCount,
                candidateCapacity,
                Clamp01((GetComponent(aabb.Min, axis) - startComponent) / directionComponent));
            AddUniqueCandidate(
                candidates,
                candidateCount,
                candidateCapacity,
                Clamp01((GetComponent(aabb.Max, axis) - startComponent) / directionComponent));
        }

        SortCandidates(candidates, candidateCount);

        float minDistanceSq = FLT_MAX;
        for (int i = 0; i < candidateCount; ++i)
        {
            const Vector3 point = a + direction * candidates[i];
            const float distanceSq = PointAABBDistanceSquared(point, aabb);
            if (distanceSq < minDistanceSq)
            {
                minDistanceSq = distanceSq;
            }
        }

        for (int i = 0; i < candidateCount - 1; ++i)
        {
            const float intervalMin = candidates[i];
            const float intervalMax = candidates[i + 1];
            if (intervalMax - intervalMin <= Constants::EPSILON)
            {
                continue;
            }

            const float intervalMid = (intervalMin + intervalMax) * 0.5f;
            float numerator = 0.0f;
            float denominator = 0.0f;

            for (int axis = 0; axis < 3; ++axis)
            {
                const float startComponent = GetComponent(a, axis);
                const float directionComponent = GetComponent(direction, axis);
                const float midComponent = startComponent + directionComponent * intervalMid;
                float targetComponent = 0.0f;
                if (midComponent < GetComponent(aabb.Min, axis))
                {
                    targetComponent = GetComponent(aabb.Min, axis);
                }
                else if (midComponent > GetComponent(aabb.Max, axis))
                {
                    targetComponent = GetComponent(aabb.Max, axis);
                }
                else
                {
                    continue;
                }

                numerator += directionComponent * (startComponent - targetComponent);
                denominator += directionComponent * directionComponent;
            }

            if (denominator <= Constants::EPSILON)
            {
                continue;
            }

            const float t = -numerator / denominator;
            if (t >= intervalMin && t <= intervalMax)
            {
                const Vector3 point = a + direction * t;
                const float distanceSq = PointAABBDistanceSquared(point, aabb);
                if (distanceSq < minDistanceSq)
                {
                    minDistanceSq = distanceSq;
                }
            }
        }

        return minDistanceSq;
    }

    inline bool HasOBBAABBSeparatingAxis(
        const Vector3& axis,
        const Vector3& centerOffset,
        const Vector3& aabbHalfExtents,
        const OBB& obb)
    {
        if (VectorUtils::LengthSquared(axis) <= Constants::EPSILON)
        {
            return false;
        }

        const float projectedAABB =
            aabbHalfExtents.x * std::fabs(axis.x)
            + aabbHalfExtents.y * std::fabs(axis.y)
            + aabbHalfExtents.z * std::fabs(axis.z);
        const float projectedOBB =
            obb.HalfExtents.x * std::fabs(VectorUtils::Dot(axis, obb.Axes[0]))
            + obb.HalfExtents.y * std::fabs(VectorUtils::Dot(axis, obb.Axes[1]))
            + obb.HalfExtents.z * std::fabs(VectorUtils::Dot(axis, obb.Axes[2]));

        return std::fabs(VectorUtils::Dot(axis, centerOffset)) > projectedAABB + projectedOBB;
    }
} // namespace Detail

inline bool RayIntersectsAABB(const Ray& ray, const AABB& aabb, float& outTMin)
{
    float tMin = 0.0f;
    float tMax = FLT_MAX;

    if (!Detail::RayIntersectsSlab(ray.Origin.x, ray.Direction.x, aabb.Min.x, aabb.Max.x, tMin, tMax))
    {
        return false;
    }
    if (!Detail::RayIntersectsSlab(ray.Origin.y, ray.Direction.y, aabb.Min.y, aabb.Max.y, tMin, tMax))
    {
        return false;
    }
    if (!Detail::RayIntersectsSlab(ray.Origin.z, ray.Direction.z, aabb.Min.z, aabb.Max.z, tMin, tMax))
    {
        return false;
    }

    outTMin = tMin;
    return true;
}

inline bool RayIntersectsSphere(const Ray& ray, const Sphere& sphere, float& outT)
{
    const Vector3 offset = ray.Origin - sphere.Center;
    const float a = VectorUtils::Dot(ray.Direction, ray.Direction);
    if (a <= Constants::EPSILON)
    {
        return false;
    }

    const float b = 2.0f * VectorUtils::Dot(offset, ray.Direction);
    const float c = VectorUtils::Dot(offset, offset) - sphere.Radius * sphere.Radius;
    const float discriminant = b * b - 4.0f * a * c;

    if (discriminant < 0.0f)
    {
        return false;
    }

    const float sqrtDiscriminant = std::sqrt(discriminant);
    const float invDenominator = 1.0f / (2.0f * a);
    const float t0 = (-b - sqrtDiscriminant) * invDenominator;
    const float t1 = (-b + sqrtDiscriminant) * invDenominator;

    if (t0 >= 0.0f)
    {
        outT = t0;
        return true;
    }
    if (t1 >= 0.0f)
    {
        outT = t1;
        return true;
    }

    return false;
}

inline bool RayIntersectsPlane(const Ray& ray, const Plane& plane, float& outT)
{
    const float denominator = VectorUtils::Dot(plane.Normal, ray.Direction);
    if (std::fabs(denominator) <= Constants::EPSILON)
    {
        return false;
    }

    const float t = (plane.Distance - VectorUtils::Dot(plane.Normal, ray.Origin)) / denominator;
    if (t < 0.0f)
    {
        return false;
    }

    outT = t;
    return true;
}

inline bool AABBIntersectsAABB(const AABB& a, const AABB& b)
{
    return a.Min.x <= b.Max.x && a.Max.x >= b.Min.x
        && a.Min.y <= b.Max.y && a.Max.y >= b.Min.y
        && a.Min.z <= b.Max.z && a.Max.z >= b.Min.z;
}

inline bool SphereIntersectsAABB(const Sphere& sphere, const AABB& aabb)
{
    const Vector3 closestPoint(
        std::fmaxf(aabb.Min.x, std::fminf(sphere.Center.x, aabb.Max.x)),
        std::fmaxf(aabb.Min.y, std::fminf(sphere.Center.y, aabb.Max.y)),
        std::fmaxf(aabb.Min.z, std::fminf(sphere.Center.z, aabb.Max.z)));
    const Vector3 offset = sphere.Center - closestPoint;
    return VectorUtils::Dot(offset, offset) <= sphere.Radius * sphere.Radius;
}

inline bool FrustumIntersectsAABB(const Frustum& frustum, const AABB& aabb)
{
    for (int planeIndex = 0; planeIndex < 6; ++planeIndex)
    {
        const Plane& plane = frustum.Planes[planeIndex];
        const Vector3 pVertex(
            plane.Normal.x >= 0.0f ? aabb.Max.x : aabb.Min.x,
            plane.Normal.y >= 0.0f ? aabb.Max.y : aabb.Min.y,
            plane.Normal.z >= 0.0f ? aabb.Max.z : aabb.Min.z);
        if (plane.SignedDistance(pVertex) < 0.0f)
        {
            return false;
        }
    }

    return true;
}

inline bool SphereIntersectsSphere(const Sphere& a, const Sphere& b)
{
    const Vector3 offset = b.Center - a.Center;
    const float radius = a.Radius + b.Radius;
    return VectorUtils::Dot(offset, offset) <= radius * radius;
}

inline bool SphereIntersectsPlane(const Sphere& sphere, const Plane& plane)
{
    return std::fabs(plane.SignedDistance(sphere.Center)) <= sphere.Radius;
}

inline bool RayIntersectsOBB(const Ray& ray, const OBB& obb, float& outT)
{
    float tMin = 0.0f;
    float tMax = FLT_MAX;
    const Vector3 centerOffset = ray.Origin - obb.Center;

    for (int axis = 0; axis < 3; ++axis)
    {
        const float halfExtent = Detail::GetComponent(obb.HalfExtents, axis);
        if (!Detail::RayIntersectsSlab(
            VectorUtils::Dot(obb.Axes[axis], centerOffset),
            VectorUtils::Dot(obb.Axes[axis], ray.Direction),
            -halfExtent,
            halfExtent,
            tMin,
            tMax))
        {
            return false;
        }
    }

    outT = tMin;
    return true;
}

inline bool RayIntersectsTriangle(
    const Ray& ray,
    const Vector3& v0,
    const Vector3& v1,
    const Vector3& v2,
    float& outT)
{
    const Vector3 edge1 = v1 - v0;
    const Vector3 edge2 = v2 - v0;
    const Vector3 pvec = VectorUtils::Cross(ray.Direction, edge2);
    const float det = VectorUtils::Dot(edge1, pvec);
    if (std::fabs(det) <= Constants::EPSILON)
    {
        return false;
    }

    const float invDet = 1.0f / det;
    const Vector3 tvec = ray.Origin - v0;
    const float u = VectorUtils::Dot(tvec, pvec) * invDet;
    if (u < 0.0f || u > 1.0f)
    {
        return false;
    }

    const Vector3 qvec = VectorUtils::Cross(tvec, edge1);
    const float v = VectorUtils::Dot(ray.Direction, qvec) * invDet;
    if (v < 0.0f || u + v > 1.0f)
    {
        return false;
    }

    const float t = VectorUtils::Dot(edge2, qvec) * invDet;
    if (t < 0.0f)
    {
        return false;
    }

    outT = t;
    return true;
}

inline bool OBBIntersectsAABB(const OBB& obb, const AABB& aabb)
{
    const Vector3 aabbCenter = aabb.Center();
    const Vector3 aabbHalfExtents = aabb.HalfExtents();
    const Vector3 centerOffset = obb.Center - aabbCenter;
    const Vector3 unitAxes[3] = {
        Vector3(1.0f, 0.0f, 0.0f),
        Vector3(0.0f, 1.0f, 0.0f),
        Vector3(0.0f, 0.0f, 1.0f)};

    for (int axis = 0; axis < 3; ++axis)
    {
        if (Detail::HasOBBAABBSeparatingAxis(unitAxes[axis], centerOffset, aabbHalfExtents, obb))
        {
            return false;
        }
    }

    for (int axis = 0; axis < 3; ++axis)
    {
        if (Detail::HasOBBAABBSeparatingAxis(obb.Axes[axis], centerOffset, aabbHalfExtents, obb))
        {
            return false;
        }
    }

    for (int aabbAxis = 0; aabbAxis < 3; ++aabbAxis)
    {
        for (int obbAxis = 0; obbAxis < 3; ++obbAxis)
        {
            const Vector3 crossAxis = VectorUtils::Cross(unitAxes[aabbAxis], obb.Axes[obbAxis]);
            if (Detail::HasOBBAABBSeparatingAxis(crossAxis, centerOffset, aabbHalfExtents, obb))
            {
                return false;
            }
        }
    }

    return true;
}

inline bool CapsuleIntersectsSphere(const Capsule& capsule, const Sphere& sphere)
{
    const float distanceSq = Detail::SegmentPointDistanceSquared(sphere.Center, capsule.PointA, capsule.PointB);
    const float radius = capsule.Radius + sphere.Radius;
    return distanceSq <= radius * radius;
}

inline bool CapsuleIntersectsAABB(const Capsule& capsule, const AABB& aabb)
{
    if (VectorUtils::DistanceSquared(capsule.PointA, capsule.PointB) <= Constants::EPSILON)
    {
        return SphereIntersectsAABB(Sphere(capsule.PointA, capsule.Radius), aabb);
    }

    const float distanceSq = Detail::SegmentAABBDistanceSquared(capsule.PointA, capsule.PointB, aabb);
    return distanceSq <= capsule.Radius * capsule.Radius;
}

} // namespace NorvesLib::Math
