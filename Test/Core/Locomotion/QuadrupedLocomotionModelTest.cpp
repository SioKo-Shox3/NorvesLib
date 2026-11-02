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

int main()
{
    CaseExistingBaseline();
    CaseGaitMinDwell();
    CaseMinimumTurnRadius();
    CaseAccelerationAndStopping();
    CaseSlopeScale();
    std::cout << "QuadrupedLocomotionModelTest PASS\n";
    return 0;
}
