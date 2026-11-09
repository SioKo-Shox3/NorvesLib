// 散布の候補を、チャンクごとに独立した NVSCT v0 へ焼く。
#pragma once

#include "ScatterPlacement.h"

namespace NorvesLib::Tools::AssetCook
{
    struct ScatterChunkCoord
    {
        int32_t ChunkX = 0;
        int32_t ChunkZ = 0;
    };

    struct ScatterBakeDesc
    {
        ScatterPlacementParams Params;
        Core::Container::VariableArray<ScatterSpeciesRule> Species;
        uint64_t TerrainCookedHash = 0;
    };

    struct ScatterBakeSpeciesStats
    {
        uint64_t InstanceCount = 0;
        uint64_t ExaminedCells = 0;
        uint64_t ThinnedCount = 0;
        bool bDensitySaturated = false;
    };

    struct ScatterBakeStats
    {
        // 入力の種と同じ順。選択した全チャンクの合計。
        Core::Container::VariableArray<ScatterBakeSpeciesStats> Species;
    };

    // onlyChunks が空なら全チャンク。重複した座標は一度だけ焼く。
    // 失敗時は outBytes と stats を空にし、成功時は error を空にする。
    [[nodiscard]] bool BakeScatter(const ScatterBakeDesc& desc, const Core::Asset::CookedTerrainData& terrain,
                                  Core::Container::Span<const ScatterChunkCoord> onlyChunks,
                                  Core::Container::VariableArray<uint8_t>& outBytes,
                                  ScatterBakeStats* stats, Core::Container::AnsiString& error);
} // namespace NorvesLib::Tools::AssetCook
