#pragma once

#include "Container/VariableArray.h"
#include <cstdint>

namespace NorvesLib::Core::AssetImport
{
    // NVSKELの上限（Skeletal::MaximumSkinLodCount）と同じ値。
    inline constexpr uint32_t MaximumSkinnedLodTargets = 8;
    // NVSKELの上限（Skeletal::MaximumSkinPartCount）と同じ値。
    inline constexpr uint32_t MaximumSkinnedLodParts = 256;

    struct SkinnedLodImportSettings
    {
        bool bRequested = false; // 節があればtrue。空objectでも既定値で頼む。
        uint32_t MinParts = 16; // 部位の数の下限（仮）。
        uint32_t MaxParts = 64; // 部位の数の上限（仮）。
        // LODごとの三角形数のLOD0に対する比（仮）。
        Container::VariableArray<double> Targets{1.0, 0.4, 0.12, 0.03};
        double ThetaRef = 0.5; // スキンの変位の誤差に使う基準の回転角（rad、仮）。
    };
} // namespace NorvesLib::Core::AssetImport
