#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>

struct FixedStepResult
{
    std::int64_t ExecutedSteps;
    std::int64_t DroppedSteps;
    std::int64_t RemainderScaledUnits;
};

FixedStepResult AdvanceFixedStep(std::int64_t deltaNanoseconds, std::int64_t remainderScaledUnits);

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    void RequireResult(const FixedStepResult& result,
                       std::int64_t expectedExecutedSteps,
                       std::int64_t expectedDroppedSteps,
                       std::int64_t expectedRemainderScaledUnits,
                       const char* message)
    {
        Require(result.ExecutedSteps == expectedExecutedSteps, message);
        Require(result.DroppedSteps == expectedDroppedSteps, message);
        Require(result.RemainderScaledUnits == expectedRemainderScaledUnits, message);
    }
} // namespace

int main()
{
    try
    {
        const FixedStepResult belowStep = AdvanceFixedStep(16'666'666, 0);
        RequireResult(belowStep, 0, 0, 999'999'960, "step未満のremainderが不正");

        const FixedStepResult exactStep = AdvanceFixedStep(1, belowStep.RemainderScaledUnits);
        RequireResult(exactStep, 1, 0, 20, "step境界の実行数またはremainderが不正");

        const FixedStepResult multipleSteps = AdvanceFixedStep(50'000'001, 0);
        RequireResult(multipleSteps, 3, 0, 60, "複数stepの実行数またはremainderが不正");

        const FixedStepResult catchUpLimit = AdvanceFixedStep(337'500'000, 0);
        RequireResult(catchUpLimit, 8, 12, 250'000'000, "catch-up上限またはwhole step破棄が不正");

        const FixedStepResult maximumDelta = AdvanceFixedStep(INT64_MAX, 0);
        RequireResult(maximumDelta,
                      8,
                      553'402'322'203,
                      286'548'420,
                      "INT64_MAXでoverflowせずにcatch-upを制限できない");

        const FixedStepResult firstSegment = AdvanceFixedStep(10'000'000, 0);
        const FixedStepResult secondSegment = AdvanceFixedStep(20'000'000, firstSegment.RemainderScaledUnits);
        const FixedStepResult combined = AdvanceFixedStep(30'000'000, 0);
        Require(firstSegment.ExecutedSteps + secondSegment.ExecutedSteps == combined.ExecutedSteps,
                "delta列でexecuted step数が決定的でない");
        Require(firstSegment.DroppedSteps + secondSegment.DroppedSteps == combined.DroppedSteps,
                "delta列でdropped step数が決定的でない");
        Require(secondSegment.RemainderScaledUnits == combined.RemainderScaledUnits,
                "delta列でremainderが決定的でない");

        std::int64_t fiftyMillisecondExecutedSteps = 0;
        std::int64_t fiftyMillisecondDroppedSteps = 0;
        std::int64_t fiftyMillisecondRemainder = 0;
        for (std::int64_t index = 0; index < 20; ++index)
        {
            const FixedStepResult result = AdvanceFixedStep(50'000'000, fiftyMillisecondRemainder);
            fiftyMillisecondExecutedSteps += result.ExecutedSteps;
            fiftyMillisecondDroppedSteps += result.DroppedSteps;
            fiftyMillisecondRemainder = result.RemainderScaledUnits;
        }

        std::int64_t hundredMillisecondExecutedSteps = 0;
        std::int64_t hundredMillisecondDroppedSteps = 0;
        std::int64_t hundredMillisecondRemainder = 0;
        for (std::int64_t index = 0; index < 10; ++index)
        {
            const FixedStepResult result = AdvanceFixedStep(100'000'000, hundredMillisecondRemainder);
            hundredMillisecondExecutedSteps += result.ExecutedSteps;
            hundredMillisecondDroppedSteps += result.DroppedSteps;
            hundredMillisecondRemainder = result.RemainderScaledUnits;
        }

        Require(fiftyMillisecondExecutedSteps == 60, "50ms列で1秒が60 stepにならない");
        Require(fiftyMillisecondDroppedSteps == 0, "50ms列でwhole stepが破棄された");
        Require(fiftyMillisecondRemainder == 0, "50ms列の1秒remainderが不正");
        Require(hundredMillisecondExecutedSteps == 60, "100ms列で1秒が60 stepにならない");
        Require(hundredMillisecondDroppedSteps == 0, "100ms列でwhole stepが破棄された");
        Require(hundredMillisecondRemainder == 0, "100ms列の1秒remainderが不正");
        Require(fiftyMillisecondExecutedSteps == hundredMillisecondExecutedSteps,
                "1秒のdelta列でexecuted step数が決定的でない");
        Require(fiftyMillisecondDroppedSteps == hundredMillisecondDroppedSteps,
                "1秒のdelta列でdropped step数が決定的でない");
        Require(fiftyMillisecondRemainder == hundredMillisecondRemainder,
                "1秒のdelta列でremainderが決定的でない");
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }

    return 0;
}
