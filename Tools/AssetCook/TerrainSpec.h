// 地形スペックと、参照入力をまとめて焼き込む API。
#pragma once
#include "HeightfieldImport.h"
#include "TerrainCooker.h"
#include <filesystem>

namespace NorvesLib::Tools::AssetCook
{
    struct TerrainSpec
    {
        std::filesystem::path SpecPath;          // 絶対パス。
        std::filesystem::path HeightSourcePath;  // スペックの場所を基準に解決した絶対パス。
        HeightImportDesc Import;                 // Scale/Offset に換算済み。TileCells・CellSizeM も入れる。
        double CellSizeM = 1.0;
        uint32_t TileCells = 128;
        uint64_t Seed = 0;
        bool bHasRecipe = false;
    };

    // スペック JSON のバイト列を解析する。specPath は相対パスの基準（その親ディレクトリ）。
    [[nodiscard]] bool ParseTerrainSpec(Core::Container::Span<const uint8_t> specBytes, const std::filesystem::path& specPath,
                                        TerrainSpec& outSpec, Core::Container::AnsiString& outError);

    struct TerrainSpecCookResult
    {
        TerrainSpec Spec;
        TerrainBuildResult Build;
        uint64_t SourceHash = 0;  // スペックと参照した全入力のバイト列から求める。
        uint32_t SourceWidth = 0;
        uint32_t SourceHeight = 0;
        uint32_t SourceBitDepth = 0;
        Core::Container::VariableArray<std::filesystem::path> InputFiles; // 先頭がスペック、続けて参照した順。
        Core::Container::VariableArray<Core::Container::AnsiString> Warnings;
    };

    // スペックを読み、参照する入力を読み、ImportHeightfield → BuildCookedTerrain まで行う。
    [[nodiscard]] bool CookTerrainSpecFile(const std::filesystem::path& specPath, TerrainSpecCookResult& outResult,
                                           Core::Container::AnsiString& outError);
} // namespace NorvesLib::Tools::AssetCook
