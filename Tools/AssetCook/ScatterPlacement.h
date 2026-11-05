// 地形上の散布を、種とセル座標だけから決定的に求める。
#pragma once

#include "Asset/CookedTerrainFormat.h"
#include "Asset/CookedScatterFormat.h"
#include "Asset/CookedTextureFormat.h"
#include "Container/String.h"
#include "Container/VariableArray.h"

#include <cstdint>

namespace NorvesLib::Tools::AssetCook
{
    struct ScatterDensitySource
    {
        bool bUseLayer = false;
        double Constant = 1.0;
        Core::Asset::AssetPackageFourCC FourCC = 0;
        uint8_t Channel = 0;
    };

    struct ScatterSpeciesRule
    {
        Core::Container::AnsiString Name;
        Core::Asset::ScatterRenderPath RenderPath = Core::Asset::ScatterRenderPath::Mega;
        Core::Container::VariableArray<Core::Container::AnsiString> LodMeshLogicalPaths;
        double MeshRadiusM = 0.5;
        double MinSpacingM = 0.0;
        ScatterDensitySource DensitySource;
        double BaseDensityPerM2 = 1.0;
        bool bHeightBandEnabled = false;
        double HeightMinM = 0.0;
        double HeightMaxM = 0.0;
        bool bSlopeBandEnabled = false;
        double SlopeMinDeg = 0.0;
        double SlopeMaxDeg = 90.0;
        bool bExcludeRoad = false;
        double RoadThreshold = 0.5;
        bool bWetnessBandEnabled = false;
        double WetnessMin = 0.0;
        double WetnessMax = 1.0;
        double ScaleMin = 1.0;
        double ScaleMax = 1.0;
        double SlopeFollow = 1.0;
        double BrightnessVariation = 0.0;
        bool bUseDryGrass = false;
        bool bCollision = false;
        double WindStiffness = 1.0;
        uint64_t WindPhaseSeed = 0;
    };

    struct ScatterPlacementParams
    {
        uint64_t Seed = 0;
        double ChunkSizeM = 64.0;
        double OriginX = 0.0;
        double OriginZ = 0.0;
    };

    struct ScatterCandidate
    {
        double X = 0.0;
        double Y = 0.0;
        double Z = 0.0;
        float Normal[3] = {0.0f, 1.0f, 0.0f};
        double Yaw = 0.0;
        double Scale = 1.0;
        uint8_t Rank = 0;
        uint64_t SortKey = 0;
        uint8_t Tint[3] = {128, 128, 128};
        uint8_t Dry = 0;
        int64_t CellX = 0;
        int64_t CellZ = 0;
    };

    struct ScatterPlacementStats
    {
        // チャンクと地形に重なるセル数。受理・間引きはチャンク所有の候補だけを数える。
        uint64_t ExaminedCells = 0;
        uint64_t AcceptedCount = 0;
        uint64_t ThinnedCount = 0;
        // 近傍の受理判定も含め、密度と最小間隔の積が飽和域に入ったか。
        bool bDensitySaturated = false;
    };

    class ScatterTerrainSampler
    {
    public:
        // 地形の値と層の解析結果を所有し、初期化後の標本化を読み取りだけにする。
        [[nodiscard]] bool Initialize(const Core::Asset::CookedTerrainData& terrain,
                                      Core::Container::AnsiString& error);
        // 全体格子の対角線を使い、メッシュと同じ世界座標の三角形上の高さを返す。
        [[nodiscard]] bool SampleHeight(double x, double z, double& y) const;
        // SampleHeight と同じ三角形の上向きの単位面法線を返す。
        [[nodiscard]] bool SampleNormal(double x, double z, float (&normal)[3]) const;
        // セル角の標本を双一次補間し、正規化したチャンネル値を返す。
        [[nodiscard]] bool SampleLayerChannel(Core::Asset::AssetPackageFourCC fourCC, uint8_t channel,
                                              double x, double z, float& value) const;
        // 配置を始める前に、必要な層とチャンネルの存在を検査する。
        [[nodiscard]] bool HasLayerChannel(Core::Asset::AssetPackageFourCC fourCC, uint8_t channel) const;
        // 未初期化の標本化を失敗として扱う。
        [[nodiscard]] bool IsInitialized() const;
        // 地形の世界座標の外周を返す。標本化は外周を含む。
        [[nodiscard]] double GetMinX() const;
        [[nodiscard]] double GetMaxX() const;
        [[nodiscard]] double GetMinZ() const;
        [[nodiscard]] double GetMaxZ() const;

    private:
        struct Layer
        {
            Core::Asset::AssetPackageFourCC FourCC = 0;
            uint8_t Channels = 0;
            Core::Container::VariableArray<Core::Asset::CookedTextureData> Textures;
        };

        [[nodiscard]] bool Locate(double x, double z, uint32_t& tileIndex,
                                  uint32_t& column, uint32_t& row, double& fu, double& fv) const;
        [[nodiscard]] bool SampleSurface(double x, double z, double& y, float (&normal)[3]) const;

        Core::Asset::CookedTerrainData m_Terrain;
        Core::Container::VariableArray<Layer> m_Layers;
        bool m_bInitialized = false;
    };

    // 明示値が 0 のときだけ、最大の拡大を含めたメッシュの直径を間隔にする。
    [[nodiscard]] double ResolveScatterMinSpacing(const ScatterSpeciesRule& rule);

    // 受理と局所最大は seed・種名・セルだけで決まり、チャンクは所有範囲の切り出しだけを担う。
    // 失敗時は out と stats を空にし、成功時は error を空にする。
    [[nodiscard]] bool PlaceScatterChunk(const ScatterPlacementParams& params, const ScatterSpeciesRule& rule,
                                         const ScatterTerrainSampler& terrain, int32_t chunkX, int32_t chunkZ,
                                         Core::Container::VariableArray<ScatterCandidate>& out,
                                         ScatterPlacementStats* stats, Core::Container::AnsiString& error);
} // namespace NorvesLib::Tools::AssetCook
