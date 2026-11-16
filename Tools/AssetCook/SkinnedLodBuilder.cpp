// スキンメッシュの LOD 簡略化に必要な前処理と部位分割を行う。
// GR80 の修復と G11 の影専用メッシュでも利用できる入力と出力を持つ。
#include "SkinnedLodBuilder.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <tuple>
#include <utility>

namespace NorvesLib::Tools::AssetCook
{
    namespace C = Core::Container;
    namespace S = Core::Skeletal;

    namespace
    {
        constexpr uint32_t Invalid = UINT32_MAX;

        struct PositionKey
        {
            uint32_t X, Y, Z, Vertex;
        };

        uint32_t PositionBits(float value)
        {
            return value == 0.0f ? 0 : std::bit_cast<uint32_t>(value);
        }

        PositionKey MakePositionKey(const S::SkeletalVertex& vertex, uint32_t index)
        {
            return {PositionBits(vertex.Position.X), PositionBits(vertex.Position.Y),
                    PositionBits(vertex.Position.Z), index};
        }

        bool SamePosition(const PositionKey& a, const PositionKey& b)
        {
            return a.X == b.X && a.Y == b.Y && a.Z == b.Z;
        }

        SkinnedLodSubMeshRange InputRange(const SkinnedLodInput& input, size_t index)
        {
            return input.SubMeshes.empty()
                ? SkinnedLodSubMeshRange{0, static_cast<uint32_t>(input.Indices.size())}
                : input.SubMeshes[index];
        }

        bool ValidateInput(const SkinnedLodInput& input)
        {
            // 番兵と出力の 32 ビット番号を確保する。
            if (input.Vertices.empty() || input.Vertices.data() == nullptr || input.Indices.empty() ||
                input.Indices.data() == nullptr || (!input.SubMeshes.empty() && input.SubMeshes.data() == nullptr) ||
                input.JointBindPositions.data() == nullptr || input.Indices.size() % 3 != 0 ||
                input.Vertices.size() >= Invalid || input.Indices.size() >= Invalid ||
                input.SubMeshes.size() >= Invalid || input.JointCount == 0 ||
                uint64_t(input.JointBindPositions.size()) != uint64_t(input.JointCount) * 3)
            {
                return false;
            }

            size_t expected = 0;
            for (const auto& range : input.SubMeshes)
            {
                if (range.IndexStart != expected || range.IndexCount % 3 != 0 ||
                    size_t(range.IndexCount) > input.Indices.size() - expected)
                {
                    return false;
                }
                expected += range.IndexCount;
            }
            if (!input.SubMeshes.empty() && expected != input.Indices.size())
            {
                return false;
            }

            for (const auto& vertex : input.Vertices)
            {
                if (!std::isfinite(vertex.Position.X) || !std::isfinite(vertex.Position.Y) ||
                    !std::isfinite(vertex.Position.Z) || !std::isfinite(vertex.Normal.X) ||
                    !std::isfinite(vertex.Normal.Y) || !std::isfinite(vertex.Normal.Z) ||
                    !std::isfinite(vertex.TexCoord.U) || !std::isfinite(vertex.TexCoord.V))
                {
                    return false;
                }
                for (size_t slot = 0; slot < 4; ++slot)
                {
                    if (vertex.JointIndices[slot] >= input.JointCount ||
                        !std::isfinite(vertex.JointWeights[slot]) || vertex.JointWeights[slot] < 0.0f)
                    {
                        return false;
                    }
                }
            }
            for (uint32_t index : input.Indices)
            {
                if (index >= input.Vertices.size())
                {
                    return false;
                }
            }
            return true;
        }

        bool ValidatePreprocessed(const SkinnedLodInput& input, const SkinnedLodPreprocessed& preprocessed)
        {
            const size_t vertexCount = input.Vertices.size();
            const size_t subMeshCount = input.SubMeshes.empty() ? 1 : input.SubMeshes.size();
            if (preprocessed.Weights.size() != vertexCount * 4 ||
                preprocessed.WeldedVertex.size() != vertexCount || preprocessed.Indices.empty() ||
                preprocessed.Indices.size() > input.Indices.size() || preprocessed.Indices.size() % 3 != 0 ||
                preprocessed.SubMeshes.size() != subMeshCount)
            {
                return false;
            }
            size_t expected = 0;
            for (const auto& range : preprocessed.SubMeshes)
            {
                if (range.IndexStart != expected || range.IndexCount == 0 || range.IndexCount % 3 != 0 ||
                    size_t(range.IndexCount) > preprocessed.Indices.size() - expected)
                {
                    return false;
                }
                expected += range.IndexCount;
            }
            if (expected != preprocessed.Indices.size())
            {
                return false;
            }
            for (size_t vertex = 0; vertex < vertexCount; ++vertex)
            {
                const uint32_t representative = preprocessed.WeldedVertex[vertex];
                if (representative >= vertexCount || preprocessed.WeldedVertex[representative] != representative ||
                    !SamePosition(MakePositionKey(input.Vertices[vertex], 0),
                                  MakePositionKey(input.Vertices[representative], 0)))
                {
                    return false;
                }
                for (size_t slot = 0; slot < 4; ++slot)
                {
                    const float weight = preprocessed.Weights[vertex * 4 + slot];
                    if (!std::isfinite(weight) || weight < 0.0f)
                    {
                        return false;
                    }
                }
            }
            for (uint32_t index : preprocessed.Indices)
            {
                if (index >= vertexCount)
                {
                    return false;
                }
            }
            return true;
        }

        bool ExactCrossComponentIsZero(float ax, float ay, float bx, float by, float cx, float cy)
        {
            // float 同士の積は double に正確に収まる。加算誤差も別の要素に保持して判定する。
            const double terms[6] = {
                double(bx) * cy, -double(bx) * ay, -double(ax) * cy,
                -double(by) * cx, double(by) * ax, double(ay) * cx};
            double expansion[6]{};
            size_t length = 0;
            for (double term : terms)
            {
                double next[6]{};
                size_t nextLength = 0;
                double high = term;
                for (size_t i = 0; i < length; ++i)
                {
                    const double sum = high + expansion[i];
                    const double virtualRight = sum - high;
                    const double low = (high - (sum - virtualRight)) + (expansion[i] - virtualRight);
                    if (low != 0.0)
                    {
                        next[nextLength++] = low;
                    }
                    high = sum;
                }
                if (high != 0.0)
                {
                    next[nextLength++] = high;
                }
                length = nextLength;
                for (size_t i = 0; i < length; ++i)
                {
                    expansion[i] = next[i];
                }
            }
            return length == 0;
        }

        bool ZeroArea(const S::SkeletalPosition& a, const S::SkeletalPosition& b,
                      const S::SkeletalPosition& c)
        {
            return ExactCrossComponentIsZero(a.Y, a.Z, b.Y, b.Z, c.Y, c.Z) &&
                   ExactCrossComponentIsZero(a.Z, a.X, b.Z, b.X, c.Z, c.X) &&
                   ExactCrossComponentIsZero(a.X, a.Y, b.X, b.Y, c.X, c.Y);
        }

        struct TriangleKey
        {
            uint32_t SubMesh, A, B, C, Triangle;
        };

        struct DisjointSet
        {
            C::VariableArray<uint32_t> Parent;
            C::VariableArray<uint8_t> Rank;

            explicit DisjointSet(size_t count) : Parent(count), Rank(count, 0)
            {
                for (size_t i = 0; i < count; ++i)
                {
                    Parent[i] = static_cast<uint32_t>(i);
                }
            }

            uint32_t Find(uint32_t value)
            {
                uint32_t root = value;
                while (Parent[root] != root)
                {
                    root = Parent[root];
                }
                while (Parent[value] != value)
                {
                    const uint32_t next = Parent[value];
                    Parent[value] = root;
                    value = next;
                }
                return root;
            }

            void Unite(uint32_t a, uint32_t b)
            {
                a = Find(a);
                b = Find(b);
                if (a == b)
                {
                    return;
                }
                if (Rank[a] < Rank[b] || (Rank[a] == Rank[b] && a > b))
                {
                    std::swap(a, b);
                }
                Parent[b] = a;
                if (Rank[a] == Rank[b])
                {
                    ++Rank[a];
                }
            }
        };

        struct EdgeRecord
        {
            uint32_t SubMesh, A, B, Primary, Triangle, Slot;
        };

        struct WorkingPart
        {
            uint32_t SubMesh = 0;
            uint32_t First = Invalid;
            uint32_t Last = Invalid;
            uint32_t MinTriangle = Invalid;
            uint32_t Count = 0;
            bool Active = true;
            C::VariableArray<uint32_t> Joints;
        };

        struct SplitTriangle
        {
            uint32_t Triangle;
            double Center[3];
        };

        struct SplitEdge
        {
            uint32_t Edge, Triangle;
            uint8_t Side;
        };

        struct HeapEntry
        {
            uint32_t Part, Count, SubMesh, MinTriangle;
        };

        // 共有辺から集めた隣接部位を、件数が多い場合も線形時間で集計できる順にする。
        void SortCandidates(C::VariableArray<uint32_t>& values, C::VariableArray<uint32_t>& scratch)
        {
            if (values.size() <= 64)
            {
                std::sort(values.begin(), values.end());
                return;
            }
            scratch.resize(values.size());
            for (uint32_t shift = 0; shift < 32; shift += 8)
            {
                size_t offsets[256]{};
                for (uint32_t value : values)
                {
                    ++offsets[(value >> shift) & 255u];
                }
                size_t offset = 0;
                for (size_t& count : offsets)
                {
                    const size_t current = count;
                    count = offset;
                    offset += current;
                }
                for (uint32_t value : values)
                {
                    scratch[offsets[(value >> shift) & 255u]++] = value;
                }
                values.swap(scratch);
            }
        }

        // 併合前の部位番号から、現在の併合先を求めて経路を圧縮する。
        uint32_t FindPart(C::VariableArray<uint32_t>& parents, uint32_t part)
        {
            uint32_t root = part;
            while (parents[root] != root)
            {
                root = parents[root];
            }
            while (parents[part] != part)
            {
                const uint32_t next = parents[part];
                parents[part] = root;
                part = next;
            }
            return root;
        }

        enum class SplitResult
        {
            Success,
            WouldExceed,
            Unsplittable,
            TooMany
        };

        // 部位内の索引列は単方向リストとし、小さい部位の併合時だけ所属を書き換える。
        class Partitioner
        {
        public:
            Partitioner(const SkinnedLodInput& input, const SkinnedLodPreprocessed& preprocessed,
                        const SkinnedLodParams& params)
                : Input(input), Mesh(preprocessed), Params(params),
                  TriangleCount(preprocessed.Indices.size() / 3), Sets(TriangleCount),
                  TrianglePart(TriangleCount, Invalid), NextTriangle(TriangleCount, Invalid),
                  TriangleEdges(TriangleCount * 3), RootPart(TriangleCount, Invalid)
            {
            }

            bool Run(SkinnedLodPartition& out, C::AnsiString& error)
            {
                BuildEdgesAndInitialParts();
                for (size_t part = 0; part < Parts.size(); ++part)
                {
                    while (Parts[part].Joints.size() > Params.MaxPartJoints)
                    {
                        C::VariableArray<uint32_t> children;
                        const SplitResult result = Split(static_cast<uint32_t>(part),
                                                         std::numeric_limits<size_t>::max(), children);
                        if (result != SplitResult::Success)
                        {
                            error = result == SplitResult::TooMany ? "skinned_lod_too_many_parts"
                                                                    : "skinned_lod_joint_limit";
                            return false;
                        }
                    }
                }
                MergeToTarget();
                SplitToTarget();
                if (ActiveCount > 256)
                {
                    error = "skinned_lod_too_many_parts";
                    return false;
                }
                WriteResult(out);
                return true;
            }

        private:
            uint32_t PrimaryJoint(uint32_t triangle) const
            {
                uint32_t joints[12]{};
                double weights[12]{};
                size_t count = 0;
                for (size_t corner = 0; corner < 3; ++corner)
                {
                    const uint32_t vertex = Mesh.Indices[size_t(triangle) * 3 + corner];
                    for (size_t slot = 0; slot < 4; ++slot)
                    {
                        const float weight = Mesh.Weights[size_t(vertex) * 4 + slot];
                        if (weight <= 0.0f)
                        {
                            continue;
                        }
                        const uint32_t joint = Input.Vertices[vertex].JointIndices[slot];
                        size_t index = 0;
                        while (index < count && joints[index] != joint)
                        {
                            ++index;
                        }
                        if (index == count)
                        {
                            joints[count++] = joint;
                        }
                        weights[index] += weight;
                    }
                }
                uint32_t best = 0;
                double bestWeight = 0.0;
                for (size_t i = 0; i < count; ++i)
                {
                    if (weights[i] > bestWeight || (weights[i] == bestWeight && joints[i] < best))
                    {
                        best = joints[i];
                        bestWeight = weights[i];
                    }
                }
                return best;
            }

            void AddJoints(WorkingPart& part, uint32_t triangle)
            {
                for (size_t corner = 0; corner < 3 && part.Joints.size() <= Params.MaxPartJoints; ++corner)
                {
                    const uint32_t vertex = Mesh.Indices[size_t(triangle) * 3 + corner];
                    for (size_t slot = 0; slot < 4 && part.Joints.size() <= Params.MaxPartJoints; ++slot)
                    {
                        if (Mesh.Weights[size_t(vertex) * 4 + slot] <= 0.0f)
                        {
                            continue;
                        }
                        const uint32_t joint = Input.Vertices[vertex].JointIndices[slot];
                        const auto found = std::lower_bound(part.Joints.begin(), part.Joints.end(), joint);
                        if (found == part.Joints.end() || *found != joint)
                        {
                            part.Joints.insert(found, joint);
                        }
                    }
                }
            }

            void Append(uint32_t partIndex, uint32_t triangle)
            {
                WorkingPart& part = Parts[partIndex];
                if (part.First == Invalid)
                {
                    part.First = triangle;
                }
                else
                {
                    NextTriangle[part.Last] = triangle;
                }
                part.Last = triangle;
                part.MinTriangle = std::min(part.MinTriangle, triangle);
                ++part.Count;
                NextTriangle[triangle] = Invalid;
                TrianglePart[triangle] = partIndex;
                AddJoints(part, triangle);
            }

            void BuildEdgesAndInitialParts()
            {
                C::VariableArray<EdgeRecord> edges;
                edges.reserve(Mesh.Indices.size());
                for (size_t subMesh = 0; subMesh < Mesh.SubMeshes.size(); ++subMesh)
                {
                    const auto& range = Mesh.SubMeshes[subMesh];
                    const size_t end = size_t(range.IndexStart) + range.IndexCount;
                    for (size_t index = range.IndexStart; index < end; index += 3)
                    {
                        const uint32_t triangle = static_cast<uint32_t>(index / 3);
                        const uint32_t primary = PrimaryJoint(triangle);
                        for (uint32_t slot = 0; slot < 3; ++slot)
                        {
                            uint32_t a = Mesh.WeldedVertex[Mesh.Indices[index + slot]];
                            uint32_t b = Mesh.WeldedVertex[Mesh.Indices[index + (slot + 1) % 3]];
                            if (a > b)
                            {
                                std::swap(a, b);
                            }
                            edges.push_back({static_cast<uint32_t>(subMesh), a, b, primary, triangle, slot});
                        }
                    }
                }
                std::sort(edges.begin(), edges.end(), [](const EdgeRecord& a, const EdgeRecord& b)
                {
                    return std::tie(a.SubMesh, a.A, a.B, a.Primary, a.Triangle, a.Slot) <
                           std::tie(b.SubMesh, b.A, b.B, b.Primary, b.Triangle, b.Slot);
                });
                EdgeOffsets.push_back(0);
                for (size_t begin = 0; begin < edges.size();)
                {
                    size_t end = begin + 1;
                    while (end < edges.size() && edges[end].SubMesh == edges[begin].SubMesh &&
                           edges[end].A == edges[begin].A && edges[end].B == edges[begin].B)
                    {
                        ++end;
                    }
                    const uint32_t edge = static_cast<uint32_t>(EdgeOffsets.size() - 1);
                    for (size_t i = begin; i < end; ++i)
                    {
                        TriangleEdges[size_t(edges[i].Triangle) * 3 + edges[i].Slot] = edge;
                        EdgeMembers.push_back(edges[i].Triangle);
                        if (i > begin && edges[i].Primary == edges[i - 1].Primary)
                        {
                            Sets.Unite(edges[i - 1].Triangle, edges[i].Triangle);
                        }
                    }
                    EdgeOffsets.push_back(static_cast<uint32_t>(end));
                    begin = end;
                }
                for (size_t subMesh = 0; subMesh < Mesh.SubMeshes.size(); ++subMesh)
                {
                    const auto& range = Mesh.SubMeshes[subMesh];
                    const size_t end = (size_t(range.IndexStart) + range.IndexCount) / 3;
                    for (size_t triangle = range.IndexStart / 3; triangle < end; ++triangle)
                    {
                        const uint32_t root = Sets.Find(static_cast<uint32_t>(triangle));
                        if (RootPart[root] == Invalid)
                        {
                            RootPart[root] = static_cast<uint32_t>(Parts.size());
                            Parts.emplace_back();
                            Parts.back().SubMesh = static_cast<uint32_t>(subMesh);
                        }
                        Append(RootPart[root], static_cast<uint32_t>(triangle));
                    }
                }
                std::fill(RootPart.begin(), RootPart.end(), Invalid);
                ActiveCount = Parts.size();
            }

            SplitResult Split(uint32_t partIndex, size_t partLimit, C::VariableArray<uint32_t>& children)
            {
                const WorkingPart& part = Parts[partIndex];
                if (part.Count < 2)
                {
                    return SplitResult::Unsplittable;
                }
                const uint32_t subMesh = part.SubMesh;
                C::VariableArray<SplitTriangle> triangles;
                triangles.reserve(part.Count);
                double minimum[3] = {std::numeric_limits<double>::max(), std::numeric_limits<double>::max(),
                                     std::numeric_limits<double>::max()};
                double maximum[3] = {-minimum[0], -minimum[1], -minimum[2]};
                for (uint32_t triangle = part.First; triangle != Invalid; triangle = NextTriangle[triangle])
                {
                    SplitTriangle item{triangle, {0.0, 0.0, 0.0}};
                    for (size_t corner = 0; corner < 3; ++corner)
                    {
                        const auto& pos = Input.Vertices[Mesh.Indices[size_t(triangle) * 3 + corner]].Position;
                        item.Center[0] += double(pos.X) / 3.0;
                        item.Center[1] += double(pos.Y) / 3.0;
                        item.Center[2] += double(pos.Z) / 3.0;
                    }
                    for (size_t axis = 0; axis < 3; ++axis)
                    {
                        minimum[axis] = std::min(minimum[axis], item.Center[axis]);
                        maximum[axis] = std::max(maximum[axis], item.Center[axis]);
                    }
                    triangles.push_back(item);
                }
                size_t axis = 0;
                for (size_t i = 1; i < 3; ++i)
                {
                    if (maximum[i] - minimum[i] > maximum[axis] - minimum[axis])
                    {
                        axis = i;
                    }
                }
                // 同一の重心は三角形番号で分け、中央値の両側を必ず空でなくする。
                std::nth_element(triangles.begin(),
                                 triangles.begin() + static_cast<std::ptrdiff_t>(triangles.size() / 2), triangles.end(),
                                 [axis](const SplitTriangle& a, const SplitTriangle& b)
                {
                    return a.Center[axis] != b.Center[axis] ? a.Center[axis] < b.Center[axis]
                                                           : a.Triangle < b.Triangle;
                });
                C::VariableArray<SplitEdge> edges;
                edges.reserve(triangles.size() * 3);
                for (size_t i = 0; i < triangles.size(); ++i)
                {
                    const uint32_t triangle = triangles[i].Triangle;
                    Sets.Parent[triangle] = triangle;
                    Sets.Rank[triangle] = 0;
                    for (size_t slot = 0; slot < 3; ++slot)
                    {
                        edges.push_back({TriangleEdges[size_t(triangle) * 3 + slot], triangle,
                                         static_cast<uint8_t>(i >= triangles.size() / 2)});
                    }
                }
                // 辺番号の基数整列で、再分割のたびに部位内の辺を線形時間でまとめる。
                C::VariableArray<SplitEdge> scratch(edges.size());
                for (uint32_t shift = 0; shift < 32; shift += 8)
                {
                    size_t offsets[256]{};
                    for (const auto& edge : edges)
                    {
                        ++offsets[(edge.Edge >> shift) & 255u];
                    }
                    size_t offset = 0;
                    for (size_t& count : offsets)
                    {
                        const size_t current = count;
                        count = offset;
                        offset += current;
                    }
                    for (const auto& edge : edges)
                    {
                        scratch[offsets[(edge.Edge >> shift) & 255u]++] = edge;
                    }
                    edges.swap(scratch);
                }
                for (size_t begin = 0; begin < edges.size();)
                {
                    size_t end = begin + 1;
                    while (end < edges.size() && edges[end].Edge == edges[begin].Edge)
                    {
                        ++end;
                    }
                    uint32_t first[2] = {Invalid, Invalid};
                    for (size_t i = begin; i < end; ++i)
                    {
                        const uint8_t side = edges[i].Side;
                        if (first[side] == Invalid)
                        {
                            first[side] = edges[i].Triangle;
                        }
                        else
                        {
                            Sets.Unite(first[side], edges[i].Triangle);
                        }
                    }
                    begin = end;
                }
                C::VariableArray<uint32_t> roots;
                for (const auto& item : triangles)
                {
                    const uint32_t root = Sets.Find(item.Triangle);
                    if (RootPart[root] == Invalid)
                    {
                        RootPart[root] = 0;
                        roots.push_back(root);
                    }
                }
                for (uint32_t root : roots)
                {
                    RootPart[root] = Invalid;
                }
                if (roots.size() - 1 > partLimit || ActiveCount > partLimit - (roots.size() - 1))
                {
                    return SplitResult::WouldExceed;
                }
                if (roots.size() - 1 >= size_t(Invalid) - Parts.size())
                {
                    return SplitResult::TooMany;
                }
                Parts[partIndex] = WorkingPart{};
                Parts[partIndex].SubMesh = subMesh;
                bool first = true;
                for (const auto& item : triangles)
                {
                    const uint32_t root = Sets.Find(item.Triangle);
                    if (RootPart[root] == Invalid)
                    {
                        uint32_t child = partIndex;
                        if (!first)
                        {
                            child = static_cast<uint32_t>(Parts.size());
                            Parts.emplace_back();
                            Parts.back().SubMesh = subMesh;
                        }
                        first = false;
                        RootPart[root] = child;
                        children.push_back(child);
                    }
                    Append(RootPart[root], item.Triangle);
                }
                for (uint32_t root : roots)
                {
                    RootPart[root] = Invalid;
                }
                ActiveCount += roots.size() - 1;
                return SplitResult::Success;
            }

            bool CanMerge(const WorkingPart& a, const WorkingPart& b) const
            {
                size_t left = 0, right = 0, count = 0;
                while (left < a.Joints.size() || right < b.Joints.size())
                {
                    if (right == b.Joints.size() ||
                        (left < a.Joints.size() && a.Joints[left] < b.Joints[right]))
                    {
                        ++left;
                    }
                    else if (left == a.Joints.size() || b.Joints[right] < a.Joints[left])
                    {
                        ++right;
                    }
                    else
                    {
                        ++left;
                        ++right;
                    }
                    if (++count > Params.MaxPartJoints)
                    {
                        return false;
                    }
                }
                return true;
            }

            HeapEntry Entry(uint32_t part) const
            {
                return {part, Parts[part].Count, Parts[part].SubMesh, Parts[part].MinTriangle};
            }

            bool Current(const HeapEntry& entry) const
            {
                return Parts[entry.Part].Active && Parts[entry.Part].Count == entry.Count &&
                       Parts[entry.Part].MinTriangle == entry.MinTriangle;
            }

            void MergeToTarget()
            {
                if (ActiveCount <= Params.MaxParts)
                {
                    return;
                }
                // 一つの辺に同じ部位の三角形が多数ある場合も、一部位につき一度だけ調べる。
                C::VariableArray<uint32_t> partOffsets, edgeParts;
                C::VariableArray<uint32_t> partSeen(Parts.size(), Invalid);
                partOffsets.reserve(EdgeOffsets.size());
                edgeParts.reserve(EdgeMembers.size());
                partOffsets.push_back(0);
                for (size_t edge = 0; edge + 1 < EdgeOffsets.size(); ++edge)
                {
                    const uint32_t edgeId = static_cast<uint32_t>(edge);
                    for (size_t i = EdgeOffsets[edge]; i < EdgeOffsets[edge + 1]; ++i)
                    {
                        const uint32_t part = TrianglePart[EdgeMembers[i]];
                        if (partSeen[part] != edgeId)
                        {
                            partSeen[part] = edgeId;
                            edgeParts.push_back(part);
                        }
                    }
                    partOffsets.push_back(static_cast<uint32_t>(edgeParts.size()));
                }
                EdgeOffsets = std::move(partOffsets);
                EdgeMembers = std::move(edgeParts);
                C::VariableArray<uint32_t> partParents(Parts.size());
                for (size_t i = 0; i < partParents.size(); ++i)
                {
                    partParents[i] = static_cast<uint32_t>(i);
                }
                const auto compare = [](const HeapEntry& a, const HeapEntry& b)
                {
                    return std::tie(a.Count, a.SubMesh, a.MinTriangle, a.Part) >
                           std::tie(b.Count, b.SubMesh, b.MinTriangle, b.Part);
                };
                C::VariableArray<HeapEntry> heap;
                heap.reserve(Parts.size());
                for (size_t i = 0; i < Parts.size(); ++i)
                {
                    if (Parts[i].Active)
                    {
                        heap.push_back(Entry(static_cast<uint32_t>(i)));
                    }
                }
                std::make_heap(heap.begin(), heap.end(), compare);
                C::VariableArray<uint32_t> edgeSeen(EdgeOffsets.size() - 1, Invalid);
                C::VariableArray<uint64_t> neighborSeen(Parts.size(), UINT64_MAX);
                C::VariableArray<uint32_t> candidates, candidateScratch;
                uint64_t edgeVisit = 0;
                while (ActiveCount > Params.MaxParts && !heap.empty())
                {
                    std::pop_heap(heap.begin(), heap.end(), compare);
                    const HeapEntry entry = heap.back();
                    heap.pop_back();
                    if (!Current(entry))
                    {
                        continue;
                    }
                    const uint32_t source = entry.Part;
                    candidates.clear();
                    for (uint32_t triangle = Parts[source].First; triangle != Invalid; triangle = NextTriangle[triangle])
                    {
                        for (size_t slot = 0; slot < 3; ++slot)
                        {
                            const uint32_t edge = TriangleEdges[size_t(triangle) * 3 + slot];
                            if (edgeSeen[edge] == source)
                            {
                                continue;
                            }
                            edgeSeen[edge] = source;
                            ++edgeVisit;
                            for (size_t i = EdgeOffsets[edge]; i < EdgeOffsets[size_t(edge) + 1]; ++i)
                            {
                                const uint32_t neighbor = FindPart(partParents, EdgeMembers[i]);
                                if (neighbor != source && neighborSeen[neighbor] != edgeVisit)
                                {
                                    neighborSeen[neighbor] = edgeVisit;
                                    if (CanMerge(Parts[source], Parts[neighbor]))
                                    {
                                        candidates.push_back(neighbor);
                                    }
                                }
                            }
                        }
                    }
                    SortCandidates(candidates, candidateScratch);
                    uint32_t target = Invalid;
                    size_t bestEdges = 0;
                    for (size_t begin = 0; begin < candidates.size();)
                    {
                        size_t end = begin + 1;
                        while (end < candidates.size() && candidates[end] == candidates[begin])
                        {
                            ++end;
                        }
                        const uint32_t candidate = candidates[begin];
                        const size_t shared = end - begin;
                        if (shared > bestEdges ||
                            (shared == bestEdges && Parts[candidate].MinTriangle < Parts[target].MinTriangle))
                        {
                            target = candidate;
                            bestEdges = shared;
                        }
                        begin = end;
                    }
                    if (target == Invalid)
                    {
                        // 関節集合は併合で増えるだけなので、今併合できない部位は後でも併合できない。
                        continue;
                    }
                    WorkingPart& from = Parts[source];
                    WorkingPart& to = Parts[target];
                    partParents[source] = target;
                    NextTriangle[to.Last] = from.First;
                    to.Last = from.Last;
                    to.Count += from.Count;
                    to.MinTriangle = std::min(to.MinTriangle, from.MinTriangle);
                    C::VariableArray<uint32_t> joints;
                    joints.reserve(to.Joints.size() + from.Joints.size());
                    std::set_union(to.Joints.begin(), to.Joints.end(), from.Joints.begin(), from.Joints.end(),
                                   std::back_inserter(joints));
                    to.Joints = std::move(joints);
                    from.Active = false;
                    from.Joints.clear();
                    --ActiveCount;
                    heap.push_back(Entry(target));
                    std::push_heap(heap.begin(), heap.end(), compare);
                }
            }

            void SplitToTarget()
            {
                const auto compare = [](const HeapEntry& a, const HeapEntry& b)
                {
                    if (a.Count != b.Count)
                    {
                        return a.Count < b.Count;
                    }
                    return std::tie(a.SubMesh, a.MinTriangle, a.Part) >
                           std::tie(b.SubMesh, b.MinTriangle, b.Part);
                };
                C::VariableArray<HeapEntry> heap;
                for (size_t i = 0; i < Parts.size(); ++i)
                {
                    if (Parts[i].Active)
                    {
                        heap.push_back(Entry(static_cast<uint32_t>(i)));
                    }
                }
                std::make_heap(heap.begin(), heap.end(), compare);
                while (ActiveCount < Params.MinParts && !heap.empty())
                {
                    std::pop_heap(heap.begin(), heap.end(), compare);
                    const HeapEntry entry = heap.back();
                    heap.pop_back();
                    if (!Current(entry))
                    {
                        continue;
                    }
                    if (entry.Count < 64)
                    {
                        break;
                    }
                    C::VariableArray<uint32_t> children;
                    if (Split(entry.Part, Params.MaxParts, children) != SplitResult::Success)
                    {
                        break;
                    }
                    for (uint32_t child : children)
                    {
                        heap.push_back(Entry(child));
                        std::push_heap(heap.begin(), heap.end(), compare);
                    }
                }
            }

            void WriteResult(SkinnedLodPartition& out)
            {
                C::VariableArray<uint32_t> order;
                order.reserve(ActiveCount);
                for (size_t i = 0; i < Parts.size(); ++i)
                {
                    if (Parts[i].Active)
                    {
                        order.push_back(static_cast<uint32_t>(i));
                    }
                }
                std::sort(order.begin(), order.end(), [this](uint32_t a, uint32_t b)
                {
                    return std::tie(Parts[a].SubMesh, Parts[a].MinTriangle) <
                           std::tie(Parts[b].SubMesh, Parts[b].MinTriangle);
                });
                out.TrianglePart.resize(TriangleCount);
                out.Parts.reserve(order.size());
                for (size_t i = 0; i < order.size(); ++i)
                {
                    WorkingPart& part = Parts[order[i]];
                    for (uint32_t triangle = part.First; triangle != Invalid; triangle = NextTriangle[triangle])
                    {
                        out.TrianglePart[triangle] = static_cast<uint32_t>(i);
                    }
                    out.Parts.push_back({part.SubMesh, part.Count, std::move(part.Joints)});
                }
                C::VariableArray<uint32_t> firstPart(Input.Vertices.size(), Invalid);
                C::VariableArray<uint8_t> boundary(Input.Vertices.size(), 0);
                for (size_t triangle = 0; triangle < TriangleCount; ++triangle)
                {
                    for (size_t corner = 0; corner < 3; ++corner)
                    {
                        const uint32_t vertex = Mesh.Indices[triangle * 3 + corner];
                        const uint32_t representative = Mesh.WeldedVertex[vertex];
                        if (firstPart[representative] == Invalid)
                        {
                            firstPart[representative] = out.TrianglePart[triangle];
                        }
                        else if (firstPart[representative] != out.TrianglePart[triangle])
                        {
                            boundary[representative] = 1;
                        }
                    }
                }
                out.BoundaryVertex.resize(Input.Vertices.size());
                for (size_t vertex = 0; vertex < Input.Vertices.size(); ++vertex)
                {
                    out.BoundaryVertex[vertex] = boundary[Mesh.WeldedVertex[vertex]];
                }
            }

            const SkinnedLodInput& Input;
            const SkinnedLodPreprocessed& Mesh;
            const SkinnedLodParams& Params;
            size_t TriangleCount;
            DisjointSet Sets;
            C::VariableArray<uint32_t> TrianglePart, NextTriangle, TriangleEdges, RootPart;
            // 初めは辺ごとの三角形、併合前に辺ごとの部位へ詰め直す。
            C::VariableArray<uint32_t> EdgeOffsets, EdgeMembers;
            C::VariableArray<WorkingPart> Parts;
            size_t ActiveCount = 0;
        };
    }

    bool PreprocessSkinnedMesh(const SkinnedLodInput& input, SkinnedLodPreprocessed& out, C::AnsiString& error)
    {
        if (!ValidateInput(input))
        {
            error = "skinned_lod_invalid_input";
            return false;
        }
        SkinnedLodPreprocessed result;
        result.Weights.resize(input.Vertices.size() * 4, 0.0f);
        result.WeldedVertex.resize(input.Vertices.size());
        C::VariableArray<PositionKey> positions;
        positions.reserve(input.Vertices.size());
        for (size_t vertex = 0; vertex < input.Vertices.size(); ++vertex)
        {
            const auto& data = input.Vertices[vertex];
            positions.push_back(MakePositionKey(data, static_cast<uint32_t>(vertex)));
            double weights[4]{};
            for (size_t slot = 0; slot < 4; ++slot)
            {
                size_t first = 0;
                while (first < slot && data.JointIndices[first] != data.JointIndices[slot])
                {
                    ++first;
                }
                weights[first] += data.JointWeights[slot];
            }
            const double sum = weights[0] + weights[1] + weights[2] + weights[3];
            if (sum == 0.0)
            {
                ++result.ZeroWeightVertexCount;
            }
            else
            {
                for (size_t slot = 0; slot < 4; ++slot)
                {
                    result.Weights[vertex * 4 + slot] = static_cast<float>(weights[slot] / sum);
                }
            }
        }
        std::sort(positions.begin(), positions.end(), [](const PositionKey& a, const PositionKey& b)
        {
            return std::tie(a.X, a.Y, a.Z, a.Vertex) < std::tie(b.X, b.Y, b.Z, b.Vertex);
        });
        for (size_t begin = 0; begin < positions.size();)
        {
            size_t end = begin + 1;
            while (end < positions.size() && SamePosition(positions[begin], positions[end]))
            {
                ++end;
            }
            const uint32_t representative = positions[begin].Vertex;
            for (size_t i = begin; i < end; ++i)
            {
                result.WeldedVertex[positions[i].Vertex] = representative;
            }
            begin = end;
        }

        const size_t subMeshCount = input.SubMeshes.empty() ? 1 : input.SubMeshes.size();
        C::VariableArray<TriangleKey> keys;
        keys.reserve(input.Indices.size() / 3);
        C::VariableArray<uint8_t> keep(input.Indices.size() / 3, 0);
        for (size_t subMesh = 0; subMesh < subMeshCount; ++subMesh)
        {
            const auto range = InputRange(input, subMesh);
            const size_t end = size_t(range.IndexStart) + range.IndexCount;
            for (size_t index = range.IndexStart; index < end; index += 3)
            {
                const uint32_t a = result.WeldedVertex[input.Indices[index]];
                const uint32_t b = result.WeldedVertex[input.Indices[index + 1]];
                const uint32_t c = result.WeldedVertex[input.Indices[index + 2]];
                if (a == b || b == c || c == a ||
                    ZeroArea(input.Vertices[a].Position, input.Vertices[b].Position, input.Vertices[c].Position))
                {
                    ++result.DegenerateTriangleCount;
                    continue;
                }
                const uint32_t triangle = static_cast<uint32_t>(index / 3);
                keep[triangle] = 1;
                if (a < b && a < c)
                {
                    keys.push_back({static_cast<uint32_t>(subMesh), a, b, c, triangle});
                }
                else if (b < c)
                {
                    keys.push_back({static_cast<uint32_t>(subMesh), b, c, a, triangle});
                }
                else
                {
                    keys.push_back({static_cast<uint32_t>(subMesh), c, a, b, triangle});
                }
            }
        }
        std::sort(keys.begin(), keys.end(), [](const TriangleKey& a, const TriangleKey& b)
        {
            return std::tie(a.SubMesh, a.A, a.B, a.C, a.Triangle) <
                   std::tie(b.SubMesh, b.A, b.B, b.C, b.Triangle);
        });
        for (size_t i = 1; i < keys.size(); ++i)
        {
            if (std::tie(keys[i - 1].SubMesh, keys[i - 1].A, keys[i - 1].B, keys[i - 1].C) ==
                std::tie(keys[i].SubMesh, keys[i].A, keys[i].B, keys[i].C))
            {
                keep[keys[i].Triangle] = 0;
                ++result.DuplicateTriangleCount;
            }
        }
        result.Indices.reserve(input.Indices.size());
        result.SubMeshes.reserve(subMeshCount);
        for (size_t subMesh = 0; subMesh < subMeshCount; ++subMesh)
        {
            const auto range = InputRange(input, subMesh);
            SkinnedLodSubMeshRange outputRange{static_cast<uint32_t>(result.Indices.size()), 0};
            const size_t end = size_t(range.IndexStart) + range.IndexCount;
            for (size_t index = range.IndexStart; index < end; index += 3)
            {
                if (keep[index / 3])
                {
                    result.Indices.push_back(input.Indices[index]);
                    result.Indices.push_back(input.Indices[index + 1]);
                    result.Indices.push_back(input.Indices[index + 2]);
                    outputRange.IndexCount += 3;
                }
            }
            if (outputRange.IndexCount == 0)
            {
                error = "skinned_lod_empty_submesh";
                return false;
            }
            result.SubMeshes.push_back(outputRange);
        }
        out = std::move(result);
        error.clear();
        return true;
    }

    bool PartitionSkinnedMesh(const SkinnedLodInput& input, const SkinnedLodPreprocessed& preprocessed,
                              const SkinnedLodParams& params, SkinnedLodPartition& out, C::AnsiString& error)
    {
        if (!ValidateInput(input))
        {
            error = "skinned_lod_invalid_input";
            return false;
        }
        if (!ValidatePreprocessed(input, preprocessed))
        {
            error = "skinned_lod_invalid_preprocessed";
            return false;
        }
        Partitioner partitioner(input, preprocessed, params);
        SkinnedLodPartition result;
        if (!partitioner.Run(result, error))
        {
            return false;
        }
        out = std::move(result);
        error.clear();
        return true;
    }
}
