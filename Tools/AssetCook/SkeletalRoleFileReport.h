#pragma once
#include "SkeletalRoleProfileCook.h"
namespace NorvesLib::Tools::AssetCook
{
    struct SingleAssetCookRequest;
    [[nodiscard]] bool BuildSkeletalRoleFileReport(const SingleAssetCookRequest& request,
                                                   const SkeletalRoleProfileCookResult& result, uint64_t dependencyHash,
                                                   Core::Container::AnsiString& outJson,
                                                   Core::Container::AnsiString& error);
} // namespace NorvesLib::Tools::AssetCook
