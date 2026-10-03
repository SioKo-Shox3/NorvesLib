#pragma once

#include "Resource/ImportSettings.h"
#include "Container/Span.h"

namespace NorvesLib::Core::AssetImport
{
    struct ImportVertexLayout
    {
        size_t Stride = 32;
        size_t PositionOffset = 0;
        size_t NormalOffset = 12;
        size_t TexCoordOffset = 24;
    };
    enum class TransformResult : uint8_t
    {
        Success, InvalidSettings, InvalidLayout, InvalidIndices, InvalidVertex,
        DegenerateFit, DegenerateSurface, Unrepresentable
    };
    struct ImportTransformOutcome
    {
        TransformResult Result = TransformResult::InvalidLayout;
        double AppliedScale = 1.0;
        // 軸/鏡像変換後、scale前の原点。customはPivot=0で設定offsetを加算する。
        double Pivot[3] = {};
        bool bFlippedWinding = false;
    };
    // interleaved頂点のfloat3位置/float3法線/float2 UVだけを書き換え、他field/paddingを保持する。
    // 頂点とindexの全spanは非交差で、呼出中は他threadから変更しないこと。
    // 全頂点の結果を検証してから書き込むため、失敗時は両spanを変更しない。追加確保なし。
    [[nodiscard]] ImportTransformOutcome ApplyImportTransform(Container::Span<uint8_t> vertices,
        size_t vertexCount, ImportVertexLayout layout, Container::Span<uint32_t> indices,
        const ImportSettings& settings) noexcept;
} // namespace NorvesLib::Core::AssetImport
