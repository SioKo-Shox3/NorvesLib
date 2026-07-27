#include <cstdint>

struct FixedStepResult
{
    std::int64_t ExecutedSteps;
    std::int64_t DroppedSteps;
    std::int64_t RemainderScaledUnits;
};

FixedStepResult AdvanceFixedStep(std::int64_t deltaNanoseconds, std::int64_t remainderScaledUnits)
{
    return { 0, 0, remainderScaledUnits };
}
