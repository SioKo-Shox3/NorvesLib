#include "Physics/CharacterMover.h"
#include <algorithm>
#include <cmath>

namespace NorvesLib::Modules::Physics
{
    namespace
    {
        using Core::Scene::EPhysicsQueryKind;
        using Core::Scene::EPhysicsSceneQueryResult;
        using Core::Scene::PhysicsQueryHit;
        using Math::Vector3;
        constexpr float MotionEpsilon = 1e-6f;
        bool Finite(const Vector3& v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }
        void Translate(Math::Capsule& shape, const Vector3& delta)
        {
            shape.PointA += delta;
            shape.PointB += delta;
        }
        bool Valid(const CharacterMoveSettings& s, const CharacterMoveRequest& r)
        {
            return Finite(r.Shape.PointA) && Finite(r.Shape.PointB) && Finite(r.Displacement) &&
                   Finite(r.PlatformDisplacement) && std::isfinite(r.Shape.Radius) && r.Shape.Radius > 0 &&
                   r.Filter.IsValid() && CharacterMover::IsValidSettings(s);
        }
        struct Solver
        {
            Core::Container::Span<const PhysicsShapeProxy> Proxies;
            const CharacterMoveSettings& Settings;
            const CharacterMoveRequest& Request;
            CharacterMoveScratch& Scratch;
            EPhysicsSceneQueryResult Error = EPhysicsSceneQueryResult::Success;
            float WalkableCos;
            bool bPlatformTransfer = false;

            bool Query(const Math::Capsule& shape, const Vector3& movement, bool overlap, size_t& count)
            {
                Core::Scene::PhysicsQueryDesc query;
                query.Kind = overlap ? EPhysicsQueryKind::OverlapCapsule : EPhysicsQueryKind::SweepCapsule;
                query.Capsule = shape;
                query.Filter = Request.Filter;
                query.Direction = movement;
                query.MaxDistance = movement.Length();
                query.MaxSweepIterations = 64;
                query.bReportStartOverlap = true;
                if (!overlap && query.MaxDistance <= MotionEpsilon)
                {
                    count = 0;
                    return true;
                }
                Error = PhysicsBroadphase::ExecuteQueryOverProxies(Proxies, query, Scratch.Hits, count);
                if (Error == EPhysicsSceneQueryResult::NoHit)
                    Error = EPhysicsSceneQueryResult::Success;
                return Error == EPhysicsSceneQueryResult::Success;
            }
            bool Sweep(const Math::Capsule& shape, const Vector3& movement, PhysicsQueryHit& hit, bool& blocked)
            {
                size_t count = 0;
                blocked = false;
                if (!Query(shape, movement, false, count))
                    return false;
                // 床と平行に動くとき、距離0の接触を壁より先に選ばない。
                const float length = movement.Length();
                if (length <= MotionEpsilon)
                    return true;
                const Vector3 direction = movement / length;
                for (size_t i = 0; i < count; ++i)
                {
                    const auto& candidate = Scratch.Hits[i];
                    if (bPlatformTransfer && Request.PlatformCollider.IsValid() &&
                        candidate.Collider == Request.PlatformCollider)
                        continue;
                    if (Vector3::Dot(direction, candidate.Normal) >= -MotionEpsilon)
                        continue;
                    if (!blocked || candidate.Distance < hit.Distance ||
                        (candidate.Distance == hit.Distance && candidate.Collider < hit.Collider))
                    {
                        hit = candidate;
                        blocked = true;
                    }
                }
                return true;
            }
            bool Depenetrate(Math::Capsule& shape, bool& stuck)
            {
                float moved = 0;
                for (uint32_t iteration = 0; iteration <= Settings.DepenetrationIterations; ++iteration)
                {
                    size_t count = 0;
                    if (!Query(shape, {}, true, count))
                        return false;
                    PhysicsQueryHit deepest;
                    bool penetrating = false;
                    for (size_t i = 0; i < count; ++i)
                    {
                        const auto& hit = Scratch.Hits[i];
                        if (hit.Depth <= MotionEpsilon)
                            continue;
                        if (!penetrating || hit.Depth > deepest.Depth ||
                            (hit.Depth == deepest.Depth && hit.Collider < deepest.Collider))
                        {
                            deepest = hit;
                            penetrating = true;
                        }
                    }
                    if (!penetrating)
                        return true;
                    const float distance = deepest.Depth + Settings.SkinWidth;
                    if (iteration == Settings.DepenetrationIterations ||
                        moved + distance > Settings.MaximumDepenetrationDistance)
                    {
                        stuck = true;
                        return true;
                    }
                    Translate(shape, deepest.Normal * distance);
                    moved += distance;
                }
                return true;
            }
            bool Slide(Math::Capsule& shape, Vector3 remaining, bool slopeLimit, CharacterMoveResult* landing = nullptr)
            {
                Vector3 planes[32];
                uint32_t planeCount = 0;
                for (uint32_t iteration = 0; iteration < Settings.SlideIterations; ++iteration)
                {
                    const float length = remaining.Length();
                    if (length <= MotionEpsilon)
                        break;
                    PhysicsQueryHit hit;
                    bool blocked;
                    if (!Sweep(shape, remaining, hit, blocked))
                        return false;
                    if (!blocked)
                    {
                        Translate(shape, remaining);
                        return true;
                    }
                    if (landing && remaining.y < 0 && hit.Normal.y >= WalkableCos)
                        Ground(hit, *landing);
                    const float travel = std::clamp(hit.Distance - Settings.SkinWidth, 0.f, length);
                    const Vector3 advance = remaining * (travel / length);
                    Translate(shape, advance);
                    remaining -= advance;
                    Vector3 normal = hit.Normal;
                    if (slopeLimit && normal.y > 0 && normal.y < WalkableCos)
                    {
                        normal.y = 0;
                        normal.Normalize();
                    }
                    planes[planeCount++] = normal;
                    // 角では既に当たった面へ戻さない。定数回数で打ち切る。
                    for (uint32_t pass = 0; pass < planeCount; ++pass)
                        for (uint32_t p = 0; p < planeCount; ++p)
                        {
                            const float inward = Vector3::Dot(remaining, planes[p]);
                            if (inward < 0)
                                remaining -= planes[p] * inward;
                        }
                }
                return true;
            }
            void Ground(const PhysicsQueryHit& hit, CharacterMoveResult& result)
            {
                result.bGrounded = true;
                result.GroundNormal = hit.Normal;
                result.GroundPoint = hit.Point;
                result.GroundCollider = hit.Collider;
                result.GroundBody = hit.Body;
            }
            bool Step(Math::Capsule& shape, const Vector3& horizontal, CharacterMoveResult& result)
            {
                if (!Request.bWasGrounded || !Request.bAllowStep || Request.Displacement.y > 0 ||
                    Settings.StepHeight <= 0 || horizontal.Length() <= MotionEpsilon)
                    return true;
                PhysicsQueryHit first;
                bool blocked;
                if (!Sweep(shape, horizontal, first, blocked))
                    return false;
                if (!blocked || first.Normal.y >= WalkableCos)
                    return true;
                Math::Capsule trial = shape;
                const float rise = Settings.StepHeight + Settings.SkinWidth;
                PhysicsQueryHit hit;
                if (!Sweep(trial, Vector3(0, rise, 0), hit, blocked))
                    return false;
                if (blocked)
                    return true;
                Translate(trial, Vector3(0, rise, 0));
                if (!Sweep(trial, horizontal, hit, blocked))
                    return false;
                if (blocked)
                    return true;
                Translate(trial, horizontal);
                const float drop = rise + Settings.GroundSnapDistance;
                if (!Sweep(trial, Vector3(0, -drop, 0), hit, blocked))
                    return false;
                if (!blocked || hit.Normal.y < WalkableCos)
                    return true;
                Translate(trial, Vector3(0, -std::max(0.f, hit.Distance - Settings.SkinWidth), 0));
                const float heightChange = trial.PointA.y - shape.PointA.y;
                if (heightChange > Settings.StepHeight + Settings.SkinWidth + MotionEpsilon)
                    return true;
                shape = trial;
                result.bStepped = true;
                Ground(hit, result);
                return true;
            }
            bool Vertical(Math::Capsule& shape, float distance, CharacterMoveResult& result)
            {
                PhysicsQueryHit hit;
                bool blocked;
                const Vector3 move(0, distance, 0);
                if (!Sweep(shape, move, hit, blocked))
                    return false;
                if (!blocked)
                {
                    Translate(shape, move);
                    return true;
                }
                const float travel = std::max(0.f, hit.Distance - Settings.SkinWidth);
                const Vector3 advance(0, std::copysign(travel, distance), 0);
                Translate(shape, advance);
                if (distance > 0)
                    result.bHitCeiling = true;
                else if (hit.Normal.y >= WalkableCos)
                    Ground(hit, result);
                else
                {
                    // 歩けない斜面は足場にせず、残りの下降を面に沿って滑らせる。
                    return Slide(shape, move - advance, false, &result);
                }
                return true;
            }
        };
    } // namespace
    bool CharacterMover::IsValidSettings(const CharacterMoveSettings& s)
    {
        return std::isfinite(s.SkinWidth) && s.SkinWidth > 0 && std::isfinite(s.StepHeight) && s.StepHeight >= 0 &&
               std::isfinite(s.GroundSnapDistance) && s.GroundSnapDistance >= 0 &&
               std::isfinite(s.MaximumSlopeDegrees) && s.MaximumSlopeDegrees >= 0 && s.MaximumSlopeDegrees < 90 &&
               std::isfinite(s.MaximumDepenetrationDistance) && s.MaximumDepenetrationDistance >= 0 &&
               s.SlideIterations > 0 && s.SlideIterations <= 32 && s.DepenetrationIterations > 0 &&
               s.DepenetrationIterations <= 32;
    }
    Core::Scene::EPhysicsSceneQueryResult CharacterMover::Move(Core::Container::Span<const PhysicsShapeProxy> proxies,
                                                               const CharacterMoveSettings& settings,
                                                               const CharacterMoveRequest& request,
                                                               CharacterMoveScratch& scratch, CharacterMoveResult& out)
    {
        if (!Valid(settings, request))
            return EPhysicsSceneQueryResult::InvalidArgument;
        scratch.Hits.resize(proxies.size());
        Solver solver{proxies,
                      settings,
                      request,
                      scratch,
                      EPhysicsSceneQueryResult::Success,
                      std::cos(settings.MaximumSlopeDegrees * Math::Constants::PI / 180.f)};
        CharacterMoveResult result;
        result.Shape = request.Shape;
        solver.bPlatformTransfer = true;
        if (!solver.Slide(result.Shape, request.PlatformDisplacement, false))
            return solver.Error;
        solver.bPlatformTransfer = false;
        if (!solver.Depenetrate(result.Shape, result.bStuck))
            return solver.Error;
        if (!result.bStuck)
        {
            const Vector3 horizontal(request.Displacement.x, 0, request.Displacement.z);
            if (!solver.Step(result.Shape, horizontal, result))
                return solver.Error;
            if (!result.bStepped && !solver.Slide(result.Shape, horizontal, true))
                return solver.Error;
            // 上昇要求ではstepによる接地を引き継がない。
            if (request.Displacement.y > 0)
                result.bGrounded = false;
            if (!solver.Vertical(result.Shape, request.Displacement.y, result))
                return solver.Error;
            if (request.bAllowGroundSnap && (request.bWasGrounded || result.bGrounded) && request.Displacement.y <= 0 &&
                settings.GroundSnapDistance > 0)
            {
                PhysicsQueryHit hit;
                bool blocked;
                if (!solver.Sweep(result.Shape, Vector3(0, -settings.GroundSnapDistance, 0), hit, blocked))
                    return solver.Error;
                if (blocked && hit.Normal.y >= solver.WalkableCos)
                {
                    Translate(result.Shape, Vector3(0, -std::max(0.f, hit.Distance - settings.SkinWidth), 0));
                    solver.Ground(hit, result);
                }
            }
        }
        if (!result.bGrounded)
        {
            result.GroundNormal = Vector3::UnitY;
            result.GroundPoint = {};
            result.GroundCollider = {};
            result.GroundBody = {};
        }
        result.Displacement = result.Shape.PointA - request.Shape.PointA;
        if (!Finite(result.Shape.PointA) || !Finite(result.Shape.PointB) || !Finite(result.Displacement))
            return EPhysicsSceneQueryResult::InvalidArgument;
        out = result;
        return EPhysicsSceneQueryResult::Success;
    }
} // namespace NorvesLib::Modules::Physics
