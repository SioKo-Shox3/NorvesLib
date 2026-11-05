// 荷重によるゲージの減少、回復、枯渇と入力拒否を検証する。
#include "Gameplay/Grip/GripSolver.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace
{
    using namespace NorvesLib::Modules::Gameplay::Grip;

#define GRIP_GAUGE_CHECK(expr) \
    do \
    { \
        if (!(expr)) \
        { \
            std::fprintf(stderr, "GripGaugeTest %d: %s\n", __LINE__, #expr); \
            std::abort(); \
        } \
    } while (false)

    constexpr float kTolerance = 1.0e-6f;

    void CheckChange(const char* name, float start, float load, bool bFeet, float dt,
                     float expectedDrain, float expectedRecovery, bool bExpectedDepleted)
    {
        const GripTuning tuning;
        GripGaugeState state{start, false};
        GripGaugeStep step;
        const bool bOk = StepGripGauge(state, load, bFeet, dt, tuning, step);
        std::printf("gauge %s start=%.9g load=%.9g feet=%d dt=%.9g drain=%.9g expected_drain=%.9g "
                    "recovery=%.9g expected_recovery=%.9g value=%.9g depleted=%d\n",
                    name, start, load, bFeet, dt, step.Drained, expectedDrain, step.Recovered,
                    expectedRecovery, state.Value, step.bDepletedNow);
        GRIP_GAUGE_CHECK(bOk);
        GRIP_GAUGE_CHECK(std::fabs(step.Drained - expectedDrain) <= kTolerance);
        GRIP_GAUGE_CHECK(std::fabs(step.Recovered - expectedRecovery) <= kTolerance);
        GRIP_GAUGE_CHECK(std::fabs(state.Value - (start - expectedDrain + expectedRecovery)) <= kTolerance);
        GRIP_GAUGE_CHECK(state.Value >= 0.0f && state.Value <= 1.0f);
        GRIP_GAUGE_CHECK(step.bDepletedNow == bExpectedDepleted);
        GRIP_GAUGE_CHECK(state.bDepleted == bExpectedDepleted);
    }

    void DrainAndRecovery()
    {
        const GripTuning tuning;
        const float dt = 0.5f;
        const float base = tuning.BaseDrainPerSecond * dt;
        const float full = (tuning.BaseDrainPerSecond + tuning.LoadDrainPerSecond) * dt;
        const float middle = (tuning.BaseDrainPerSecond + 0.5f * tuning.LoadDrainPerSecond) * dt;
        CheckChange("below_free_no_feet", 0.5f, 0.5f * tuning.FreeLoadG, false, dt, base, 0.0f, false);
        CheckChange("at_free_no_feet", 0.5f, tuning.FreeLoadG, false, dt, base, 0.0f, false);
        CheckChange("at_panic", 0.5f, tuning.PanicLoadG, false, dt, full, 0.0f, false);
        CheckChange("above_panic", 0.5f, tuning.PanicLoadG + 1.0f, false, dt, full, 0.0f, false);
        CheckChange("midpoint", 0.5f, 0.5f * (tuning.FreeLoadG + tuning.PanicLoadG), false, dt,
                    middle, 0.0f, false);
        CheckChange("below_free_feet", 0.5f, 0.5f * tuning.FreeLoadG, true, dt, 0.0f,
                    tuning.RecoveryPerSecond * dt, false);
        CheckChange("at_free_feet", 0.5f, tuning.FreeLoadG, true, dt, 0.0f,
                    tuning.RecoveryPerSecond * dt, false);
        CheckChange("above_free_feet", 0.5f, 0.5f * (tuning.FreeLoadG + tuning.PanicLoadG), true, dt,
                    middle, 0.0f, false);
        CheckChange("clamp_full", 0.98f, tuning.FreeLoadG, true, 1.0f, 0.0f, 0.02f, false);
        CheckChange("clamp_empty", 0.05f, tuning.PanicLoadG, false, 1.0f, 0.05f, 0.0f, true);
        CheckChange("zero_dt", 0.5f, tuning.PanicLoadG, false, 0.0f, 0.0f, 0.0f, false);
    }

    void DepletionAndReset()
    {
        const GripTuning tuning;
        GripGaugeState state{0.03f, false};
        GripGaugeStep step;
        bool bOk = StepGripGauge(state, tuning.PanicLoadG, false, 0.1f, tuning, step);
        std::printf("depletion first value=%.9g drain=%.9g marked=%d\n", state.Value, step.Drained, step.bDepletedNow);
        GRIP_GAUGE_CHECK(bOk);
        GRIP_GAUGE_CHECK(state.Value > 0.0f && !state.bDepleted && !step.bDepletedNow);
        const float before = state.Value;
        bOk = StepGripGauge(state, tuning.PanicLoadG, false, 0.1f, tuning, step);
        std::printf("depletion crossing value=%.9g drain=%.9g expected_drain=%.9g marked=%d\n",
                    state.Value, step.Drained, before, step.bDepletedNow);
        GRIP_GAUGE_CHECK(bOk);
        GRIP_GAUGE_CHECK(state.Value == 0.0f && state.bDepleted && step.bDepletedNow);
        GRIP_GAUGE_CHECK(std::fabs(step.Drained - before) <= kTolerance);
        for (int frame = 0; frame < 120; ++frame)
        {
            const bool bFeet = (frame % 2) == 0;
            bOk = StepGripGauge(state, bFeet ? 0.0f : tuning.PanicLoadG, bFeet, 1.0f, tuning, step);
            if (!bOk || state.Value != 0.0f || !state.bDepleted || step.bDepletedNow ||
                step.Drained != 0.0f || step.Recovered != 0.0f)
            {
                std::printf("depletion locked frame=%d ok=%d value=%.9g marked=%d drain=%.9g recovery=%.9g\n",
                            frame, bOk, state.Value, step.bDepletedNow, step.Drained, step.Recovered);
            }
            GRIP_GAUGE_CHECK(bOk);
            GRIP_GAUGE_CHECK(state.Value == 0.0f && state.bDepleted && !step.bDepletedNow);
            GRIP_GAUGE_CHECK(step.Drained == 0.0f && step.Recovered == 0.0f);
        }
        std::printf("depletion locked_frames=120 value=%.9g\n", state.Value);
        ResetGripGauge(state);
        std::printf("depletion reset value=%.9g depleted=%d\n", state.Value, state.bDepleted);
        GRIP_GAUGE_CHECK(state.Value == 1.0f && !state.bDepleted);
        bOk = StepGripGauge(state, tuning.PanicLoadG, false, 10.0f, tuning, step);
        std::printf("depletion after_reset value=%.9g drain=%.9g marked=%d\n", state.Value, step.Drained, step.bDepletedNow);
        GRIP_GAUGE_CHECK(bOk);
        GRIP_GAUGE_CHECK(state.Value == 0.0f && state.bDepleted && step.bDepletedNow);
        GRIP_GAUGE_CHECK(std::fabs(step.Drained - 1.0f) <= kTolerance);
    }

    void Reject(const char* name, GripGaugeState state, float load, float dt, const GripTuning& tuning)
    {
        GripGaugeStep step{0.25f, 0.5f, true};
        unsigned char beforeState[sizeof(state)];
        unsigned char beforeStep[sizeof(step)];
        std::memcpy(beforeState, &state, sizeof(state));
        std::memcpy(beforeStep, &step, sizeof(step));
        const bool bOk = StepGripGauge(state, load, true, dt, tuning, step);
        const bool bStateUnchanged = std::memcmp(beforeState, &state, sizeof(state)) == 0;
        const bool bStepUnchanged = std::memcmp(beforeStep, &step, sizeof(step)) == 0;
        std::printf("gauge reject %s ok=%d state_unchanged=%d step_unchanged=%d\n",
                    name, bOk, bStateUnchanged, bStepUnchanged);
        GRIP_GAUGE_CHECK(!bOk);
        GRIP_GAUGE_CHECK(bStateUnchanged);
        GRIP_GAUGE_CHECK(bStepUnchanged);
    }

    void InvalidInputs()
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float infinity = std::numeric_limits<float>::infinity();
        const GripTuning tuning;
        const GripGaugeState state{0.5f, false};
        Reject("load_nan", state, nan, 0.1f, tuning);
        Reject("load_inf", state, infinity, 0.1f, tuning);
        Reject("dt_nan", state, 1.0f, nan, tuning);
        Reject("dt_inf", state, 1.0f, infinity, tuning);
        Reject("dt_negative", state, 1.0f, -0.1f, tuning);
        Reject("state_nan", GripGaugeState{nan, false}, 1.0f, 0.1f, tuning);
        GripTuning invalidTuning = tuning;
        invalidTuning.PanicLoadG = infinity;
        Reject("tuning_inf", state, 1.0f, 0.1f, invalidTuning);
    }
} // namespace

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    DrainAndRecovery();
    DepletionAndReset();
    InvalidInputs();
    std::printf("GripGaugeTest PASS\n");
    return 0;
}
