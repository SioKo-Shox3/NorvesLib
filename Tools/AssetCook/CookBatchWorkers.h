#pragma once
#include "CookOutputPlan.h"
namespace NorvesLib::Tools::AssetCook::Detail
{
    inline constexpr uint32_t MaximumCookBatchJobs = 64;
    // 出力が互いに独立と検証されたstageだけをcookする。公開と集約は呼出元で直列に行う。
    [[nodiscard]] bool CookPreparedBatch(Core::Container::Span<const CookPreparedPlan> stages, uint32_t jobs,
                                         Core::Container::AnsiString& error);
} // namespace NorvesLib::Tools::AssetCook::Detail
