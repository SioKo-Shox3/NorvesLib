#include "Math/GeometryIntersection.h"

#include <cfloat>
#include <cmath>

namespace NorvesLib::Math
{
namespace
{
    constexpr float ContactEpsilon = Constants::EPSILON;

    float ClampToRange(float value, float minValue, float maxValue)
    {
        return std::fmaxf(minValue, std::fminf(value, maxValue));
    }

    Vector3 ToLocal(const OBB& box, const Vector3& point)
    {
        const Vector3 offset = point - box.Center;
        return Vector3(
            VectorUtils::Dot(offset, box.Axes[0]),
            VectorUtils::Dot(offset, box.Axes[1]),
            VectorUtils::Dot(offset, box.Axes[2]));
    }

    Vector3 ToWorld(const OBB& box, const Vector3& point)
    {
        return box.Center
            + box.Axes[0] * point.x
            + box.Axes[1] * point.y
            + box.Axes[2] * point.z;
    }

    Vector3 ClosestPointOnOBB(const OBB& box, const Vector3& point)
    {
        const Vector3 localPoint = ToLocal(box, point);
        return ToWorld(
            box,
            Vector3(
                ClampToRange(localPoint.x, -box.HalfExtents.x, box.HalfExtents.x),
                ClampToRange(localPoint.y, -box.HalfExtents.y, box.HalfExtents.y),
                ClampToRange(localPoint.z, -box.HalfExtents.z, box.HalfExtents.z)));
    }

    Vector3 GetInteriorNormal(const OBB& box, const Vector3& point)
    {
        const Vector3 localPoint = ToLocal(box, point);
        float bestDistance = localPoint.x + box.HalfExtents.x;
        Vector3 normal = box.Axes[0];

        const float distances[5] = {
            box.HalfExtents.x - localPoint.x,
            localPoint.y + box.HalfExtents.y,
            box.HalfExtents.y - localPoint.y,
            localPoint.z + box.HalfExtents.z,
            box.HalfExtents.z - localPoint.z};
        const Vector3 normals[5] = {
            -1.0f * box.Axes[0],
            box.Axes[1],
            -1.0f * box.Axes[1],
            box.Axes[2],
            -1.0f * box.Axes[2]};

        for (int index = 0; index < 5; ++index)
        {
            if (distances[index] < bestDistance)
            {
                bestDistance = distances[index];
                normal = normals[index];
            }
        }

        return normal;
    }

    float GetInteriorDistance(const OBB& box, const Vector3& point)
    {
        const Vector3 localPoint = ToLocal(box, point);
        float bestDistance = localPoint.x + box.HalfExtents.x;
        const float distances[5] = {
            box.HalfExtents.x - localPoint.x,
            localPoint.y + box.HalfExtents.y,
            box.HalfExtents.y - localPoint.y,
            localPoint.z + box.HalfExtents.z,
            box.HalfExtents.z - localPoint.z};

        for (int index = 0; index < 5; ++index)
        {
            if (distances[index] < bestDistance)
            {
                bestDistance = distances[index];
            }
        }

        return bestDistance;
    }

    Vector3 GetSupportPoint(const OBB& box, const Vector3& direction)
    {
        Vector3 result = box.Center;
        for (int axis = 0; axis < 3; ++axis)
        {
            const float projection = VectorUtils::Dot(box.Axes[axis], direction);
            const float sign = projection > ContactEpsilon ? 1.0f : projection < -ContactEpsilon ? -1.0f : 0.0f;
            const float extent = axis == 0 ? box.HalfExtents.x : axis == 1 ? box.HalfExtents.y : box.HalfExtents.z;
            result += box.Axes[axis] * (sign * extent);
        }

        return result;
    }

    Vector3 GetFallbackNormal(const Vector3& direction)
    {
        const float length = VectorUtils::Length(direction);
        if (length > ContactEpsilon)
        {
            return direction / length;
        }

        return Vector3(1.0f, 0.0f, 0.0f);
    }

    Vector3 GetPerpendicularNormal(const Vector3& segment)
    {
        const float lengthSquared = VectorUtils::Dot(segment, segment);
        if (lengthSquared <= ContactEpsilon)
        {
            return Vector3(1.0f, 0.0f, 0.0f);
        }

        const Vector3 candidateAxes[3] = {
            Vector3(1.0f, 0.0f, 0.0f),
            Vector3(0.0f, 1.0f, 0.0f),
            Vector3(0.0f, 0.0f, 1.0f)};
        for (int axis = 0; axis < 3; ++axis)
        {
            const Vector3 perpendicular = candidateAxes[axis]
                - segment * (VectorUtils::Dot(candidateAxes[axis], segment) / lengthSquared);
            const float perpendicularLength = VectorUtils::Length(perpendicular);
            if (perpendicularLength > ContactEpsilon)
            {
                return perpendicular / perpendicularLength;
            }
        }

        return Vector3(1.0f, 0.0f, 0.0f);
    }

    Vector3 ClosestPointOnSegment(const Vector3& point, const Vector3& start, const Vector3& end)
    {
        const Vector3 segment = end - start;
        const float lengthSquared = VectorUtils::Dot(segment, segment);
        if (lengthSquared <= ContactEpsilon)
        {
            return start;
        }

        const float t = ClampToRange(VectorUtils::Dot(point - start, segment) / lengthSquared, 0.0f, 1.0f);
        return start + segment * t;
    }

    void AddCandidate(float* candidates, int& candidateCount, float value)
    {
        if (value < 0.0f || value > 1.0f)
        {
            return;
        }

        for (int index = 0; index < candidateCount; ++index)
        {
            if (std::fabs(candidates[index] - value) <= ContactEpsilon)
            {
                return;
            }
        }

        candidates[candidateCount] = value;
        ++candidateCount;
    }

    void SortCandidates(float* candidates, int candidateCount)
    {
        for (int index = 1; index < candidateCount; ++index)
        {
            const float value = candidates[index];
            int sortedIndex = index - 1;
            while (sortedIndex >= 0 && candidates[sortedIndex] > value)
            {
                candidates[sortedIndex + 1] = candidates[sortedIndex];
                --sortedIndex;
            }

            candidates[sortedIndex + 1] = value;
        }
    }

    Vector3 ClosestPointOnAABB(const Vector3& point, const Vector3& halfExtents)
    {
        return Vector3(
            ClampToRange(point.x, -halfExtents.x, halfExtents.x),
            ClampToRange(point.y, -halfExtents.y, halfExtents.y),
            ClampToRange(point.z, -halfExtents.z, halfExtents.z));
    }

    void EvaluateSegmentAABBCandidate(
        const Vector3& start,
        const Vector3& direction,
        const Vector3& halfExtents,
        float t,
        float& inOutDistanceSquared,
        Vector3& outSegmentPoint,
        Vector3& outBoxPoint)
    {
        const Vector3 segmentPoint = start + direction * t;
        const Vector3 boxPoint = ClosestPointOnAABB(segmentPoint, halfExtents);
        const float distanceSquared = VectorUtils::DistanceSquared(segmentPoint, boxPoint);
        if (distanceSquared < inOutDistanceSquared)
        {
            inOutDistanceSquared = distanceSquared;
            outSegmentPoint = segmentPoint;
            outBoxPoint = boxPoint;
        }
    }

    void ClosestPointsSegmentOBB(
        const Capsule& capsule,
        const OBB& box,
        Vector3& outSegmentPoint,
        Vector3& outBoxPoint)
    {
        const Vector3 start = ToLocal(box, capsule.PointA);
        const Vector3 end = ToLocal(box, capsule.PointB);
        const Vector3 direction = end - start;
        float candidates[8];
        int candidateCount = 0;
        AddCandidate(candidates, candidateCount, 0.0f);
        AddCandidate(candidates, candidateCount, 1.0f);

        const float startComponents[3] = {start.x, start.y, start.z};
        const float directionComponents[3] = {direction.x, direction.y, direction.z};
        const float extents[3] = {box.HalfExtents.x, box.HalfExtents.y, box.HalfExtents.z};
        for (int axis = 0; axis < 3; ++axis)
        {
            if (std::fabs(directionComponents[axis]) <= ContactEpsilon)
            {
                continue;
            }

            AddCandidate(candidates, candidateCount, (-extents[axis] - startComponents[axis]) / directionComponents[axis]);
            AddCandidate(candidates, candidateCount, (extents[axis] - startComponents[axis]) / directionComponents[axis]);
        }

        SortCandidates(candidates, candidateCount);

        float distanceSquared = FLT_MAX;
        Vector3 localSegmentPoint;
        Vector3 localBoxPoint;
        for (int index = 0; index < candidateCount; ++index)
        {
            EvaluateSegmentAABBCandidate(
                start,
                direction,
                box.HalfExtents,
                candidates[index],
                distanceSquared,
                localSegmentPoint,
                localBoxPoint);
        }

        for (int index = 0; index + 1 < candidateCount; ++index)
        {
            const float intervalStart = candidates[index];
            const float intervalEnd = candidates[index + 1];
            const float midpoint = (intervalStart + intervalEnd) * 0.5f;
            const Vector3 midpointPoint = start + direction * midpoint;
            float numerator = 0.0f;
            float denominator = 0.0f;

            const float pointComponents[3] = {midpointPoint.x, midpointPoint.y, midpointPoint.z};
            for (int axis = 0; axis < 3; ++axis)
            {
                if (pointComponents[axis] < -extents[axis])
                {
                    const float offset = startComponents[axis] + extents[axis];
                    numerator += directionComponents[axis] * offset;
                    denominator += directionComponents[axis] * directionComponents[axis];
                }
                else if (pointComponents[axis] > extents[axis])
                {
                    const float offset = startComponents[axis] - extents[axis];
                    numerator += directionComponents[axis] * offset;
                    denominator += directionComponents[axis] * directionComponents[axis];
                }
            }

            if (denominator > ContactEpsilon)
            {
                const float t = -numerator / denominator;
                if (t > intervalStart && t < intervalEnd)
                {
                    EvaluateSegmentAABBCandidate(
                        start,
                        direction,
                        box.HalfExtents,
                        t,
                        distanceSquared,
                        localSegmentPoint,
                        localBoxPoint);
                }
            }
        }

        outSegmentPoint = ToWorld(box, localSegmentPoint);
        outBoxPoint = ToWorld(box, localBoxPoint);
    }

    float ProjectRadius(const OBB& box, const Vector3& axis)
    {
        return box.HalfExtents.x * std::fabs(VectorUtils::Dot(box.Axes[0], axis))
            + box.HalfExtents.y * std::fabs(VectorUtils::Dot(box.Axes[1], axis))
            + box.HalfExtents.z * std::fabs(VectorUtils::Dot(box.Axes[2], axis));
    }

    bool TestOBBAxis(
        const OBB& a,
        const OBB& b,
        const Vector3& centerOffset,
        const Vector3& axis,
        float& inOutDepth,
        Vector3& inOutNormal)
    {
        const float axisLength = VectorUtils::Length(axis);
        if (axisLength <= ContactEpsilon)
        {
            return true;
        }

        const Vector3 normalAxis = axis / axisLength;
        const float signedDistance = VectorUtils::Dot(centerOffset, normalAxis);
        const float depth = ProjectRadius(a, normalAxis) + ProjectRadius(b, normalAxis) - std::fabs(signedDistance);
        if (depth < 0.0f)
        {
            return false;
        }

        if (depth < inOutDepth)
        {
            inOutDepth = depth;
            inOutNormal = signedDistance < 0.0f ? -1.0f * normalAxis : normalAxis;
        }

        return true;
    }

    Vector3 GetCapsuleSupportPoint(const Capsule& capsule, const Vector3& direction)
    {
        const float pointAProjection = VectorUtils::Dot(capsule.PointA, direction);
        const float pointBProjection = VectorUtils::Dot(capsule.PointB, direction);
        const Vector3 endpoint = pointBProjection > pointAProjection ? capsule.PointB : capsule.PointA;
        return endpoint + direction * capsule.Radius;
    }

    bool TestCapsuleOBBAxis(
        const Capsule& capsule,
        const OBB& box,
        const Vector3& centerOffset,
        const Vector3& axis,
        float& inOutDepth,
        Vector3& inOutNormal)
    {
        const float axisLength = VectorUtils::Length(axis);
        if (axisLength <= ContactEpsilon)
        {
            return true;
        }

        const Vector3 normalAxis = axis / axisLength;
        const Vector3 segment = capsule.PointB - capsule.PointA;
        const float capsuleRadius = capsule.Radius + std::fabs(VectorUtils::Dot(segment, normalAxis)) * 0.5f;
        const float boxRadius = ProjectRadius(box, normalAxis);
        const float signedDistance = VectorUtils::Dot(centerOffset, normalAxis);
        const float depth = capsuleRadius + boxRadius - std::fabs(signedDistance);
        if (depth < 0.0f)
        {
            return false;
        }

        if (depth < inOutDepth)
        {
            inOutDepth = depth;
            inOutNormal = signedDistance < 0.0f ? -1.0f * normalAxis : normalAxis;
        }

        return true;
    }

    void SetContact(
        const Vector3& normal,
        float depth,
        const Vector3& witnessA,
        const Vector3& witnessB,
        GeometryContact& outContact)
    {
        outContact.Normal = normal;
        outContact.Depth = std::fmaxf(0.0f, depth);
        outContact.Point = (witnessA + witnessB) * 0.5f;
    }
}

bool ComputeContact(const Sphere& a, const Sphere& b, GeometryContact& outContact)
{
    const Vector3 offset = b.Center - a.Center;
    const float distance = VectorUtils::Length(offset);
    const float radius = a.Radius + b.Radius;
    if (distance > radius)
    {
        return false;
    }

    const Vector3 normal = GetFallbackNormal(offset);
    SetContact(
        normal,
        radius - distance,
        a.Center + normal * a.Radius,
        b.Center - normal * b.Radius,
        outContact);
    return true;
}

bool ComputeContact(const Sphere& a, const OBB& b, GeometryContact& outContact)
{
    const Vector3 closestPoint = ClosestPointOnOBB(b, a.Center);
    const Vector3 offset = closestPoint - a.Center;
    const float distance = VectorUtils::Length(offset);
    if (distance > a.Radius)
    {
        return false;
    }

    if (distance > ContactEpsilon)
    {
        const Vector3 normal = offset / distance;
        SetContact(normal, a.Radius - distance, a.Center + normal * a.Radius, closestPoint, outContact);
        return true;
    }

    const Vector3 normal = GetInteriorNormal(b, a.Center);
    SetContact(
        normal,
        a.Radius + GetInteriorDistance(b, a.Center),
        a.Center + normal * a.Radius,
        GetSupportPoint(b, -1.0f * normal),
        outContact);
    return true;
}

bool ComputeContact(const OBB& a, const OBB& b, GeometryContact& outContact)
{
    const Vector3 centerOffset = b.Center - a.Center;
    float depth = FLT_MAX;
    Vector3 normal(1.0f, 0.0f, 0.0f);

    for (int axis = 0; axis < 3; ++axis)
    {
        if (!TestOBBAxis(a, b, centerOffset, a.Axes[axis], depth, normal))
        {
            return false;
        }
    }

    for (int axis = 0; axis < 3; ++axis)
    {
        if (!TestOBBAxis(a, b, centerOffset, b.Axes[axis], depth, normal))
        {
            return false;
        }
    }

    for (int aAxis = 0; aAxis < 3; ++aAxis)
    {
        for (int bAxis = 0; bAxis < 3; ++bAxis)
        {
            if (!TestOBBAxis(a, b, centerOffset, VectorUtils::Cross(a.Axes[aAxis], b.Axes[bAxis]), depth, normal))
            {
                return false;
            }
        }
    }

    SetContact(normal, depth, GetSupportPoint(a, normal), GetSupportPoint(b, -1.0f * normal), outContact);
    return true;
}

bool ComputeContact(const Capsule& a, const Sphere& b, GeometryContact& outContact)
{
    const Vector3 capsulePoint = ClosestPointOnSegment(b.Center, a.PointA, a.PointB);
    const Vector3 offset = b.Center - capsulePoint;
    const float distance = VectorUtils::Length(offset);
    const float radius = a.Radius + b.Radius;
    if (distance > radius)
    {
        return false;
    }

    const Vector3 normal = distance > ContactEpsilon ? offset / distance : GetPerpendicularNormal(a.PointB - a.PointA);
    SetContact(
        normal,
        radius - distance,
        capsulePoint + normal * a.Radius,
        b.Center - normal * b.Radius,
        outContact);
    return true;
}

bool ComputeContact(const Capsule& a, const OBB& b, GeometryContact& outContact)
{
    Vector3 capsulePoint;
    Vector3 boxPoint;
    ClosestPointsSegmentOBB(a, b, capsulePoint, boxPoint);
    const Vector3 offset = boxPoint - capsulePoint;
    const float distance = VectorUtils::Length(offset);
    if (distance > a.Radius)
    {
        return false;
    }

    if (distance > ContactEpsilon)
    {
        const Vector3 normal = offset / distance;
        SetContact(normal, a.Radius - distance, capsulePoint + normal * a.Radius, boxPoint, outContact);
        return true;
    }

    const Vector3 centerOffset = b.Center - (a.PointA + a.PointB) * 0.5f;
    const Vector3 segment = a.PointB - a.PointA;
    float depth = FLT_MAX;
    Vector3 normal(1.0f, 0.0f, 0.0f);

    for (int axis = 0; axis < 3; ++axis)
    {
        if (!TestCapsuleOBBAxis(a, b, centerOffset, b.Axes[axis], depth, normal))
        {
            return false;
        }
    }

    for (int axis = 0; axis < 3; ++axis)
    {
        if (!TestCapsuleOBBAxis(a, b, centerOffset, VectorUtils::Cross(segment, b.Axes[axis]), depth, normal))
        {
            return false;
        }
    }

    SetContact(normal, depth, GetCapsuleSupportPoint(a, normal), GetSupportPoint(b, -1.0f * normal), outContact);
    return true;
}

bool ComputeContact(const Capsule& a, const Capsule& b, GeometryContact& outContact)
{
    const Vector3 d1 = a.PointB - a.PointA;
    const Vector3 d2 = b.PointB - b.PointA;
    const Vector3 offset = a.PointA - b.PointA;
    const float aLengthSquared = VectorUtils::Dot(d1, d1);
    const float bLengthSquared = VectorUtils::Dot(d2, d2);
    const float dot = VectorUtils::Dot(d1, d2);
    const float d1Offset = VectorUtils::Dot(d1, offset);
    const float d2Offset = VectorUtils::Dot(d2, offset);
    float s = 0.0f;
    float t = 0.0f;

    if (aLengthSquared <= ContactEpsilon && bLengthSquared <= ContactEpsilon)
    {
        s = 0.0f;
        t = 0.0f;
    }
    else if (aLengthSquared <= ContactEpsilon)
    {
        t = ClampToRange(d2Offset / bLengthSquared, 0.0f, 1.0f);
    }
    else if (bLengthSquared <= ContactEpsilon)
    {
        s = ClampToRange(-d1Offset / aLengthSquared, 0.0f, 1.0f);
    }
    else
    {
        const float denominator = aLengthSquared * bLengthSquared - dot * dot;
        if (denominator > ContactEpsilon)
        {
            s = ClampToRange((dot * d2Offset - d1Offset * bLengthSquared) / denominator, 0.0f, 1.0f);
        }

        const float tNumerator = dot * s + d2Offset;
        if (tNumerator < 0.0f)
        {
            t = 0.0f;
            s = ClampToRange(-d1Offset / aLengthSquared, 0.0f, 1.0f);
        }
        else if (tNumerator > bLengthSquared)
        {
            t = 1.0f;
            s = ClampToRange((dot - d1Offset) / aLengthSquared, 0.0f, 1.0f);
        }
        else
        {
            t = tNumerator / bLengthSquared;
        }
    }

    const Vector3 pointA = a.PointA + d1 * s;
    const Vector3 pointB = b.PointA + d2 * t;
    const Vector3 pointOffset = pointB - pointA;
    const float distance = VectorUtils::Length(pointOffset);
    const float radius = a.Radius + b.Radius;
    if (distance > radius)
    {
        return false;
    }

    Vector3 normal;
    if (distance > ContactEpsilon)
    {
        normal = pointOffset / distance;
    }
    else if (aLengthSquared <= ContactEpsilon && bLengthSquared <= ContactEpsilon)
    {
        normal = Vector3(1.0f, 0.0f, 0.0f);
    }
    else if (aLengthSquared > ContactEpsilon && bLengthSquared > ContactEpsilon)
    {
        const Vector3 cross = VectorUtils::Cross(d1, d2);
        normal = VectorUtils::Length(cross) > ContactEpsilon ? GetFallbackNormal(cross) : GetPerpendicularNormal(d1);
    }
    else
    {
        normal = GetPerpendicularNormal(aLengthSquared > ContactEpsilon ? d1 : d2);
    }
    SetContact(
        normal,
        radius - distance,
        pointA + normal * a.Radius,
        pointB - normal * b.Radius,
        outContact);
    return true;
}

} // namespace NorvesLib::Math
