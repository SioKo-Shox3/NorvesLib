#include "Engine/FixedStepScheduler.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>
#include <thread>

using namespace NorvesLib::Core::Engine;

namespace
{
    void AssertResult(const FixedStepAdvanceResult& result,
                      EFixedStepAdvanceStatus status,
                      uint64_t executedSteps,
                      uint64_t droppedSteps,
                      uint64_t remainderScaledUnits)
    {
        assert(result.Status == status);
        assert(result.ExecutedSteps == executedSteps);
        assert(result.DroppedSteps == droppedSteps);
        assert(result.RemainderScaledUnits == remainderScaledUnits);
    }

    void TestLessThanStepDoesNotRun()
    {
        FixedStepScheduler scheduler;
        scheduler.BeginRun();

        const FixedStepAdvanceResult result = scheduler.Advance(16'666'666, true);

        AssertResult(result, EFixedStepAdvanceStatus::Advanced, 0, 0, 999'999'960);
    }

    void TestExactStepRunsOnce()
    {
        FixedStepScheduler scheduler;
        scheduler.BeginRun();

        scheduler.Advance(16'666'666, true);
        const FixedStepAdvanceResult result = scheduler.Advance(1, true);

        AssertResult(result, EFixedStepAdvanceStatus::Advanced, 1, 0, 20);
    }

    void TestMultipleStepsKeepRemainder()
    {
        FixedStepScheduler scheduler;
        scheduler.BeginRun();

        const FixedStepAdvanceResult result = scheduler.Advance(50'000'001, true);

        AssertResult(result, EFixedStepAdvanceStatus::Advanced, 3, 0, 60);
    }

    void AdvanceSequence(FixedStepScheduler& scheduler,
                         int64_t deltaNanoseconds,
                         uint64_t callCount,
                         uint64_t& totalExecutedSteps,
                         uint64_t& totalDroppedSteps,
                         uint64_t& remainderScaledUnits)
    {
        totalExecutedSteps = 0;
        totalDroppedSteps = 0;
        remainderScaledUnits = 0;
        for (uint64_t i = 0; i < callCount; ++i)
        {
            const FixedStepAdvanceResult result = scheduler.Advance(deltaNanoseconds, true);
            assert(result.Status == EFixedStepAdvanceStatus::Advanced);
            totalExecutedSteps += result.ExecutedSteps;
            totalDroppedSteps += result.DroppedSteps;
            remainderScaledUnits = result.RemainderScaledUnits;
        }
    }

    void TestDeltaSequencesWithSameElapsedTimeAreDeterministic()
    {
        for (uint64_t repetition = 0; repetition < 64; ++repetition)
        {
            FixedStepScheduler firstScheduler;
            firstScheduler.BeginRun();
            uint64_t firstExecutedSteps = 0;
            uint64_t firstDroppedSteps = 0;
            uint64_t firstRemainderScaledUnits = 0;
            AdvanceSequence(firstScheduler, 50'000'000, 20,
                            firstExecutedSteps, firstDroppedSteps, firstRemainderScaledUnits);

            FixedStepScheduler secondScheduler;
            secondScheduler.BeginRun();
            uint64_t secondExecutedSteps = 0;
            uint64_t secondDroppedSteps = 0;
            uint64_t secondRemainderScaledUnits = 0;
            AdvanceSequence(secondScheduler, 100'000'000, 10,
                            secondExecutedSteps, secondDroppedSteps, secondRemainderScaledUnits);

            assert(firstExecutedSteps == 60);
            assert(firstDroppedSteps == 0);
            assert(firstRemainderScaledUnits == 0);
            assert(secondExecutedSteps == firstExecutedSteps);
            assert(secondDroppedSteps == firstDroppedSteps);
            assert(secondRemainderScaledUnits == firstRemainderScaledUnits);
        }
    }

    void TestCatchUpCapDiscardsWholeStepsAndKeepsRemainder()
    {
        for (uint64_t repetition = 0; repetition < 64; ++repetition)
        {
            FixedStepScheduler scheduler;
            scheduler.BeginRun();

            const FixedStepAdvanceResult result = scheduler.Advance(337'500'000, true);

            AssertResult(result, EFixedStepAdvanceStatus::Advanced, 8, 12, 250'000'000);
        }
    }

    void TestPauseKeepsRemainderAndResumeExcludesPausedTime()
    {
        FixedStepScheduler scheduler;
        scheduler.BeginRun();

        const FixedStepAdvanceResult beforePause = scheduler.Advance(8'333'333, true);
        const FixedStepAdvanceResult paused = scheduler.Advance(1'000'000'000, false);
        const FixedStepAdvanceResult resumed = scheduler.Advance(8'333'334, true);

        AssertResult(beforePause, EFixedStepAdvanceStatus::Advanced, 0, 0, 499'999'980);
        AssertResult(paused, EFixedStepAdvanceStatus::Paused, 0, 0, 499'999'980);
        AssertResult(resumed, EFixedStepAdvanceStatus::Advanced, 1, 0, 20);
    }

    void TestZeroNegativeAndNotRunningAreNoOp()
    {
        for (uint64_t repetition = 0; repetition < 64; ++repetition)
        {
            FixedStepScheduler scheduler;

            AssertResult(scheduler.Advance(1, true), EFixedStepAdvanceStatus::NotRunning, 0, 0, 0);
            scheduler.BeginRun();
            const FixedStepAdvanceResult seeded = scheduler.Advance(8'333'333, true);
            AssertResult(seeded, EFixedStepAdvanceStatus::Advanced, 0, 0, 499'999'980);
            AssertResult(scheduler.Advance(0, true), EFixedStepAdvanceStatus::InvalidDelta, 0, 0, 499'999'980);
            AssertResult(scheduler.Advance(-1, true), EFixedStepAdvanceStatus::InvalidDelta, 0, 0, 499'999'980);
            scheduler.EndRun();
            AssertResult(scheduler.Advance(1, true), EFixedStepAdvanceStatus::NotRunning, 0, 0, 499'999'980);
        }
    }

    void TestExtremeDeltaReturnsExactCountsWithoutOverflow()
    {
        FixedStepScheduler scheduler;
        scheduler.BeginRun();

        const FixedStepAdvanceResult result = scheduler.Advance(std::numeric_limits<int64_t>::max(), true);

        AssertResult(result, EFixedStepAdvanceStatus::Advanced, 8, 553'402'322'203, 286'548'420);
    }

    void TestWrongThreadLeavesResultAndAccumulatorUntouched()
    {
        FixedStepScheduler scheduler;
        scheduler.BeginRun();
        const FixedStepAdvanceResult seeded = scheduler.Advance(8'333'333, true);
        AssertResult(seeded, EFixedStepAdvanceStatus::Advanced, 0, 0, 499'999'980);

        for (uint64_t repetition = 0; repetition < 64; ++repetition)
        {
            FixedStepAdvanceResult workerResult{};
            std::thread worker([&scheduler, &workerResult]()
            {
                workerResult = scheduler.Advance(1'000'000'000, true);
            });
            worker.join();

            AssertResult(workerResult, EFixedStepAdvanceStatus::WrongThread, 0, 0, 499'999'980);
        }

        const FixedStepAdvanceResult resumed = scheduler.Advance(8'333'334, true);
        AssertResult(resumed, EFixedStepAdvanceStatus::Advanced, 1, 0, 20);
    }
} // namespace

int main()
{
    TestLessThanStepDoesNotRun();
    TestExactStepRunsOnce();
    TestMultipleStepsKeepRemainder();
    TestDeltaSequencesWithSameElapsedTimeAreDeterministic();
    TestCatchUpCapDiscardsWholeStepsAndKeepsRemainder();
    TestPauseKeepsRemainderAndResumeExcludesPausedTime();
    TestZeroNegativeAndNotRunningAreNoOp();
    TestExtremeDeltaReturnsExactCountsWithoutOverflow();
    TestWrongThreadLeavesResultAndAccumulatorUntouched();

    std::cout << "FixedStepSchedulerTest passed\n";
    return 0;
}
