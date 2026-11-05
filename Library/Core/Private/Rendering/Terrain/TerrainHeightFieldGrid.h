#pragma once

#include "Asset/AssetBlob.h"
#include "Container/VariableArray.h"
#include "Rendering/Terrain/TerrainHeightField.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace NorvesLib::Core::Rendering::Terrain
{
    /// 格子の高さ場の中身。頂点 (column, row) はエンジンの (FirstX + column × Spacing, FirstZ + row × Spacing)。
    /// 行 0 が北端（-Z の端）で、列 0 が西端（-X の端）
    struct TerrainHeightField::GridData
    {
        /// float32 の little-endian、行優先、見出しなし（Columns × Rows × 4 バイト）
        Asset::AssetBlob Bytes;
        uint32_t Columns = 0;
        uint32_t Rows = 0;
        double FirstX = 0.0;
        double FirstZ = 0.0;
        double Spacing = 1.0;
        uint32_t SplatLayerCount = 0;
        /// 頂点ごとに SplatLayerCount バイト、行優先
        Container::VariableArray<uint8_t> SplatWeights;
        Container::VariableArray<Container::AnsiString> SplatLayerNames;
        uint32_t TileCells = 0;
        uint32_t TileCountX = 0;
        uint32_t TileCountZ = 0;
        /// 造成の後の頂点の高さ（造成の範囲を囲む頂点の矩形だけ。行優先）。造成が無ければ空
        Container::VariableArray<float> GradedHeights;
        uint32_t GradedFirstColumn = 0;
        uint32_t GradedFirstRow = 0;
        uint32_t GradedColumns = 0;
        uint32_t GradedRows = 0;

        /// 8 × 8 升目ごとの高さの範囲（行優先。外周のブロックは残りの升目だけを覆う）
        static constexpr uint32_t kBlockCells = 8u;
        uint32_t BlockColumns = 0;
        uint32_t BlockRows = 0;
        Container::VariableArray<float> BlockMinHeight;
        Container::VariableArray<float> BlockMaxHeight;

        /// ファイルの値
        float FileAt(uint32_t column, uint32_t row) const
        {
            float value = 0.0f;
            std::memcpy(&value, Bytes.GetData() + (static_cast<size_t>(row) * Columns + column) * sizeof(float),
                        sizeof(float));
            return value;
        }

        /// 頂点の高さ（造成の範囲は造成の後の値）
        float At(uint32_t column, uint32_t row) const
        {
            // 符号なしの引き算なので、矩形の左・上の外は大きな値になって外と判定される
            const uint32_t localColumn = column - GradedFirstColumn;
            const uint32_t localRow = row - GradedFirstRow;
            if (localColumn < GradedColumns && localRow < GradedRows)
            {
                return GradedHeights[static_cast<size_t>(localRow) * GradedColumns + localColumn];
            }
            float value = 0.0f;
            std::memcpy(&value, Bytes.GetData() + (static_cast<size_t>(row) * Columns + column) * sizeof(float),
                        sizeof(float));
            return value;
        }

        /// 造成後の頂点から、各ブロックに含まれる頂点の最小・最大の高さを作る
        void BuildBlockBounds();
    };
} // namespace NorvesLib::Core::Rendering::Terrain
