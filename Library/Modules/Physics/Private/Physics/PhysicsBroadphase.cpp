#include "Physics/PhysicsBroadphase.h"

#include "Math/GeometryIntersection.h"
#include "Math/VectorUtils.h"

#include <cmath>

namespace NorvesLib::Modules::Physics
{
    namespace
    {
        struct SweepEndpoint
        {
            float Coordinate = 0.0f;
            Core::Scene::ColliderHandle Collider;
            bool bIsMin = false;
        };

        bool IsHandleLess(const Core::Scene::ColliderHandle& left, const Core::Scene::ColliderHandle& right)
        {
            return left < right;
        }

        bool IsPairLess(const PhysicsCandidatePair& left, const PhysicsCandidatePair& right)
        {
            return IsHandleLess(left.First, right.First)
                || (left.First == right.First && IsHandleLess(left.Second, right.Second));
        }

        bool IsEndpointLess(const SweepEndpoint& left, const SweepEndpoint& right)
        {
            if (left.Coordinate != right.Coordinate)
            {
                return left.Coordinate < right.Coordinate;
            }
            if (left.bIsMin != right.bIsMin)
            {
                return left.bIsMin;
            }
            return IsHandleLess(left.Collider, right.Collider);
        }

        void ReverseContact(Math::GeometryContact& contact)
        {
            contact.Normal *= -1.0f;
        }

        bool ComputeOverlap(
            const Math::Sphere& query,
            const PhysicsShapeProxy& proxy,
            Math::GeometryContact& outContact)
        {
            if (proxy.Shape == EPhysicsProxyShape::Sphere)
            {
                return Math::ComputeContact(query, proxy.Sphere, outContact);
            }
            if (proxy.Shape == EPhysicsProxyShape::Box)
            {
                return Math::ComputeContact(query, proxy.Box, outContact);
            }

            ReverseContact(outContact);
            if (!Math::ComputeContact(proxy.Capsule, query, outContact))
            {
                return false;
            }
            ReverseContact(outContact);
            return true;
        }

        bool ComputeOverlap(
            const Math::OBB& query,
            const PhysicsShapeProxy& proxy,
            Math::GeometryContact& outContact)
        {
            if (proxy.Shape == EPhysicsProxyShape::Sphere)
            {
                if (!Math::ComputeContact(proxy.Sphere, query, outContact))
                {
                    return false;
                }
                ReverseContact(outContact);
                return true;
            }
            if (proxy.Shape == EPhysicsProxyShape::Box)
            {
                return Math::ComputeContact(query, proxy.Box, outContact);
            }

            if (!Math::ComputeContact(proxy.Capsule, query, outContact))
            {
                return false;
            }
            ReverseContact(outContact);
            return true;
        }

        bool ComputeOverlap(
            const Math::Capsule& query,
            const PhysicsShapeProxy& proxy,
            Math::GeometryContact& outContact)
        {
            if (proxy.Shape == EPhysicsProxyShape::Sphere)
            {
                return Math::ComputeContact(query, proxy.Sphere, outContact);
            }
            if (proxy.Shape == EPhysicsProxyShape::Box)
            {
                return Math::ComputeContact(query, proxy.Box, outContact);
            }
            return Math::ComputeContact(query, proxy.Capsule, outContact);
        }

        bool IsPointInOBB(const Math::Vector3& point, const Math::OBB& box)
        {
            const Math::Vector3 offset = point - box.Center;
            return std::fabs(Math::VectorUtils::Dot(offset, box.Axes[0])) <= box.HalfExtents.x
                && std::fabs(Math::VectorUtils::Dot(offset, box.Axes[1])) <= box.HalfExtents.y
                && std::fabs(Math::VectorUtils::Dot(offset, box.Axes[2])) <= box.HalfExtents.z;
        }

        bool IsPointInCapsule(const Math::Vector3& point, const Math::Capsule& capsule)
        {
            const Math::Vector3 segment = capsule.PointB - capsule.PointA;
            const float segmentLengthSquared = Math::VectorUtils::Dot(segment, segment);
            float parameter = 0.0f;
            if (segmentLengthSquared > Math::Constants::EPSILON)
            {
                parameter = Math::VectorUtils::Dot(point - capsule.PointA, segment) / segmentLengthSquared;
                parameter = std::fmaxf(0.0f, std::fminf(parameter, 1.0f));
            }
            const Math::Vector3 closestPoint = capsule.PointA + segment * parameter;
            return Math::VectorUtils::DistanceSquared(point, closestPoint) <= capsule.Radius * capsule.Radius;
        }

        bool RaycastCapsule(const Math::Ray& ray, const Math::Capsule& capsule, float& outDistance)
        {
            if (IsPointInCapsule(ray.Origin, capsule))
            {
                outDistance = 0.0f;
                return true;
            }

            const Math::Vector3 segment = capsule.PointB - capsule.PointA;
            const Math::Vector3 originOffset = ray.Origin - capsule.PointA;
            const float segmentLengthSquared = Math::VectorUtils::Dot(segment, segment);
            if (segmentLengthSquared <= Math::Constants::EPSILON)
            {
                return Math::RayIntersectsSphere(ray, Math::Sphere(capsule.PointA, capsule.Radius), outDistance);
            }

            const float segmentDirection = Math::VectorUtils::Dot(segment, ray.Direction);
            const float segmentOrigin = Math::VectorUtils::Dot(segment, originOffset);
            const float rayOrigin = Math::VectorUtils::Dot(ray.Direction, originOffset);
            const float originLengthSquared = Math::VectorUtils::Dot(originOffset, originOffset);
            const float quadraticA = segmentLengthSquared - segmentDirection * segmentDirection;
            const float quadraticB = segmentLengthSquared * rayOrigin - segmentOrigin * segmentDirection;
            const float quadraticC = segmentLengthSquared * originLengthSquared - segmentOrigin * segmentOrigin
                - capsule.Radius * capsule.Radius * segmentLengthSquared;
            const float discriminant = quadraticB * quadraticB - quadraticA * quadraticC;

            if (quadraticA > Math::Constants::EPSILON && discriminant >= 0.0f)
            {
                const float distance = (-quadraticB - std::sqrt(discriminant)) / quadraticA;
                const float segmentParameter = segmentOrigin + distance * segmentDirection;
                if (distance >= 0.0f && segmentParameter >= 0.0f && segmentParameter <= segmentLengthSquared)
                {
                    outDistance = distance;
                    return true;
                }
            }

            float firstDistance = 0.0f;
            float secondDistance = 0.0f;
            const bool bFirst = Math::RayIntersectsSphere(ray, Math::Sphere(capsule.PointA, capsule.Radius), firstDistance);
            const bool bSecond = Math::RayIntersectsSphere(ray, Math::Sphere(capsule.PointB, capsule.Radius), secondDistance);
            if (!bFirst && !bSecond)
            {
                return false;
            }
            outDistance = !bSecond || (bFirst && firstDistance <= secondDistance) ? firstDistance : secondDistance;
            return true;
        }

        bool RaycastProxy(const Math::Ray& ray, const PhysicsShapeProxy& proxy, float& outDistance)
        {
            if (proxy.Shape == EPhysicsProxyShape::Sphere)
            {
                if (proxy.Sphere.Contains(ray.Origin))
                {
                    outDistance = 0.0f;
                    return true;
                }
                return Math::RayIntersectsSphere(ray, proxy.Sphere, outDistance);
            }
            if (proxy.Shape == EPhysicsProxyShape::Box)
            {
                if (IsPointInOBB(ray.Origin, proxy.Box))
                {
                    outDistance = 0.0f;
                    return true;
                }
                return Math::RayIntersectsOBB(ray, proxy.Box, outDistance);
            }
            return RaycastCapsule(ray, proxy.Capsule, outDistance);
        }

        Math::Vector3 CalculateRayNormal(const Math::Ray& ray, const PhysicsShapeProxy& proxy, float distance)
        {
            if (distance == 0.0f)
            {
                return Math::Vector3();
            }
            const Math::Vector3 point = ray.PointAt(distance);
            if (proxy.Shape == EPhysicsProxyShape::Sphere)
            {
                return Math::VectorUtils::Normalize(point - proxy.Sphere.Center);
            }
            if (proxy.Shape == EPhysicsProxyShape::Capsule)
            {
                const Math::Vector3 segment = proxy.Capsule.PointB - proxy.Capsule.PointA;
                const float segmentLengthSquared = Math::VectorUtils::Dot(segment, segment);
                float parameter = 0.0f;
                if (segmentLengthSquared > Math::Constants::EPSILON)
                {
                    parameter = Math::VectorUtils::Dot(point - proxy.Capsule.PointA, segment) / segmentLengthSquared;
                    parameter = std::fmaxf(0.0f, std::fminf(parameter, 1.0f));
                }
                return Math::VectorUtils::Normalize(point - (proxy.Capsule.PointA + segment * parameter));
            }

            const Math::Vector3 local(
                Math::VectorUtils::Dot(point - proxy.Box.Center, proxy.Box.Axes[0]),
                Math::VectorUtils::Dot(point - proxy.Box.Center, proxy.Box.Axes[1]),
                Math::VectorUtils::Dot(point - proxy.Box.Center, proxy.Box.Axes[2]));
            const float distances[3] = {
                std::fabs(std::fabs(local.x) - proxy.Box.HalfExtents.x),
                std::fabs(std::fabs(local.y) - proxy.Box.HalfExtents.y),
                std::fabs(std::fabs(local.z) - proxy.Box.HalfExtents.z)};
            int normalAxis = 0;
            if (distances[1] < distances[normalAxis])
            {
                normalAxis = 1;
            }
            if (distances[2] < distances[normalAxis])
            {
                normalAxis = 2;
            }
            const float component = normalAxis == 0 ? local.x : (normalAxis == 1 ? local.y : local.z);
            return proxy.Box.Axes[normalAxis] * (component < 0.0f ? -1.0f : 1.0f);
        }

        void AppendOverlapHit(
            const PhysicsShapeProxy& proxy,
            const Math::GeometryContact& contact,
            Core::Container::VariableArray<Core::Scene::PhysicsOverlapHit>& outHits)
        {
            Core::Scene::PhysicsOverlapHit hit;
            hit.Collider = proxy.Collider;
            hit.Body = proxy.Body;
            hit.Entity = proxy.Entity;
            hit.bHasEntity = proxy.bHasEntity;
            hit.Contact = contact;
            outHits.push_back(hit);
        }
    } // namespace

    void PhysicsBroadphase::SetProxies(Core::Container::VariableArray<PhysicsShapeProxy> proxies)
    {
        for (PhysicsShapeProxy& proxy : proxies)
        {
            proxy.Bounds = CalculateBounds(proxy);
        }

        for (size_t index = 1; index < proxies.size(); ++index)
        {
            PhysicsShapeProxy value = proxies[index];
            size_t insertIndex = index;
            while (insertIndex > 0 && IsHandleLess(value.Collider, proxies[insertIndex - 1].Collider))
            {
                proxies[insertIndex] = proxies[insertIndex - 1];
                --insertIndex;
            }
            proxies[insertIndex] = value;
        }

        m_Proxies = std::move(proxies);
        BuildCandidatePairs();
    }

    const Core::Container::VariableArray<PhysicsShapeProxy>& PhysicsBroadphase::GetProxies() const
    {
        return m_Proxies;
    }

    const Core::Container::VariableArray<PhysicsCandidatePair>& PhysicsBroadphase::GetCandidatePairs() const
    {
        return m_CandidatePairs;
    }

    bool PhysicsBroadphase::Raycast(
        const Math::Ray& ray,
        float maxDistance,
        Core::Scene::PhysicsRaycastHit& outHit) const
    {
        bool bFound = false;
        float closestDistance = maxDistance;
        for (const PhysicsShapeProxy& proxy : m_Proxies)
        {
            float distance = 0.0f;
            if (!RaycastProxy(ray, proxy, distance) || distance > maxDistance)
            {
                continue;
            }
            if (bFound && distance > closestDistance)
            {
                continue;
            }

            bFound = true;
            closestDistance = distance;
            outHit.Collider = proxy.Collider;
            outHit.Body = proxy.Body;
            outHit.Entity = proxy.Entity;
            outHit.bHasEntity = proxy.bHasEntity;
            outHit.Distance = distance;
            outHit.Point = ray.PointAt(distance);
            outHit.Normal = CalculateRayNormal(ray, proxy, distance);
        }
        return bFound;
    }

    void PhysicsBroadphase::OverlapSphere(
        const Math::Sphere& sphere,
        Core::Container::VariableArray<Core::Scene::PhysicsOverlapHit>& outHits) const
    {
        for (const PhysicsShapeProxy& proxy : m_Proxies)
        {
            Math::GeometryContact contact;
            if (ComputeOverlap(sphere, proxy, contact))
            {
                AppendOverlapHit(proxy, contact, outHits);
            }
        }
    }

    void PhysicsBroadphase::OverlapBox(
        const Math::OBB& box,
        Core::Container::VariableArray<Core::Scene::PhysicsOverlapHit>& outHits) const
    {
        for (const PhysicsShapeProxy& proxy : m_Proxies)
        {
            Math::GeometryContact contact;
            if (ComputeOverlap(box, proxy, contact))
            {
                AppendOverlapHit(proxy, contact, outHits);
            }
        }
    }

    void PhysicsBroadphase::OverlapCapsule(
        const Math::Capsule& capsule,
        Core::Container::VariableArray<Core::Scene::PhysicsOverlapHit>& outHits) const
    {
        for (const PhysicsShapeProxy& proxy : m_Proxies)
        {
            Math::GeometryContact contact;
            if (ComputeOverlap(capsule, proxy, contact))
            {
                AppendOverlapHit(proxy, contact, outHits);
            }
        }
    }

    Math::AABB PhysicsBroadphase::CalculateBounds(const PhysicsShapeProxy& proxy)
    {
        if (proxy.Shape == EPhysicsProxyShape::Sphere)
        {
            return Math::AABB::FromCenterExtents(proxy.Sphere.Center, Math::Vector3(proxy.Sphere.Radius));
        }
        if (proxy.Shape == EPhysicsProxyShape::Capsule)
        {
            Math::AABB bounds = Math::AABB::FromCenterExtents(
                proxy.Capsule.PointA,
                Math::Vector3(proxy.Capsule.Radius));
            bounds.Merge(Math::AABB::FromCenterExtents(
                proxy.Capsule.PointB,
                Math::Vector3(proxy.Capsule.Radius)));
            return bounds;
        }

        const Math::Vector3 extents(
            std::fabs(proxy.Box.Axes[0].x) * proxy.Box.HalfExtents.x
                + std::fabs(proxy.Box.Axes[1].x) * proxy.Box.HalfExtents.y
                + std::fabs(proxy.Box.Axes[2].x) * proxy.Box.HalfExtents.z,
            std::fabs(proxy.Box.Axes[0].y) * proxy.Box.HalfExtents.x
                + std::fabs(proxy.Box.Axes[1].y) * proxy.Box.HalfExtents.y
                + std::fabs(proxy.Box.Axes[2].y) * proxy.Box.HalfExtents.z,
            std::fabs(proxy.Box.Axes[0].z) * proxy.Box.HalfExtents.x
                + std::fabs(proxy.Box.Axes[1].z) * proxy.Box.HalfExtents.y
                + std::fabs(proxy.Box.Axes[2].z) * proxy.Box.HalfExtents.z);
        return Math::AABB::FromCenterExtents(proxy.Box.Center, extents);
    }

    bool PhysicsBroadphase::ComputeContact(
        const PhysicsShapeProxy& first,
        const PhysicsShapeProxy& second,
        Math::GeometryContact& outContact)
    {
        if (first.Shape == EPhysicsProxyShape::Sphere)
        {
            if (second.Shape == EPhysicsProxyShape::Sphere)
            {
                return Math::ComputeContact(first.Sphere, second.Sphere, outContact);
            }
            if (second.Shape == EPhysicsProxyShape::Box)
            {
                return Math::ComputeContact(first.Sphere, second.Box, outContact);
            }
            if (!Math::ComputeContact(second.Capsule, first.Sphere, outContact))
            {
                return false;
            }
            ReverseContact(outContact);
            return true;
        }
        if (first.Shape == EPhysicsProxyShape::Box)
        {
            if (second.Shape == EPhysicsProxyShape::Sphere)
            {
                if (!Math::ComputeContact(second.Sphere, first.Box, outContact))
                {
                    return false;
                }
                ReverseContact(outContact);
                return true;
            }
            if (second.Shape == EPhysicsProxyShape::Box)
            {
                return Math::ComputeContact(first.Box, second.Box, outContact);
            }
            if (!Math::ComputeContact(second.Capsule, first.Box, outContact))
            {
                return false;
            }
            ReverseContact(outContact);
            return true;
        }
        if (second.Shape == EPhysicsProxyShape::Sphere)
        {
            return Math::ComputeContact(first.Capsule, second.Sphere, outContact);
        }
        if (second.Shape == EPhysicsProxyShape::Box)
        {
            return Math::ComputeContact(first.Capsule, second.Box, outContact);
        }
        return Math::ComputeContact(first.Capsule, second.Capsule, outContact);
    }

    void PhysicsBroadphase::BuildCandidatePairs()
    {
        Core::Container::VariableArray<SweepEndpoint> endpoints;
        endpoints.reserve(m_Proxies.size() * 2);
        for (const PhysicsShapeProxy& proxy : m_Proxies)
        {
            endpoints.push_back(SweepEndpoint{proxy.Bounds.Min.x, proxy.Collider, true});
            endpoints.push_back(SweepEndpoint{proxy.Bounds.Max.x, proxy.Collider, false});
        }

        for (size_t index = 1; index < endpoints.size(); ++index)
        {
            SweepEndpoint value = endpoints[index];
            size_t insertIndex = index;
            while (insertIndex > 0 && IsEndpointLess(value, endpoints[insertIndex - 1]))
            {
                endpoints[insertIndex] = endpoints[insertIndex - 1];
                --insertIndex;
            }
            endpoints[insertIndex] = value;
        }

        m_CandidatePairs.clear();
        Core::Container::VariableArray<Core::Scene::ColliderHandle> active;
        for (const SweepEndpoint& endpoint : endpoints)
        {
            if (endpoint.bIsMin)
            {
                for (const Core::Scene::ColliderHandle& other : active)
                {
                    PhysicsCandidatePair pair;
                    pair.First = IsHandleLess(endpoint.Collider, other) ? endpoint.Collider : other;
                    pair.Second = IsHandleLess(endpoint.Collider, other) ? other : endpoint.Collider;
                    bool bDuplicate = false;
                    for (const PhysicsCandidatePair& existing : m_CandidatePairs)
                    {
                        if (existing.First == pair.First && existing.Second == pair.Second)
                        {
                            bDuplicate = true;
                            break;
                        }
                    }
                    if (!bDuplicate)
                    {
                        m_CandidatePairs.push_back(pair);
                    }
                }
                active.push_back(endpoint.Collider);
                continue;
            }

            for (size_t index = 0; index < active.size(); ++index)
            {
                if (active[index] == endpoint.Collider)
                {
                    active[index] = active.back();
                    active.pop_back();
                    break;
                }
            }
        }

        for (size_t index = 1; index < m_CandidatePairs.size(); ++index)
        {
            PhysicsCandidatePair value = m_CandidatePairs[index];
            size_t insertIndex = index;
            while (insertIndex > 0 && IsPairLess(value, m_CandidatePairs[insertIndex - 1]))
            {
                m_CandidatePairs[insertIndex] = m_CandidatePairs[insertIndex - 1];
                --insertIndex;
            }
            m_CandidatePairs[insertIndex] = value;
        }
    }
} // namespace NorvesLib::Modules::Physics
