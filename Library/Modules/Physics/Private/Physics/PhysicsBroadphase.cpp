#include "Physics/PhysicsBroadphase.h"

#include "Math/GeometryIntersection.h"
#include "Math/VectorUtils.h"

#include <cmath>
#include <cfloat>
#include <limits>

namespace NorvesLib::Modules::Physics
{
    namespace
    {
        struct SweepEndpoint
        {
            float Coordinate = 0.0f;
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

        bool RaycastProxy(const Math::Ray& ray, const PhysicsShapeProxy& proxy, double& outDistance)
        {
            if (proxy.Shape == EPhysicsProxyShape::Sphere)
            {
                return RaycastSphere(ray, proxy.Sphere, outDistance);
            }
            if (proxy.Shape == EPhysicsProxyShape::Box)
            {
                return RaycastBox(ray,proxy.Box,outDistance);
            }
            return RaycastCapsule(ray, proxy.Capsule, outDistance);
        }

        Math::Vector3 CalculateRayNormal(const Math::Ray& ray, const PhysicsShapeProxy& proxy, double distance)
        {
            if (distance == 0.0f)
            {
                return Math::Vector3();
            }
            if (proxy.Shape == EPhysicsProxyShape::Sphere)
            {
                return RayNormal((RayDouble(ray.Origin) - RayDouble(proxy.Sphere.Center)) + RayDouble(ray.Direction)*distance);
            }
            if (proxy.Shape == EPhysicsProxyShape::Capsule)
            {
                const auto segment = RayDouble(proxy.Capsule.PointB) - RayDouble(proxy.Capsule.PointA);
                const auto offset = (RayDouble(ray.Origin) - RayDouble(proxy.Capsule.PointA)) + RayDouble(ray.Direction)*distance;
                const double lengthSquared = RayDot(segment,segment);
                const double parameter = lengthSquared > 0 ? std::fmax(0.0,std::fmin(1.0,RayDot(offset,segment)/lengthSquared)) : 0;
                const auto normal = offset - segment*parameter;
                return RayNormal(normal);
            }

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
            hit.UserData = proxy.UserData;
            outHits.push_back(hit);
        }

        template<typename TQuery>
        void AppendOverlapHits(
            const TQuery& query,
            const Core::Container::VariableArray<PhysicsShapeProxy>& proxies,
            Core::Container::VariableArray<Core::Scene::PhysicsOverlapHit>& outHits)
        {
            for (const PhysicsShapeProxy& proxy : proxies)
            {
                Math::GeometryContact contact;
                if (ComputeOverlap(query, proxy, contact))
                {
                    AppendOverlapHit(proxy, contact, outHits);
                }
            }
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
            return proxy.Shape == EPhysicsProxyShape::Sphere ? IsValidQueryShape(proxy.Sphere)
                : proxy.Shape == EPhysicsProxyShape::Box ? IsValidQueryShape(proxy.Box)
                : proxy.Shape == EPhysicsProxyShape::Capsule && IsValidQueryShape(proxy.Capsule);
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
            if (proxy.Shape == EPhysicsProxyShape::Box)
            {
                // 許容Gram誤差1e-4の軸ではdotの逆写像と前向きboundsが一致しない。
                // 3軸の誤差を見込み、最大半径長の1e-3を加えて両領域を包む。
                const float padding = 1e-3f * std::fmax(proxy.Box.HalfExtents.x,
                    std::fmax(proxy.Box.HalfExtents.y,proxy.Box.HalfExtents.z));
                const Math::Vector3 expansion(padding,padding,padding);
                bounds.Min -= expansion;
                bounds.Max += expansion;
            }
            return bounds;
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
            if (proxy.Shape == EPhysicsProxyShape::Sphere)
            {
                includeSphere(proxy.Sphere);
            }
            else if (proxy.Shape == EPhysicsProxyShape::Box)
            {
                includeBox(proxy.Box);
            }
            else
            {
                includeCapsule(proxy.Capsule);
            }
            const double squared = scale * scale;
            return bRepresentable && squared * squared * squared <= static_cast<double>(FLT_MAX) / 4096.0;
        }

        Core::Scene::EPhysicsSceneQueryResult ValidateProxyForQuery(const PhysicsShapeProxy& proxy,
            const Core::Scene::PhysicsQueryDesc& query)
        {
            using Result = Core::Scene::EPhysicsSceneQueryResult;
            if (!IsValidProxyGeometry(proxy))
            {
                return Result::InvalidArgument;
            }
            if (!query.Filter.Accepts(proxy.Layer,proxy.bTrigger,proxy.Collider,proxy.Body))
            {
                return Result::NoHit;
            }
            if (!HasRepresentableQueryScale(proxy,query))
            {
                return Result::InvalidArgument;
            }
            const bool bSweep = query.Kind == Core::Scene::EPhysicsQueryKind::SweepSphere ||
                query.Kind == Core::Scene::EPhysicsQueryKind::SweepCapsule;
            if (bSweep && proxy.Shape == EPhysicsProxyShape::Box && !Math::IsValidSweepBox(proxy.Box))
            {
                return Result::InvalidArgument;
            }
            return Result::Success;
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

    Core::Scene::EPhysicsSceneQueryResult PhysicsBroadphase::QueryProxy(const PhysicsShapeProxy& proxy,
        const Core::Scene::PhysicsQueryDesc& query, Core::Scene::PhysicsQueryHit& outHit)
    {
        using Result = Core::Scene::EPhysicsSceneQueryResult;
        using Kind = Core::Scene::EPhysicsQueryKind;
        outHit = {};
        if (!IsValidQuery(query))
        {
            return Result::InvalidArgument;
        }
        const auto validation = ValidateProxyForQuery(proxy,query);
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
            if (!RaycastProxy(ray, proxy, preciseDistance) || !StoreRayDistance(preciseDistance,hit.Distance)
                || hit.Distance > query.MaxDistance)
            {
                return Result::NoHit;
            }
            if (!RayPoint(ray,preciseDistance,hit.Point))
            {
                return Result::InvalidArgument;
            }
            hit.Normal = CalculateRayNormal(ray,proxy,preciseDistance);
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
        outHit = hit;
        return Result::Success;
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
        };
        Core::Scene::EPhysicsSceneQueryResult CheckQueryCandidate(const PhysicsShapeProxy& proxy, void* context)
        {
            return ValidateProxyForQuery(proxy,static_cast<QueryCollection*>(context)->Query);
        }
        Core::Scene::EPhysicsSceneQueryResult CollectQueryCandidate(const PhysicsShapeProxy& proxy, void* context)
        {
            using Result = Core::Scene::EPhysicsSceneQueryResult;
            auto& collection = *static_cast<QueryCollection*>(context);
            Core::Scene::PhysicsQueryHit hit;
            const auto result = PhysicsBroadphase::QueryProxy(proxy,collection.Query,hit);
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

    Core::Scene::EPhysicsSceneQueryResult PhysicsBroadphase::ExecuteQueryOverProxies(
        Core::Container::Span<const PhysicsShapeProxy> proxies, const Core::Scene::PhysicsQueryDesc& query,
        Core::Container::Span<Core::Scene::PhysicsQueryHit> outHits, size_t& outHitCount)
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
        if (!IsValidQuery(query) || (proxies.size() != 0 && proxies.data() == nullptr))
        {
            return Result::InvalidArgument;
        }
        const size_t limit = QueryHitLimit(proxies.size(),query);
        if (outHits.size() < limit)
        {
            return Result::InvalidArgument;
        }
        QueryCollection collection{query,outHits,limit};
        Result result;
        if (query.Kind == Core::Scene::EPhysicsQueryKind::RaycastClosest || query.Kind == Core::Scene::EPhysicsQueryKind::RaycastAll)
        {
            result = VisitProxiesAlongRay(proxies,query.Ray,query.MaxDistance,CollectQueryCandidate,&collection,CheckQueryCandidate);
        }
        else
        {
            Math::AABB bounds;
            if (TryQueryBounds(query,bounds))
            {
                result = VisitProxiesInAabb(proxies,bounds,CollectQueryCandidate,&collection,CheckQueryCandidate);
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

    Core::Scene::EPhysicsSceneQueryResult PhysicsBroadphase::ExecuteQuery(
        const Core::Scene::PhysicsQueryDesc& query,
        Core::Container::VariableArray<Core::Scene::PhysicsQueryHit>& outHits) const
    {
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
            double preciseDistance = 0;
            if (!RaycastProxy(ray, proxy, preciseDistance) || !StoreRayDistance(preciseDistance,distance) || distance > maxDistance)
            {
                continue;
            }
            if (bFound && distance > closestDistance)
            {
                continue;
            }

            Math::Vector3 point;
            if (!RayPoint(ray,preciseDistance,point))
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
            outHit.Point = point;
            outHit.Normal = CalculateRayNormal(ray,proxy,preciseDistance);
            outHit.UserData = proxy.UserData;
        }
        return bFound;
    }

    void PhysicsBroadphase::OverlapSphere(
        const Math::Sphere& sphere,
        Core::Container::VariableArray<Core::Scene::PhysicsOverlapHit>& outHits) const
    {
        AppendOverlapHits(sphere, m_Proxies, outHits);
    }

    void PhysicsBroadphase::OverlapBox(
        const Math::OBB& box,
        Core::Container::VariableArray<Core::Scene::PhysicsOverlapHit>& outHits) const
    {
        AppendOverlapHits(box, m_Proxies, outHits);
    }

    void PhysicsBroadphase::OverlapCapsule(
        const Math::Capsule& capsule,
        Core::Container::VariableArray<Core::Scene::PhysicsOverlapHit>& outHits) const
    {
        AppendOverlapHits(capsule, m_Proxies, outHits);
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
            endpoints.push_back(SweepEndpoint{proxy.Bounds.Min.x, proxy.Collider, true, proxy.Layer, proxy.Mask});
            endpoints.push_back(SweepEndpoint{proxy.Bounds.Max.x, proxy.Collider, false, proxy.Layer, proxy.Mask});
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
        Core::Container::VariableArray<SweepEndpoint> active;
        for (const SweepEndpoint& endpoint : endpoints)
        {
            if (endpoint.bIsMin)
            {
                for (const SweepEndpoint& other : active)
                {
                    if (!Core::Scene::CanPhysicsLayersInteract(endpoint.Layer, endpoint.Mask, other.Layer, other.Mask))
                    {
                        continue;
                    }
                    PhysicsCandidatePair pair;
                    pair.First = IsHandleLess(endpoint.Collider, other.Collider) ? endpoint.Collider : other.Collider;
                    pair.Second = IsHandleLess(endpoint.Collider, other.Collider) ? other.Collider : endpoint.Collider;
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
