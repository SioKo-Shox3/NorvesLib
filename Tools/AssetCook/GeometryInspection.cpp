#include "GeometryInspection.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace NorvesLib::Tools::AssetCook
{
    namespace
    {
        struct ByteRange { uintptr_t Begin; size_t Size; };
        bool Overlaps(ByteRange a, ByteRange b) noexcept
        {
            return a.Size!=0 && b.Size!=0 &&
                (a.Begin<=b.Begin ? b.Begin-a.Begin<a.Size : a.Begin-b.Begin<b.Size);
        }
        uint32_t FindRoot(Core::Container::Span<uint32_t> parents,uint32_t index) noexcept
        {
            while (parents[index]!=index)
            {
                parents[index]=parents[parents[index]];
                index=parents[index];
            }
            return index;
        }
    }
    bool InspectGeometry(Core::Container::Span<const InspectionVertex> vertices,
        Core::Container::Span<const uint32_t> indices, Core::Container::Span<uint32_t> order,
        Core::Container::Span<uint32_t> representatives, Core::Container::Span<uint32_t> parents,
        GeometryInspection& outInspection) noexcept
    {
        const size_t count=vertices.size();
        if (!vertices.data() || count==0 || count>std::numeric_limits<uint32_t>::max() ||
            count>std::numeric_limits<size_t>::max()/sizeof(InspectionVertex) ||
            !indices.data() || indices.empty() || indices.size()%3!=0 ||
            indices.size()/3>std::numeric_limits<uint32_t>::max() ||
            indices.size()>std::numeric_limits<size_t>::max()/sizeof(uint32_t) ||
            !order.data() || !representatives.data() || !parents.data() ||
            order.size()<count || representatives.size()<count || parents.size()<count ||
            order.size()>std::numeric_limits<size_t>::max()/sizeof(uint32_t) ||
            representatives.size()>std::numeric_limits<size_t>::max()/sizeof(uint32_t) ||
            parents.size()>std::numeric_limits<size_t>::max()/sizeof(uint32_t))
        {
            return false;
        }
        const ByteRange ranges[]={
            {reinterpret_cast<uintptr_t>(vertices.data()),count*sizeof(InspectionVertex)},
            {reinterpret_cast<uintptr_t>(indices.data()),indices.size()*sizeof(uint32_t)},
            {reinterpret_cast<uintptr_t>(order.data()),order.size()*sizeof(uint32_t)},
            {reinterpret_cast<uintptr_t>(representatives.data()),representatives.size()*sizeof(uint32_t)},
            {reinterpret_cast<uintptr_t>(parents.data()),parents.size()*sizeof(uint32_t)},
            {reinterpret_cast<uintptr_t>(&outInspection),sizeof(outInspection)}};
        for (size_t a=0;a<6;++a)
        {
            for (size_t b=a+1;b<6;++b)
            {
                if (Overlaps(ranges[a],ranges[b]))
                {
                    return false;
                }
            }
        }
        GeometryInspection result;
        result.VertexCount=static_cast<uint32_t>(count);
        result.TriangleCount=static_cast<uint32_t>(indices.size()/3);
        for (size_t axis=0;axis<3;++axis)
        {
            result.Minimum[axis]=result.Maximum[axis]=vertices[0].Position[axis];
        }
        for (const auto& vertex : vertices)
        {
            bool bZero=true;
            for (size_t axis=0;axis<3;++axis)
            {
                if (!std::isfinite(vertex.Position[axis]) || !std::isfinite(vertex.Normal[axis]))
                {
                    return false;
                }
                result.Minimum[axis]=std::min(result.Minimum[axis],static_cast<double>(vertex.Position[axis]));
                result.Maximum[axis]=std::max(result.Maximum[axis],static_cast<double>(vertex.Position[axis]));
                bZero=bZero && vertex.Normal[axis]==0;
            }
            result.ZeroNormalCount+=bZero ? 1u : 0u;
        }
        for (const uint32_t index : indices)
        {
            if (index>=count)
            {
                return false;
            }
        }
        for (size_t axis=0;axis<3;++axis)
        {
            result.Length[axis]=result.Maximum[axis]-result.Minimum[axis];
            if (result.Length[axis]>result.Length[result.LongestAxis])
            {
                result.LongestAxis=static_cast<uint32_t>(axis);
            }
        }
        for (size_t index=0;index<count;++index)
        {
            order[index]=static_cast<uint32_t>(index);
        }
        std::sort(order.begin(),order.begin()+count,[&](uint32_t a,uint32_t b)
        {
            for (size_t axis=0;axis<3;++axis)
            {
                if (vertices[a].Position[axis]!=vertices[b].Position[axis])
                {
                    return vertices[a].Position[axis]<vertices[b].Position[axis];
                }
            }
            return a<b;
        });
        for (size_t sorted=0;sorted<count;++sorted)
        {
            bool bNew=sorted==0;
            if (!bNew)
            {
                for (size_t axis=0;axis<3;++axis)
                {
                    bNew=bNew || vertices[order[sorted]].Position[axis]!=vertices[order[sorted-1]].Position[axis];
                }
            }
            result.WeldedVertexCount+=bNew ? 1u : 0u;
            representatives[order[sorted]]=result.WeldedVertexCount-1;
        }
        // sort用storageをunion-by-rankへ再利用する。
        for (uint32_t index=0;index<result.WeldedVertexCount;++index)
        {
            parents[index]=index;
            order[index]=0;
        }
        result.ConnectedComponentCount=result.WeldedVertexCount;
        for (size_t triangle=0;triangle<indices.size();triangle+=3)
        {
            for (size_t edge=1;edge<3;++edge)
            {
                uint32_t a=FindRoot(parents,representatives[indices[triangle]]);
                uint32_t b=FindRoot(parents,representatives[indices[triangle+edge]]);
                if (a==b)
                {
                    continue;
                }
                if (order[a]<order[b])
                {
                    std::swap(a,b);
                }
                parents[b]=a;
                if (order[a]==order[b])
                {
                    ++order[a];
                }
                --result.ConnectedComponentCount;
            }
        }
        outInspection=result;
        return true;
    }
    bool InspectGeometryClosure(Core::Container::Span<const InspectionVertex> vertices,
                                Core::Container::Span<const uint32_t> indices, Core::Container::Span<uint32_t> order,
                                Core::Container::Span<uint32_t> representatives,
                                Core::Container::Span<uint32_t> parents,
                                Core::Container::Span<GeometryClosureEdge> edges, const GeometryClosurePolicy& policy,
                                GeometryClosureInspection& outInspection) noexcept
    {
        if (!std::isfinite(policy.MaximumBoundaryFraction) || policy.MaximumBoundaryFraction < 0 ||
            policy.MaximumBoundaryFraction > 1 || !edges.data() || edges.size() < indices.size() ||
            edges.size() > std::numeric_limits<size_t>::max() / sizeof(GeometryClosureEdge) ||
            vertices.size() > std::numeric_limits<size_t>::max() / sizeof(InspectionVertex) ||
            indices.size() > std::numeric_limits<size_t>::max() / sizeof(uint32_t) ||
            order.size() > std::numeric_limits<size_t>::max() / sizeof(uint32_t) ||
            representatives.size() > std::numeric_limits<size_t>::max() / sizeof(uint32_t) ||
            parents.size() > std::numeric_limits<size_t>::max() / sizeof(uint32_t))
        {
            return false;
        }
        const ByteRange ranges[] = {
            {reinterpret_cast<uintptr_t>(vertices.data()), vertices.size() * sizeof(InspectionVertex)},
            {reinterpret_cast<uintptr_t>(indices.data()), indices.size() * sizeof(uint32_t)},
            {reinterpret_cast<uintptr_t>(order.data()), order.size() * sizeof(uint32_t)},
            {reinterpret_cast<uintptr_t>(representatives.data()), representatives.size() * sizeof(uint32_t)},
            {reinterpret_cast<uintptr_t>(parents.data()), parents.size() * sizeof(uint32_t)},
            {reinterpret_cast<uintptr_t>(edges.data()), edges.size() * sizeof(GeometryClosureEdge)},
            {reinterpret_cast<uintptr_t>(&outInspection), sizeof(outInspection)},
            {reinterpret_cast<uintptr_t>(&policy), sizeof(policy)}};
        for (size_t a = 0; a < 8; ++a)
        {
            for (size_t b = a + 1; b < 8; ++b)
            {
                if (Overlaps(ranges[a], ranges[b]))
                {
                    return false;
                }
            }
        }
        GeometryInspection welded;
        if (!InspectGeometry(vertices, indices, order, representatives, parents, welded))
        {
            return false;
        }
        GeometryClosureInspection result;
        size_t count = 0;
        for (size_t i = 0; i < indices.size(); i += 3)
        {
            const uint32_t ids[] = {representatives[indices[i]], representatives[indices[i + 1]],
                                    representatives[indices[i + 2]]};
            if (ids[0] == ids[1] || ids[1] == ids[2] || ids[2] == ids[0])
            {
                ++result.WeldedDegenerateTriangles;
                continue;
            }
            for (size_t j = 0; j < 3; ++j)
            {
                const auto a = ids[j], b = ids[(j + 1) % 3];
                edges[count++] = {std::min(a, b), std::max(a, b), a < b ? 1u : 0u};
            }
        }
        std::sort(edges.begin(), edges.begin() + count,
                  [](const auto& a, const auto& b)
                  {
                      return a.A < b.A || (a.A == b.A && a.B < b.B);
                  });
        for (size_t start = 0; start < count;)
        {
            size_t end = start + 1;
            while (end < count && edges[end].A == edges[start].A && edges[end].B == edges[start].B)
            {
                ++end;
            }
            ++result.UniqueEdges;
            if (end - start == 1)
            {
                ++result.BoundaryEdges;
            }
            else if (end - start != 2)
            {
                ++result.NonManifoldEdges;
            }
            else if (edges[start].Forward == edges[start + 1].Forward)
            {
                ++result.SameDirectionPairs;
            }
            start = end;
        }
        result.BoundaryFraction =
            result.UniqueEdges ? static_cast<double>(result.BoundaryEdges) / result.UniqueEdges : 1;
        result.bAlmostClosed = result.UniqueEdges != 0 && result.NonManifoldEdges == 0 &&
                               result.SameDirectionPairs == 0 && result.WeldedDegenerateTriangles == 0 &&
                               result.BoundaryFraction <= policy.MaximumBoundaryFraction;
        outInspection = result;
        return true;
    }
} // namespace NorvesLib::Tools::AssetCook
