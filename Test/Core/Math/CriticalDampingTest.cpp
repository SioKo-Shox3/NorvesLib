#include "Math/CriticalDamping.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iomanip>
#include <iostream>
#include <random>
using namespace NorvesLib::Math;
int main()
{
    float p = 10, v = 0;
    assert(TryCriticalDamp(p, v, 0, .2f, .2f));
    assert(std::fabs(p - 5) < 1e-5);
    float referenceP = 12, referenceV = -3;
    assert(TryCriticalDamp(referenceP, referenceV, 2, .35f, 2));
    for (int rate : {30, 60, 144})
    {
        float x = 12, speed = -3;
        for (int i = 0; i < rate * 2; ++i)
            assert(TryCriticalDamp(x, speed, 2, .35f, 1.f / rate));
        assert(std::fabs(x - referenceP) < 2e-5);
        assert(std::fabs(speed - referenceV) < 2e-5);
    }
    p = 1;
    v = 4;
    assert(TryCriticalDamp(p, v, 8, 0, 0) && p == 1 && v == 4);
    assert(TryCriticalDamp(p, v, 8, 0, 1) && p == 8 && v == 0);
    p = 1;
    v = 4;
    const float inf = std::numeric_limits<float>::infinity();
    assert(!TryCriticalDamp(p, v, 8, -1, 1) && p == 1 && v == 4);
    assert(!TryCriticalDamp(p, v, 8, 1, -1) && p == 1 && v == 4);
    assert(!TryCriticalDamp(p, v, inf, 1, 1) && p == 1 && v == 4);
    assert(!TryCriticalDamp(p, v, 8, 1, inf) && p == 1 && v == 4);
    assert(TryCriticalDamp(p, v, 8, std::numeric_limits<float>::denorm_min(), std::numeric_limits<float>::max()) &&
           p == 8 && v == 0);
    Vector3 position(1, 2, 3), velocity(4, 5, 6);
    assert(!TryCriticalDamp(position, velocity, Vector3(8), Vector3(.2f, -1, .2f), .1f));
    assert(position == Vector3(1, 2, 3) && velocity == Vector3(4, 5, 6));
    assert(TryCriticalDamp(position, velocity, Vector3(8), Vector3(.1f, .3f, .1f), .1f));
    assert(position.x != position.y);
    // 大きな位置誤差でも、指数だけで早期打ち切りせず解析値を保つ。
    p = 1e38f;
    v = 0;
    assert(TryCriticalDamp(p, v, 0, 1, static_cast<float>(80 / 1.6783469900166605)));
    assert(p > 1e4f && std::isfinite(v));
    p = 1;
    v = 0;
    assert(TryCriticalDamp(p, v, 1e20f, 1, 1e-10f));
    assert(std::fabs(p - 2.408424377f) < 1e-5f);
    assert(!TryCriticalDamp(p, p, 0, 1, 1));
    // 初速度ゼロの固定目標への追従で、位置誤差の符号が反転しないことを確かめる。
    {
        constexpr float target = 2.f;
        constexpr float initialPosition = 12.f;
        const float initialError = initialPosition - target;
        for (int rate : {30, 60, 144})
        {
            float position = initialPosition;
            float velocity = 0.f;
            bool bSignReversal = false;
            for (int i = 0; i < rate * 3; ++i)
            {
                assert(TryCriticalDamp(position, velocity, target, .35f, 1.f / rate));
                const float error = position - target;
                bSignReversal = bSignReversal || (initialError > 0.f ? error < 0.f : error > 0.f);
                assert(!bSignReversal);
            }
            std::cout << "no_overshoot rate_hz=" << rate << " sign_reversal=" << bSignReversal << '\n';
        }
    }

    // 不規則な時間刻みでも、合計１秒の位置と速度が一括計算に一致することを確かめる。
    {
        constexpr int ticksPerSecond = 1 << 20;
        constexpr int minimumTicks = (ticksPerSecond + 239) / 240;
        constexpr int maximumTicks = ticksPerSecond / 20;
        constexpr float target = 2.f;
        float singlePosition = 12.f;
        float singleVelocity = -3.f;
        float splitPosition = singlePosition;
        float splitVelocity = singleVelocity;
        assert(TryCriticalDamp(singlePosition, singleVelocity, target, .35f, 1.f));

        std::mt19937 random(0x3dca51u);
        std::uniform_int_distribution<int> stepDistribution(minimumTicks, maximumTicks);
        int remainingTicks = ticksPerSecond;
        while (remainingTicks > 0)
        {
            int stepTicks = remainingTicks <= maximumTicks ? remainingTicks : stepDistribution(random);
            if (remainingTicks > maximumTicks && remainingTicks - stepTicks < minimumTicks)
            {
                stepTicks = remainingTicks - minimumTicks;
            }
            assert(stepTicks >= minimumTicks && stepTicks <= maximumTicks);
            const float dt = static_cast<float>(stepTicks) / ticksPerSecond;
            assert(TryCriticalDamp(splitPosition, splitVelocity, target, .35f, dt));
            remainingTicks -= stepTicks;
        }
        const float positionRelativeError =
            std::fabs(splitPosition - singlePosition) / std::fmax(std::fabs(singlePosition), 1.f);
        const float velocityRelativeError =
            std::fabs(splitVelocity - singleVelocity) / std::fmax(std::fabs(singleVelocity), 1.f);
        assert(positionRelativeError <= 1e-4f);
        assert(velocityRelativeError <= 1e-4f);
        std::cout << std::setprecision(9) << "irregular_dt position_relative_error=" << positionRelativeError
                  << " velocity_relative_error=" << velocityRelativeError << '\n';
    }

    // 一定速度で動く目標を追うとき、隣接フレーム間の速度差が上限を超えないことを確かめる。
    {
        constexpr float targetSpeed = 1.f;
        for (int rate : {30, 60, 144})
        {
            const float dt = 1.f / rate;
            const float maximumAllowedDelta = 2.f * std::fabs(targetSpeed) * dt;
            float position = 0.f;
            float velocity = 0.f;
            float maximumVelocityDelta = 0.f;
            for (int frame = 1; frame <= 300; ++frame)
            {
                const float previousVelocity = velocity;
                const float target = targetSpeed * (frame * dt);
                assert(TryCriticalDamp(position, velocity, target, .35f, dt));
                const float velocityDelta = std::fabs(velocity - previousVelocity);
                maximumVelocityDelta = std::fmax(maximumVelocityDelta, velocityDelta);
                assert(velocityDelta <= maximumAllowedDelta);
            }
            std::cout << std::setprecision(9) << "moving_target rate_hz=" << rate
                      << " maximum_velocity_delta=" << maximumVelocityDelta
                      << " allowed_delta=" << maximumAllowedDelta << '\n';
        }
    }
    std::cout << "CriticalDampingTest PASS\n";
    return 0;
}
