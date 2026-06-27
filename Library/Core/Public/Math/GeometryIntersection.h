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

} // namespace NorvesLib::Math
