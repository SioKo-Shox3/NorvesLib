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

    struct TerrainPuddleRule
    {
        double BlurSigmaM = 4.0;      // 高さの分離ガウスぼかしの σ [m]。0 ならぼかしとの差は 0。
        double BlurFullM = 0.25;      // ぼかした高さと元の高さの差がこの深さ [m] で項が 1。
        double BlurWeight = 0.4;      // 局所の凹みの重み [無次元]。
        double FillFullM = 0.15;      // 窪地を埋めた高さと元の高さの差がこの深さ [m] で項が 1。
        double FillWeight = 0.4;      // 窪地の深さの重み [無次元]。
        double FlowWeight = 0.2;      // 流れの集積の重み [無次元]。
        double FlowRefCells = 2000.0; // log1p(集積)/log1p(この標本数) で項が 1。
        double SlopeStartDeg = 2.0;   // 凹み度を下げ始める傾斜 [度]。
        double SlopeZeroDeg = 6.0;    // 凹み度が 0 になる傾斜 [度]。
        TerrainNoiseRule Noise{0.0, 8.0}; // 乗算雑音の振幅 [無次元] と格子間隔 [m]。
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

    enum class TerrainPaintTarget : uint8_t { Wetness, DryGrass, Road, Rock, GrassDensity, Splat, Puddle };
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
        TerrainPuddleRule Puddle;
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
        Core::Container::VariableArray<uint8_t> PuddleR8; // 1 バイト/標本。空なら PDL0 を書かない（層の無い古い形）。
    };

    // paints は recipe.Paints と同じ数・順。結果はスレッド数に依らない。
    // threadCount が 0 ならハードウェアの並列数を使う。
    [[nodiscard]] bool ComputeTerrainMasks(const TerrainHeightGrid& grid, double cellSizeM, uint32_t seed,
                                           const TerrainRecipe& recipe, Core::Container::Span<const TerrainPaintImage> paints,
                                           TerrainMaskFields& outFields, Core::Container::AnsiString& outError, uint32_t threadCount = 0);
} // namespace NorvesLib::Tools::AssetCook
