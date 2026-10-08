#include "Locomotion/QuadrupedLocomotionModel.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
using namespace NorvesLib::Core::Locomotion;
using NorvesLib::Math::Vector3;
namespace
{
    void Step(LocomotionState& s, const QuadrupedLocomotionParams& p, const LocomotionIntent& i,
              const LocomotionGroundInfo& g, float dt, LocomotionOutput& o)
    {
        assert(QuadrupedLocomotionModel::Step(s, p, i, g, dt, o) == LocomotionStepStatus::Success);
    }
} // namespace
int main()
{
    QuadrupedLocomotionParams p;
    assert(QuadrupedLocomotionModel::IsValidParams(p));
    p.TargetHalfLife = 0;
    p.TurnSlowdown = 0;
    for (auto& gait : p.Gaits)
    {
        gait.Acceleration = 2;
        gait.Deceleration = 2;
        gait.Brake = 4;
    }
    LocomotionGroundInfo g;
    g.bReady = g.bGrounded = true;
    LocomotionIntent i;
    i.Move = Vector3::UnitZ;
    LocomotionState s;
    LocomotionOutput o;
    Step(s, p, i, g, 1, o);
    assert(std::fabs(s.Speed - 2) < 1e-4 && std::fabs(o.PlanarDisplacement.z - 1) < 1e-4);
    i.Move = {};
    Step(s, p, i, g, 1, o);
    assert(s.Speed < 1e-4 && std::fabs(o.PlanarDisplacement.z - 1) < 1e-4 && o.DeltaYaw == 0);
    for (int rate : {30, 60, 144})
    {
        LocomotionState a;
        Vector3 position;
        i.Move = Vector3::UnitZ;
        for (int n = 0; n < rate * 3; ++n)
        {
            Step(a, p, i, g, 1.f / rate, o);
            position += o.PlanarDisplacement;
        }
        assert(std::fabs(position.z - 8) < .003f && std::fabs(a.Speed - 4) < .003f);
    }
    // 実速度のヒステリシス。足場速度は歩様判定に含めない。
    s = {};
    i = {};
    unsigned changes = 0;
    unsigned last = 0;
    for (int n = 0; n < 300; ++n)
    {
        g.Velocity.z = 2.2f * (n % 2 ? 1.02f : .98f);
        Step(s, p, i, g, 1.f / 60, o);
        changes += o.Gait != last;
        last = o.Gait;
        float sum = 0;
        for (float w : o.GaitWeights)
        {
            assert(w >= 0 && w <= 1);
            sum += w;
        }
        assert(std::fabs(sum - 1) < 1e-5);
    }
    assert(changes == 1);
    s = {};
    g.PlatformVelocity = g.Velocity = Vector3(0, 0, 20);
    Step(s, p, i, g, 1, o);
    assert(o.Gait == 0 && o.PhaseDelta == 0);
    g.PlatformVelocity = {};
    g.Velocity = {};
    // 一定速の全力旋回。半径はv/omegaとv²/a_latの大きい方。
    s = {};
    s.Speed = 4;
    s.PlanarVelocity = Vector3(0, 0, 4);
    p.Gaits[0].LateralAcceleration = 2;
    p.Gaits[0].TurnRateCap = 2;
    i.Move = Vector3::UnitX;
    Step(s, p, i, g, 1, o);
    const double radius = 8, angle = .5;
    assert(std::fabs(o.PlanarDisplacement.x - radius * (1 - std::cos(angle))) < .001);
    assert(std::fabs(o.PlanarDisplacement.z - radius * std::sin(angle)) < .001);
    assert(std::fabs(o.DeltaYaw - angle) < 1e-4);
    double lastScale = 2;
    for (int degree = 0; degree <= 45; degree += 5)
    {
        const float angle = degree * 3.14159265f / 180;
        const auto scale =
            QuadrupedLocomotionModel::SlopeSpeedScale(p, Vector3(0, std::cos(angle), -std::sin(angle)), Vector3::UnitZ);
        assert(scale <= lastScale && scale >= p.MinimumSlopeScale);
        lastScale = scale;
    }
    // ジャンプ要求と受理は別。鉛直位置は偽Bodyが解析積分する。
    p = {};
    p.JumpHeightMin = p.JumpHeightMax = 1;
    s = {};
    i = {};
    i.bJumpPressed = true;
    Step(s, p, i, g, 1.f / 60, o);
    assert(o.bJumpRequested);
    const double launch = o.JumpSpeed;
    assert(std::fabs(launch * launch / (2 * g.Gravity) - 1) < 1e-5);
    for (int rate : {30, 60, 144})
    {
        const double dt = 1.0 / rate;
        double y = 0, speed = launch, apex = 0, time = 0;
        for (int frame = 0; frame < rate * 3; ++frame)
        {
            const double previous = y;
            y += speed * dt - .5 * g.Gravity * dt * dt;
            speed -= g.Gravity * dt;
            apex = std::max(apex, y);
            if (y < 0)
            {
                // 最後の区間だけ二次式で地面への到達時刻を求める。
                const double initial = speed + g.Gravity * dt;
                time += (initial + std::sqrt(initial * initial + 2 * g.Gravity * previous)) / g.Gravity;
                break;
            }
            time += dt;
        }
        assert(std::fabs(apex - 1) < .02 && std::fabs(time - 2 * launch / g.Gravity) < .001);
    }
    QuadrupedLocomotionModel::AcknowledgeJump(s);
    i.bJumpPressed = false;
    g.bGrounded = false;
    Step(s, p, i, g, 1.f / 60, o);
    assert(!o.bJumpRequested);
    i.bJumpPressed = true;
    Step(s, p, i, g, 1.f / 60, o);
    assert(!o.bJumpRequested);
    g.bGrounded = true;
    i.bJumpPressed = false;
    Step(s, p, i, g, 1.f / 60, o);
    assert(o.bLanded && o.bJumpRequested);
    // 自然離地なら期限内だけ発射要求を出す。
    s = {};
    i = {};
    Step(s, p, i, g, .01f, o);
    g.bGrounded = false;
    Step(s, p, i, g, .05f, o);
    i.bJumpPressed = true;
    Step(s, p, i, g, .01f, o);
    assert(o.bJumpRequested);
    s = {};
    i = {};
    g.bGrounded = true;
    Step(s, p, i, g, .01f, o);
    g.bGrounded = false;
    Step(s, p, i, g, .11f, o);
    i.bJumpPressed = true;
    Step(s, p, i, g, .01f, o);
    assert(!o.bJumpRequested);
    {
        auto stopParams = p;
        stopParams.TargetHalfLife = 0;
        stopParams.Gaits[0].Deceleration = 168.009155f;
        LocomotionState stopping;
        stopping.Speed = .572488546f;
        LocomotionIntent noInput;
        LocomotionGroundInfo flat;
        flat.bReady = flat.bGrounded = true;
        Step(stopping, stopParams, noInput, flat, 1.f / 240, o);
        assert(stopping.Speed == 0);
    }
    {
        auto landingParams = p;
        landingParams.LandingRecovery = 0;
        landingParams.TargetHalfLife = 0;
        LocomotionState falling;
        falling.bInitialized = true;
        falling.PreviousVerticalVelocity = -10;
        falling.Speed = 4;
        LocomotionGroundInfo elevator;
        elevator.bReady = elevator.bGrounded = true;
        elevator.PlatformVelocity.y = -10;
        LocomotionIntent forward;
        forward.Move = Vector3::UnitZ;
        Step(falling, landingParams, forward, elevator, 1.f / 60, o);
        assert(std::fabs(falling.Speed - 4) < 1e-5 && o.bLanded);
    }
    const auto old = s;
    o.Speed = 123;
    assert(QuadrupedLocomotionModel::Step(s, p, i, g, 2, o) == LocomotionStepStatus::InvalidArgument);
    assert(s.Speed == old.Speed && o.Speed == 123);
    Step(s, p, i, g, 0, o);
    assert(s.Speed == old.Speed && !o.bJumpRequested && o.PlanarDisplacement == Vector3::Zero);
    p.Gaits[1].DownThreshold = p.Gaits[0].UpThreshold;
    assert(!QuadrupedLocomotionModel::IsValidParams(p));
    std::cout << "QuadrupedLocomotionModelTest PASS\n";
    return 0;
}
