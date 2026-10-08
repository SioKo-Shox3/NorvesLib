#pragma once
#include "SkeletalRoleFileCook.h"
namespace NorvesLib::Tools::AssetCook::Detail
{
    enum class SkeletalRoleFilePoint : uint8_t
    {
        BeforeFirstWrite,
        BeforeManifestWrite,
        BeforeRecord
    };
    using SkeletalRoleFileProbe = void (*)(SkeletalRoleFilePoint, void*);
    [[nodiscard]] bool CookSkeletalRoleFileWithProbe(const SingleAssetCookRequest& request,
                                                     SkeletalRoleFileCookResult& out,
                                                     Core::Container::AnsiString& error, SkeletalRoleFileProbe probe,
                                                     void* context);
} // namespace NorvesLib::Tools::AssetCook::Detail
