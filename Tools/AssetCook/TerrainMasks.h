// 地形の全体格子から各種マスクを決定的に生成する API。
#pragma once

#include "HeightfieldImport.h"
#include "Asset/AssetPackageFormat.h"

#include <cstdint>
#include <filesystem>

namespace NorvesLib::Tools::AssetCook
{
    inline constexpr Core::Asset::AssetPackageFourCC TerrainMaskLayerFourCC = Core::Asset::MakeAssetPackageFourCC('M', 'S', 'K', '0');
    inline constexpr Core::Asset::AssetPackageFourCC TerrainSplat0LayerFourCC = Core::Asset::MakeAssetPackageFourCC('S', 'P', 'L', '0');
    inline constexpr Core::Asset::AssetPackageFourCC TerrainSplat1LayerFourCC = Core::Asset::MakeAssetPackageFourCC('S', 'P', 'L', '1');
    inline constexpr Core::Asset::AssetPackageFourCC TerrainGrassLayerFourCC = Core::Asset::MakeAssetPackageFourCC('G', 'R', 'S', '0');
    inline constexpr const char* TerrainMaskSemantic = "terrain.mask.r=wetness.g=dry_grass.b=road.a=rock";
    inline constexpr const char* TerrainSplatSemanticPrefix = "terrain.splat:";
    inline constexpr const char* TerrainGrassSemantic = "terrain.grass.density";

    struct TerrainBandRule
    {
        bool bEnabled = false;
        double Min = 0.0;
        double Max = 0.0;
        double Soft = 0.0;
    };

    struct TerrainNoiseRule
    {
        double Amplitude = 0.0;
        double ScaleM = 16.0;
    };

    struct TerrainWetnessRule
    {
        double FlowWeight = 0.5;
        double FlowRefCells = 2000.0;
        double LowWeight = 0.25;
        double LowFullM = 0.0;
        double LowZeroM = 8.0;
        double ConcaveWeight = 0.25;
        double ConcaveFullPerM = 0.05;
    };

    struct TerrainDryGrassRule
    {
        double Base = 0.3;
        TerrainNoiseRule Noise;
    };

    struct TerrainRockRule
    {
        double SlopeStartDeg = 35.0;
        double SlopeFullDeg = 55.0;
    };

    struct TerrainSplatLayerRule
    {
        Core::Container::AnsiString Name;
        double Weight = 1.0;
        TerrainBandRule SlopeDeg;
        TerrainBandRule HeightM;
        TerrainBandRule Wetness;
        TerrainNoiseRule Noise;
    };

    struct TerrainGrassDensityRule
    {
        double Base = 1.0;
        double SlopeStartDeg = 25.0;
        double SlopeZeroDeg = 40.0;
        double WetStart = 0.8;
        double WetZero = 1.0;
        double RoadSuppress = 1.0;
        double RockSuppress = 1.0;
    };

    enum class TerrainPaintTarget : uint8_t { Wetness, DryGrass, Road, Rock, GrassDensity, Splat };
    enum class TerrainPaintMode : uint8_t { Override, Multiply };

    struct TerrainPaintRule
    {
        TerrainPaintTarget Target = TerrainPaintTarget::Road;
        uint32_t SplatLayerIndex = 0;
        TerrainPaintMode Mode = TerrainPaintMode::Override;
        std::filesystem::path Path;
    };

    struct TerrainRecipe
    {
        TerrainWetnessRule Wetness;
        TerrainDryGrassRule DryGrass;
        TerrainRockRule Rock;
        Core::Container::VariableArray<TerrainSplatLayerRule> SplatLayers;
        TerrainGrassDensityRule GrassDensity;
        Core::Container::VariableArray<TerrainPaintRule> Paints;
    };

    // index = z * Width + x。flip_z 適用済み。
    struct TerrainPaintImage
    {
        uint32_t Width = 0;
        uint32_t Height = 0;
        Core::Container::VariableArray<uint8_t> Values;
        Core::Container::VariableArray<uint8_t> Alpha;
    };

    // 8bit PNG を値とアルファへ展開する。flipZ なら z 行を反転する。
    [[nodiscard]] bool DecodeTerrainPaintPng(Core::Container::Span<const uint8_t> pngBytes, bool flipZ,
                                             TerrainPaintImage& out, Core::Container::AnsiString& outError);

    // 全体格子の大きさは Width×Height、index = z * Width + x。
    struct TerrainMaskFields
    {
        uint32_t Width = 0;
        uint32_t Height = 0;
        Core::Container::VariableArray<uint8_t> MaskRgba;   // 4 バイト/標本。
        Core::Container::VariableArray<uint8_t> Splat0Rgba; // 空なら SPL0 を書かない。
        Core::Container::VariableArray<uint8_t> Splat1Rgba; // 空なら SPL1 を書かない。
        Core::Container::VariableArray<uint8_t> GrassR8;    // 1 バイト/標本。
        Core::Container::VariableArray<Core::Container::AnsiString> SplatLayerNames;
    };

    // paints は recipe.Paints と同じ数・順。
    [[nodiscard]] bool ComputeTerrainMasks(const TerrainHeightGrid& grid, double cellSizeM, uint32_t seed,
                                           const TerrainRecipe& recipe, Core::Container::Span<const TerrainPaintImage> paints,
                                           TerrainMaskFields& outFields, Core::Container::AnsiString& outError);
} // namespace NorvesLib::Tools::AssetCook
