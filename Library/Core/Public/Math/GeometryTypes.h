#pragma once

#include "MathTypes.h"
#include "Matrix4x4.h"
#include "Vector3.h"
#include "VectorUtils.h"
#include <cfloat>
#include <cmath>

namespace NorvesLib::Math
{

struct Ray
{
    Vector3 Origin;
    Vector3 Direction;

    Ray()
        : Origin()
        , Direction()
    {
    }

    Ray(const Vector3& origin, const Vector3& direction)
        : Origin(origin)
        , Direction(direction)
    {
    }

    Vector3 PointAt(float t) const
    {
        return Origin + Direction * t;
    }
};

/**
 * @brief Hesse plane form: Normal dot X = Distance.
 *
 * SignedDistance >= 0 is the inside/front side. When converting from a
 * homogeneous clip-space plane (a,b,c,d) where ax + by + cz + d = 0,
 * use Normal = (a,b,c) / |n| and Distance = -d / |n|.
 */
struct Plane
{
    Vector3 Normal;
    float Distance;

    Plane()
        : Normal()
        , Distance(0.0f)
    {
    }

    Plane(const Vector3& normal, float distance)
        : Normal()
        , Distance(0.0f)
    {
        SetNormalized(normal, distance);
    }

    Plane(const Vector3& normal, const Vector3& pointOnPlane)
        : Plane(normal, VectorUtils::Dot(normal, pointOnPlane))
    {
    }

    Plane(const Vector3& p0, const Vector3& p1, const Vector3& p2)
        : Plane(VectorUtils::Cross(p1 - p0, p2 - p0), p0)
    {
    }

    float SignedDistance(const Vector3& point) const
    {
        return VectorUtils::Dot(Normal, point) - Distance;
    }

private:
    void SetNormalized(const Vector3& normal, float distance)
    {
        const float length = VectorUtils::Length(normal);
        if (length > Constants::EPSILON)
        {
            Normal = normal / length;
            Distance = distance / length;
            return;
        }

        Normal = normal;
        Distance = distance;
    }
};

struct Sphere
{
    Vector3 Center;
    float Radius;

    Sphere()
        : Center()
        , Radius(0.0f)
    {
    }

    Sphere(const Vector3& center, float radius)
        : Center(center)
        , Radius(radius)
    {
    }

    bool Contains(const Vector3& point) const
    {
        const Vector3 offset = point - Center;
        return VectorUtils::Dot(offset, offset) <= Radius * Radius;
    }
};

struct AABB
{
    Vector3 Min;
    Vector3 Max;

    AABB()
        : Min()
        , Max()
    {
    }

    AABB(const Vector3& min, const Vector3& max)
        : Min(min)
        , Max(max)
    {
    }

    Vector3 Center() const
    {
        return (Min + Max) * 0.5f;
    }

    Vector3 Extents() const
    {
        return Max - Min;
    }

    Vector3 HalfExtents() const
    {
        return Extents() * 0.5f;
    }

    bool Contains(const Vector3& point) const
    {
        return point.x >= Min.x && point.x <= Max.x
            && point.y >= Min.y && point.y <= Max.y
            && point.z >= Min.z && point.z <= Max.z;
    }

    void Expand(const Vector3& point)
    {
        Min = Vector3(std::fminf(Min.x, point.x), std::fminf(Min.y, point.y), std::fminf(Min.z, point.z));
        Max = Vector3(std::fmaxf(Max.x, point.x), std::fmaxf(Max.y, point.y), std::fmaxf(Max.z, point.z));
    }

    void Merge(const AABB& other)
    {
        Min = Vector3(std::fminf(Min.x, other.Min.x), std::fminf(Min.y, other.Min.y), std::fminf(Min.z, other.Min.z));
        Max = Vector3(std::fmaxf(Max.x, other.Max.x), std::fmaxf(Max.y, other.Max.y), std::fmaxf(Max.z, other.Max.z));
    }

    static AABB FromCenterExtents(const Vector3& center, const Vector3& halfExtents)
    {
        return AABB(center - halfExtents, center + halfExtents);
    }

    static AABB CreateInvalid()
    {
        return AABB(
            Vector3(FLT_MAX, FLT_MAX, FLT_MAX),
            Vector3(-FLT_MAX, -FLT_MAX, -FLT_MAX));
    }
};

struct OBB
{
    Vector3 Center;
    Vector3 HalfExtents;
    Vector3 Axes[3];

    OBB()
        : Center()
        , HalfExtents()
        , Axes{
            Vector3(1.0f, 0.0f, 0.0f),
            Vector3(0.0f, 1.0f, 0.0f),
            Vector3(0.0f, 0.0f, 1.0f)}
    {
    }

    OBB(const Vector3& center,
        const Vector3& halfExtents,
        const Vector3& axisX,
        const Vector3& axisY,
        const Vector3& axisZ)
        : Center(center)
        , HalfExtents(halfExtents)
        , Axes{axisX, axisY, axisZ}
    {
    }
};

enum class FrustumPlane
{
    Left,
    Right,
    Bottom,
    Top,
    Near,
    Far
};

struct Frustum
{
    Plane Planes[6];

    static Frustum FromViewProjection(
        const Matrix4x4& viewProj,
        ClipSpaceDepthRange depthRange = ClipSpaceDepthRange::ZeroToOne)
    {
        const Vector4 row0 = viewProj.GetRow(0);
        const Vector4 row1 = viewProj.GetRow(1);
        const Vector4 row2 = viewProj.GetRow(2);
        const Vector4 row3 = viewProj.GetRow(3);

        Frustum result;
        result.Planes[static_cast<int>(FrustumPlane::Left)] =
            CreatePlaneFromClipSpace(row3 + row0);
        result.Planes[static_cast<int>(FrustumPlane::Right)] =
            CreatePlaneFromClipSpace(row3 - row0);
        result.Planes[static_cast<int>(FrustumPlane::Bottom)] =
            CreatePlaneFromClipSpace(row3 + row1);
        result.Planes[static_cast<int>(FrustumPlane::Top)] =
            CreatePlaneFromClipSpace(row3 - row1);
        result.Planes[static_cast<int>(FrustumPlane::Near)] =
            CreatePlaneFromClipSpace(depthRange == ClipSpaceDepthRange::ZeroToOne ? row2 : row3 + row2);
        result.Planes[static_cast<int>(FrustumPlane::Far)] =
            CreatePlaneFromClipSpace(row3 - row2);
        return result;
    }

private:
    static Plane CreatePlaneFromClipSpace(const Vector4& plane)
    {
        return Plane(Vector3(plane.x, plane.y, plane.z), -plane.w);
    }
};

} // namespace NorvesLib::Math
