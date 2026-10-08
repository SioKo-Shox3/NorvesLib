#include "Engine/FixedStepScheduler.h"
#include "Scene/RenderTransformHistory.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <iostream>
using namespace NorvesLib;
using namespace NorvesLib::Core;
int main()
{
    for (uint32_t rate : {60u, 120u})
    {
        Engine::FixedStepScheduler scheduler;
        assert(scheduler.SetRate(rate));
        scheduler.BeginRun();
        Scene::RenderTransformHistory history;
        Math::Transform simulation, render;
        assert(history.Reset(simulation));
        float previousOn = 0, previousOff = 0, previousOnDelta = 0, previousOffDelta = 0;
        double onVariation = 0, offVariation = 0;
        uint64_t totalSteps = 0;
        unsigned offZero = 0;
        for (int64_t frame = 0; frame < 288; ++frame)
        {
            const int64_t raw = (frame + 1) * 1000000000 / 144 - frame * 1000000000 / 144;
            const auto step = scheduler.Advance(raw, true);
            assert(step.Status == Engine::EFixedStepAdvanceStatus::Advanced && step.DroppedSteps == 0);
            totalSteps += step.ExecutedSteps;
            for (uint64_t i = 0; i < step.ExecutedSteps; ++i)
            {
                assert(history.Prepare(simulation));
                simulation.position.x += scheduler.GetDeltaSeconds();
                assert(history.Capture(simulation));
            }
            const auto physical = simulation;
            assert(history.Evaluate(float(step.RemainderScaledUnits) / 1000000000, render));
            assert(simulation == physical);
            const float onDelta = render.position.x - previousOn, offDelta = simulation.position.x - previousOff;
            if (frame > 5)
            {
                assert(std::fabs(onDelta - 1.f / 144) < 1e-5f);
                if (std::fabs(offDelta) < 1e-7f)
                    ++offZero;
                onVariation += double(onDelta - previousOnDelta) * (onDelta - previousOnDelta);
                offVariation += double(offDelta - previousOffDelta) * (offDelta - previousOffDelta);
            }
            previousOn = render.position.x;
            previousOff = simulation.position.x;
            previousOnDelta = onDelta;
            previousOffDelta = offDelta;
        }
        assert(totalSteps == 2 * rate && offZero > (rate == 60 ? 150u : 35u));
        assert(onVariation < offVariation * 1e-4);
        const auto stopped = render;
        for (unsigned i = 0; i < 20; ++i)
        {
            const auto step = scheduler.Advance(0, true);
            assert(step.Status == Engine::EFixedStepAdvanceStatus::Advanced && step.ExecutedSteps == 0);
            assert(history.Evaluate(float(step.RemainderScaledUnits) / 1000000000, render) && render == stopped);
        }
        scheduler.EndRun();
    }
    std::cout << "RenderInterpolationCadenceTest passed\n";
    return 0;
}
