#pragma once

#include "MeshCooker.h"
#include <filesystem>

namespace NorvesLib::Tools::AssetCook
{
    // 不在/不整合/読込失敗はいずれもmiss。書込やcookは行わない。
    [[nodiscard]] bool IsModelCookCacheCurrent(const std::filesystem::path& manifestPath,
        const std::filesystem::path& packagePath, Core::Container::AnsiStringView logicalPath,
        Core::Container::AnsiStringView variant, Core::Container::AnsiStringView format,
        Core::Container::AnsiStringView entryName, const ModelCookFingerprint& fingerprint);
} // namespace NorvesLib::Tools::AssetCook
