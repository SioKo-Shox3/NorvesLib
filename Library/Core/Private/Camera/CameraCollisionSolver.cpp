#include "Camera/CameraCollisionSolver.h"
#include "Math/CriticalDamping.h"
#include <algorithm>
#include <cmath>
namespace NorvesLib::Core::Camera
{
    namespace
    {
        bool Finite(const Math::Vector3& v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }
        bool Nonnegative(float value)
        {
            return std::isfinite(value) && value >= 0;
        }
    } // namespace
    CameraCollisionResult CameraCollisionSolver::Solve(CameraCollisionState& state,
                                                       const CameraCollisionSettings& settings,
                                                       const CameraCollisionRequest& request, const ICameraProbe& probe,
                                                       float dt, CameraCollisionOutput& out)
    {
        if (!Nonnegative(dt) || !Finite(request.Pivot) || !Finite(request.Direction) ||
            !Nonnegative(request.DesiredLength) || !Nonnegative(settings.ProbeRadius) || settings.ProbeRadius == 0 ||
            !Nonnegative(settings.Margin) || !Nonnegative(settings.ExtendHalfLife) ||
            !Nonnegative(settings.MaximumExtendSpeed) || !Nonnegative(state.EffectiveLength) ||
            !Nonnegative(state.ExtendVelocity))
            return CameraCollisionResult::InvalidArgument;
        const double norm =
            std::hypot(static_cast<double>(request.Direction.x), static_cast<double>(request.Direction.y),
                       static_cast<double>(request.Direction.z));
        if (norm == 0)
            return CameraCollisionResult::InvalidArgument;
        if (dt == 0)
            return CameraCollisionResult::Paused;
        CameraProbeRequest query;
        query.Origin = request.Pivot;
        query.Direction = {static_cast<float>(request.Direction.x / norm),
                           static_cast<float>(request.Direction.y / norm),
                           static_cast<float>(request.Direction.z / norm)};
        query.Distance = request.DesiredLength;
        query.Radius = settings.ProbeRadius;
        CameraProbeHit hit;
        const auto result = probe.SweepSphere(query, hit);
        if (result != CameraProbeResult::Hit && result != CameraProbeResult::Clear)
            return CameraCollisionResult::ProbeFailed;
        if (result == CameraProbeResult::Hit && (!Nonnegative(hit.Distance) || hit.Distance > request.DesiredLength))
            return CameraCollisionResult::ProbeFailed;
        if (result == CameraProbeResult::Hit && hit.bStartPenetrating)
            return CameraCollisionResult::StartOverlapping;
        CameraCollisionOutput next;
        CameraCollisionState candidate = state;
        next.bObstructed = result == CameraProbeResult::Hit;
        next.SafeLimit = next.bObstructed ? std::max(0.f, hit.Distance - settings.Margin) : request.DesiredLength;
        if (!candidate.bInitialized || next.SafeLimit <= candidate.EffectiveLength)
        {
            candidate.EffectiveLength = next.SafeLimit;
            candidate.ExtendVelocity = 0;
            candidate.bInitialized = true;
        }
        else
        {
            const float previous = candidate.EffectiveLength;
            candidate.ExtendVelocity = std::clamp(candidate.ExtendVelocity, 0.f, settings.MaximumExtendSpeed);
            if (!Math::TryCriticalDamp(candidate.EffectiveLength, candidate.ExtendVelocity, next.SafeLimit,
                                       settings.ExtendHalfLife, dt))
                return CameraCollisionResult::InvalidArgument;
            const double rateLimit = previous + static_cast<double>(settings.MaximumExtendSpeed) * dt;
            candidate.EffectiveLength = static_cast<float>(
                std::clamp(static_cast<double>(candidate.EffectiveLength), static_cast<double>(previous),
                           std::min(static_cast<double>(next.SafeLimit), rateLimit)));
            candidate.ExtendVelocity = std::clamp(candidate.ExtendVelocity, 0.f, settings.MaximumExtendSpeed);
            if (candidate.EffectiveLength >= next.SafeLimit)
                candidate.ExtendVelocity = 0;
        }
        next.EffectiveLength = candidate.EffectiveLength;
        next.Position = request.Pivot + query.Direction * next.EffectiveLength;
        if (!Finite(next.Position))
            return CameraCollisionResult::InvalidArgument;
        state = candidate;
        out = next;
        return CameraCollisionResult::Success;
    }
} // namespace NorvesLib::Core::Camera
