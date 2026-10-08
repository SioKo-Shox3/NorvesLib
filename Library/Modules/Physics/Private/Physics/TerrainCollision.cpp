#include "Physics/TerrainCollision.h"
#include "Math/GeometryIntersection.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace NorvesLib::Modules::Physics
{
    using V = Math::Vector3;
    namespace
    {
        float Dot(const V& a, const V& b)
        {
            return Math::VectorUtils::Dot(a, b);
        }
        V Cross(const V& a, const V& b)
        {
            return Math::VectorUtils::Cross(a, b);
        }
        V World(const PhysicsShapeProxy& p, const V& v)
        {
            return p.TerrainTransform.TransformPoint(p.TerrainLocalPose.TransformPoint(v));
        }
        V InversePoint(const Math::Transform& transform, const V& point)
        {
            const auto& q = transform.rotation;
            const V unrotated = Math::Quaternion(-q.x, -q.y, -q.z, q.w) * (point - transform.position);
            return {unrotated.x / transform.scale.x, unrotated.y / transform.scale.y, unrotated.z / transform.scale.z};
        }
        V Local(const PhysicsShapeProxy& p, const V& v)
        {
            return InversePoint(p.TerrainLocalPose, InversePoint(p.TerrainTransform, v));
        }
        template <class F> void Triangles(const PhysicsShapeProxy& proxy, const Math::AABB& bounds, F visit)
        {
            if (!proxy.HeightField)
                return;
            V lo = Local(proxy, bounds.Min), hi = lo;
            for (unsigned mask = 1; mask < 8; ++mask)
            {
                const V p =
                    Local(proxy, {mask & 1 ? bounds.Max.x : bounds.Min.x, mask & 2 ? bounds.Max.y : bounds.Min.y,
                                  mask & 4 ? bounds.Max.z : bounds.Min.z});
                lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
                hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
            }
            const auto& field = *proxy.HeightField;
            const float spacing = field.GetSpacing();
            if (hi.x < 0 || hi.z < 0 || lo.x > field.GetBounds().Max.x || lo.z > field.GetBounds().Max.z)
                return;
            const auto cell = [&](float value, uint32_t size) {
                return uint32_t(std::clamp(std::floor(double(value) / spacing), 0.0, double(size - 2)));
            };
            const auto x0 = cell(lo.x, field.GetWidth()), x1 = cell(hi.x, field.GetWidth());
            const auto z0 = cell(lo.z, field.GetDepth()), z1 = cell(hi.z, field.GetDepth());
            for (uint32_t z = z0; z <= z1; ++z)
                for (uint32_t x = x0; x <= x1; ++x)
                    for (uint32_t t = 0; t < 2; ++t)
                    {
                        V a, b, c;
                        field.GetTriangle(x, z, t, a, b, c);
                        visit(World(proxy, a), World(proxy, b), World(proxy, c));
                    }
        }
        V PointTriangle(const V& p, const V& a, const V& b, const V& c)
        {
            const V ab = b - a, ac = c - a, ap = p - a;
            const float d1 = Dot(ab, ap), d2 = Dot(ac, ap);
            if (d1 <= 0 && d2 <= 0)
                return a;
            const V bp = p - b;
            const float d3 = Dot(ab, bp), d4 = Dot(ac, bp);
            if (d3 >= 0 && d4 <= d3)
                return b;
            const float vc = d1 * d4 - d3 * d2;
            if (vc <= 0 && d1 >= 0 && d3 <= 0)
                return a + ab * (d1 / (d1 - d3));
            const V cp = p - c;
            const float d5 = Dot(ab, cp), d6 = Dot(ac, cp);
            if (d6 >= 0 && d5 <= d6)
                return c;
            const float vb = d5 * d2 - d1 * d6;
            if (vb <= 0 && d2 >= 0 && d6 <= 0)
                return a + ac * (d2 / (d2 - d6));
            const float va = d3 * d6 - d5 * d4;
            if (va <= 0 && d4 - d3 >= 0 && d5 - d6 >= 0)
                return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
            const float denominator = va + vb + vc;
            if (std::abs(denominator) < 1e-20f)
                return a;
            return a + ab * (vb / denominator) + ac * (vc / denominator);
        }
        void Segments(const V& p, const V& q, const V& a, const V& b, V& first, V& second)
        {
            const V d1 = q - p, d2 = b - a, r = p - a;
            const float aa = Dot(d1, d1), ee = Dot(d2, d2), f = Dot(d2, r);
            float s = 0, t = 0;
            if (aa <= 1e-12f)
                t = ee > 1e-12f ? std::clamp(f / ee, 0.f, 1.f) : 0;
            else
            {
                const float c = Dot(d1, r);
                if (ee <= 1e-12f)
                    s = std::clamp(-c / aa, 0.f, 1.f);
                else
                {
                    const float bb = Dot(d1, d2), denominator = aa * ee - bb * bb;
                    if (denominator > 1e-12f)
                        s = std::clamp((bb * f - c * ee) / denominator, 0.f, 1.f);
                    t = (bb * s + f) / ee;
                    if (t < 0)
                    {
                        t = 0;
                        s = std::clamp(-c / aa, 0.f, 1.f);
                    }
                    else if (t > 1)
                    {
                        t = 1;
                        s = std::clamp((bb - c) / aa, 0.f, 1.f);
                    }
                }
            }
            first = p + d1 * s;
            second = a + d2 * t;
        }
        float SegmentTriangle(const V& p, const V& q, const V& a, const V& b, const V& c, V& axis, V& triangle)
        {
            float hit = 0;
            if (Math::RayIntersectsTriangle({p, q - p}, a, b, c, hit) && hit <= 1)
            {
                axis = triangle = p + (q - p) * hit;
                return 0;
            }
            axis = p;
            triangle = PointTriangle(p, a, b, c);
            float best = (axis - triangle).LengthSquared();
            const auto take = [&](const V& u, const V& v) {
                const float d = (u - v).LengthSquared();
                if (d < best)
                {
                    best = d;
                    axis = u;
                    triangle = v;
                }
            };
            take(q, PointTriangle(q, a, b, c));
            V x, y;
            Segments(p, q, a, b, x, y);
            take(x, y);
            Segments(p, q, b, c, x, y);
            take(x, y);
            Segments(p, q, c, a, x, y);
            take(x, y);
            return std::sqrt(std::max(0.f, best));
        }
        Math::AABB CapsuleBounds(const Math::Capsule& c)
        {
            auto bounds = Math::AABB::FromCenterExtents(c.PointA, V(c.Radius));
            bounds.Merge(Math::AABB::FromCenterExtents(c.PointB, V(c.Radius)));
            return bounds;
        }
        V ContactNormal(const V& axis, const V& triangle, const V& a, const V& b, const V& c, const V& center)
        {
            const auto delta = axis - triangle;
            if (delta.LengthSquared() > 1e-12f)
                return delta.Normalized();
            auto normal = Cross(b - a, c - a).Normalized();
            if (Dot(center - a, normal) < 0)
                normal *= -1;
            return normal;
        }
    } // namespace
    bool IsValidTerrainProxy(const PhysicsShapeProxy& proxy)
    {
        const auto valid = [](const Math::Transform& t) {
            const auto& s = t.scale;
            const auto& p = t.position;
            const auto& q = t.rotation;
            const float length = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
            return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && std::isfinite(length) &&
                   std::abs(length - 1.f) < 1e-3f && std::isfinite(s.x) && std::isfinite(s.y) && std::isfinite(s.z) &&
                   std::abs(s.x) > 1e-6f && std::abs(s.y) > 1e-6f && std::abs(s.z) > 1e-6f;
        };
        return proxy.HeightField && proxy.HeightField->GetWidth() >= 2 && proxy.HeightField->GetDepth() >= 2 &&
               valid(proxy.TerrainTransform) && valid(proxy.TerrainLocalPose);
    }
    Math::AABB TerrainBounds(const PhysicsShapeProxy& proxy)
    {
        const auto& b = proxy.HeightField->GetBounds();
        V first = World(proxy, b.Min);
        Math::AABB result(first, first);
        for (unsigned mask = 1; mask < 8; ++mask)
        {
            const V point = World(
                proxy, {mask & 1 ? b.Max.x : b.Min.x, mask & 2 ? b.Max.y : b.Min.y, mask & 4 ? b.Max.z : b.Min.z});
            result.Merge(Math::AABB(point, point));
        }
        return result;
    }
    bool RaycastTerrain(const PhysicsShapeProxy& proxy, const Math::Ray& ray, float maximum, float& distance, V& normal)
    {
        if (!IsValidTerrainProxy(proxy))
            return false;
        const auto bounds = TerrainBounds(proxy);
        double near = 0, far = maximum;
        const float o[] = {ray.Origin.x, ray.Origin.y, ray.Origin.z},
                    d[] = {ray.Direction.x, ray.Direction.y, ray.Direction.z};
        const float lo[] = {bounds.Min.x, bounds.Min.y, bounds.Min.z},
                    hi[] = {bounds.Max.x, bounds.Max.y, bounds.Max.z};
        for (unsigned i = 0; i < 3; ++i)
        {
            if (std::abs(d[i]) < 1e-12f)
            {
                if (o[i] < lo[i] || o[i] > hi[i])
                    return false;
                continue;
            }
            const double a = (double(lo[i]) - o[i]) / d[i], b = (double(hi[i]) - o[i]) / d[i];
            near = std::max(near, std::min(a, b));
            far = std::min(far, std::max(a, b));
            if (near > far)
                return false;
        }
        if (far < 0)
            return false;
        const V start = ray.Origin + ray.Direction * float(near), end = ray.Origin + ray.Direction * float(far);
        Math::AABB area(start, start);
        area.Merge(Math::AABB(end, end));
        bool found = false;
        float best = maximum;
        Triangles(proxy, area, [&](const V& a, const V& b, const V& c) {
            float t = 0;
            if (Math::RayIntersectsTriangle(ray, a, b, c, t) && t <= best)
            {
                best = t;
                normal = Cross(b - a, c - a).Normalized();
                if (Dot(normal, ray.Direction) > 0)
                    normal *= -1;
                found = true;
            }
        });
        if (found)
            distance = best;
        return found;
    }
    bool TerrainContact(const Math::Capsule& capsule, const PhysicsShapeProxy& proxy, Math::GeometryContact& out)
    {
        bool found = false;
        float deepest = -1;
        Triangles(proxy, CapsuleBounds(capsule), [&](const V& a, const V& b, const V& c) {
            V axis, triangle;
            const float distance = SegmentTriangle(capsule.PointA, capsule.PointB, a, b, c, axis, triangle);
            if (distance > capsule.Radius)
                return;
            const float depth = capsule.Radius - distance;
            if (depth <= deepest)
                return;
            const auto normal = ContactNormal(axis, triangle, a, b, c, (capsule.PointA + capsule.PointB) * .5f);
            out.Normal = normal * -1;
            out.Depth = depth;
            out.Point = (axis - normal * capsule.Radius + triangle) * .5f;
            deepest = depth;
            found = true;
        });
        return found;
    }
    bool TerrainContact(const Math::Sphere& sphere, const PhysicsShapeProxy& proxy, Math::GeometryContact& out)
    {
        return TerrainContact(Math::Capsule(sphere.Center, sphere.Center, sphere.Radius), proxy, out);
    }
    bool TerrainContact(const Math::OBB& box, const PhysicsShapeProxy& proxy, Math::GeometryContact& out)
    {
        const V ext(std::abs(box.Axes[0].x) * box.HalfExtents.x + std::abs(box.Axes[1].x) * box.HalfExtents.y +
                        std::abs(box.Axes[2].x) * box.HalfExtents.z,
                    std::abs(box.Axes[0].y) * box.HalfExtents.x + std::abs(box.Axes[1].y) * box.HalfExtents.y +
                        std::abs(box.Axes[2].y) * box.HalfExtents.z,
                    std::abs(box.Axes[0].z) * box.HalfExtents.x + std::abs(box.Axes[1].z) * box.HalfExtents.y +
                        std::abs(box.Axes[2].z) * box.HalfExtents.z);
        bool found = false;
        float deepest = -1;
        Triangles(proxy, Math::AABB::FromCenterExtents(box.Center, ext), [&](const V& a, const V& b, const V& c) {
            const auto local = [&](const V& p) {
                const auto v = p - box.Center;
                return V(Dot(v, box.Axes[0]), Dot(v, box.Axes[1]), Dot(v, box.Axes[2]));
            };
            const V p = local(a), q = local(b), r = local(c), edges[] = {q - p, r - q, p - r};
            float depth = std::numeric_limits<float>::max();
            V axisBest;
            bool separated = false;
            const auto axis = [&](V n) {
                const float length = n.Length();
                if (length < 1e-7f || separated)
                    return;
                n = n / length;
                const float x = Dot(p, n), y = Dot(q, n), z = Dot(r, n), min = std::min({x, y, z}),
                            max = std::max({x, y, z});
                const float radius = std::abs(n.x) * box.HalfExtents.x + std::abs(n.y) * box.HalfExtents.y +
                                     std::abs(n.z) * box.HalfExtents.z;
                if (min > radius || max < -radius)
                {
                    separated = true;
                    return;
                }
                const float overlap = std::min(radius - min, max + radius);
                if (overlap < depth)
                {
                    depth = overlap;
                    axisBest = Dot((p + q + r) / 3, n) < 0 ? n * -1 : n;
                }
            };
            axis(V::UnitX);
            axis(V::UnitY);
            axis(V::UnitZ);
            axis(Cross(q - p, r - p));
            for (const auto& e : edges)
            {
                axis(Cross(e, V::UnitX));
                axis(Cross(e, V::UnitY));
                axis(Cross(e, V::UnitZ));
            }
            if (separated || depth <= deepest)
                return;
            out.Normal = box.Axes[0] * axisBest.x + box.Axes[1] * axisBest.y + box.Axes[2] * axisBest.z;
            out.Depth = depth;
            out.Point = PointTriangle(box.Center, a, b, c);
            deepest = depth;
            found = true;
        });
        return found;
    }
    Math::GeometrySweepHit SweepTerrain(const Math::Capsule& capsule, const PhysicsShapeProxy& proxy,
                                        const V& direction, float maximum, const Math::GeometrySweepSettings& settings)
    {
        Math::GeometrySweepHit best;
        best.Distance = maximum;
        const auto unit = maximum > 0 ? direction.Normalized() : V::Zero;
        auto area = CapsuleBounds(capsule);
        auto end = capsule;
        end.PointA += unit * maximum;
        end.PointB += unit * maximum;
        area.Merge(CapsuleBounds(end));
        Triangles(proxy, area, [&](const V& a, const V& b, const V& c) {
            float travel = 0;
            Math::GeometrySweepHit hit;
            for (uint32_t iteration = 0; iteration < settings.MaxIterations; ++iteration)
            {
                V axis, triangle;
                const V offset = unit * travel;
                const float gap =
                    SegmentTriangle(capsule.PointA + offset, capsule.PointB + offset, a, b, c, axis, triangle) -
                    capsule.Radius;
                const V normal =
                    ContactNormal(axis, triangle, a, b, c, (capsule.PointA + capsule.PointB) * .5f + offset);
                if (gap <= settings.DistanceTolerance)
                {
                    if (travel == 0 && !settings.bReportStartOverlap)
                        return;
                    hit.Result = Math::EGeometrySweepResult::Hit;
                    hit.Distance = travel;
                    hit.Point = triangle;
                    hit.Normal = normal;
                    hit.Depth = std::max(0.f, -gap);
                    hit.bStartPenetrating = travel == 0;
                    break;
                }
                const float approach = -Dot(unit, normal);
                if (approach <= 1e-7f)
                    return;
                const float next = travel + gap / approach;
                if (next > maximum || next > best.Distance)
                    return;
                if (next <= travel)
                {
                    hit.Result = Math::EGeometrySweepResult::IterationLimit;
                    hit.Distance = travel;
                    break;
                }
                travel = next;
                if (iteration + 1 == settings.MaxIterations)
                {
                    hit.Result = Math::EGeometrySweepResult::IterationLimit;
                    hit.Distance = travel;
                }
            }
            if (hit.Result != Math::EGeometrySweepResult::NoHit && hit.Distance <= best.Distance)
                best = hit;
        });
        return best;
    }
} // namespace NorvesLib::Modules::Physics
