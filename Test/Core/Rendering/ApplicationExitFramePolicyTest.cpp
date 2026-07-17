#include "Engine/ApplicationExitFramePolicy.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>

using namespace NorvesLib::Core::Engine::Detail;

namespace
{
    void AssertArgument(ExitFrameOptionsAccumulator& options, const TCHAR* pArgument, ExitFrameArgumentKind expectedKind, uint64_t expectedValue)
    {
        const ExitFrameArgumentResult result = AccumulateExitFrameArgument(options, pArgument);
        assert(result.Kind == expectedKind);
        assert(result.Value == expectedValue);
    }

    void TestArgumentParsingAndSelection()
    {
        ExitFrameOptionsAccumulator options{};
        AssertArgument(options, TEXT("--exit-after-frames=1"), ExitFrameArgumentKind::LegacyValid, 1);
        AssertArgument(options, TEXT("--exit-after-frames=18446744073709551615"), ExitFrameArgumentKind::LegacyValid, std::numeric_limits<uint64_t>::max());
        AssertArgument(options, TEXT("--exit-after-frames=0"), ExitFrameArgumentKind::LegacyInvalid, 0);
        AssertArgument(options, TEXT("--exit-after-frames=-1"), ExitFrameArgumentKind::LegacyInvalid, 0);
        AssertArgument(options, TEXT("--exit-after-frames=18446744073709551616"), ExitFrameArgumentKind::LegacyInvalid, 0);
        assert(options.LegacyTarget == std::numeric_limits<uint64_t>::max());

        AssertArgument(options, TEXT("--exit-after-frames=9"), ExitFrameArgumentKind::LegacyValid, 9);
        AssertArgument(options, TEXT("--exit-after-frames=bad"), ExitFrameArgumentKind::LegacyInvalid, 0);
        assert(options.LegacyTarget == 9);
        AssertArgument(options, TEXT("--exit-after-frames=7"), ExitFrameArgumentKind::LegacyValid, 7);
        assert(options.LegacyTarget == 7);

        AssertArgument(options, TEXT("--exit-after-rendered-frames=4"), ExitFrameArgumentKind::RenderedValid, 4);
        AssertArgument(options, TEXT("--exit-after-rendered-frames=0"), ExitFrameArgumentKind::RenderedInvalid, 0);
        assert(options.RenderedTarget == 4);
        AssertArgument(options, TEXT("--wait-for-asset-settle"), ExitFrameArgumentKind::WaitForAssetSettle, 0);

        const ExitFrameSelection renderedSelection = SelectExitFrameSelection(options);
        assert(renderedSelection.Metric == ExitFrameMetric::RenderedFrames);
        assert(renderedSelection.Target == 4);
        assert(renderedSelection.bWaitForAssetSettle);

        ExitFrameOptionsAccumulator reverseOrder{};
        AssertArgument(reverseOrder, TEXT("--exit-after-rendered-frames=3"), ExitFrameArgumentKind::RenderedValid, 3);
        AssertArgument(reverseOrder, TEXT("--exit-after-frames=2"), ExitFrameArgumentKind::LegacyValid, 2);
        assert(SelectExitFrameSelection(reverseOrder).Metric == ExitFrameMetric::RenderedFrames);

        ExitFrameOptionsAccumulator legacyOnly{};
        AssertArgument(legacyOnly, TEXT("--exit-after-frames=2"), ExitFrameArgumentKind::LegacyValid, 2);
        const ExitFrameSelection legacySelection = SelectExitFrameSelection(legacyOnly);
        assert(legacySelection.Metric == ExitFrameMetric::GameThreadFrames);
        assert(legacySelection.Target == 2);

        ExitFrameOptionsAccumulator waitOnly{};
        AssertArgument(waitOnly, TEXT("--wait-for-asset-settle"), ExitFrameArgumentKind::WaitForAssetSettle, 0);
        const ExitFrameSelection noTargetSelection = SelectExitFrameSelection(waitOnly);
        assert(noTargetSelection.Metric == ExitFrameMetric::None);
        assert(!noTargetSelection.bWaitForAssetSettle);
    }

    void TestSettleStateMachine()
    {
        bool bObserved = false;
        bool bBaselineLatched = false;
        uint64_t baseline = 0;

        ObservePendingAssets(false, bObserved, bBaselineLatched);
        assert(!bObserved);
        assert(!bBaselineLatched);
        assert(!EvaluateSettledRenderedExit(false, 1, 3, bObserved, bBaselineLatched, baseline));

        ObservePendingAssets(true, bObserved, bBaselineLatched);
        assert(bObserved);
        assert(!bBaselineLatched);
        assert(!EvaluateSettledRenderedExit(true, 4, 3, bObserved, bBaselineLatched, baseline));
        assert(!EvaluateSettledRenderedExit(false, 5, 3, bObserved, bBaselineLatched, baseline));
        assert(bBaselineLatched);
        assert(baseline == 5);
        assert(!EvaluateSettledRenderedExit(false, 7, 3, bObserved, bBaselineLatched, baseline));
        assert(EvaluateSettledRenderedExit(false, 8, 3, bObserved, bBaselineLatched, baseline));

        assert(!EvaluateSettledRenderedExit(true, 9, 3, bObserved, bBaselineLatched, baseline));
        assert(!bBaselineLatched);
        assert(!EvaluateSettledRenderedExit(false, 10, 3, bObserved, bBaselineLatched, baseline));
        assert(baseline == 10);
        assert(!EvaluateSettledRenderedExit(false, 9, 3, bObserved, bBaselineLatched, baseline));
        assert(baseline == 9);
        assert(EvaluateSettledRenderedExit(false, 12, 3, bObserved, bBaselineLatched, baseline));
    }
}

int main()
{
    TestArgumentParsingAndSelection();
    TestSettleStateMachine();
    std::cout << "ApplicationExitFramePolicyTest passed\n";
    return 0;
}
