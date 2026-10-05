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
    // 成功時representatives[元vertex]は位置完全一致のdense weld ID。order/parentsは意味を保証しない。
    // 失敗時outを保持。workspaceは検証後に変更され得る。
    [[nodiscard]] bool InspectGeometry(Core::Container::Span<const InspectionVertex> vertices,
        Core::Container::Span<const uint32_t> indices, Core::Container::Span<uint32_t> order,
        Core::Container::Span<uint32_t> representatives, Core::Container::Span<uint32_t> parents,
        GeometryInspection& outInspection) noexcept;
    struct GeometryClosurePolicy
    {
        double MaximumBoundaryFraction = 0.001;
    };
    struct GeometryClosureEdge
    {
        uint32_t A = 0, B = 0, Forward = 0;
    };
    struct GeometryClosureInspection
    {
        uint64_t UniqueEdges = 0, BoundaryEdges = 0, NonManifoldEdges = 0, SameDirectionPairs = 0;
        uint64_t WeldedDegenerateTriangles = 0;
        double BoundaryFraction = 1;
        bool bAlmostClosed = false;
    };
    inline constexpr uint32_t GeometryClosureAlgorithmRevision = 1;
    // Auto用の暫定方針。位置は完全一致だけを溶接し、非多様体/向き不整合/溶接後の退化は閉鎖扱いしない。
    // edgesはindices数以上の独立workspace。全workspaceは入力/outと非重複。成功時だけoutを更新する。
    [[nodiscard]] bool InspectGeometryClosure(
        Core::Container::Span<const InspectionVertex> vertices, Core::Container::Span<const uint32_t> indices,
        Core::Container::Span<uint32_t> order, Core::Container::Span<uint32_t> representatives,
        Core::Container::Span<uint32_t> parents, Core::Container::Span<GeometryClosureEdge> edges,
        const GeometryClosurePolicy& policy, GeometryClosureInspection& outInspection) noexcept;
} // namespace NorvesLib::Tools::AssetCook
