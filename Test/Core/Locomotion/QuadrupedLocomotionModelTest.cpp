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
static void CaseExistingBaseline()
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
}

static QuadrupedLocomotionParams MakeTestParams()
{
    QuadrupedLocomotionParams p;
    p.Gaits[0] = {0, 2, 2.2f, 0, .2f, 2, 1.5f, 6, 1, 6, .8f};
    p.Gaits[1] = {1.6f, 4, 4.4f, 1.8f, .2f, 2, 1.5f, 6, 1, 6, 1.4f};
    p.Gaits[2] = {3.5f, 7, 7.7f, 3.6f, .2f, 2, 1.5f, 6, 1, 6, 2};
    p.Gaits[3] = {6, 12, 12, 6.3f, .2f, 2, 1.5f, 6, 1, 6, 2.8f};
    p.TargetHalfLife = 0;
    p.PivotSpeed = .25f;
    p.PivotEnterAngle = 1.2f;
    p.PivotExitAngle = .25f;
    p.PivotRate = 4;
    p.TurnSlowdown = 0;
    p.SlopeLimit = 1.2f;
    p.UphillCoefficient = .7f;
    p.DownhillBoost = .25f;
    p.MinimumSlopeScale = .2f;
    p.LeanGain = 1;
    p.MaximumBank = .45f;
    p.LeanHalfLife = .1f;
    p.JumpHeightMin = .6f;
    p.JumpHeightMax = 1.2f;
    p.CoyoteTime = .1f;
    p.JumpBuffer = .12f;
    p.AirControl = .25f;
    p.AirTurnRate = 1.5f;
    p.LandingSpeedLoss = .15f;
    p.LandingRecovery = .12f;
    return p;
}

static void CaseGaitMinDwell()
{
    auto p = MakeTestParams();
    for (auto& gait : p.Gaits)
    {
        gait.MinDwell = .2f;
    }
    assert(QuadrupedLocomotionModel::IsValidParams(p));

    constexpr float dt = 1.f / 240;
    LocomotionGroundInfo g;
    g.bReady = g.bGrounded = true;
    LocomotionIntent i;
    LocomotionState s;
    LocomotionOutput o;

    // 先に 1 から 0 へ切り替え、最初の検査も実際の遷移から始める。
    s.Gait = 1;
    s.GaitElapsed = p.Gaits[1].MinDwell;
    g.Velocity = {};
    Step(s, p, i, g, dt, o);
    assert(s.Gait == 0);
    double time = dt;
    double previousTransition = time - s.GaitElapsed;

    const unsigned destinations[] = {1, 2, 3, 2, 1, 0};
    for (unsigned destination : destinations)
    {
        const unsigned source = s.Gait;
        const double dwell = p.Gaits[source].MinDwell;
        g.Velocity.z = destination > source ? p.Gaits[source].UpThreshold + .5f
                                           : p.Gaits[source].DownThreshold * .5f;
        bool bChanged = false;
        for (int frame = 0; frame < 52 && !bChanged; ++frame)
        {
            Step(s, p, i, g, dt, o);
            time += dt;
            if (time - previousTransition < dwell - 1e-6)
            {
                assert(s.Gait == source);
            }
            if (s.Gait != source)
            {
                assert(s.Gait == destination);
                // 刻み内で切り替わった後の経過分を引き、遷移時刻を復元する。
                const double transition = time - s.GaitElapsed;
                const double elapsed = transition - previousTransition;
                assert(elapsed >= dwell - 1e-6);
                assert(elapsed <= dwell + dt + 1e-6);
                std::cout << "GaitMinDwell " << source << "->" << destination
                          << " expected_s=" << dwell << " actual_s=" << elapsed << '\n';
                previousTransition = transition;
                bChanged = true;
            }
        }
        assert(bChanged);
    }
}

static void CaseMinimumTurnRadius()
{
    constexpr float speed = 3;
    constexpr float dt = 1.f / 60;
    constexpr double angleOffset = 80.0 * 3.14159265358979323846 / 180.0;
    for (int test = 0; test < 2; ++test)
    {
        auto p = MakeTestParams();
        p.Gaits[0].TurnRateCap = test == 0 ? .75f : 2.f;
        p.Gaits[0].LateralAcceleration = test == 0 ? 6.f : 1.5f;
        assert(QuadrupedLocomotionModel::IsValidParams(p));

        const double rateRadius = speed / static_cast<double>(p.Gaits[0].TurnRateCap);
        const double lateralRadius = speed * speed / static_cast<double>(p.Gaits[0].LateralAcceleration);
        assert(test == 0 ? rateRadius > lateralRadius : lateralRadius > rateRadius);
        const double expected = std::max(rateRadius, lateralRadius);

        LocomotionGroundInfo g;
        g.bReady = g.bGrounded = true;
        g.Velocity = {};
        LocomotionIntent i;
        LocomotionState s;
        s.Speed = speed;
        s.PlanarVelocity = Vector3(0, 0, speed);
        LocomotionOutput o;
        double x = 0, z = 0, middleX = 0, middleZ = 0;
        const double magnitude = speed / static_cast<double>(p.Gaits[1].SpeedMax);
        for (int frame = 0; frame < 180; ++frame)
        {
            const double inputYaw = s.Yaw + angleOffset;
            i.Move = Vector3(static_cast<float>(magnitude * std::sin(inputYaw)), 0,
                             static_cast<float>(magnitude * std::cos(inputYaw)));
            Step(s, p, i, g, dt, o);
            assert(s.Gait == 0 && !s.bPivot && std::fabs(s.Speed - speed) < 1e-4);
            x += o.PlanarDisplacement.x;
            z += o.PlanarDisplacement.z;
            if (frame == 89)
            {
                middleX = x;
                middleZ = z;
            }
        }

        // 開始点・中点・終点の外接円を使い、周回数によらず半径を求める。
        const double a = std::hypot(middleX, middleZ);
        const double b = std::hypot(x, z);
        const double c = std::hypot(x - middleX, z - middleZ);
        const double cross = std::fabs(middleX * z - middleZ * x);
        assert(cross > 1e-6);
        const double radius = a * b * c / (2 * cross);
        assert(std::fabs(radius - expected) <= expected * .05);
        std::cout << "MinimumTurnRadius limit=" << (test == 0 ? "turn_rate" : "lateral_acceleration")
                  << " expected_m=" << expected << " actual_m=" << radius << " other_m="
                  << (test == 0 ? lateralRadius : rateRadius) << '\n';
    }
}

static void CaseAccelerationAndStopping()
{
    auto p = MakeTestParams();
    p.Gaits[0].Acceleration = 2;
    p.Gaits[0].Deceleration = 1.5f;
    p.Gaits[0].Brake = 6;
    assert(QuadrupedLocomotionModel::IsValidParams(p));

    constexpr float dt = 1.f / 240;
    constexpr float target = 3;
    const float magnitude = target / p.Gaits[1].SpeedMax;
    LocomotionGroundInfo g;
    g.bReady = g.bGrounded = true;
    g.Velocity = {};
    LocomotionIntent i;
    i.Move = Vector3(0, 0, magnitude);
    LocomotionState s;
    LocomotionOutput o;

    int frames = 0;
    while (s.Speed < target * .999f && frames < 600)
    {
        Step(s, p, i, g, dt, o);
        assert(s.Gait == 0);
        ++frames;
    }
    assert(s.Speed >= target * .999f);
    const double accelerationTime = static_cast<double>(frames) * dt;
    const double expectedTime = target / static_cast<double>(p.Gaits[0].Acceleration);
    assert(std::fabs(accelerationTime - expectedTime) <= expectedTime * .02);
    std::cout << "Acceleration expected_s=" << expectedTime << " actual_s=" << accelerationTime
              << " speed_mps=" << s.Speed << '\n';

    while (s.Speed < target && frames < 600)
    {
        Step(s, p, i, g, dt, o);
        ++frames;
    }
    assert(s.Speed == target);
    i.Move = {};
    double distance = 0;
    frames = 0;
    while (s.Speed > 0 && frames < 1000)
    {
        Step(s, p, i, g, dt, o);
        assert(s.Gait == 0);
        distance += std::hypot(static_cast<double>(o.PlanarDisplacement.x), o.PlanarDisplacement.z);
        ++frames;
    }
    assert(s.Speed == 0);
    const double expectedDistance = target * target / (2.0 * p.Gaits[0].Deceleration);
    assert(std::fabs(distance - expectedDistance) <= expectedDistance * .02);
    std::cout << "Stopping expected_m=" << expectedDistance << " actual_m=" << distance << '\n';

    s = {};
    s.Speed = target;
    s.PlanarVelocity = Vector3(0, 0, target);
    i.Move = Vector3(0, 0, -magnitude);
    const double expectedBrake = p.Gaits[0].Brake * static_cast<double>(dt);
    const double ordinaryDeceleration = p.Gaits[0].Deceleration * static_cast<double>(dt);
    for (int frame = 0; frame < 4; ++frame)
    {
        const float before = s.Speed;
        Step(s, p, i, g, dt, o);
        const double decrease = static_cast<double>(before) - s.Speed;
        assert(s.Gait == 0 && !s.bPivot);
        assert(std::fabs(decrease - expectedBrake) < 1e-5);
        assert(std::fabs(decrease - ordinaryDeceleration) > (expectedBrake - ordinaryDeceleration) * .5);
        std::cout << "ReverseBrake step=" << frame + 1 << " expected_mps=" << expectedBrake
                  << " actual_mps=" << decrease << " deceleration_mps=" << ordinaryDeceleration << '\n';
    }
}

static void CaseSlopeScale()
{
    auto p = MakeTestParams();
    p.SlopeLimit = 1.2f;
    p.UphillCoefficient = .7f;
    p.DownhillBoost = .25f;
    p.MinimumSlopeScale = .2f;
    assert(QuadrupedLocomotionModel::IsValidParams(p));

    double previousUp = 2, previousDown = 0;
    double maxUpError = 0, maxDownError = 0;
    float up45 = 0, down45 = 0, up60 = 0, down60 = 0;
    for (int degree = 0; degree <= 60; degree += 5)
    {
        const float angle = degree * 3.14159265f / 180;
        const float up = QuadrupedLocomotionModel::SlopeSpeedScale(
            p, Vector3(0, std::cos(angle), -std::sin(angle)), Vector3::UnitZ);
        const float down = QuadrupedLocomotionModel::SlopeSpeedScale(
            p, Vector3(0, std::cos(angle), std::sin(angle)), Vector3::UnitZ);
        const double grade = std::tan(degree * 3.14159265358979323846 / 180.0);
        const double expectedUp = std::max(static_cast<double>(p.MinimumSlopeScale),
                                           1.0 - p.UphillCoefficient * grade);
        const double expectedDown = std::min(1.0 + p.DownhillBoost, 1.0 + p.DownhillBoost * grade);
        maxUpError = std::max(maxUpError, std::fabs(up - expectedUp));
        maxDownError = std::max(maxDownError, std::fabs(down - expectedDown));
        assert(std::fabs(up - expectedUp) < 1e-5 && std::fabs(down - expectedDown) < 1e-5);
        assert(up <= previousUp && up >= p.MinimumSlopeScale);
        assert(down >= previousDown && down <= 1.0 + p.DownhillBoost);
        assert(degree == 0 ? down == 1 : down > 1);
        if (degree >= 45)
        {
            assert(std::fabs(down - (1.0 + p.DownhillBoost)) < 1e-6);
        }
        if (degree == 45)
        {
            up45 = up;
            down45 = down;
        }
        if (degree == 60)
        {
            up60 = up;
            down60 = down;
        }
        previousUp = up;
        previousDown = down;
    }
    std::cout << "SlopeScale degrees=45 expected_up=" << 1.0 - p.UphillCoefficient
              << " actual_up=" << up45 << " expected_down=" << 1.0 + p.DownhillBoost
              << " actual_down=" << down45 << '\n';
    std::cout << "SlopeScale degrees=60 expected_up=" << p.MinimumSlopeScale
              << " actual_up=" << up60 << " expected_down=" << 1.0 + p.DownhillBoost
              << " actual_down=" << down60 << " max_up_error=" << maxUpError
              << " max_down_error=" << maxDownError << '\n';
}


static void CaseJumpTiming()
{
    constexpr float dt = 1.f / 256;
    constexpr int limitFrames = 16;
    auto p = MakeTestParams();
    p.JumpBuffer = limitFrames * dt;
    p.CoyoteTime = limitFrames * dt;
    assert(QuadrupedLocomotionModel::IsValidParams(p));

    for (int test = 0; test < 2; ++test)
    {
        const bool bInside = test == 0;
        const int elapsedFrames = limitFrames + (bInside ? -1 : 1);
        LocomotionGroundInfo g;
        g.bReady = g.bGrounded = true;
        LocomotionIntent i;
        i.bJumpPressed = true;
        LocomotionState s;
        LocomotionOutput o;
        Step(s, p, i, g, dt, o);
        assert(o.bJumpRequested);
        QuadrupedLocomotionModel::AcknowledgeJump(s);
        assert(s.bJumpConsumed);
        g.bGrounded = false;
        i.bJumpPressed = false;
        Step(s, p, i, g, dt, o);
        i.bJumpPressed = true;
        Step(s, p, i, g, dt, o);
        assert(!o.bJumpRequested);
        i.bJumpPressed = false;
        for (int frame = 1; frame < elapsedFrames; ++frame)
        {
            Step(s, p, i, g, dt, o);
            assert(!o.bJumpRequested);
        }
        const float remaining = s.JumpBufferRemaining;
        g.bGrounded = true;
        Step(s, p, i, g, dt, o);
        std::cout << "JumpBuffer elapsed_s=" << elapsedFrames * dt << " remaining_s=" << remaining
                  << " expected=" << bInside << " actual=" << o.bJumpRequested << '\n';
        assert(o.bLanded && o.bJumpRequested == bInside);
    }

    for (int test = 0; test < 2; ++test)
    {
        const bool bInside = test == 0;
        const int elapsedFrames = limitFrames + (bInside ? -1 : 1);
        LocomotionGroundInfo g;
        g.bReady = g.bGrounded = true;
        LocomotionIntent i;
        LocomotionState s;
        LocomotionOutput o;
        Step(s, p, i, g, dt, o);
        g.bGrounded = false;
        for (int frame = 0; frame < elapsedFrames; ++frame)
        {
            Step(s, p, i, g, dt, o);
            assert(!o.bJumpRequested);
        }
        const float remaining = s.CoyoteRemaining;
        i.bJumpPressed = true;
        Step(s, p, i, g, dt, o);
        std::cout << "Coyote elapsed_s=" << elapsedFrames * dt << " remaining_s=" << remaining
                  << " expected=" << bInside << " actual=" << o.bJumpRequested << '\n';
        assert(o.bJumpRequested == bInside);
    }
}

static void CaseLandingRecovery()
{
    constexpr float dt = 1.f / 256;
    constexpr int recoveryFrames = 30;
    constexpr float speed = 4;
    auto p = MakeTestParams();
    p.LandingRecovery = recoveryFrames * dt;
    p.LandingSpeedLoss = .15f;
    p.Gaits[0].Deceleration = 6;
    p.Gaits[0].Brake = 8;
    assert(QuadrupedLocomotionModel::IsValidParams(p));

    LocomotionGroundInfo g;
    g.bReady = true;
    g.Velocity.y = -10;
    g.Velocity.z = speed;
    LocomotionIntent i;
    i.Move = Vector3::UnitZ;
    LocomotionState s;
    s.Speed = speed;
    s.PlanarVelocity = Vector3(0, 0, speed);
    LocomotionOutput o;
    Step(s, p, i, g, dt, o);
    assert(std::fabs(s.Speed - speed) < 1e-5);
    const LocomotionState beforeLanding = s;
    g.bGrounded = true;
    g.Velocity.y = 0;
    const float expected = speed * (1 - p.LandingSpeedLoss);
    float minimum = speed, maximum = 0;
    LocomotionState landingState;
    for (int frame = 1; frame <= recoveryFrames; ++frame)
    {
        g.Velocity.z = s.Speed;
        Step(s, p, i, g, dt, o);
        if (frame == 1)
        {
            std::cout << "Landing initial expected_mps=" << expected << " actual_mps=" << s.Speed << std::endl;
            assert(o.bLanded);
            landingState = s;
        }
        minimum = std::min(minimum, s.Speed);
        maximum = std::max(maximum, s.Speed);
        assert(std::fabs(s.Speed - expected) < 1e-4);
        assert(std::fabs(s.LandingRemaining - (recoveryFrames - frame) * dt) < 1e-6);
    }
    assert(s.LandingRemaining == 0);
    g.Velocity.z = s.Speed;
    Step(s, p, i, g, dt, o);
    const float expectedAfter = expected + p.Gaits[0].Acceleration * dt;
    std::cout << "Landing recovery expected_s=" << p.LandingRecovery << " actual_s=" << recoveryFrames * dt
              << " min_mps=" << minimum << " max_mps=" << maximum
              << " expected_next_mps=" << expectedAfter << " actual_next_mps=" << s.Speed << '\n';
    assert(std::fabs(s.Speed - expectedAfter) < 1e-5);

    for (int test = 0; test < 4; ++test)
    {
        s = landingState;
        i.Move = test == 0 ? Vector3::Zero : test == 1 ? Vector3(0, 0, .5f)
                                            : test == 2 ? Vector3(0, 0, -1) : Vector3::UnitX;
        if (test == 3)
        {
            s.Speed = .1f;
            s.PlanarVelocity = Vector3(0, 0, s.Speed);
        }
        const float before = s.Speed;
        const float rate = test == 2 ? p.Gaits[0].Brake : p.Gaits[0].Deceleration;
        g.Velocity.z = s.Speed;
        Step(s, p, i, g, dt, o);
        const float decrease = before - s.Speed;
        std::cout << "Landing deceleration case=" << test << " expected_mps=" << rate * dt
                  << " actual_mps=" << decrease << '\n';
        assert(std::fabs(decrease - rate * dt) < 1e-5);
        if (test == 3)
        {
            assert(s.bPivot);
        }
    }

    auto smoothParams = p;
    smoothParams.TargetHalfLife = .05f;
    assert(QuadrupedLocomotionModel::IsValidParams(smoothParams));
    s = landingState;
    i.Move = Vector3::UnitZ;
    g.Velocity.z = s.Speed;
    Step(s, smoothParams, i, g, dt, o);
    std::cout << "Landing smoothed_target expected_mps=" << expected << " actual_mps=" << s.Speed << '\n';
    assert(std::fabs(s.Speed - expected) < 1e-5);

    auto noRecoveryParams = p;
    noRecoveryParams.LandingRecovery = 0;
    assert(QuadrupedLocomotionModel::IsValidParams(noRecoveryParams));
    s = beforeLanding;
    g.Velocity.z = s.Speed;
    Step(s, noRecoveryParams, i, g, dt, o);
    std::cout << "Landing no_recovery expected_mps=" << expectedAfter << " actual_mps=" << s.Speed << '\n';
    assert(o.bLanded && std::fabs(s.Speed - expectedAfter) < 1e-5);
}

static void CaseAirControl()
{
    constexpr float dt = 1.f / 60;
    constexpr float speed = 2;
    constexpr float angle = 80 * 3.14159265f / 180;
    auto p = MakeTestParams();
    p.AirControl = .25f;
    p.AirTurnRate = .5f;
    p.Gaits[0].TurnRateCap = 3;
    p.Gaits[0].LateralAcceleration = 6;
    assert(QuadrupedLocomotionModel::IsValidParams(p));
    assert(std::min(p.Gaits[0].TurnRateCap, p.Gaits[0].LateralAcceleration / speed) > p.AirTurnRate);

    for (int test = 0; test < 2; ++test)
    {
        const float magnitude = test == 0 ? 1.f : .25f;
        LocomotionGroundInfo g;
        g.bReady = true;
        g.Velocity.z = speed;
        LocomotionIntent i;
        i.Move = Vector3(magnitude * std::sin(angle), 0, magnitude * std::cos(angle));
        LocomotionState s;
        s.Speed = speed;
        s.PlanarVelocity = Vector3(0, 0, speed);
        LocomotionOutput o;
        Step(s, p, i, g, dt, o);
        const float expectedTurn = p.AirTurnRate * dt;
        const float expectedSpeedChange = (test == 0 ? p.Gaits[0].Acceleration : p.Gaits[0].Deceleration) *
                                          p.AirControl * dt;
        const float speedChange = std::fabs(s.Speed - speed);
        std::cout << "AirControl case=" << test << " expected_turn=" << expectedTurn
                  << " actual_turn=" << std::fabs(o.DeltaYaw) << " expected_speed_change=" << expectedSpeedChange
                  << " actual_speed_change=" << speedChange << '\n';
        assert(std::fabs(o.DeltaYaw) <= expectedTurn + 1e-6f);
        assert(std::fabs(std::fabs(o.DeltaYaw) - expectedTurn) < 1e-6f);
        assert(speedChange <= expectedSpeedChange + 1e-6f);
        assert(std::fabs(speedChange - expectedSpeedChange) < 1e-6f);
        assert(test == 0 ? s.Speed > speed : s.Speed < speed);
    }
}

static bool EqualVector(const Vector3& a, const Vector3& b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

static bool EqualState(const LocomotionState& a, const LocomotionState& b)
{
    return EqualVector(a.PlanarVelocity, b.PlanarVelocity) && a.Speed == b.Speed && a.Yaw == b.Yaw &&
           a.SmoothedTarget == b.SmoothedTarget && a.TargetDerivative == b.TargetDerivative &&
           a.GaitElapsed == b.GaitElapsed && a.Phase == b.Phase && a.Pitch == b.Pitch && a.Bank == b.Bank &&
           a.JumpBufferRemaining == b.JumpBufferRemaining && a.CoyoteRemaining == b.CoyoteRemaining &&
           a.AirTime == b.AirTime && a.LandingRemaining == b.LandingRemaining &&
           a.PreviousVerticalVelocity == b.PreviousVerticalVelocity && a.Gait == b.Gait &&
           a.bInitialized == b.bInitialized && a.bWasGrounded == b.bWasGrounded && a.bPivot == b.bPivot &&
           a.bJumpConsumed == b.bJumpConsumed;
}

static bool EqualOutput(const LocomotionOutput& a, const LocomotionOutput& b)
{
    for (int index = 0; index < 4; ++index)
    {
        if (a.GaitWeights[index] != b.GaitWeights[index])
        {
            return false;
        }
    }
    return EqualVector(a.PlanarDisplacement, b.PlanarDisplacement) && EqualVector(a.PlanarVelocity, b.PlanarVelocity) &&
           a.Yaw == b.Yaw && a.DeltaYaw == b.DeltaYaw && a.TurnRate == b.TurnRate && a.Speed == b.Speed &&
           a.PhaseDelta == b.PhaseDelta && a.Pitch == b.Pitch && a.Bank == b.Bank && a.AirTime == b.AirTime &&
           a.VerticalVelocity == b.VerticalVelocity && a.JumpSpeed == b.JumpSpeed && a.Gait == b.Gait &&
           a.bGrounded == b.bGrounded && a.bLanded == b.bLanded && a.bJumpRequested == b.bJumpRequested;
}

static bool FiniteVector(const Vector3& v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

static bool FiniteState(const LocomotionState& s)
{
    return FiniteVector(s.PlanarVelocity) && std::isfinite(s.Speed) && std::isfinite(s.Yaw) &&
           std::isfinite(s.SmoothedTarget) && std::isfinite(s.TargetDerivative) && std::isfinite(s.GaitElapsed) &&
           std::isfinite(s.Phase) && std::isfinite(s.Pitch) && std::isfinite(s.Bank) &&
           std::isfinite(s.JumpBufferRemaining) && std::isfinite(s.CoyoteRemaining) && std::isfinite(s.AirTime) &&
           std::isfinite(s.LandingRemaining) && std::isfinite(s.PreviousVerticalVelocity);
}

static bool FiniteOutput(const LocomotionOutput& o)
{
    for (float weight : o.GaitWeights)
    {
        if (!std::isfinite(weight))
        {
            return false;
        }
    }
    return FiniteVector(o.PlanarDisplacement) && FiniteVector(o.PlanarVelocity) && std::isfinite(o.Yaw) &&
           std::isfinite(o.DeltaYaw) && std::isfinite(o.TurnRate) && std::isfinite(o.Speed) &&
           std::isfinite(o.PhaseDelta) && std::isfinite(o.Pitch) && std::isfinite(o.Bank) &&
           std::isfinite(o.AirTime) && std::isfinite(o.VerticalVelocity) && std::isfinite(o.JumpSpeed);
}

static void CaseExtremeTimeSteps()
{
    auto p = MakeTestParams();
    assert(QuadrupedLocomotionModel::IsValidParams(p));
    LocomotionGroundInfo g;
    g.bReady = g.bGrounded = true;
    g.Velocity = Vector3(1, .2f, 1.5f);
    g.PlatformVelocity = Vector3(.1f, 0, .2f);
    LocomotionIntent i;
    i.Move = Vector3::UnitX;
    i.bJumpPressed = true;
    LocomotionState initial;
    initial.PlanarVelocity = Vector3(1.6f, 0, 1.8f);
    initial.Speed = 2.5f;
    initial.Yaw = .7f;
    initial.SmoothedTarget = 1.2f;
    initial.TargetDerivative = .3f;
    initial.GaitElapsed = .3f;
    initial.Phase = .4f;
    initial.Pitch = .1f;
    initial.Bank = -.2f;
    initial.JumpBufferRemaining = .03f;
    initial.CoyoteRemaining = .08f;
    initial.AirTime = .04f;
    initial.LandingRemaining = .03f;
    initial.PreviousVerticalVelocity = -2;
    initial.bInitialized = initial.bWasGrounded = true;

    LocomotionOutput sentinel;
    sentinel.PlanarDisplacement = Vector3(1, 2, 3);
    sentinel.PlanarVelocity = Vector3(4, 5, 6);
    sentinel.Yaw = 7;
    sentinel.DeltaYaw = 8;
    sentinel.TurnRate = 9;
    sentinel.Speed = 10;
    for (int index = 0; index < 4; ++index)
    {
        sentinel.GaitWeights[index] = static_cast<float>(11 + index);
    }
    sentinel.PhaseDelta = 15;
    sentinel.Pitch = 16;
    sentinel.Bank = 17;
    sentinel.AirTime = 18;
    sentinel.VerticalVelocity = 19;
    sentinel.JumpSpeed = 20;
    sentinel.Gait = 3;
    sentinel.bGrounded = sentinel.bLanded = sentinel.bJumpRequested = true;

    const auto originalPrecision = std::cout.precision();
    std::cout.precision(std::numeric_limits<float>::max_digits10);
    const float validTimes[] = {0, 1e-7f, 1};
    for (float dt : validTimes)
    {
        LocomotionState s = initial;
        LocomotionOutput o = sentinel;
        const auto status = QuadrupedLocomotionModel::Step(s, p, i, g, dt, o);
        assert(status == LocomotionStepStatus::Success);
        assert(FiniteState(s) && FiniteOutput(o));
        if (dt == 0)
        {
            assert(EqualState(s, initial));
            assert(EqualVector(o.PlanarDisplacement, Vector3::Zero) && !o.bJumpRequested);
        }
        else
        {
            assert(o.bJumpRequested && o.DeltaYaw != 0);
        }
        std::cout << "ExtremeTime valid_dt=" << dt << " status=success finite_state=1 finite_output=1"
                  << " state_unchanged=" << EqualState(s, initial) << " delta_yaw=" << o.DeltaYaw << '\n';
    }

    const float invalidTimes[] = {std::nextafter(1.f, 2.f), 2, -1.f / 60, std::numeric_limits<float>::quiet_NaN()};
    for (float dt : invalidTimes)
    {
        LocomotionState s = initial;
        LocomotionOutput o = sentinel;
        const auto status = QuadrupedLocomotionModel::Step(s, p, i, g, dt, o);
        assert(status == LocomotionStepStatus::InvalidArgument);
        assert(EqualState(s, initial) && EqualOutput(o, sentinel));
        std::cout << "ExtremeTime invalid_dt=" << dt << " status=invalid state_unchanged=1 output_unchanged=1\n";
    }
    std::cout.precision(originalPrecision);
}

static void CaseZeroInputHoldsYaw()
{
    constexpr float initialYaw = 1.3f;
    constexpr float dt = 1.f / 60;
    auto p = MakeTestParams();
    assert(QuadrupedLocomotionModel::IsValidParams(p));
    LocomotionGroundInfo g;
    g.bReady = g.bGrounded = true;
    LocomotionIntent i;
    LocomotionState s;
    s.Yaw = initialYaw;
    s.Speed = 2;
    s.PlanarVelocity = Vector3(2 * std::sin(initialYaw), 0, 2 * std::cos(initialYaw));
    LocomotionOutput o;
    float maxYawError = 0;
    for (int frame = 0; frame < 600; ++frame)
    {
        Step(s, p, i, g, dt, o);
        maxYawError = std::max(maxYawError, std::fabs(s.Yaw - initialYaw));
        assert(std::fabs(s.Yaw - initialYaw) <= 1e-6f && o.DeltaYaw == 0);
    }
    std::cout << "ZeroInput expected_yaw=" << initialYaw << " actual_yaw=" << s.Yaw
              << " max_yaw_error=" << maxYawError << " delta_yaw=0 speed=" << s.Speed << '\n';
}

static void CaseFrameRateIndependence()
{
    constexpr double pi = 3.14159265358979323846;
    const int rates[] = {30, 60, 144};
    struct Result
    {
        double X = 0, Z = 0, Yaw = 0;
    };
    Result results[3];
    auto p = MakeTestParams();
    p.TargetHalfLife = .1f;
    p.TurnSlowdown = .15f;
    assert(QuadrupedLocomotionModel::IsValidParams(p));
    const auto originalPrecision = std::cout.precision();
    std::cout.precision(9);

    for (int test = 0; test < 3; ++test)
    {
        const int rate = rates[test];
        const float dt = 1.f / rate;
        LocomotionGroundInfo g;
        g.bReady = g.bGrounded = true;
        g.Velocity = {};
        LocomotionIntent i;
        LocomotionState s;
        LocomotionOutput o;
        bool bAccelerated = false, bReleasedDeceleration = false, bReverseBraked = false;
        for (int frame = 0; frame < rate * 3; ++frame)
        {
            // 加算誤差を持ち越さず、各フレームの開始時刻で入力を選ぶ。
            const double time = static_cast<double>(frame) / rate;
            if (time < .5)
            {
                i.Move = Vector3(0, 0, .75f);
            }
            else if (time < 1.5)
            {
                i.Move = Vector3(.8f, 0, 0);
            }
            else if (time < 2)
            {
                i.Move = {};
            }
            else
            {
                i.Move = Vector3(-.6f, 0, -.6f);
            }
            const float previousSpeed = s.Speed;
            const double error = time >= 2 ? std::remainder(-3 * pi / 4 - s.Yaw, 2 * pi) : 0;
            Step(s, p, i, g, dt, o);
            assert(s.Gait == 0);
            bAccelerated |= time < .5 && s.Speed > previousSpeed;
            bReleasedDeceleration |= time >= 1.5 && time < 2 && s.Speed < previousSpeed;
            bReverseBraked |= time >= 2 && std::cos(error) < 0 && s.Speed < previousSpeed;
            results[test].X += o.PlanarDisplacement.x;
            results[test].Z += o.PlanarDisplacement.z;
        }
        results[test].Yaw = s.Yaw;
        assert(bAccelerated && bReleasedDeceleration && bReverseBraked);
        const double rotation = std::fabs(std::remainder(results[test].Yaw, 2 * pi));
        std::cout << "FrameRateIndependence rate=" << rate << " dt_s=" << dt
                  << " final_x_m=" << results[test].X << " final_z_m=" << results[test].Z
                  << " final_yaw_rad=" << results[test].Yaw << " expected_rotation_min_rad=0.5 actual_rotation_rad="
                  << rotation << '\n';
        assert(rotation >= .5);
    }

    double maxPositionDifference = 0, maxYawDifference = 0;
    for (int first = 0; first < 3; ++first)
    {
        for (int second = first + 1; second < 3; ++second)
        {
            const double positionDifference = std::hypot(results[first].X - results[second].X,
                                                         results[first].Z - results[second].Z);
            const double yawDifference = std::fabs(std::remainder(results[first].Yaw - results[second].Yaw, 2 * pi));
            maxPositionDifference = std::max(maxPositionDifference, positionDifference);
            maxYawDifference = std::max(maxYawDifference, yawDifference);
            std::cout << "FrameRateIndependence pair=" << rates[first] << ',' << rates[second]
                      << " expected_position_max_m=0.02 actual_position_m=" << positionDifference
                      << " expected_yaw_max_rad=0.01 actual_yaw_rad=" << yawDifference << '\n';
            assert(positionDifference <= .02 && yawDifference <= .01);
        }
    }
    std::cout << "FrameRateIndependence max_position_difference_m=" << maxPositionDifference
              << " max_yaw_difference_rad=" << maxYawDifference << '\n';
    std::cout.precision(originalPrecision);
}

static void CasePivotTurn()
{
    constexpr float dt = 1.f / 256;
    constexpr double pi = 3.14159265358979323846;
    auto p = MakeTestParams();
    p.TargetHalfLife = 0;
    p.TurnSlowdown = 0;
    p.PivotSpeed = .25f;
    p.PivotEnterAngle = 1.2f;
    p.PivotExitAngle = .25f;
    p.PivotRate = 4;
    assert(QuadrupedLocomotionModel::IsValidParams(p));
    LocomotionGroundInfo g;
    g.bReady = g.bGrounded = true;
    LocomotionIntent i;
    const float inputYaw = p.PivotEnterAngle + .05f;
    i.Move = Vector3(std::sin(inputYaw), 0, std::cos(inputYaw));
    LocomotionState s;
    LocomotionOutput o;
    int pivotSteps = 0;
    bool bExited = false;
    double maxTurnError = 0, maxPivotSpeed = 0;
    const double expectedTurn = p.PivotRate * static_cast<double>(dt);
    for (int frame = 0; frame < 256; ++frame)
    {
        const float previousYaw = s.Yaw;
        const double previousError = std::fabs(std::remainder(static_cast<double>(inputYaw) - previousYaw, 2 * pi));
        Step(s, p, i, g, dt, o);
        if (frame == 0)
        {
            assert(s.bPivot);
        }
        const double remaining = std::fabs(std::remainder(static_cast<double>(inputYaw) - s.Yaw, 2 * pi));
        if (s.bPivot)
        {
            assert(previousError > p.PivotExitAngle);
            ++pivotSteps;
            maxPivotSpeed = std::max(maxPivotSpeed, static_cast<double>(s.Speed));
            const double actualTurn = std::remainder(static_cast<double>(s.Yaw) - previousYaw, 2 * pi);
            maxTurnError = std::max(maxTurnError, std::fabs(actualTurn - expectedTurn));
            assert(std::fabs(actualTurn - expectedTurn) < 1e-6);
            assert(std::fabs(o.DeltaYaw - expectedTurn) < 1e-6);
            assert(s.Speed == 0 && o.PlanarDisplacement == Vector3::Zero);
        }
        else
        {
            assert(previousError <= p.PivotExitAngle && remaining < p.PivotExitAngle);
            std::cout << "PivotTurn exit_step=" << frame + 1 << " expected_error_below_rad=" << p.PivotExitAngle
                      << " actual_error_rad=" << remaining << " error_before_rad=" << previousError << '\n';
            bExited = true;
            break;
        }
    }
    assert(pivotSteps > 0 && bExited);
    std::cout << "PivotTurn pivot_steps=" << pivotSteps << " expected_turn_rad=" << expectedTurn
              << " max_turn_error_rad=" << maxTurnError << " expected_speed_mps=0 actual_max_pivot_speed_mps="
              << maxPivotSpeed << '\n';

    for (int test = 0; test < 2; ++test)
    {
        s = {};
        const float angle = p.PivotEnterAngle + (test == 0 ? -.05f : .05f);
        s.Speed = test == 0 ? 0 : p.PivotSpeed;
        s.PlanarVelocity = Vector3(0, 0, s.Speed);
        const float initialSpeed = s.Speed;
        i.Move = Vector3(std::sin(angle), 0, std::cos(angle));
        Step(s, p, i, g, dt, o);
        std::cout << "PivotTurn counterexample=" << (test == 0 ? "below_enter_angle" : "at_pivot_speed")
                  << " input_error_rad=" << angle << " initial_speed_mps=" << initialSpeed
                  << " expected_pivot=0 actual_pivot=" << s.bPivot << '\n';
        assert(!s.bPivot);
    }
}

static void CaseTurnBank()
{
    constexpr float dt = 1.f / 256;
    constexpr float speed = 3;
    for (int saturated = 0; saturated < 2; ++saturated)
    {
        for (int direction : {-1, 1})
        {
            auto p = MakeTestParams();
            p.TargetHalfLife = 0;
            p.TurnSlowdown = 0;
            p.Gaits[0].TurnRateCap = 1;
            p.Gaits[0].LateralAcceleration = 6;
            p.LeanGain = saturated ? 5.f : 1.f;
            p.MaximumBank = .45f;
            p.LeanHalfLife = .05f;
            assert(QuadrupedLocomotionModel::IsValidParams(p));
            LocomotionGroundInfo g;
            g.bReady = g.bGrounded = true;
            g.Gravity = 9.81f;
            LocomotionIntent i;
            LocomotionState s;
            s.Speed = s.SmoothedTarget = speed;
            s.PlanarVelocity = Vector3(0, 0, speed);
            LocomotionOutput o;
            const float magnitude = speed / p.Gaits[1].SpeedMax;
            double maxBank = 0, maxSettledError = 0, expected = 0;
            for (int frame = 0; frame < 512; ++frame)
            {
                // 旋回方向へ一定のずれを与え、速さと角速度を一定に保つ。
                const float inputYaw = s.Yaw + direction * .8f;
                i.Move = Vector3(magnitude * std::sin(inputYaw), 0, magnitude * std::cos(inputYaw));
                Step(s, p, i, g, dt, o);
                assert(s.Gait == 0 && !s.bPivot && std::fabs(s.Speed - speed) < 1e-5);
                assert(std::fabs(o.TurnRate - direction * p.Gaits[0].TurnRateCap) < 1e-5);
                const double physicalBank = std::atan2(static_cast<double>(s.Speed) * o.TurnRate, g.Gravity);
                const double target = physicalBank * p.LeanGain;
                expected = std::clamp(target, -static_cast<double>(p.MaximumBank),
                                      static_cast<double>(p.MaximumBank));
                assert(o.Bank * physicalBank > 0 && s.Bank == o.Bank);
                assert(std::fabs(o.Bank) <= p.MaximumBank);
                maxBank = std::max(maxBank, std::fabs(static_cast<double>(o.Bank)));
                if (saturated)
                {
                    assert(std::fabs(target) > p.MaximumBank);
                }
                else
                {
                    assert(std::fabs(target) < p.MaximumBank);
                }
                // 1 秒の整定後から全刻みを検査し、終端だけの偶然の一致を避ける。
                if (frame >= 256)
                {
                    const double error = std::fabs(o.Bank - expected);
                    maxSettledError = std::max(maxSettledError, error);
                    assert(error <= (saturated ? 1e-5 : std::fabs(expected) * .02));
                }
            }
            std::cout << "TurnBank direction=" << direction << " saturated=" << saturated
                      << " expected_bank_rad=" << expected << " actual_bank_rad=" << o.Bank
                      << " expected_max_rad=" << p.MaximumBank << " actual_max_rad=" << maxBank
                      << " max_settled_error_rad=" << maxSettledError
                      << " allowed_error_rad=" << (saturated ? 1e-5 : std::fabs(expected) * .02) << '\n';
        }
    }
}

int main()
{
    CaseExistingBaseline();
    CaseGaitMinDwell();
    CaseMinimumTurnRadius();
    CaseAccelerationAndStopping();
    CaseSlopeScale();
    CaseJumpTiming();
    CaseLandingRecovery();
    CaseAirControl();
    CaseExtremeTimeSteps();
    CaseZeroInputHoldsYaw();
    CaseFrameRateIndependence();
    CasePivotTurn();
    CaseTurnBank();
    std::cout << "QuadrupedLocomotionModelTest PASS\n";
    return 0;
}
