#pragma once

#include "Container/String.h"
#include "Container/VariableArray.h"
#include <cstdint>
#include <filesystem>

// 草の配置を NVGFLD v0 に焼き、元の入力との照合を行う。
namespace NorvesLib::Tools::AssetCook
{
    struct GrassFieldCookResult
    {
        Core::Container::VariableArray<uint8_t> Bytes;
        uint64_t SourceHash = 0;
        uint64_t PayloadHash = 0;
        uint32_t ChunkCount = 0;
        uint32_t SpeciesCount = 0;
    };

    [[nodiscard]] bool CookGrassFieldSpecFile(const std::filesystem::path& path, GrassFieldCookResult& out,
                                              Core::Container::AnsiString& error);
    // 先頭引数が照合コマンドなら処理し、終了コードを返す。
    [[nodiscard]] bool RunGrassFieldCheckCommand(int argc, char** argv, int& exitCode);
} // namespace NorvesLib::Tools::AssetCook
