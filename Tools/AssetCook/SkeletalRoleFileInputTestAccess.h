#pragma once
#include "SkeletalRoleFileInput.h"
namespace NorvesLib::Tools::AssetCook::Detail
{
    // 長さ観測後・同じstreamからのread直前だけに注入する。製品入口はnullptr。
    using SkeletalRoleReadProbe = void (*)(const std::filesystem::path&, void*);
    [[nodiscard]] bool LoadSkeletalRoleFileInputsWithProbe(const SkeletalRoleFileRequest& request,
                                                           SkeletalRoleFileInputs& out,
                                                           Core::Container::AnsiString& error,
                                                           SkeletalRoleReadProbe probe, void* context);
} // namespace NorvesLib::Tools::AssetCook::Detail
