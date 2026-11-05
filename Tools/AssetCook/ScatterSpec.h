// 散布スペックと参照地形を読み、NVSCT v0 に焼く。
#pragma once

#include "ScatterBaker.h"
#include <filesystem>

namespace NorvesLib::Tools::AssetCook
{
    struct ScatterSpecCookResult
    {
        Core::Container::VariableArray<uint8_t> Bytes;
        Core::Container::VariableArray<Core::Container::AnsiString> SpeciesNames;
        ScatterBakeStats Stats;
        uint64_t SourceHash = 0;
        uint64_t PayloadHash = 0;
        uint64_t InstanceCount = 0;
        uint32_t ChunkCount = 0;
    };

    // 参照地形の cooked_hash が記録と異なれば、散布を焼く前に失敗する。
    [[nodiscard]] bool CookScatterSpecFile(const std::filesystem::path& path, ScatterSpecCookResult& out,
                                            Core::Container::AnsiString& error);
} // namespace NorvesLib::Tools::AssetCook
