// 手で置く配置の一覧を検査し、地形と同じ格子の NVSCT にまとめる。
#pragma once

#include "ScatterSpec.h"

namespace NorvesLib::Tools::AssetCook
{
    enum class ScatterPlacementListError
    {
        Json,
        InvalidSpecies,
        UnknownSpecies,
        NonFinite,
        ScaleOutOfRange,
        TintOutOfRange,
        InvalidSeed,
        InvalidCollision,
        InvalidChunkSize,
        OutOfGrid,
        TerrainMismatch,
        Quantization,
        TooManyInstances,
    };

    [[nodiscard]] const char* GetScatterPlacementListErrorName(ScatterPlacementListError error);

    // double を越える数値も構文解析し、値の検査で適切な種別に分ける。
    [[nodiscard]] bool ParseScatterPlacementListDocument(Core::Container::Span<const uint8_t> bytes,
                                                        Core::JsonDocument& document,
                                                        Core::Container::AnsiString& nonFiniteMarker,
                                                        bool& bPlacement,
                                                        Core::Container::AnsiString& error);
    [[nodiscard]] bool CookScatterPlacementList(const std::filesystem::path& path,
                                               Core::Container::Span<const uint8_t> bytes, Core::JsonValue root,
                                               const Core::Container::AnsiString& nonFiniteMarker,
                                               ScatterSpecCookResult& out, Core::Container::AnsiString& error);
} // namespace NorvesLib::Tools::AssetCook
