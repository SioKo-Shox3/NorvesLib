#pragma once
#include "CookOutputSetGuard.h"
namespace NorvesLib::Tools::AssetCook::Detail
{
    struct CookOutputSetGuardStats
    {
        size_t LocatorOccurrences = 0, UniqueLocators = 0, AggregateIdentityObservations = 0;
        // 既存Prepare/Stableのsource byte読込や固定件数の個別guard queryは数えない。
    };
    [[nodiscard]] bool ValidateCookOutputSetForTest(Core::Container::Span<const CookPreparedPlan> finalPlans,
                                                    Core::Container::Span<const std::filesystem::path> protectedFiles,
                                                    CookOutputSetGuardStats& stats, Core::Container::AnsiString& error,
                                                    void (*afterObservation)(void*) = nullptr, void* context = nullptr);
} // namespace NorvesLib::Tools::AssetCook::Detail
