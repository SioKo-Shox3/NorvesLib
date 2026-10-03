#pragma once

#include "Resource/ImportSettings.h"

namespace NorvesLib::Core::AssetImport
{
    inline constexpr uint32_t ImportCookAlgorithmVersion = 1;
    struct ImportSettingsHash
    {
        bool bValid = false;
        uint64_t Value = 0;
    };
    // 既存FNV stateへu64長+正規化bytes+u32 algorithm versionをLEで連結する。
    // sidecar無しなら設定値/algorithm versionに触れず、既存stateをそのまま返す。
    [[nodiscard]] ImportSettingsHash AppendImportSettingsHash(uint64_t sourceHash, bool bPresent,
        const ImportSettings& settings, uint32_t algorithmVersion = ImportCookAlgorithmVersion) noexcept;
} // namespace NorvesLib::Core::AssetImport
