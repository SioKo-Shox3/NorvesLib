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
        DegenerateFit, DegenerateSurface, Unrepresentable, UnsupportedOrigin
    };
    // 骨格v0で適用するのはscale/fitだけ。軸・鏡像・原点・UV・巻きは別対応まで拒否する。
    [[nodiscard]] bool SupportsSkeletalScaleImport(const ImportSettings& settings) noexcept;

    struct UniformImportScale
    {
        TransformResult Result = TransformResult::Unrepresentable;
        double Value = 1.0;
    };
    // 既に軸を揃えたboundsから正の一様倍率を解決する。骨格と静的meshで同じfit式を使う。
    [[nodiscard]] UniformImportScale ResolveUniformImportScale(const ImportSettings& settings,
        const double (&minimum)[3], const double (&maximum)[3]) noexcept;
    // 有限なfloat成分を倍率変換する。overflow/完全underflowは拒否し、失敗時outを保持する。
    [[nodiscard]] bool TryScaleImportValue(float value, double scale, float& outValue) noexcept;

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
