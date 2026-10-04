#pragma once
#include "CookOutputPlan.h"
namespace NorvesLib::Tools::AssetCook
{
    inline constexpr size_t MaximumCookSetPlans = 4096;
    inline constexpr size_t MaximumCookSetOutputs = 16384;
    inline constexpr size_t MaximumCookSetProtectedOccurrences = 65536;
    inline constexpr size_t MaximumCookSetMetadataBytes = 32 * 1024 * 1024;
    // FINAL planの観測を再検証し、集合を跨ぐ出力/入力/controlの衝突を読み取り専用で調べる。
    // controlはfile endpointを予約する。Directory自体、所有/公開許可、atomic snapshotは扱わない。
    [[nodiscard]] bool ValidateCookOutputSet(Core::Container::Span<const CookPreparedPlan> finalPlans,
                                             Core::Container::Span<const std::filesystem::path> specAndControlFiles,
                                             Core::Container::AnsiString& error);
} // namespace NorvesLib::Tools::AssetCook
