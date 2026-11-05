#include "Physics/PhysicsBroadphase.h"

#include "Math/GeometryIntersection.h"
#include "Math/VectorUtils.h"
#include "Rendering/Terrain/TerrainHeightField.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cfloat>
#include <limits>
#include <type_traits>

// 高さ場の依存ヘッダーから入る Windows の旧マクロと、既存の距離変数との衝突を避ける。
#ifdef near
#undef near
#endif
#ifdef far
#undef far
#endif

namespace NorvesLib::Modules::Physics
{
    // 木の形と形状値を共有する。組のフィルターは毎回のプロキシから読む。
    struct PhysicsStaticTreeSnapshot
    {
        struct ProxySignature
        {
            Core::Scene::ColliderHandle Collider;
            EPhysicsProxyShape Shape;
            Math::Sphere Sphere;
            Math::OBB Box;
            Math::Capsule Capsule;
            Math::AABB Bounds;

            bool operator==(const ProxySignature& other) const
            {
                if (Collider != other.Collider || Shape != other.Shape
                    || Bounds.Min != other.Bounds.Min || Bounds.Max != other.Bounds.Max)
                {
                    return false;
                }
                switch (Shape)
                {
                case EPhysicsProxyShape::Sphere:
                    return Sphere.Center == other.Sphere.Center && Sphere.Radius == other.Sphere.Radius;
                case EPhysicsProxyShape::Box:
                    return Box.Center == other.Box.Center && Box.HalfExtents == other.Box.HalfExtents
                        && Box.Axes[0] == other.Box.Axes[0] && Box.Axes[1] == other.Box.Axes[1]
                        && Box.Axes[2] == other.Box.Axes[2];
                case EPhysicsProxyShape::Capsule:
                    return Capsule.PointA == other.Capsule.PointA && Capsule.PointB == other.Capsule.PointB
                        && Capsule.Radius == other.Capsule.Radius;
                case EPhysicsProxyShape::HeightField:
                    return false;
                }
                return false;
            }
        };

        Core::Container::VariableArray<ProxySignature> Signatures;
        StaticAabbTree Tree;
    };

    namespace
    {
        struct SweepEndpoint
        {
            float Coordinate = 0.0f;
            size_t ProxyIndex = 0;
            Core::Scene::ColliderHandle Collider;
            bool bIsMin = false;
            Core::Scene::PhysicsCollisionMask Layer = Core::Scene::DefaultPhysicsLayer;
            Core::Scene::PhysicsCollisionMask Mask = Core::Scene::AllPhysicsLayers;
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

        bool CanProxyMobilitiesInteract(EPhysicsProxyMobility first, EPhysicsProxyMobility second)
        {
            return first == EPhysicsProxyMobility::Unspecified || second == EPhysicsProxyMobility::Unspecified
                || first == EPhysicsProxyMobility::Dynamic || second == EPhysicsProxyMobility::Dynamic
                || first == EPhysicsProxyMobility::Trigger || second == EPhysicsProxyMobility::Trigger;
        }

        void ReverseContact(Math::GeometryContact& contact)
        {
            contact.Normal *= -1.0f;
        }

        bool StoreHeightFieldContact(const PhysicsShapeProxy& proxy,
            const Core::Container::VariableArray<Math::GeometryContact>& contacts, Math::GeometryContact& outContact,
            bool bReverseNormal = true)
        {
            if (contacts.empty())
            {
                return false;
            }
            const Math::GeometryContact* deepest = &contacts[0];
            for (const auto& contact : contacts)
            {
                if (contact.Depth > deepest->Depth)
                {
                    deepest = &contact;
                }
            }
            outContact = *deepest;
            outContact.Point += proxy.HeightFieldOrigin;
            if (bReverseNormal)
            {
                // 問い合わせ側では、地形から形へ向く法線を反転する。
                ReverseContact(outContact);
            }
            return true;
        }

        bool ComputeOverlap(
            const Math::Sphere& query,
            const PhysicsShapeProxy& proxy,
            Math::GeometryContact& outContact)
        {
            switch (proxy.Shape)
            {
            case EPhysicsProxyShape::Sphere:
                return Math::ComputeContact(query, proxy.Sphere, outContact);
            case EPhysicsProxyShape::Box:
                return Math::ComputeContact(query, proxy.Box, outContact);
            case EPhysicsProxyShape::Capsule:
                ReverseContact(outContact);
                if (!Math::ComputeContact(proxy.Capsule, query, outContact))
                {
                    return false;
                }
                ReverseContact(outContact);
                return true;
            case EPhysicsProxyShape::HeightField:
            {
                Math::Sphere local = query;
                local.Center -= proxy.HeightFieldOrigin;
                Core::Container::VariableArray<Math::GeometryContact> contacts;
                proxy.HeightField->OverlapSphere(local, contacts);
                return StoreHeightFieldContact(proxy, contacts, outContact);
            }
            }
            return false;
        }

        bool ComputeOverlap(
            const Math::OBB& query,
            const PhysicsShapeProxy& proxy,
            Math::GeometryContact& outContact)
        {
            switch (proxy.Shape)
            {
            case EPhysicsProxyShape::Sphere:
                if (!Math::ComputeContact(proxy.Sphere, query, outContact))
                {
                    return false;
                }
                ReverseContact(outContact);
                return true;
            case EPhysicsProxyShape::Box:
                return Math::ComputeContact(query, proxy.Box, outContact);
            case EPhysicsProxyShape::Capsule:
                if (!Math::ComputeContact(proxy.Capsule, query, outContact))
                {
                    return false;
                }
                ReverseContact(outContact);
                return true;
            case EPhysicsProxyShape::HeightField:
            {
                Math::OBB local = query;
                local.Center -= proxy.HeightFieldOrigin;
                Core::Container::VariableArray<Math::GeometryContact> contacts;
                proxy.HeightField->OverlapBox(local, contacts);
                return StoreHeightFieldContact(proxy, contacts, outContact);
            }
            }
            return false;
        }

        bool ComputeOverlap(
            const Math::Capsule& query,
            const PhysicsShapeProxy& proxy,
            Math::GeometryContact& outContact)
        {
            switch (proxy.Shape)
            {
            case EPhysicsProxyShape::Sphere:
                return Math::ComputeContact(query, proxy.Sphere, outContact);
            case EPhysicsProxyShape::Box:
                return Math::ComputeContact(query, proxy.Box, outContact);
            case EPhysicsProxyShape::Capsule:
                return Math::ComputeContact(query, proxy.Capsule, outContact);
            case EPhysicsProxyShape::HeightField:
            {
                Math::Capsule local = query;
                local.PointA -= proxy.HeightFieldOrigin;
                local.PointB -= proxy.HeightFieldOrigin;
                Core::Container::VariableArray<Math::GeometryContact> contacts;
                proxy.HeightField->OverlapCapsule(local, contacts);
                return StoreHeightFieldContact(proxy, contacts, outContact);
            }
            }
            return false;
        }

        struct RayVectorD
        {
            double X, Y, Z;
            RayVectorD operator+(const RayVectorD& other) const
            {
                return {X+other.X,Y+other.Y,Z+other.Z};
            }
            RayVectorD operator-(const RayVectorD& other) const
            {
                return {X-other.X,Y-other.Y,Z-other.Z};
            }
            RayVectorD operator*(double scale) const
            {
                return {X*scale,Y*scale,Z*scale};
            }
        };
        RayVectorD RayDouble(const Math::Vector3& value)
        {
            return {value.x,value.y,value.z};
        }
        double RayDot(const RayVectorD& first, const RayVectorD& second)
        {
            return first.X*second.X + first.Y*second.Y + first.Z*second.Z;
        }
        RayVectorD RayCross(const RayVectorD& first, const RayVectorD& second)
        {
            return {first.Y*second.Z-first.Z*second.Y, first.Z*second.X-first.X*second.Z,
                first.X*second.Y-first.Y*second.X};
        }
        bool RaySection(double radiusSquared, double lineDistanceSquared, double& outSection)
        {
            outSection = radiusSquared - lineDistanceSquared;
            const double rounding = 32 * std::numeric_limits<double>::epsilon() *
                std::fmax(radiusSquared,lineDistanceSquared);
            if (!std::isfinite(outSection) || outSection < -rounding)
            {
                return false;
            }
            outSection = std::fmax(0.0,outSection);
            return true;
        }
        bool SphereRayDistance(const RayVectorD& origin, const RayVectorD& direction,
            const Math::Sphere& sphere, double& outDistance)
        {
            const auto offset = origin - RayDouble(sphere.Center);
            const double radiusSquared = static_cast<double>(sphere.Radius)*sphere.Radius;
            if (RayDot(offset,offset) <= radiusSquared)
            {
                outDistance = 0;
                return true;
            }
            const double a = RayDot(direction,direction);
            if (!(a > 0) || !std::isfinite(a))
            {
                return false;
            }
            // b*b-a*cの大きな同値同士を引かず、ray直線から中心までの距離で断面を判定。
            const auto cross = RayCross(offset,direction);
            double section = 0;
            if (!RaySection(radiusSquared,RayDot(cross,cross)/a,section))
            {
                return false;
            }
            const double center = -RayDot(offset,direction)/a;
            const double half = std::sqrt(section/a);
            const double near = center-half, far = center+half;
            if (far < 0)
            {
                return false;
            }
            outDistance = near >= 0 ? near : far;
            return std::isfinite(outDistance);
        }
        bool StoreRayDistance(double distance, float& outDistance)
        {
            if (!std::isfinite(distance) || distance < 0 || distance > FLT_MAX)
            {
                return false;
            }
            outDistance = static_cast<float>(distance);
            return true;
        }
        bool RaycastSphere(const Math::Ray& ray, const Math::Sphere& sphere, double& outDistance)
        {
            return SphereRayDistance(RayDouble(ray.Origin),RayDouble(ray.Direction),sphere,outDistance);
        }
        bool RayPoint(const Math::Ray& ray, double distance, Math::Vector3& outPoint)
        {
            const double x = static_cast<double>(ray.Origin.x)+ray.Direction.x*distance;
            const double y = static_cast<double>(ray.Origin.y)+ray.Direction.y*distance;
            const double z = static_cast<double>(ray.Origin.z)+ray.Direction.z*distance;
            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
                std::fabs(x) > FLT_MAX || std::fabs(y) > FLT_MAX || std::fabs(z) > FLT_MAX)
            {
                return false;
            }
            outPoint = Math::Vector3(static_cast<float>(x),static_cast<float>(y),static_cast<float>(z));
            return true;
        }
        Math::Vector3 RayNormal(const RayVectorD& normal)
        {
            const double length = std::sqrt(RayDot(normal,normal));
            return length > 0 ? Math::Vector3(static_cast<float>(normal.X/length),
                static_cast<float>(normal.Y/length),static_cast<float>(normal.Z/length)) : Math::Vector3();
        }

        bool RaycastCapsule(const Math::Ray& ray, const Math::Capsule& capsule, double& outDistance)
        {
            const auto origin = RayDouble(ray.Origin), direction = RayDouble(ray.Direction);
            const auto segment = RayDouble(capsule.PointB) - RayDouble(capsule.PointA);
            const auto offset = origin - RayDouble(capsule.PointA);
            const double lengthSquared = RayDot(segment,segment);
            if (lengthSquared == 0)
            {
                return RaycastSphere(ray,Math::Sphere(capsule.PointA,capsule.Radius),outDistance);
            }
            const double projection = RayDot(segment,offset);
            const double parameter = std::fmax(0.0,std::fmin(1.0,projection/lengthSquared));
            const auto closest = offset - segment*parameter;
            const double radiusSquared = static_cast<double>(capsule.Radius)*capsule.Radius;
            if (RayDot(closest,closest) <= radiusSquared)
            {
                outDistance = 0;
                return true;
            }
            double best = std::numeric_limits<double>::infinity();
            // 端球と有限円筒の和集合。全ての有効な正根から最小を選ぶ。
            for (const auto& center : {capsule.PointA,capsule.PointB})
            {
                double distance = 0;
                if (SphereRayDistance(origin,direction,Math::Sphere(center,capsule.Radius),distance))
                {
                    best = std::fmin(best,distance);
                }
            }
            const auto perpendicularOrigin = RayCross(offset,segment);
            const auto perpendicularDirection = RayCross(direction,segment);
            const double a = RayDot(perpendicularDirection,perpendicularDirection);
            if (a > 0 && std::isfinite(a))
            {
                const auto cross = RayCross(perpendicularOrigin,perpendicularDirection);
                double section = 0;
                if (RaySection(radiusSquared*lengthSquared,RayDot(cross,cross)/a,section))
                {
                    const double center = -RayDot(perpendicularOrigin,perpendicularDirection)/a;
                    const double half = std::sqrt(section/a);
                    const double alongDirection = RayDot(segment,direction);
                    for (double distance : {center-half,center+half})
                    {
                        const double along = projection+distance*alongDirection;
                        if (distance >= 0 && along >= 0 && along <= lengthSquared)
                        {
                            best = std::fmin(best,distance);
                        }
                    }
                }
            }
            if (!std::isfinite(best))
            {
                return false;
            }
            outDistance = best;
            return true;
        }

        bool RaycastBox(const Math::Ray& ray, const Math::OBB& box, double& outDistance)
        {
            const auto offset = RayDouble(ray.Origin) - RayDouble(box.Center);
            const auto direction = RayDouble(ray.Direction);
            const double half[3]{box.HalfExtents.x,box.HalfExtents.y,box.HalfExtents.z};
            double near = 0, far = std::numeric_limits<double>::infinity();
            for (int axis = 0; axis < 3; ++axis)
            {
                const auto basis = RayDouble(box.Axes[axis]);
                const double origin = RayDot(offset,basis), delta = RayDot(direction,basis);
                if (delta == 0)
                {
                    if (origin < -half[axis] || origin > half[axis])
                    {
                        return false;
                    }
                    continue;
                }
                const double first = (-half[axis]-origin)/delta, last = (half[axis]-origin)/delta;
                near = std::fmax(near,std::fmin(first,last));
                far = std::fmin(far,std::fmax(first,last));
                if (near > far)
                {
                    return false;
                }
            }
            outDistance = near;
            return std::isfinite(near);
        }

        bool RaycastProxy(const Math::Ray& ray, float maxDistance, const PhysicsShapeProxy& proxy,
            double& outDistance, Core::Rendering::Terrain::TerrainRayHit& outTerrainHit)
        {
            switch (proxy.Shape)
            {
            case EPhysicsProxyShape::Sphere:
                return RaycastSphere(ray, proxy.Sphere, outDistance);
            case EPhysicsProxyShape::Box:
                return RaycastBox(ray,proxy.Box,outDistance);
            case EPhysicsProxyShape::Capsule:
                return RaycastCapsule(ray, proxy.Capsule, outDistance);
            case EPhysicsProxyShape::HeightField:
            {
                const Math::Ray local(ray.Origin - proxy.HeightFieldOrigin, ray.Direction);
                if (!proxy.HeightField->Raycast(local, maxDistance, outTerrainHit))
                {
                    return false;
                }
                outDistance = outTerrainHit.Distance;
                return true;
            }
            }
            return false;
        }

        bool CalculateRayPoint(const Math::Ray& ray, const PhysicsShapeProxy& proxy, double distance,
            const Math::Vector3& terrainPoint, Math::Vector3& outPoint)
        {
            switch (proxy.Shape)
            {
            case EPhysicsProxyShape::Sphere:
            case EPhysicsProxyShape::Box:
            case EPhysicsProxyShape::Capsule:
                return RayPoint(ray, distance, outPoint);
            case EPhysicsProxyShape::HeightField:
                outPoint = terrainPoint + proxy.HeightFieldOrigin;
                return true;
            }
            return false;
        }

        Math::Vector3 CalculateRayNormal(const Math::Ray& ray, const PhysicsShapeProxy& proxy, double distance,
            const Math::Vector3& terrainNormal)
        {
            if (distance == 0.0f)
            {
                return Math::Vector3();
            }
            switch (proxy.Shape)
            {
            case EPhysicsProxyShape::Sphere:
                return RayNormal((RayDouble(ray.Origin) - RayDouble(proxy.Sphere.Center)) + RayDouble(ray.Direction)*distance);
            case EPhysicsProxyShape::Capsule:
            {
                const auto segment = RayDouble(proxy.Capsule.PointB) - RayDouble(proxy.Capsule.PointA);
                const auto offset = (RayDouble(ray.Origin) - RayDouble(proxy.Capsule.PointA)) + RayDouble(ray.Direction)*distance;
                const double lengthSquared = RayDot(segment,segment);
                const double parameter = lengthSquared > 0 ? std::fmax(0.0,std::fmin(1.0,RayDot(offset,segment)/lengthSquared)) : 0;
                const auto normal = offset - segment*parameter;
                return RayNormal(normal);
            }
            case EPhysicsProxyShape::Box:
            {
                const auto offset = (RayDouble(ray.Origin)-RayDouble(proxy.Box.Center))+RayDouble(ray.Direction)*distance;
                const double local[3]{RayDot(offset,RayDouble(proxy.Box.Axes[0])),
                    RayDot(offset,RayDouble(proxy.Box.Axes[1])),RayDot(offset,RayDouble(proxy.Box.Axes[2]))};
                const double distances[3]{std::fabs(std::fabs(local[0])-proxy.Box.HalfExtents.x),
                    std::fabs(std::fabs(local[1])-proxy.Box.HalfExtents.y),std::fabs(std::fabs(local[2])-proxy.Box.HalfExtents.z)};
                int normalAxis = 0;
                if (distances[1] < distances[normalAxis])
                {
                    normalAxis = 1;
                }
                if (distances[2] < distances[normalAxis])
                {
                    normalAxis = 2;
                }
                return RayNormal(RayDouble(proxy.Box.Axes[normalAxis])*(local[normalAxis] < 0 ? -1.0 : 1.0));
            }
            case EPhysicsProxyShape::HeightField:
                return terrainNormal;
            }
            return Math::Vector3();
        }

    } // namespace

    namespace
    {
        bool IsFiniteQueryVector(const Math::Vector3& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool IsValidQueryShape(const Math::Sphere& shape)
        {
            return IsFiniteQueryVector(shape.Center) && std::isfinite(shape.Radius) && shape.Radius >= 0.0f;
        }

        bool IsValidQueryShape(const Math::Capsule& shape)
        {
            return IsFiniteQueryVector(shape.PointA) && IsFiniteQueryVector(shape.PointB)
                && std::isfinite(shape.Radius) && shape.Radius >= 0.0f;
        }

        bool IsValidQueryShape(const Math::OBB& shape)
        {
            if (!IsFiniteQueryVector(shape.Center) || !IsFiniteQueryVector(shape.HalfExtents)
                || shape.HalfExtents.x < 0.0f || shape.HalfExtents.y < 0.0f || shape.HalfExtents.z < 0.0f)
            {
                return false;
            }
            for (int axis = 0; axis < 3; ++axis)
            {
                if (!IsFiniteQueryVector(shape.Axes[axis])
                    || std::fabs(Math::VectorUtils::Dot(shape.Axes[axis], shape.Axes[axis]) - 1.0f) > 1e-4f
                    || std::fabs(Math::VectorUtils::Dot(shape.Axes[axis], shape.Axes[(axis + 1) % 3])) > 1e-4f)
                {
                    return false;
                }
            }
            return true;
        }

        Math::Vector3 QueryUnit(const Math::Vector3& value)
        {
            const double length = std::sqrt(static_cast<double>(value.x) * value.x
                + static_cast<double>(value.y) * value.y + static_cast<double>(value.z) * value.z);
            return Math::Vector3(static_cast<float>(value.x / length),
                static_cast<float>(value.y / length), static_cast<float>(value.z / length));
        }

        bool IsNonzeroQueryDirection(const Math::Vector3& value)
        {
            return IsFiniteQueryVector(value) && (value.x != 0.0f || value.y != 0.0f || value.z != 0.0f);
        }

        bool IsValidProxyGeometry(const PhysicsShapeProxy& proxy)
        {
            switch (proxy.Shape)
            {
            case EPhysicsProxyShape::Sphere:
                return IsValidQueryShape(proxy.Sphere);
            case EPhysicsProxyShape::Box:
                return IsValidQueryShape(proxy.Box);
            case EPhysicsProxyShape::Capsule:
                return IsValidQueryShape(proxy.Capsule);
            case EPhysicsProxyShape::HeightField:
            {
                Math::AABB bounds;
                return proxy.HeightField && proxy.HeightField->IsGrid()
                    && IsFiniteQueryVector(proxy.HeightFieldOrigin) && proxy.HeightField->GetBounds(bounds);
            }
            }
            return false;
        }

        bool IsUsableQueryBounds(const Math::AABB& bounds)
        {
            return IsFiniteQueryVector(bounds.Min) && IsFiniteQueryVector(bounds.Max)
                && bounds.Min.x <= bounds.Max.x && bounds.Min.y <= bounds.Max.y && bounds.Min.z <= bounds.Max.z;
        }

        double BoundCoordinateScale(const Math::AABB& bounds)
        {
            const double coordinates[]{bounds.Min.x,bounds.Min.y,bounds.Min.z,bounds.Max.x,bounds.Max.y,bounds.Max.z};
            double scale = 1;
            for (double value : coordinates)
            {
                scale = std::fmax(scale,std::fabs(value));
            }
            return scale;
        }

        bool BoundsMayOverlap(const Math::AABB& first, const Math::AABB& second)
        {
            if (!IsUsableQueryBounds(second))
            {
                return true;
            }
            // float bounds丸めとworld座標相対のsweep許容より広く取り、狭め過ぎない。
            const double margin = 1e-4 + 1e-5 * std::fmax(BoundCoordinateScale(first), BoundCoordinateScale(second));
            return static_cast<double>(first.Max.x) + margin >= second.Min.x && static_cast<double>(second.Max.x) + margin >= first.Min.x
                && static_cast<double>(first.Max.y) + margin >= second.Min.y && static_cast<double>(second.Max.y) + margin >= first.Min.y
                && static_cast<double>(first.Max.z) + margin >= second.Min.z && static_cast<double>(second.Max.z) + margin >= first.Min.z;
        }

        bool BoundsMayMeetRay(const Math::AABB& bounds, const Math::Vector3& origin,
            const Math::Vector3& unitDirection, float maxDistance)
        {
            if (!IsUsableQueryBounds(bounds))
            {
                return true;
            }
            const double low[3]{bounds.Min.x, bounds.Min.y, bounds.Min.z};
            const double high[3]{bounds.Max.x, bounds.Max.y, bounds.Max.z};
            const double start[3]{origin.x, origin.y, origin.z};
            const double direction[3]{unitDirection.x, unitDirection.y, unitDirection.z};
            double near = 0, far = maxDistance;
            for (int axis = 0; axis < 3; ++axis)
            {
                // rayにはsweepの距離許容がないため、軸別の丸め余裕だけを使う。
                const double margin = 1e-4 + 1e-5 * std::fmax(1.0,
                    std::fmax(std::fabs(start[axis]),std::fmax(std::fabs(low[axis]),std::fabs(high[axis]))));
                if (direction[axis] == 0)
                {
                    if (start[axis] < low[axis] - margin || start[axis] > high[axis] + margin)
                    {
                        return false;
                    }
                    continue;
                }
                const double first = (low[axis] - margin - start[axis]) / direction[axis];
                const double last = (high[axis] + margin - start[axis]) / direction[axis];
                near = std::fmax(near, std::fmin(first,last));
                far = std::fmin(far, std::fmax(first,last));
                if (near > far)
                {
                    return false;
                }
            }
            return true;
        }

        Math::AABB ConservativeProxyBounds(const PhysicsShapeProxy& proxy)
        {
            auto bounds = PhysicsBroadphase::CalculateBounds(proxy);
            switch (proxy.Shape)
            {
            case EPhysicsProxyShape::Sphere:
            case EPhysicsProxyShape::Capsule:
            case EPhysicsProxyShape::HeightField:
                return bounds;
            case EPhysicsProxyShape::Box:
            {
                // 許容Gram誤差1e-4の軸ではdotの逆写像と前向きboundsが一致しない。
                // 3軸の誤差を見込み、最大半径長の1e-3を加えて両領域を包む。
                const float padding = 1e-3f * std::fmax(proxy.Box.HalfExtents.x,
                    std::fmax(proxy.Box.HalfExtents.y,proxy.Box.HalfExtents.z));
                const Math::Vector3 expansion(padding,padding,padding);
                bounds.Min -= expansion;
                bounds.Max += expansion;
                return bounds;
            }
            }
            return Math::AABB();
        }

        template<typename Predicate>
        Core::Scene::EPhysicsSceneQueryResult VisitProxyCandidates(
            Core::Container::Span<const PhysicsShapeProxy> proxies, PhysicsBroadphase::ProxyVisitCallback visitor,
            void* context, PhysicsBroadphase::ProxyVisitCallback precheck, const Predicate& candidate)
        {
            using Result = Core::Scene::EPhysicsSceneQueryResult;
            if (visitor == nullptr || (proxies.size() != 0 && proxies.data() == nullptr))
            {
                return Result::InvalidArgument;
            }
            for (size_t index = 0; index < proxies.size(); ++index)
            {
                const auto& proxy = proxies[index];
                if (precheck)
                {
                    const auto result = precheck(proxy,context);
                    if (result == Result::NoHit)
                    {
                        continue;
                    }
                    if (result != Result::Success)
                    {
                        return result;
                    }
                }
                // 不正proxyを空間除外で隠さず、呼出側の検証へ渡す。
                if (IsValidProxyGeometry(proxy) && !candidate(ConservativeProxyBounds(proxy)))
                {
                    continue;
                }
                const auto result = visitor(proxy,context);
                if (result != Result::Success && result != Result::NoHit)
                {
                    return result;
                }
            }
            return Result::Success;
        }

        // 接触/分離に残るfloat幾何の保守的な尺度上限を維持する。
        // ray内部のdouble化だけでは全queryの安全域を拡大しない。始点からの相対尺度で判定。
        bool HasRepresentableQueryScale(const PhysicsShapeProxy& proxy, const Core::Scene::PhysicsQueryDesc& query)
        {
            using Kind = Core::Scene::EPhysicsQueryKind;
            const Math::Vector3 origin = query.Kind == Kind::RaycastClosest || query.Kind == Kind::RaycastAll ? query.Ray.Origin
                : query.Kind == Kind::OverlapSphere || query.Kind == Kind::SweepSphere ? query.Sphere.Center
                : query.Kind == Kind::OverlapBox ? query.Box.Center : query.Capsule.PointA;
            double scale = 1.0;
            bool bRepresentable = true;
            auto includePoint = [&](const Math::Vector3& point)
            {
                const double coordinates[3] = {point.x, point.y, point.z};
                const double origins[3] = {origin.x, origin.y, origin.z};
                for (int axis = 0; axis < 3; ++axis)
                {
                    bRepresentable = bRepresentable && std::fabs(coordinates[axis]) <= FLT_MAX / 16.0;
                    scale = std::fmax(scale, std::fabs(coordinates[axis] - origins[axis]));
                }
            };
            auto includeSphere = [&](const Math::Sphere& shape)
            {
                includePoint(shape.Center);
                scale = std::fmax(scale, shape.Radius);
            };
            auto includeBox = [&](const Math::OBB& shape)
            {
                includePoint(shape.Center);
                scale = std::fmax(scale, std::fmax(shape.HalfExtents.x, std::fmax(shape.HalfExtents.y, shape.HalfExtents.z)));
            };
            auto includeCapsule = [&](const Math::Capsule& shape)
            {
                includePoint(shape.PointA);
                includePoint(shape.PointB);
                scale = std::fmax(scale, shape.Radius);
            };
            includePoint(origin);
            if (query.Kind == Kind::OverlapSphere || query.Kind == Kind::SweepSphere)
            {
                includeSphere(query.Sphere);
            }
            else if (query.Kind == Kind::OverlapBox)
            {
                includeBox(query.Box);
            }
            else if (query.Kind == Kind::OverlapCapsule || query.Kind == Kind::SweepCapsule)
            {
                includeCapsule(query.Capsule);
            }
            const auto isRepresentable = [&]()
            {
                const double squared = scale * scale;
                return bRepresentable && squared * squared * squared <= static_cast<double>(FLT_MAX) / 4096.0;
            };
            switch (proxy.Shape)
            {
            case EPhysicsProxyShape::Sphere:
                includeSphere(proxy.Sphere);
                return isRepresentable();
            case EPhysicsProxyShape::Box:
                includeBox(proxy.Box);
                return isRepresentable();
            case EPhysicsProxyShape::Capsule:
                includeCapsule(proxy.Capsule);
                return isRepresentable();
            case EPhysicsProxyShape::HeightField:
                // 境界の大きさは高さ場自身の照会が扱うため、平行移動の原点だけを含める。
                includePoint(proxy.HeightFieldOrigin);
                return isRepresentable();
            }
            return false;
        }

        Core::Scene::EPhysicsSceneQueryResult ValidateProxyForQuery(const PhysicsShapeProxy& proxy,
            const Core::Scene::PhysicsQueryDesc& query, bool bApplyFilter)
        {
            using Result = Core::Scene::EPhysicsSceneQueryResult;
            if (!IsValidProxyGeometry(proxy))
            {
                return Result::InvalidArgument;
            }
            if (bApplyFilter && !query.Filter.Accepts(proxy.Layer,proxy.bTrigger,proxy.Collider,proxy.Body))
            {
                return Result::NoHit;
            }
            if (!HasRepresentableQueryScale(proxy,query))
            {
                return Result::InvalidArgument;
            }
            const bool bSweep = query.Kind == Core::Scene::EPhysicsQueryKind::SweepSphere ||
                query.Kind == Core::Scene::EPhysicsQueryKind::SweepCapsule;
            switch (proxy.Shape)
            {
            case EPhysicsProxyShape::Sphere:
            case EPhysicsProxyShape::Capsule:
            case EPhysicsProxyShape::HeightField:
                return Result::Success;
            case EPhysicsProxyShape::Box:
                return bSweep && !Math::IsValidSweepBox(proxy.Box) ? Result::InvalidArgument : Result::Success;
            }
            return Result::InvalidArgument;
        }

        bool TryQueryBounds(const Core::Scene::PhysicsQueryDesc& query, Math::AABB& outBounds)
        {
            using Kind = Core::Scene::EPhysicsQueryKind;
            PhysicsShapeProxy shape;
            if (query.Kind == Kind::OverlapSphere || query.Kind == Kind::SweepSphere)
            {
                shape.Shape = EPhysicsProxyShape::Sphere;
                shape.Sphere = query.Sphere;
            }
            else if (query.Kind == Kind::OverlapBox)
            {
                shape.Shape = EPhysicsProxyShape::Box;
                shape.Box = query.Box;
            }
            else
            {
                shape.Shape = EPhysicsProxyShape::Capsule;
                shape.Capsule = query.Capsule;
            }
            outBounds = ConservativeProxyBounds(shape);
            if (!IsUsableQueryBounds(outBounds))
            {
                return false;
            }
            if ((query.Kind == Kind::SweepSphere || query.Kind == Kind::SweepCapsule) && query.MaxDistance > 0)
            {
                const auto unit = QueryUnit(query.Direction);
                const double delta[3]{static_cast<double>(unit.x)*query.MaxDistance,
                    static_cast<double>(unit.y)*query.MaxDistance,static_cast<double>(unit.z)*query.MaxDistance};
                const double low[3]{outBounds.Min.x,outBounds.Min.y,outBounds.Min.z};
                const double high[3]{outBounds.Max.x,outBounds.Max.y,outBounds.Max.z};
                float minimum[3]{}, maximum[3]{};
                for (int axis = 0; axis < 3; ++axis)
                {
                    const double first = std::fmin(low[axis],low[axis]+delta[axis]);
                    const double last = std::fmax(high[axis],high[axis]+delta[axis]);
                    if (!std::isfinite(first) || !std::isfinite(last) || std::fabs(first) > FLT_MAX || std::fabs(last) > FLT_MAX)
                    {
                        return false;
                    }
                    minimum[axis] = static_cast<float>(first);
                    maximum[axis] = static_cast<float>(last);
                }
                outBounds = Math::AABB(Math::Vector3(minimum[0],minimum[1],minimum[2]),Math::Vector3(maximum[0],maximum[1],maximum[2]));
            }
            return true;
        }

        Math::GeometrySweepHit SweepProxy(const Math::Capsule& shape, const PhysicsShapeProxy& proxy,
            const Core::Scene::PhysicsQueryDesc& query)
        {
            Math::GeometrySweepSettings settings;
            settings.bReportStartOverlap = query.bReportStartOverlap;
            settings.MaxIterations = query.MaxSweepIterations;
            switch (proxy.Shape)
            {
            case EPhysicsProxyShape::Sphere:
                return Math::SweepCapsule(shape, proxy.Sphere, query.Direction, query.MaxDistance, settings);
            case EPhysicsProxyShape::Box:
                return Math::SweepCapsule(shape, proxy.Box, query.Direction, query.MaxDistance, settings);
            case EPhysicsProxyShape::Capsule:
                return Math::SweepCapsule(shape, proxy.Capsule, query.Direction, query.MaxDistance, settings);
            case EPhysicsProxyShape::HeightField:
            {
                // 高さ場は内部で反復回数を管理するため、MaxSweepIterations は使わない。
                Math::GeometrySweepHit hit;
                bool bHit;
                if (query.Kind == Core::Scene::EPhysicsQueryKind::SweepSphere)
                {
                    Math::Sphere local = query.Sphere;
                    local.Center -= proxy.HeightFieldOrigin;
                    bHit = proxy.HeightField->SweepSphere(local, query.Direction, query.MaxDistance, hit);
                }
                else
                {
                    Math::Capsule local = shape;
                    local.PointA -= proxy.HeightFieldOrigin;
                    local.PointB -= proxy.HeightFieldOrigin;
                    bHit = proxy.HeightField->SweepCapsule(local, query.Direction, query.MaxDistance, hit);
                }
                if (bHit)
                {
                    if (!query.bReportStartOverlap && hit.bStartPenetrating)
                    {
                        return Math::GeometrySweepHit();
                    }
                    hit.Result = Math::EGeometrySweepResult::Hit;
                }
                else if (hit.Result != Math::EGeometrySweepResult::IterationLimit)
                {
                    // false でも未確定の安全側位置は残るため、Result で反復上限を区別する。
                    return Math::GeometrySweepHit();
                }
                hit.Point += proxy.HeightFieldOrigin;
                return hit;
            }
            }
            Math::GeometrySweepHit result;
            result.Result = Math::EGeometrySweepResult::InvalidArgument;
            return result;
        }
    }

    Core::Scene::EPhysicsSceneQueryResult PhysicsBroadphase::VisitProxiesInAabb(
        Core::Container::Span<const PhysicsShapeProxy> proxies, const Math::AABB& bounds,
        ProxyVisitCallback visitor, void* context, ProxyVisitCallback precheck)
    {
        if (!IsUsableQueryBounds(bounds))
        {
            return Core::Scene::EPhysicsSceneQueryResult::InvalidArgument;
        }
        return VisitProxyCandidates(proxies, visitor, context, precheck,
            [&](const Math::AABB& candidate) { return BoundsMayOverlap(bounds,candidate); });
    }

    Core::Scene::EPhysicsSceneQueryResult PhysicsBroadphase::VisitProxiesAlongRay(
        Core::Container::Span<const PhysicsShapeProxy> proxies, const Math::Ray& ray, float maxDistance,
        ProxyVisitCallback visitor, void* context, ProxyVisitCallback precheck)
    {
        if (!IsFiniteQueryVector(ray.Origin) || !IsNonzeroQueryDirection(ray.Direction) ||
            !std::isfinite(maxDistance) || maxDistance < 0)
        {
            return Core::Scene::EPhysicsSceneQueryResult::InvalidArgument;
        }
        const auto unit = QueryUnit(ray.Direction);
        return VisitProxyCandidates(proxies, visitor, context, precheck,
            [&](const Math::AABB& candidate) { return BoundsMayMeetRay(candidate,ray.Origin,unit,maxDistance); });
    }

    bool PhysicsBroadphase::IsValidQuery(const Core::Scene::PhysicsQueryDesc& query)
    {
        using Kind = Core::Scene::EPhysicsQueryKind;
        if (!query.Filter.IsValid() || query.MaxHits == 0)
        {
            return false;
        }
        switch (query.Kind)
        {
        case Kind::RaycastClosest:
        case Kind::RaycastAll:
            return IsFiniteQueryVector(query.Ray.Origin) && IsNonzeroQueryDirection(query.Ray.Direction)
                && std::isfinite(query.MaxDistance) && query.MaxDistance >= 0.0f;
        case Kind::OverlapSphere:
            return IsValidQueryShape(query.Sphere);
        case Kind::OverlapBox:
            return IsValidQueryShape(query.Box);
        case Kind::OverlapCapsule:
            return IsValidQueryShape(query.Capsule);
        case Kind::SweepSphere:
        case Kind::SweepCapsule:
            return (query.Kind == Kind::SweepSphere ? IsValidQueryShape(query.Sphere) : IsValidQueryShape(query.Capsule))
                && query.MaxSweepIterations > 0
                && IsFiniteQueryVector(query.Direction) && std::isfinite(query.MaxDistance) && query.MaxDistance >= 0.0f
                && (query.MaxDistance == 0.0f || IsNonzeroQueryDirection(query.Direction));
        }
        return false;
    }

    namespace
    {
        uint8_t CalculateSurfaceId(const PhysicsShapeProxy& proxy, const Math::Vector3& point)
        {
            switch (proxy.Shape)
            {
            case EPhysicsProxyShape::Sphere:
            case EPhysicsProxyShape::Box:
            case EPhysicsProxyShape::Capsule:
                return proxy.SurfaceId;
            case EPhysicsProxyShape::HeightField:
                return proxy.HeightField->SampleSurface(point.x - proxy.HeightFieldOrigin.x,
                    point.z - proxy.HeightFieldOrigin.z);
            }
            return Core::Scene::kInvalidPhysicsSurfaceId;
        }

        Core::Scene::EPhysicsSceneQueryResult QueryProxyImpl(const PhysicsShapeProxy& proxy,
            const Core::Scene::PhysicsQueryDesc& query, Core::Scene::PhysicsQueryHit& outHit, bool bApplyFilter)
        {
            using Result = Core::Scene::EPhysicsSceneQueryResult;
            using Kind = Core::Scene::EPhysicsQueryKind;
            outHit = {};
            if (!PhysicsBroadphase::IsValidQuery(query))
            {
                return Result::InvalidArgument;
            }
            const auto validation = ValidateProxyForQuery(proxy,query,bApplyFilter);
            if (validation != Result::Success)
            {
                return validation;
            }
            Core::Scene::PhysicsQueryHit hit;
            hit.Collider = proxy.Collider;
            hit.Body = proxy.Body;
            hit.Entity = proxy.Entity;
            hit.bHasEntity = proxy.bHasEntity;
            hit.UserData = proxy.UserData;
            if (query.Kind == Kind::RaycastClosest || query.Kind == Kind::RaycastAll)
            {
                const Math::Ray ray(query.Ray.Origin, QueryUnit(query.Ray.Direction));
                double preciseDistance = 0;
                Core::Rendering::Terrain::TerrainRayHit terrainHit;
                if (!RaycastProxy(ray, query.MaxDistance, proxy, preciseDistance, terrainHit)
                    || !StoreRayDistance(preciseDistance,hit.Distance)
                    || hit.Distance > query.MaxDistance)
                {
                    return Result::NoHit;
                }
                if (!CalculateRayPoint(ray, proxy, preciseDistance, terrainHit.Point, hit.Point))
                {
                    return Result::InvalidArgument;
                }
                hit.Normal = CalculateRayNormal(ray,proxy,preciseDistance,terrainHit.Normal);
            }
            else if (query.Kind == Kind::SweepSphere || query.Kind == Kind::SweepCapsule)
            {
                const Math::Capsule shape = query.Kind == Kind::SweepSphere
                    ? Math::Capsule(query.Sphere.Center, query.Sphere.Center, query.Sphere.Radius) : query.Capsule;
                const auto sweep = SweepProxy(shape, proxy, query);
                if (sweep.Result == Math::EGeometrySweepResult::InvalidArgument)
                {
                    return Result::InvalidArgument;
                }
                if (sweep.Result == Math::EGeometrySweepResult::IterationLimit)
                {
                    return Result::IterationLimit;
                }
                if (sweep.Result == Math::EGeometrySweepResult::NoHit)
                {
                    return Result::NoHit;
                }
                hit.Distance = sweep.Distance;
                hit.Point = sweep.Point;
                hit.Normal = sweep.Normal;
                hit.Depth = sweep.Depth;
                hit.bStartPenetrating = sweep.bStartPenetrating;
            }
            else
            {
                Math::GeometryContact contact;
                const bool bHit = query.Kind == Kind::OverlapSphere ? ComputeOverlap(query.Sphere, proxy, contact)
                    : query.Kind == Kind::OverlapBox ? ComputeOverlap(query.Box, proxy, contact)
                    : ComputeOverlap(query.Capsule, proxy, contact);
                if (!bHit)
                {
                    return Result::NoHit;
                }
                hit.Point = contact.Point;
                hit.Normal = -1.0f * contact.Normal;
                hit.Depth = contact.Depth;
            }
            if (!std::isfinite(hit.Distance) || hit.Distance < 0.0f || !std::isfinite(hit.Depth) || hit.Depth < 0.0f
                || !IsFiniteQueryVector(hit.Point) || !IsFiniteQueryVector(hit.Normal))
            {
                return Result::InvalidArgument;
            }
            hit.SurfaceId = CalculateSurfaceId(proxy, hit.Point);
            outHit = hit;
            return Result::Success;
        }

    }

    Core::Scene::EPhysicsSceneQueryResult PhysicsBroadphase::QueryProxy(const PhysicsShapeProxy& proxy,
        const Core::Scene::PhysicsQueryDesc& query, Core::Scene::PhysicsQueryHit& outHit)
    {
        return QueryProxyImpl(proxy,query,outHit,true);
    }

    namespace
    {
        size_t QueryHitLimit(size_t proxyCount, const Core::Scene::PhysicsQueryDesc& query)
        {
            const size_t requested = query.Kind == Core::Scene::EPhysicsQueryKind::RaycastClosest
                ? 1 : static_cast<size_t>(query.MaxHits);
            return proxyCount < requested ? proxyCount : requested;
        }

        bool IsQueryHitLess(const Core::Scene::PhysicsQueryHit& a, const Core::Scene::PhysicsQueryHit& b,
            Core::Scene::EPhysicsQueryKind kind)
        {
            using Kind = Core::Scene::EPhysicsQueryKind;
            if (kind == Kind::OverlapSphere || kind == Kind::OverlapBox || kind == Kind::OverlapCapsule)
            {
                return a.Collider < b.Collider;
            }
            if (a.Distance != b.Distance)
            {
                return a.Distance < b.Distance;
            }
            // 旧最近接rayは同距離なら大きいhandle。All/Sweepは小さいhandleから返す。
            return kind == Kind::RaycastClosest ? b.Collider < a.Collider : a.Collider < b.Collider;
        }
        struct QueryCollection
        {
            const Core::Scene::PhysicsQueryDesc& Query;
            Core::Container::Span<Core::Scene::PhysicsQueryHit> Hits;
            size_t Limit;
            size_t Count = 0;
            bool bApplyFilter = true;
        };
        Core::Scene::EPhysicsSceneQueryResult CheckQueryCandidate(const PhysicsShapeProxy& proxy, void* context)
        {
            const auto& collection = *static_cast<QueryCollection*>(context);
            return ValidateProxyForQuery(proxy,collection.Query,collection.bApplyFilter);
        }
        Core::Scene::EPhysicsSceneQueryResult CollectQueryCandidate(const PhysicsShapeProxy& proxy, void* context)
        {
            using Result = Core::Scene::EPhysicsSceneQueryResult;
            auto& collection = *static_cast<QueryCollection*>(context);
            Core::Scene::PhysicsQueryHit hit;
            const auto result = QueryProxyImpl(proxy,collection.Query,hit,collection.bApplyFilter);
            if (result != Result::Success)
            {
                return result;
            }
            size_t position = 0;
            while (position < collection.Count && !IsQueryHitLess(hit,collection.Hits[position],collection.Query.Kind))
            {
                ++position;
            }
            if (position >= collection.Limit)
            {
                return Result::Success;
            }
            const size_t end = collection.Count < collection.Limit ? collection.Count : collection.Limit-1;
            for (size_t index = end; index > position; --index)
            {
                collection.Hits[index] = collection.Hits[index-1];
            }
            collection.Hits[position] = hit;
            if (collection.Count < collection.Limit)
            {
                ++collection.Count;
            }
            return Result::Success;
        }
    }

    namespace
    {
        Core::Scene::EPhysicsSceneQueryResult ExecuteQueryOverProxiesImpl(
            Core::Container::Span<const PhysicsShapeProxy> proxies, const Core::Scene::PhysicsQueryDesc& query,
            Core::Container::Span<Core::Scene::PhysicsQueryHit> outHits, size_t& outHitCount, bool bApplyFilter)
        {
            using Result = Core::Scene::EPhysicsSceneQueryResult;
            outHitCount = 0;
            if (outHits.size() != 0 && outHits.data() == nullptr)
            {
                return Result::InvalidArgument;
            }
            for (auto& hit : outHits)
            {
                hit = {};
            }
            if (!PhysicsBroadphase::IsValidQuery(query) || (proxies.size() != 0 && proxies.data() == nullptr))
            {
                return Result::InvalidArgument;
            }
            const size_t limit = QueryHitLimit(proxies.size(),query);
            if (outHits.size() < limit)
            {
                return Result::InvalidArgument;
            }
            QueryCollection collection{query,outHits,limit,0,bApplyFilter};
            Result result;
            if (query.Kind == Core::Scene::EPhysicsQueryKind::RaycastClosest || query.Kind == Core::Scene::EPhysicsQueryKind::RaycastAll)
            {
                result = PhysicsBroadphase::VisitProxiesAlongRay(proxies,query.Ray,query.MaxDistance,CollectQueryCandidate,&collection,CheckQueryCandidate);
            }
            else
            {
                Math::AABB bounds;
                if (TryQueryBounds(query,bounds))
                {
                    result = PhysicsBroadphase::VisitProxiesInAabb(proxies,bounds,CollectQueryCandidate,&collection,CheckQueryCandidate);
                }
                else
                {
                    // 巨大な掃引等でfloat boundsを表せないときは、検証を保った全探索へ戻す。
                    result = VisitProxyCandidates(proxies,CollectQueryCandidate,&collection,CheckQueryCandidate,
                        [](const Math::AABB&) { return true; });
                }
            }
            if (result != Result::Success)
            {
                for (auto& hit : outHits)
                {
                    hit = {};
                }
                return result;
            }
            outHitCount = collection.Count;
            return outHitCount == 0 ? Result::NoHit : Result::Success;
        }

    }

    Core::Scene::EPhysicsSceneQueryResult PhysicsBroadphase::ExecuteQueryOverProxies(
        Core::Container::Span<const PhysicsShapeProxy> proxies, const Core::Scene::PhysicsQueryDesc& query,
        Core::Container::Span<Core::Scene::PhysicsQueryHit> outHits, size_t& outHitCount)
    {
        return ExecuteQueryOverProxiesImpl(proxies,query,outHits,outHitCount,true);
    }

    Core::Scene::EPhysicsSceneQueryResult PhysicsBroadphase::ExecuteQuery(
        const Core::Scene::PhysicsQueryDesc& query,
        Core::Container::VariableArray<Core::Scene::PhysicsQueryHit>& outHits) const
    {
        m_Stats.VisitedNodeCount += m_Proxies.size();
        using Result = Core::Scene::EPhysicsSceneQueryResult;
        outHits.clear();
        if (!IsValidQuery(query))
        {
            return Result::InvalidArgument;
        }
        outHits.resize(QueryHitLimit(m_Proxies.size(), query));
        size_t count = 0;
        const Result result = ExecuteQueryOverProxies(
            Core::Container::Span<const PhysicsShapeProxy>(m_Proxies.data(), m_Proxies.size()), query,
            Core::Container::Span<Core::Scene::PhysicsQueryHit>(outHits.data(), outHits.size()), count);
        outHits.resize(result == Result::Success ? count : 0);
        return result;
    }

    void PhysicsBroadphase::SetProxies(Core::Container::VariableArray<PhysicsShapeProxy> proxies)
    {
        m_Stats = {};
        for (PhysicsShapeProxy& proxy : proxies)
        {
            proxy.Bounds = CalculateBounds(proxy);
        }

        if (proxies.size() > 1)
        {
            std::sort(proxies.begin(), proxies.end(), [](const PhysicsShapeProxy& left, const PhysicsShapeProxy& right)
            {
                return IsHandleLess(left.Collider, right.Collider);
            });
        }

        m_Proxies = std::move(proxies);
        assert(m_Proxies.size() <= UINT32_MAX);
        m_TreeProxyIndices.clear();
        m_NonTreeProxyIndices.clear();
        Core::Container::VariableArray<PhysicsStaticTreeSnapshot::ProxySignature> signatures;
        for (size_t index = 0; index < m_Proxies.size(); ++index)
        {
            const PhysicsShapeProxy& proxy = m_Proxies[index];
            // 2 回以上変わったら動く側へ移し、120 ステップ変わらなければ戻す規則を追加する場所。
            const bool bUseTree = proxy.bStatic && proxy.Shape != EPhysicsProxyShape::HeightField
                && IsValidProxyGeometry(proxy);
            if (bUseTree)
            {
                m_TreeProxyIndices.push_back(static_cast<uint32_t>(index));
                signatures.push_back({proxy.Collider, proxy.Shape, proxy.Sphere, proxy.Box, proxy.Capsule, proxy.Bounds});
            }
            else
            {
                m_NonTreeProxyIndices.push_back(static_cast<uint32_t>(index));
            }
        }

        if (signatures.empty())
        {
            m_StaticTree.reset();
        }
        else if (!m_StaticTree || m_StaticTree->Signatures.size() != signatures.size()
            || !std::equal(signatures.begin(), signatures.end(), m_StaticTree->Signatures.begin()))
        {
            auto snapshot = Core::Container::MakeShared<PhysicsStaticTreeSnapshot>();
            snapshot->Signatures = std::move(signatures);
            Core::Container::VariableArray<Math::AABB> bounds;
            Core::Container::VariableArray<Core::Scene::ColliderHandle> handles;
            bounds.reserve(m_TreeProxyIndices.size());
            handles.reserve(m_TreeProxyIndices.size());
            for (uint32_t index : m_TreeProxyIndices)
            {
                const PhysicsShapeProxy& proxy = m_Proxies[index];
                Math::AABB conservativeBounds = ConservativeProxyBounds(proxy);
                conservativeBounds.Merge(proxy.Bounds);
                bounds.push_back(conservativeBounds);
                handles.push_back(proxy.Collider);
            }
            snapshot->Tree.Build(
                Core::Container::Span<const Math::AABB>(bounds.data(), bounds.size()),
                Core::Container::Span<const Core::Scene::ColliderHandle>(handles.data(), handles.size()));
            m_StaticTree = std::move(snapshot);
            m_Stats.TreeRebuildCount = 1;
        }

        BuildCandidatePairs();
        m_Stats.CandidatePairCount = m_CandidatePairs.size();
    }

    void PhysicsBroadphase::ShareStaticTreeFrom(const PhysicsBroadphase& source)
    {
        m_StaticTree = source.m_StaticTree;
    }

    const StaticAabbTree* PhysicsBroadphase::GetStaticTree() const
    {
        return m_StaticTree ? &m_StaticTree->Tree : nullptr;
    }

    const Core::Container::VariableArray<PhysicsShapeProxy>& PhysicsBroadphase::GetProxies() const
    {
        return m_Proxies;
    }

    const Core::Container::VariableArray<PhysicsCandidatePair>& PhysicsBroadphase::GetCandidatePairs() const
    {
        return m_CandidatePairs;
    }

    const PhysicsBroadphaseStats& PhysicsBroadphase::GetStats() const
    {
        return m_Stats;
    }

    const PhysicsShapeProxy* PhysicsBroadphase::FindProxy(Core::Scene::ColliderHandle handle) const
    {
        size_t first = 0;
        size_t count = m_Proxies.size();
        while (count != 0)
        {
            const size_t step = count / 2;
            const size_t middle = first + step;
            ++m_Stats.ProxyLookupCount;
            if (IsHandleLess(m_Proxies[middle].Collider, handle))
            {
                first = middle + 1;
                count -= step + 1;
            }
            else
            {
                count = step;
            }
        }
        if (first < m_Proxies.size())
        {
            ++m_Stats.ProxyLookupCount;
            if (m_Proxies[first].Collider == handle)
            {
                return &m_Proxies[first];
            }
        }
        return nullptr;
    }

    const PhysicsShapeProxy* PhysicsBroadphase::GetPairProxy(uint32_t proxyIndex, Core::Scene::ColliderHandle expected) const
    {
        ++m_Stats.ProxyLookupCount;
        if (proxyIndex < m_Proxies.size() && m_Proxies[proxyIndex].Collider == expected)
        {
            return &m_Proxies[proxyIndex];
        }
        return FindProxy(expected);
    }

    void PhysicsBroadphase::RecordNarrowPhaseTest() const
    {
        ++m_Stats.NarrowPhaseTestCount;
    }

    Core::Scene::EPhysicsSceneQueryResult PhysicsBroadphase::RaycastOverProxies(Core::Container::Span<const PhysicsShapeProxy> proxies,
        const Math::Ray& ray, float maxDistance, Core::Scene::PhysicsRaycastHit& outHit)
    {
        using Result = Core::Scene::EPhysicsSceneQueryResult;
        outHit = {};
        Core::Scene::PhysicsQueryDesc query;
        query.Ray = ray;
        query.MaxDistance = maxDistance;
        query.MaxHits = 1;
        Core::Scene::PhysicsQueryHit hit[1];
        size_t count = 0;
        const auto result = ExecuteQueryOverProxiesImpl(proxies,query,hit,count,false);
        if (result == Result::Success)
        {
            outHit.Collider = hit[0].Collider;
            outHit.Body = hit[0].Body;
            outHit.Entity = hit[0].Entity;
            outHit.bHasEntity = hit[0].bHasEntity;
            outHit.Point = hit[0].Point;
            outHit.Normal = hit[0].Normal;
            outHit.Distance = hit[0].Distance;
            outHit.UserData = hit[0].UserData;
            outHit.SurfaceId = hit[0].SurfaceId;
        }
        return result;
    }

    Core::Scene::EPhysicsSceneQueryResult PhysicsBroadphase::OverlapOverProxies(Core::Container::Span<const PhysicsShapeProxy> proxies,
        const Core::Scene::PhysicsQueryDesc& query, Core::Container::Span<Core::Scene::PhysicsQueryHit> scratch,
        Core::Container::Span<Core::Scene::PhysicsOverlapHit> outHits, size_t& outHitCount)
    {
        using Result = Core::Scene::EPhysicsSceneQueryResult;
        using Kind = Core::Scene::EPhysicsQueryKind;
        outHitCount = 0;
        if (outHits.size() != 0 && outHits.data() == nullptr)
        {
            return Result::InvalidArgument;
        }
        for (auto& hit : outHits)
        {
            hit = {};
        }
        if ((query.Kind != Kind::OverlapSphere && query.Kind != Kind::OverlapBox && query.Kind != Kind::OverlapCapsule)
            || outHits.size() < QueryHitLimit(proxies.size(),query))
        {
            return Result::InvalidArgument;
        }
        size_t count = 0;
        const auto result = ExecuteQueryOverProxiesImpl(proxies,query,scratch,count,false);
        if (result != Result::Success)
        {
            return result;
        }
        for (size_t index = 0; index < count; ++index)
        {
            const auto& source = scratch[index];
            auto& hit = outHits[index];
            hit.Collider = source.Collider;
            hit.Body = source.Body;
            hit.Entity = source.Entity;
            hit.bHasEntity = source.bHasEntity;
            hit.UserData = source.UserData;
            hit.SurfaceId = source.SurfaceId;
            hit.Contact.Normal = source.Normal * -1.0f;
            hit.Contact.Depth = source.Depth;
            hit.Contact.Point = source.Point;
        }
        outHitCount = count;
        return Result::Success;
    }

    Core::Scene::EPhysicsSceneQueryResult PhysicsBroadphase::RaycastQuery(const Math::Ray& ray, float maxDistance,
        Core::Scene::PhysicsRaycastHit& outHit) const
    {
        m_Stats.VisitedNodeCount += m_Proxies.size();
        return RaycastOverProxies({m_Proxies.data(),m_Proxies.size()},ray,maxDistance,outHit);
    }

    bool PhysicsBroadphase::Raycast(const Math::Ray& ray, float maxDistance, Core::Scene::PhysicsRaycastHit& outHit) const
    {
        return RaycastQuery(ray,maxDistance,outHit) == Core::Scene::EPhysicsSceneQueryResult::Success;
    }

    Core::Scene::EPhysicsSceneQueryResult PhysicsBroadphase::AppendOverlapQuery(const Core::Scene::PhysicsQueryDesc& query,
        Core::Container::VariableArray<Core::Scene::PhysicsOverlapHit>& outHits) const
    {
        m_Stats.VisitedNodeCount += m_Proxies.size();
        using Result = Core::Scene::EPhysicsSceneQueryResult;
        using Kind = Core::Scene::EPhysicsQueryKind;
        if (!IsValidQuery(query) || (query.Kind != Kind::OverlapSphere && query.Kind != Kind::OverlapBox && query.Kind != Kind::OverlapCapsule))
        {
            return Result::InvalidArgument;
        }
        const size_t capacity = QueryHitLimit(m_Proxies.size(),query);
        Core::Container::VariableArray<Core::Scene::PhysicsQueryHit> scratch(capacity);
        Core::Container::VariableArray<Core::Scene::PhysicsOverlapHit> converted(capacity);
        size_t count = 0;
        const auto result = OverlapOverProxies({m_Proxies.data(),m_Proxies.size()},query,
            {scratch.data(),scratch.size()},{converted.data(),converted.size()},count);
        if (result == Result::Success)
        {
            static_assert(std::is_nothrow_copy_constructible_v<Core::Scene::PhysicsOverlapHit>);
            converted.resize(count);
            outHits.insert(outHits.end(),converted.begin(),converted.end());
        }
        return result;
    }

    void PhysicsBroadphase::OverlapSphere(const Math::Sphere& sphere,
        Core::Container::VariableArray<Core::Scene::PhysicsOverlapHit>& outHits) const
    {
        Core::Scene::PhysicsQueryDesc query;
        query.Kind = Core::Scene::EPhysicsQueryKind::OverlapSphere;
        query.Sphere = sphere;
        (void)AppendOverlapQuery(query,outHits);
    }

    void PhysicsBroadphase::OverlapBox(const Math::OBB& box,
        Core::Container::VariableArray<Core::Scene::PhysicsOverlapHit>& outHits) const
    {
        Core::Scene::PhysicsQueryDesc query;
        query.Kind = Core::Scene::EPhysicsQueryKind::OverlapBox;
        query.Box = box;
        (void)AppendOverlapQuery(query,outHits);
    }

    void PhysicsBroadphase::OverlapCapsule(const Math::Capsule& capsule,
        Core::Container::VariableArray<Core::Scene::PhysicsOverlapHit>& outHits) const
    {
        Core::Scene::PhysicsQueryDesc query;
        query.Kind = Core::Scene::EPhysicsQueryKind::OverlapCapsule;
        query.Capsule = capsule;
        (void)AppendOverlapQuery(query,outHits);
    }

    Math::AABB PhysicsBroadphase::CalculateBounds(const PhysicsShapeProxy& proxy)
    {
        switch (proxy.Shape)
        {
        case EPhysicsProxyShape::Sphere:
            return Math::AABB::FromCenterExtents(proxy.Sphere.Center, Math::Vector3(proxy.Sphere.Radius));
        case EPhysicsProxyShape::Capsule:
        {
            Math::AABB bounds = Math::AABB::FromCenterExtents(
                proxy.Capsule.PointA,
                Math::Vector3(proxy.Capsule.Radius));
            bounds.Merge(Math::AABB::FromCenterExtents(
                proxy.Capsule.PointB,
                Math::Vector3(proxy.Capsule.Radius)));
            return bounds;
        }
        case EPhysicsProxyShape::Box:
        {
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
        case EPhysicsProxyShape::HeightField:
        {
            Math::AABB bounds;
            if (!proxy.HeightField || !proxy.HeightField->GetBounds(bounds))
            {
                return Math::AABB();
            }
            // 地表下は固体なので、最大水平幅（最低 1 m）だけ下へ広げる。
            // この下端より深い物体はブロードフェーズの問い合わせでは扱わない。
            const float depth = std::fmax(1.0f,
                std::fmax(bounds.Max.x - bounds.Min.x, bounds.Max.z - bounds.Min.z));
            bounds.Min.y -= depth;
            bounds.Min += proxy.HeightFieldOrigin;
            bounds.Max += proxy.HeightFieldOrigin;
            return bounds;
        }
        }
        return Math::AABB();
    }

    bool PhysicsBroadphase::ComputeContact(
        const PhysicsShapeProxy& first,
        const PhysicsShapeProxy& second,
        Math::GeometryContact& outContact)
    {
        switch (first.Shape)
        {
        case EPhysicsProxyShape::Sphere:
            switch (second.Shape)
            {
            case EPhysicsProxyShape::Sphere:
                return Math::ComputeContact(first.Sphere, second.Sphere, outContact);
            case EPhysicsProxyShape::Box:
                return Math::ComputeContact(first.Sphere, second.Box, outContact);
            case EPhysicsProxyShape::Capsule:
                if (!Math::ComputeContact(second.Capsule, first.Sphere, outContact))
                {
                    return false;
                }
                ReverseContact(outContact);
                return true;
            case EPhysicsProxyShape::HeightField:
                return ComputeOverlap(first.Sphere, second, outContact);
            }
            return false;
        case EPhysicsProxyShape::Box:
            switch (second.Shape)
            {
            case EPhysicsProxyShape::Sphere:
                if (!Math::ComputeContact(second.Sphere, first.Box, outContact))
                {
                    return false;
                }
                ReverseContact(outContact);
                return true;
            case EPhysicsProxyShape::Box:
                return Math::ComputeContact(first.Box, second.Box, outContact);
            case EPhysicsProxyShape::Capsule:
                if (!Math::ComputeContact(second.Capsule, first.Box, outContact))
                {
                    return false;
                }
                ReverseContact(outContact);
                return true;
            case EPhysicsProxyShape::HeightField:
                return ComputeOverlap(first.Box, second, outContact);
            }
            return false;
        case EPhysicsProxyShape::Capsule:
            switch (second.Shape)
            {
            case EPhysicsProxyShape::Sphere:
                return Math::ComputeContact(first.Capsule, second.Sphere, outContact);
            case EPhysicsProxyShape::Box:
                return Math::ComputeContact(first.Capsule, second.Box, outContact);
            case EPhysicsProxyShape::Capsule:
                return Math::ComputeContact(first.Capsule, second.Capsule, outContact);
            case EPhysicsProxyShape::HeightField:
                return ComputeOverlap(first.Capsule, second, outContact);
            }
            return false;
        case EPhysicsProxyShape::HeightField:
            switch (second.Shape)
            {
            case EPhysicsProxyShape::Sphere:
            {
                Math::Sphere local = second.Sphere;
                local.Center -= first.HeightFieldOrigin;
                Core::Container::VariableArray<Math::GeometryContact> contacts;
                first.HeightField->OverlapSphere(local, contacts);
                return StoreHeightFieldContact(first, contacts, outContact, false);
            }
            case EPhysicsProxyShape::Box:
            {
                Math::OBB local = second.Box;
                local.Center -= first.HeightFieldOrigin;
                Core::Container::VariableArray<Math::GeometryContact> contacts;
                first.HeightField->OverlapBox(local, contacts);
                return StoreHeightFieldContact(first, contacts, outContact, false);
            }
            case EPhysicsProxyShape::Capsule:
            {
                Math::Capsule local = second.Capsule;
                local.PointA -= first.HeightFieldOrigin;
                local.PointB -= first.HeightFieldOrigin;
                Core::Container::VariableArray<Math::GeometryContact> contacts;
                first.HeightField->OverlapCapsule(local, contacts);
                return StoreHeightFieldContact(first, contacts, outContact, false);
            }
            case EPhysicsProxyShape::HeightField:
                return false;
            }
            return false;
        }
        return false;
    }

    void PhysicsBroadphase::BuildCandidatePairs()
    {
        Core::Container::VariableArray<SweepEndpoint> endpoints;
        Core::Container::VariableArray<size_t> heightFieldIndices;
        Core::Container::VariableArray<size_t> shapeIndices;
        endpoints.reserve(m_NonTreeProxyIndices.size() * 2);
        for (size_t index = 0; index < m_Proxies.size(); ++index)
        {
            const PhysicsShapeProxy& proxy = m_Proxies[index];
            // 地形の AABB は全コライダーと重なるため、SAP の走査を膨らませないよう別枠にする。
            if (proxy.Shape == EPhysicsProxyShape::HeightField)
            {
                heightFieldIndices.push_back(index);
                continue;
            }
            shapeIndices.push_back(index);
        }
        for (uint32_t index : m_NonTreeProxyIndices)
        {
            const PhysicsShapeProxy& proxy = m_Proxies[index];
            if (proxy.Shape == EPhysicsProxyShape::HeightField)
            {
                continue;
            }
            endpoints.push_back(SweepEndpoint{proxy.Bounds.Min.x, index, proxy.Collider, true, proxy.Layer, proxy.Mask});
            endpoints.push_back(SweepEndpoint{proxy.Bounds.Max.x, index, proxy.Collider, false, proxy.Layer, proxy.Mask});
        }

        if (endpoints.size() > 1)
        {
            std::sort(endpoints.begin(), endpoints.end(), IsEndpointLess);
        }

        m_CandidatePairs.clear();
        const auto appendPair = [&](uint32_t firstIndex, uint32_t secondIndex)
        {
            if (IsHandleLess(m_Proxies[secondIndex].Collider, m_Proxies[firstIndex].Collider))
            {
                std::swap(firstIndex, secondIndex);
            }
            m_CandidatePairs.push_back({m_Proxies[firstIndex].Collider, m_Proxies[secondIndex].Collider,
                firstIndex, secondIndex});
        };
        const auto canPair = [](const PhysicsShapeProxy& first, const PhysicsShapeProxy& second)
        {
            return first.Bounds.Min.x <= second.Bounds.Max.x && first.Bounds.Max.x >= second.Bounds.Min.x
                && first.Bounds.Min.y <= second.Bounds.Max.y && first.Bounds.Max.y >= second.Bounds.Min.y
                && first.Bounds.Min.z <= second.Bounds.Max.z && first.Bounds.Max.z >= second.Bounds.Min.z
                && Core::Scene::CanPhysicsLayersInteract(first.Layer, first.Mask, second.Layer, second.Mask)
                && CanProxyMobilitiesInteract(first.Mobility, second.Mobility);
        };
        Core::Container::VariableArray<SweepEndpoint> active;
        for (const SweepEndpoint& endpoint : endpoints)
        {
            if (endpoint.bIsMin)
            {
                for (const SweepEndpoint& other : active)
                {
                    const PhysicsShapeProxy& first = m_Proxies[endpoint.ProxyIndex];
                    const PhysicsShapeProxy& second = m_Proxies[other.ProxyIndex];
                    if (!Core::Scene::CanPhysicsLayersInteract(endpoint.Layer, endpoint.Mask, other.Layer, other.Mask)
                        || !CanProxyMobilitiesInteract(first.Mobility, second.Mobility)
                        || !(first.Bounds.Min.y <= second.Bounds.Max.y && first.Bounds.Max.y >= second.Bounds.Min.y
                            && first.Bounds.Min.z <= second.Bounds.Max.z && first.Bounds.Max.z >= second.Bounds.Min.z))
                    {
                        continue;
                    }
                    appendPair(static_cast<uint32_t>(endpoint.ProxyIndex), static_cast<uint32_t>(other.ProxyIndex));
                }
                active.push_back(endpoint);
                continue;
            }

            for (size_t index = 0; index < active.size(); ++index)
            {
                if (active[index].Collider == endpoint.Collider)
                {
                    active[index] = active.back();
                    active.pop_back();
                    break;
                }
            }
        }

        if (m_StaticTree)
        {
            // SAP と木の所属は排他的なので、この経路では各組を動く側から 1 回だけ作る。
            for (uint32_t index : m_NonTreeProxyIndices)
            {
                const PhysicsShapeProxy& proxy = m_Proxies[index];
                if (proxy.Shape == EPhysicsProxyShape::HeightField)
                {
                    continue;
                }
                m_StaticTree->Tree.QueryAabb(proxy.Bounds, [&](uint32_t item)
                {
                    const uint32_t staticIndex = m_TreeProxyIndices[item];
                    if (canPair(proxy, m_Proxies[staticIndex]))
                    {
                        appendPair(index, staticIndex);
                    }
                });
            }

            for (uint32_t index : m_TreeProxyIndices)
            {
                const PhysicsShapeProxy& proxy = m_Proxies[index];
                if (!CanProxyMobilitiesInteract(proxy.Mobility, EPhysicsProxyMobility::Static))
                {
                    continue;
                }
                m_StaticTree->Tree.QueryAabb(proxy.Bounds, [&](uint32_t item)
                {
                    const uint32_t otherIndex = m_TreeProxyIndices[item];
                    const PhysicsShapeProxy& other = m_Proxies[otherIndex];
                    // 両方が能動側なら、ハンドルが小さい側からだけ作る。
                    if (index == otherIndex
                        || (CanProxyMobilitiesInteract(other.Mobility, EPhysicsProxyMobility::Static)
                            && !IsHandleLess(proxy.Collider, other.Collider)))
                    {
                        return;
                    }
                    if (canPair(proxy, other))
                    {
                        appendPair(index, otherIndex);
                    }
                });
            }
        }

        if (m_CandidatePairs.size() > 1)
        {
            std::sort(m_CandidatePairs.begin(), m_CandidatePairs.end(), IsPairLess);
#ifndef NDEBUG
            for (size_t index = 1; index < m_CandidatePairs.size(); ++index)
            {
                assert(IsPairLess(m_CandidatePairs[index - 1], m_CandidatePairs[index]));
            }
#endif
        }

        if (heightFieldIndices.empty())
        {
            return;
        }

        Core::Container::VariableArray<PhysicsCandidatePair> mergedPairs;
        mergedPairs.reserve(m_CandidatePairs.size());
        size_t sapIndex = 0;
        size_t heightFieldStart = 0;
        size_t shapeStart = 0;
        // Collider 順の先頭を外側にし、後続の異種だけを走査して地形の組も IsPairLess 順に作る。
        for (size_t firstIndex = 0; firstIndex < m_Proxies.size(); ++firstIndex)
        {
            const PhysicsShapeProxy& first = m_Proxies[firstIndex];
            const bool bHeightField = first.Shape == EPhysicsProxyShape::HeightField;
            if (bHeightField)
            {
                ++heightFieldStart;
            }
            else
            {
                ++shapeStart;
            }
            const auto& secondIndices = bHeightField ? shapeIndices : heightFieldIndices;
            const size_t secondStart = bHeightField ? shapeStart : heightFieldStart;
            for (size_t index = secondStart; index < secondIndices.size(); ++index)
            {
                const PhysicsShapeProxy& second = m_Proxies[secondIndices[index]];
                if (!(first.Bounds.Min.x <= second.Bounds.Max.x && first.Bounds.Max.x >= second.Bounds.Min.x
                    && first.Bounds.Min.y <= second.Bounds.Max.y && first.Bounds.Max.y >= second.Bounds.Min.y
                    && first.Bounds.Min.z <= second.Bounds.Max.z && first.Bounds.Max.z >= second.Bounds.Min.z)
                    || !Core::Scene::CanPhysicsLayersInteract(first.Layer, first.Mask, second.Layer, second.Mask)
                    || !CanProxyMobilitiesInteract(first.Mobility, second.Mobility))
                {
                    continue;
                }

                const PhysicsCandidatePair pair{first.Collider, second.Collider,
                    static_cast<uint32_t>(firstIndex), static_cast<uint32_t>(secondIndices[index])};
                // 整列済みの列には地形の組が無いので、重複検査せず線形に併合する。
                while (sapIndex < m_CandidatePairs.size() && IsPairLess(m_CandidatePairs[sapIndex], pair))
                {
                    mergedPairs.push_back(m_CandidatePairs[sapIndex++]);
                }
                mergedPairs.push_back(pair);
            }
        }
        while (sapIndex < m_CandidatePairs.size())
        {
            mergedPairs.push_back(m_CandidatePairs[sapIndex++]);
        }
        m_CandidatePairs = std::move(mergedPairs);
    }
} // namespace NorvesLib::Modules::Physics
