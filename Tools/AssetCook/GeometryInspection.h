#pragma once

#include "Container/Span.h"
#include <cstdint>

namespace NorvesLib::Tools::AssetCook
{
    struct InspectionVertex
    {
        float Position[3] = {};
        float Normal[3] = {};
    };
    struct GeometryInspection
    {
        double Minimum[3] = {};
        double Maximum[3] = {};
        double Length[3] = {};
        uint32_t LongestAxis = 0;
        uint32_t VertexCount = 0;
        uint32_t TriangleCount = 0;
        uint32_t WeldedVertexCount = 0;
        uint32_t ConnectedComponentCount = 0;
        uint32_t ZeroNormalCount = 0;
    };
    // 位置の数値完全一致で溶接する（+0/-0は同じ）。接続は三角形の頂点共有で数える。
    // 未参照の溶接頂点も孤立成分として数える。元データは変更しない。
    // 3本のworkspaceは各vertex数以上で、相互・入力・出力に非重複であること。
    // 失敗時outを保持。検証後のworkspaceは作業用に変更するが意味を保証しない。
    [[nodiscard]] bool InspectGeometry(Core::Container::Span<const InspectionVertex> vertices,
        Core::Container::Span<const uint32_t> indices, Core::Container::Span<uint32_t> order,
        Core::Container::Span<uint32_t> representatives, Core::Container::Span<uint32_t> parents,
        GeometryInspection& outInspection) noexcept;
} // namespace NorvesLib::Tools::AssetCook
