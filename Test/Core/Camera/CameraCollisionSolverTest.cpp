#include "Camera/CameraCollisionSolver.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace NorvesLib::Core::Camera;
using NorvesLib::Math::Vector3;
namespace
{
    class Probe final : public ICameraProbe
    {
      public:
        CameraProbeResult Result = CameraProbeResult::Clear;
        CameraProbeHit Hit;
        bool bThrow = false;
        mutable unsigned Calls = 0;
        mutable CameraProbeRequest Last;
        CameraProbeResult SweepSphere(const CameraProbeRequest& request, CameraProbeHit& out) const override
        {
            ++Calls;
            Last = request;
            if (bThrow)
                throw std::runtime_error("probe");
            out = Hit;
            return Result;
        }
    };
} // namespace
int main()
{
    Probe probe;
    CameraCollisionState s;
    CameraCollisionSettings p;
    CameraCollisionRequest r;
    CameraCollisionOutput o;
    r.Direction = Vector3(0, 0, -2);
    r.DesiredLength = 5;
    assert(CameraCollisionSolver::Solve(s, p, r, probe, .016f, o) == CameraCollisionResult::Success);
    assert(o.EffectiveLength == 5 && o.Position == Vector3(0, 0, -5) && probe.Last.Direction == Vector3(0, 0, -1));
    probe.Result = CameraProbeResult::Hit;
    probe.Hit.Distance = 2;
    assert(CameraCollisionSolver::Solve(s, p, r, probe, .001f, o) == CameraCollisionResult::Success);
    assert(std::fabs(o.EffectiveLength - 1.95f) < 1e-6 && s.ExtendVelocity == 0 && o.bObstructed);
    probe.Result = CameraProbeResult::Clear;
    const float previous = s.EffectiveLength;
    assert(CameraCollisionSolver::Solve(s, p, r, probe, .1f, o) == CameraCollisionResult::Success);
    assert(o.EffectiveLength > previous && o.EffectiveLength <= previous + p.MaximumExtendSpeed * .1f &&
           o.EffectiveLength < 5);
    p.MaximumExtendSpeed = 1000;
    double reference = -1;
    for (int rate : {30, 60, 144})
    {
        s = {1, 0, true};
        for (int frame = 0; frame < rate; ++frame)
            assert(CameraCollisionSolver::Solve(s, p, r, probe, 1.f / rate, o) == CameraCollisionResult::Success);
        if (reference < 0)
            reference = o.EffectiveLength;
        assert(std::fabs(o.EffectiveLength - reference) < 1e-4);
    }
    {
        auto slowSettings = p;
        slowSettings.ExtendHalfLife = 1.048463f;
        slowSettings.MaximumExtendSpeed = 5.247734f;
        CameraCollisionState changing{.5752412f, 56.010975f, true};
        auto farther = r;
        farther.DesiredLength = 9.704998f;
        assert(CameraCollisionSolver::Solve(changing, slowSettings, farther, probe, 1.428497f, o) ==
               CameraCollisionResult::Success);
        assert(changing.ExtendVelocity >= 0 && changing.ExtendVelocity <= slowSettings.MaximumExtendSpeed);
        assert(CameraCollisionSolver::Solve(changing, slowSettings, farther, probe, .016f, o) ==
               CameraCollisionResult::Success);
    }
    probe.Result = CameraProbeResult::Hit;
    probe.Hit.Distance = .01f;
    assert(CameraCollisionSolver::Solve(s, p, r, probe, .016f, o) == CameraCollisionResult::Success &&
           o.EffectiveLength == 0);
    probe.Result = CameraProbeResult::Clear;
    const auto calls = probe.Calls;
    o.EffectiveLength = 123;
    assert(CameraCollisionSolver::Solve(s, p, r, probe, 0, o) == CameraCollisionResult::Paused &&
           probe.Calls == calls && o.EffectiveLength == 123);
    probe.Result = CameraProbeResult::Failed;
    assert(CameraCollisionSolver::Solve(s, p, r, probe, .1f, o) == CameraCollisionResult::ProbeFailed &&
           s.EffectiveLength == 0 && o.EffectiveLength == 123);
    probe.Result = CameraProbeResult::Hit;
    probe.Hit = {0, true};
    assert(CameraCollisionSolver::Solve(s, p, r, probe, .1f, o) == CameraCollisionResult::StartOverlapping &&
           o.EffectiveLength == 123);
    probe.Hit = {-1, false};
    assert(CameraCollisionSolver::Solve(s, p, r, probe, .1f, o) == CameraCollisionResult::ProbeFailed &&
           o.EffectiveLength == 123);
    probe.Hit = {6, false};
    assert(CameraCollisionSolver::Solve(s, p, r, probe, .1f, o) == CameraCollisionResult::ProbeFailed &&
           o.EffectiveLength == 123);
    probe.Hit = {std::numeric_limits<float>::quiet_NaN(), false};
    assert(CameraCollisionSolver::Solve(s, p, r, probe, .1f, o) == CameraCollisionResult::ProbeFailed &&
           o.EffectiveLength == 123);
    probe.bThrow = true;
    bool threw = false;
    try
    {
        CameraCollisionSolver::Solve(s, p, r, probe, .1f, o);
    }
    catch (const std::runtime_error&)
    {
        threw = true;
    }
    assert(threw && s.EffectiveLength == 0 && o.EffectiveLength == 123);
    probe.bThrow = false;
    probe.Result = CameraProbeResult::Clear;
    r.DesiredLength = 0;
    assert(CameraCollisionSolver::Solve(s, p, r, probe, .1f, o) == CameraCollisionResult::Success &&
           o.EffectiveLength == 0);
    assert(CameraCollisionSolver::Solve(s, p, r, probe, -1, o) == CameraCollisionResult::InvalidArgument);
    assert(CameraCollisionSolver::Solve(s, p, r, probe, std::numeric_limits<float>::infinity(), o) ==
           CameraCollisionResult::InvalidArgument);
    r.Direction = {};
    assert(CameraCollisionSolver::Solve(s, p, r, probe, .1f, o) == CameraCollisionResult::InvalidArgument);
    std::cout << "CameraCollisionSolverTest PASS\n";
    return 0;
}
