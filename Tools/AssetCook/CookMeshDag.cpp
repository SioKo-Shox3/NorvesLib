#include "CookMeshDag.h"

#include "CookMeshOptimizer.h"
#include "Rendering/MegaGeometry/MeshClusterizer.h"

#include <meshoptimizer.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace NorvesLib::Tools::AssetCook
{
    namespace
    {
        using Core::Asset::CookedMeshCluster;
        using Core::Asset::CookedMeshClusterGroup;
        using Core::Asset::CookedMeshFloat3;
        using Core::Asset::CookedMeshVertex;
        using Core::Container::AnsiString;
        using Core::Container::VariableArray;
        using Core::Rendering::MegaGeometry::MeshCluster;
        using Core::Rendering::MegaGeometry::MeshClusterizer;
        namespace Format = Core::Asset::CookedMeshFormatV1;

        constexpr size_t ClusterMaxTriangles = Format::ClusterMaxTriangles;
        constexpr size_t ClusterMaxVertices = Format::ClusterMaxVertices;
        // 1 グループのクラスタ数の目標。これ以下の数のクラスタしか残っていない段は、全部を 1 グループにする。
        constexpr size_t GroupTargetSize = 4;
        constexpr size_t SingleGroupMaxClusters = GroupTargetSize + GroupTargetSize / 3;
        constexpr float ConeWeight = 0.25f;
        // 簡略化の誤差に混ぜる属性の重み(外形の大きさに対する相対値)。法線と UV の食い違いも誤差として数える。
        constexpr float AttributeWeightNormal = 0.2f;
        constexpr float AttributeWeightUv = 0.1f;
        constexpr size_t AttributeCount = 5;
        constexpr uint32_t InvalidIndex = 0xffffffffu;
        constexpr size_t FallbackMaxTriangles = 32768;
        // 呼び出し側が fallbackMinTriangles で明示した下限の上限。既定の目標（FallbackMaxTriangles）では影・レイトレが
        // 実面からずれて自己遮蔽を起こす、変位した大きな球（石の盛り上がりと目地が数 cm）のためにある。
        constexpr size_t FallbackMinOverrideMaxTriangles = 131072;
        constexpr size_t FallbackReductionDivisor = 16;
        // 1 段で三角形がこの割合(%)も減らない段は、簡略化が進まないので、そこを根の段として打ち切る。
        constexpr uint64_t MinProgressPercent = 15;

        struct BuildCluster
        {
            // 溶接後の頂点番号(メッシュ全体で通しの番号)の三角形リスト
            VariableArray<uint32_t> Indices;
            CookedMeshFloat3 Center;
            float Radius = 0.0f;
            float Error = 0.0f;
            CookedMeshFloat3 ConeAxis;
            float ConeCutoff = -1.0f;
            // このクラスタを作ったグループ（1つ細かい段）の番号。最も細かい段は InvalidGroupId
            uint32_t SourceGroup = Format::InvalidGroupId;
        };

        struct BuiltCluster
        {
            BuildCluster Cluster;
            uint32_t Level = 0;
            uint32_t GroupId = Format::InvalidGroupId;
            CookedMeshFloat3 ParentCenter;
            float ParentRadius = 0.0f;
            float ParentError = Format::RootParentError;
            bool bRoot = false;
        };

        struct LevelGroup
        {
            CookedMeshFloat3 Center;
            float Radius = 0.0f;
            float Error = 0.0f;
            // 段のクラスタ(current)の添字
            VariableArray<uint32_t> Members;
        };

        struct LevelOutput
        {
            VariableArray<LevelGroup> Groups;
            VariableArray<BuildCluster> Next;
            uint64_t NextTriangles = 0;
            // 属性の継ぎ目をまたぐ統合を許さないと半分に届かず、許して簡略化したグループの数
            uint32_t PermissiveGroups = 0;
            // 安全な簡略化が見つからず、簡略化せずに残したグループの数
            uint32_t RejectedGroups = 0;
        };

        struct WeldedMesh
        {
            VariableArray<CookedMeshVertex> Vertices;
            // float3 / float5(法線 3 + UV 2)を頂点の順に並べたもの
            VariableArray<float> Positions;
            VariableArray<float> Attributes;
            // 位置が同じ頂点(UV の継ぎ目などで分かれたもの)を同じ番号にする表
            VariableArray<uint32_t> PositionRemap;
            VariableArray<uint32_t> Indices;
            float Scale = 0.0f;

            size_t VertexCount() const
            {
                return Vertices.size();
            }
        };

        // 簡略化の作業領域。グループごとに頂点を詰め直すので、全体の頂点数の表は 1 度だけ作って使い回す。
        struct SimplifyScratch
        {
            VariableArray<uint32_t> LocalOfGlobal;
            VariableArray<uint32_t> LocalToGlobal;
            VariableArray<uint32_t> LocalIndices;
            VariableArray<float> LocalPositions;
            VariableArray<float> LocalAttributes;
            VariableArray<uint8_t> LocalLock;
            // 段の入力全体の辺(ソート済み)。簡略化が作る新しい辺が、隣のグループの辺と重ならないかの確認に使う。
            VariableArray<uint64_t> LevelInputEdges;
            // 段で、これまでのグループが簡略化で新しく作った辺(ソート済み)
            VariableArray<uint64_t> ClaimedEdges;
        };

        double DistanceBetween(const CookedMeshFloat3& a, const CookedMeshFloat3& b)
        {
            const double dx = static_cast<double>(a.X) - static_cast<double>(b.X);
            const double dy = static_cast<double>(a.Y) - static_cast<double>(b.Y);
            const double dz = static_cast<double>(a.Z) - static_cast<double>(b.Z);
            return std::sqrt(dx * dx + dy * dy + dz * dz);
        }

        float RoundUpToFloat(double value)
        {
            float rounded = static_cast<float>(value);
            if (static_cast<double>(rounded) < value)
            {
                rounded = std::nextafter(rounded, std::numeric_limits<float>::max());
            }
            return rounded;
        }

        // 点(radii が null)または球の集まりを包む球。中心は meshoptimizer、半径はすべての要素を確実に包む値で求め直す。
        void EncloseSpheres(const float* centers, const float* radii, size_t count, CookedMeshFloat3& outCenter,
                            float& outRadius)
        {
            outCenter = {};
            outRadius = 0.0f;
            if (count == 0)
            {
                return;
            }

            const meshopt_Bounds bounds = meshopt_computeSphereBounds(
                centers, count, sizeof(float) * 3, radii, radii != nullptr ? sizeof(float) : 0);
            outCenter = {bounds.center[0], bounds.center[1], bounds.center[2]};

            double needed = 0.0;
            for (size_t i = 0; i < count; ++i)
            {
                const CookedMeshFloat3 point = {centers[i * 3 + 0], centers[i * 3 + 1], centers[i * 3 + 2]};
                const double radius = radii != nullptr ? static_cast<double>(radii[i]) : 0.0;
                needed = std::max(needed, DistanceBetween(outCenter, point) + radius);
            }
            outRadius = RoundUpToFloat(needed);
        }

        void ComputeClusterSphere(const WeldedMesh& mesh, BuildCluster& cluster)
        {
            VariableArray<uint32_t> unique = cluster.Indices;
            std::sort(unique.begin(), unique.end());
            unique.erase(std::unique(unique.begin(), unique.end()), unique.end());

            VariableArray<float> points;
            points.reserve(unique.size() * 3);
            for (const uint32_t vertex : unique)
            {
                points.push_back(mesh.Positions[static_cast<size_t>(vertex) * 3 + 0]);
                points.push_back(mesh.Positions[static_cast<size_t>(vertex) * 3 + 1]);
                points.push_back(mesh.Positions[static_cast<size_t>(vertex) * 3 + 2]);
            }
            EncloseSpheres(points.data(), nullptr, unique.size(), cluster.Center, cluster.Radius);
        }

        void ComputeClusterCone(const WeldedMesh& mesh, BuildCluster& cluster)
        {
            MeshCluster cone;
            cone.IndexOffset = 0;
            cone.IndexCount = static_cast<uint32_t>(cluster.Indices.size());
            MeshClusterizer::ComputeNormalCone(mesh.Positions.data(), static_cast<uint32_t>(sizeof(float) * 3),
                                               cluster.Indices.data(), cone);
            cluster.ConeAxis = {cone.ConeAxisX, cone.ConeAxisY, cone.ConeAxisZ};
            cluster.ConeCutoff = cone.ConeCutoff;
        }

        bool BuildWeldedMesh(const CookedMeshVertex* vertices, size_t vertexCount, const uint32_t* indices,
                             size_t indexCount, WeldedMesh& outMesh, AnsiString& error)
        {
            if (vertices == nullptr || indices == nullptr || vertexCount == 0 || indexCount == 0 || indexCount % 3 != 0)
            {
                error = "LOD の階層の入力が空か、索引数が 3 の倍数ではありません";
                return false;
            }
            if (vertexCount > std::numeric_limits<uint32_t>::max() || indexCount > std::numeric_limits<uint32_t>::max())
            {
                error = "LOD の階層の入力が 32bit の上限を超えています";
                return false;
            }
            for (size_t i = 0; i < indexCount; ++i)
            {
                if (indices[i] >= vertexCount)
                {
                    error = "LOD の階層の入力の索引が頂点数の範囲外です";
                    return false;
                }
            }

            // -0.0 と 0.0 が別の頂点にならないよう、0 を足して正の 0 にそろえる(ビット列で比べて溶接するため)
            VariableArray<CookedMeshVertex> source(vertexCount);
            for (size_t v = 0; v < vertexCount; ++v)
            {
                CookedMeshVertex vertex = vertices[v];
                float* values[] = {&vertex.Position.X, &vertex.Position.Y, &vertex.Position.Z, &vertex.Normal.X,
                                   &vertex.Normal.Y,   &vertex.Normal.Z,   &vertex.TexCoord.U, &vertex.TexCoord.V};
                for (float* value : values)
                {
                    if (!std::isfinite(*value))
                    {
                        error = "LOD の階層の入力の頂点に NaN か無限大があります";
                        return false;
                    }
                    *value += 0.0f;
                }
                source[v] = vertex;
            }

            VariableArray<uint32_t> remap(vertexCount, 0u);
            const size_t uniqueCount = meshopt_generateVertexRemap(remap.data(), indices, indexCount, source.data(),
                                                                   vertexCount, sizeof(CookedMeshVertex));
            VariableArray<CookedMeshVertex> unique(uniqueCount);
            meshopt_remapVertexBuffer(unique.data(), source.data(), vertexCount, sizeof(CookedMeshVertex), remap.data());
            VariableArray<uint32_t> remapped(indexCount, 0u);
            meshopt_remapIndexBuffer(remapped.data(), indices, indexCount, remap.data());

            // 溶接で同じ頂点になった三角形(縮退)を除き、残った三角形が使う頂点だけを詰め直す
            VariableArray<uint32_t> compactOf(uniqueCount, InvalidIndex);
            outMesh = WeldedMesh{};
            for (size_t i = 0; i < indexCount; i += 3)
            {
                const uint32_t a = remapped[i];
                const uint32_t b = remapped[i + 1];
                const uint32_t c = remapped[i + 2];
                if (a == b || b == c || a == c)
                {
                    continue;
                }
                for (const uint32_t vertex : {a, b, c})
                {
                    if (compactOf[vertex] == InvalidIndex)
                    {
                        compactOf[vertex] = static_cast<uint32_t>(outMesh.Vertices.size());
                        outMesh.Vertices.push_back(unique[vertex]);
                    }
                    outMesh.Indices.push_back(compactOf[vertex]);
                }
            }
            if (outMesh.Indices.empty())
            {
                error = "LOD の階層の入力の三角形がすべて縮退しています";
                return false;
            }

            const size_t weldedCount = outMesh.Vertices.size();
            outMesh.Positions.reserve(weldedCount * 3);
            outMesh.Attributes.reserve(weldedCount * AttributeCount);
            for (const CookedMeshVertex& vertex : outMesh.Vertices)
            {
                outMesh.Positions.push_back(vertex.Position.X);
                outMesh.Positions.push_back(vertex.Position.Y);
                outMesh.Positions.push_back(vertex.Position.Z);
                outMesh.Attributes.push_back(vertex.Normal.X);
                outMesh.Attributes.push_back(vertex.Normal.Y);
                outMesh.Attributes.push_back(vertex.Normal.Z);
                outMesh.Attributes.push_back(vertex.TexCoord.U);
                outMesh.Attributes.push_back(vertex.TexCoord.V);
            }
            outMesh.PositionRemap.assign(weldedCount, 0u);
            meshopt_generatePositionRemap(outMesh.PositionRemap.data(), outMesh.Positions.data(), weldedCount,
                                          sizeof(float) * 3);
            outMesh.Scale = meshopt_simplifyScale(outMesh.Positions.data(), weldedCount, sizeof(float) * 3);
            if (!(outMesh.Scale > 0.0f) || !std::isfinite(outMesh.Scale))
            {
                error = "LOD の階層の入力の外形の大きさが 0 か不正です";
                return false;
            }
            return true;
        }

        // 三角形リストを 128 三角形・128 頂点以下のクラスタに分ける。indices は positions の頂点番号を指し、
        // localToGlobal が null でなければ、結果の頂点番号をそれで通しの番号へ引き直す。
        void BuildClustersFromTriangles(const uint32_t* indices, size_t indexCount, const float* positions,
                                        size_t vertexCount, const uint32_t* localToGlobal,
                                        VariableArray<BuildCluster>& outClusters)
        {
            const size_t maxMeshlets = meshopt_buildMeshletsBound(indexCount, ClusterMaxVertices, ClusterMaxTriangles);
            VariableArray<meshopt_Meshlet> meshlets(maxMeshlets);
            VariableArray<unsigned int> meshletVertices(indexCount, 0u);
            VariableArray<unsigned char> meshletTriangles(indexCount, 0);
            const size_t meshletCount = meshopt_buildMeshlets(meshlets.data(),
                                                              meshletVertices.data(),
                                                              meshletTriangles.data(),
                                                              indices,
                                                              indexCount,
                                                              positions,
                                                              vertexCount,
                                                              sizeof(float) * 3,
                                                              ClusterMaxVertices,
                                                              ClusterMaxTriangles,
                                                              ConeWeight);
            for (size_t m = 0; m < meshletCount; ++m)
            {
                const meshopt_Meshlet& meshlet = meshlets[m];
                BuildCluster cluster;
                cluster.Indices.reserve(static_cast<size_t>(meshlet.triangle_count) * 3);
                for (size_t i = 0; i < static_cast<size_t>(meshlet.triangle_count) * 3; ++i)
                {
                    const uint32_t local =
                        meshletVertices[meshlet.vertex_offset + meshletTriangles[meshlet.triangle_offset + i]];
                    cluster.Indices.push_back(localToGlobal != nullptr ? localToGlobal[local] : local);
                }
                outClusters.push_back(std::move(cluster));
            }
        }

        // 異なるグループの三角形が共有する辺の両端(位置番号)に印を付ける。位置番号は PositionRemap の値で、
        // UV の継ぎ目で分かれた頂点も同じ位置なら同じ番号になる。
        VariableArray<uint8_t> ComputeGroupBoundaryLocks(const WeldedMesh& mesh,
                                                         const VariableArray<BuildCluster>& clusters,
                                                         const VariableArray<uint32_t>& groupOfCluster)
        {
            struct EdgeRecord
            {
                uint64_t Key = 0;
                uint32_t Group = 0;
            };

            size_t triangleTotal = 0;
            for (const BuildCluster& cluster : clusters)
            {
                triangleTotal += cluster.Indices.size() / 3;
            }
            VariableArray<EdgeRecord> edges;
            edges.reserve(triangleTotal * 3);
            for (size_t c = 0; c < clusters.size(); ++c)
            {
                const VariableArray<uint32_t>& indices = clusters[c].Indices;
                for (size_t i = 0; i < indices.size(); i += 3)
                {
                    for (size_t e = 0; e < 3; ++e)
                    {
                        const uint32_t a = mesh.PositionRemap[indices[i + e]];
                        const uint32_t b = mesh.PositionRemap[indices[i + (e + 1) % 3]];
                        if (a == b)
                        {
                            continue;
                        }
                        const uint32_t lo = std::min(a, b);
                        const uint32_t hi = std::max(a, b);
                        edges.push_back({(static_cast<uint64_t>(lo) << 32) | hi, groupOfCluster[c]});
                    }
                }
            }
            std::sort(edges.begin(), edges.end(), [](const EdgeRecord& lhs, const EdgeRecord& rhs) {
                return lhs.Key != rhs.Key ? lhs.Key < rhs.Key : lhs.Group < rhs.Group;
            });

            VariableArray<uint8_t> lockByPosition(mesh.VertexCount(), 0);
            for (size_t i = 0; i < edges.size();)
            {
                size_t j = i + 1;
                bool bCrossesGroups = false;
                while (j < edges.size() && edges[j].Key == edges[i].Key)
                {
                    bCrossesGroups = bCrossesGroups || edges[j].Group != edges[i].Group;
                    ++j;
                }
                if (bCrossesGroups)
                {
                    lockByPosition[static_cast<uint32_t>(edges[i].Key >> 32)] = 1;
                    lockByPosition[static_cast<uint32_t>(edges[i].Key & 0xffffffffull)] = 1;
                }
                i = j;
            }
            return lockByPosition;
        }

        // 三角形の辺を、位置が同じ頂点を同じ頂点として無向で列挙する(重複を残してソートする)。indices は局所の頂点番号で、
        // localToGlobal が null なら通しの頂点番号として扱う。
        void CollectPositionEdges(const WeldedMesh& mesh,
                                  const uint32_t* localToGlobal,
                                  const uint32_t* indices,
                                  size_t indexCount,
                                  VariableArray<uint64_t>& outEdges)
        {
            outEdges.clear();
            outEdges.reserve(indexCount);
            for (size_t i = 0; i + 2 < indexCount; i += 3)
            {
                for (size_t e = 0; e < 3; ++e)
                {
                    const uint32_t from = indices[i + e];
                    const uint32_t to = indices[i + (e + 1) % 3];
                    const uint32_t a = mesh.PositionRemap[localToGlobal != nullptr ? localToGlobal[from] : from];
                    const uint32_t b = mesh.PositionRemap[localToGlobal != nullptr ? localToGlobal[to] : to];
                    if (a != b)
                    {
                        outEdges.push_back((static_cast<uint64_t>(std::min(a, b)) << 32) | std::max(a, b));
                    }
                }
            }
            std::sort(outEdges.begin(), outEdges.end());
        }

        bool ContainsEdge(const VariableArray<uint64_t>& sortedEdges, uint64_t key)
        {
            return std::binary_search(sortedEdges.begin(), sortedEdges.end(), key);
        }

        // 簡略化の出力が、非多様体の辺(3 枚以上の三角形が共有する辺)を作らないか。
        //   - グループの入力にある辺: 入力の共有数以下(縁の辺は 1 枚のまま)
        //   - 入力にない新しい辺: 2 枚以下で、段の入力の辺(隣のグループの辺)や、先に簡略化したグループが作った辺と重ならない。
        //     重なると、隣と並べた切り口で 1 本の辺に 4 枚が集まる。
        bool IsOutputEdgeSafe(const VariableArray<uint64_t>& inputEdges,
                              const VariableArray<uint64_t>& outputEdges,
                              const SimplifyScratch& scratch)
        {
            for (size_t i = 0; i < outputEdges.size();)
            {
                size_t j = i + 1;
                while (j < outputEdges.size() && outputEdges[j] == outputEdges[i])
                {
                    ++j;
                }
                const auto inputRange = std::equal_range(inputEdges.begin(), inputEdges.end(), outputEdges[i]);
                const size_t inputCount = static_cast<size_t>(inputRange.second - inputRange.first);
                if (inputCount > 0)
                {
                    if (j - i > inputCount)
                    {
                        return false;
                    }
                }
                else if (j - i > 2 || ContainsEdge(scratch.LevelInputEdges, outputEdges[i]) ||
                         ContainsEdge(scratch.ClaimedEdges, outputEdges[i]))
                {
                    return false;
                }
                i = j;
            }
            return true;
        }

        // 1 グループ(メンバのクラスタ)を、境界の頂点を固定して半分に簡略化し、クラスタに分け直す。
        bool SimplifyGroup(const WeldedMesh& mesh,
                           const VariableArray<BuildCluster>& current,
                           const VariableArray<uint8_t>& lockByPosition,
                           SimplifyScratch& scratch,
                           LevelGroup& group,
                           LevelOutput& output,
                           AnsiString& error)
        {
            scratch.LocalToGlobal.clear();
            scratch.LocalIndices.clear();
            float maxMemberError = 0.0f;
            VariableArray<float> memberCenters;
            VariableArray<float> memberRadii;
            for (const uint32_t member : group.Members)
            {
                const BuildCluster& cluster = current[member];
                maxMemberError = std::max(maxMemberError, cluster.Error);
                memberCenters.push_back(cluster.Center.X);
                memberCenters.push_back(cluster.Center.Y);
                memberCenters.push_back(cluster.Center.Z);
                memberRadii.push_back(cluster.Radius);
                for (const uint32_t vertex : cluster.Indices)
                {
                    if (scratch.LocalOfGlobal[vertex] == InvalidIndex)
                    {
                        scratch.LocalOfGlobal[vertex] = static_cast<uint32_t>(scratch.LocalToGlobal.size());
                        scratch.LocalToGlobal.push_back(vertex);
                    }
                    scratch.LocalIndices.push_back(scratch.LocalOfGlobal[vertex]);
                }
            }

            const size_t localCount = scratch.LocalToGlobal.size();
            scratch.LocalPositions.resize(localCount * 3);
            scratch.LocalAttributes.resize(localCount * AttributeCount);
            scratch.LocalLock.resize(localCount);
            for (size_t local = 0; local < localCount; ++local)
            {
                const size_t global = scratch.LocalToGlobal[local];
                std::memcpy(&scratch.LocalPositions[local * 3], &mesh.Positions[global * 3], sizeof(float) * 3);
                std::memcpy(&scratch.LocalAttributes[local * AttributeCount], &mesh.Attributes[global * AttributeCount],
                            sizeof(float) * AttributeCount);
                scratch.LocalLock[local] = lockByPosition[mesh.PositionRemap[global]];
                scratch.LocalOfGlobal[global] = InvalidIndex;
            }

            CookSimplifyParams params;
            const size_t triangleCount = scratch.LocalIndices.size() / 3;
            const size_t halfTarget = std::max<size_t>(3, (triangleCount / 2) * 3);
            // 絶対の誤差で扱うので、上限は外形の大きさ。目標の三角形数に届くところまで簡略化する。
            params.TargetErrorRelative = mesh.Scale;
            params.bErrorAbsolute = true;
            params.bClampAttributeError = true;
            params.VertexLock = scratch.LocalLock;
            params.Attributes = scratch.LocalAttributes;
            params.AttributeCount = AttributeCount;
            // 属性の重みは無次元(簡略化は位置を外形の大きさで正規化するので、座標の単位に左右されない)
            params.AttributeWeights = {AttributeWeightNormal, AttributeWeightNormal, AttributeWeightNormal,
                                       AttributeWeightUv, AttributeWeightUv};

            // 簡略化の出力のうち、辺を非多様体にしないものだけを候補にする
            VariableArray<uint64_t> inputEdges;
            VariableArray<uint64_t> outputEdges;
            CollectPositionEdges(mesh, scratch.LocalToGlobal.data(), scratch.LocalIndices.data(),
                                 scratch.LocalIndices.size(), inputEdges);
            CookSimplifyResult simplified;
            bool bHaveCandidate = false;
            bool bCandidatePermissive = false;
            const auto attempt = [&](size_t targetIndexCount, bool bPermissive) -> bool {
                params.TargetIndexCount = targetIndexCount;
                params.bPermissive = bPermissive;
                CookSimplifyResult candidate;
                if (!SimplifyMeshTriangles(scratch.LocalPositions.data(), localCount, sizeof(float) * 3,
                                           scratch.LocalIndices.data(), scratch.LocalIndices.size(), params, candidate,
                                           error))
                {
                    return false;
                }
                CollectPositionEdges(mesh, scratch.LocalToGlobal.data(), candidate.Indices.data(),
                                     candidate.Indices.size(), outputEdges);
                if (IsOutputEdgeSafe(inputEdges, outputEdges, scratch) &&
                    (!bHaveCandidate || candidate.Indices.size() < simplified.Indices.size()))
                {
                    simplified = std::move(candidate);
                    bHaveCandidate = true;
                    bCandidatePermissive = bPermissive;
                }
                return true;
            };

            if (!attempt(halfTarget, false))
            {
                return false;
            }
            // 属性の継ぎ目を保つ簡略化で半分に届かないグループ(継ぎ目だらけのスキャン資産など)は、
            // 属性の不連続をまたぐ統合も許して再試行し、三角形が少ない方を採る。
            const size_t targetTriangles = halfTarget / 3;
            if (!bHaveCandidate || simplified.Indices.size() / 3 * 4 > targetTriangles * 5)
            {
                if (!attempt(halfTarget, true))
                {
                    return false;
                }
            }
            // どちらも非多様体の辺を作るなら、控えめな目標でやり直す
            if (!bHaveCandidate && !attempt(std::max<size_t>(3, (triangleCount * 3 / 4) * 3), false))
            {
                return false;
            }
            if (!bHaveCandidate)
            {
                // それでも安全な簡略化が無いグループは、簡略化せずそのまま残す(次の段で、近くのグループと合わせて再挑戦する)
                simplified = CookSimplifyResult{};
                simplified.Indices = scratch.LocalIndices;
                ++output.RejectedGroups;
            }
            else
            {
                if (bCandidatePermissive)
                {
                    ++output.PermissiveGroups;
                }
                // このグループが新しく作った辺を記録し、あとのグループが同じ辺を作らないようにする
                CollectPositionEdges(mesh, scratch.LocalToGlobal.data(), simplified.Indices.data(),
                                     simplified.Indices.size(), outputEdges);
                for (size_t i = 0; i < outputEdges.size(); ++i)
                {
                    if ((i == 0 || outputEdges[i] != outputEdges[i - 1]) && !ContainsEdge(inputEdges, outputEdges[i]))
                    {
                        scratch.ClaimedEdges.push_back(outputEdges[i]);
                    }
                }
                std::sort(scratch.ClaimedEdges.begin(), scratch.ClaimedEdges.end());
            }
            if (!std::isfinite(simplified.ErrorAbsolute) || simplified.ErrorAbsolute < 0.0f)
            {
                error = "簡略化の誤差が不正な値です";
                return false;
            }

            // グループの誤差 = メンバの誤差の最大 + 簡略化の誤差(親の誤差は子より小さくならない)
            group.Error = maxMemberError + simplified.ErrorAbsolute;
            if (!std::isfinite(group.Error) || group.Error >= Format::RootParentError)
            {
                error = "グループの誤差が有限の範囲を超えました";
                return false;
            }
            EncloseSpheres(memberCenters.data(), memberRadii.data(), group.Members.size(), group.Center, group.Radius);

            const size_t firstNew = output.Next.size();
            if (!simplified.Indices.empty())
            {
                BuildClustersFromTriangles(simplified.Indices.data(), simplified.Indices.size(),
                                           scratch.LocalPositions.data(), localCount, scratch.LocalToGlobal.data(),
                                           output.Next);
            }
            // この時点で output.Groups にはまだこのグループが入っていないので、段の中の通し番号は現在の件数になる
            const uint32_t localGroupId = static_cast<uint32_t>(output.Groups.size());
            for (size_t i = firstNew; i < output.Next.size(); ++i)
            {
                BuildCluster& cluster = output.Next[i];
                cluster.SourceGroup = localGroupId;
                cluster.Center = group.Center;
                cluster.Radius = group.Radius;
                cluster.Error = group.Error;
                ComputeClusterCone(mesh, cluster);
                output.NextTriangles += cluster.Indices.size() / 3;
            }
            return true;
        }

        // 1 段分の処理。段のクラスタをグループに分け、グループごとに簡略化して、次の段のクラスタを作る。
        bool ProcessLevel(const WeldedMesh& mesh,
                          const VariableArray<BuildCluster>& current,
                          SimplifyScratch& scratch,
                          LevelOutput& output,
                          AnsiString& error)
        {
            const size_t clusterCount = current.size();
            VariableArray<uint32_t> groupOfCluster(clusterCount, 0u);
            size_t groupCount = 1;
            if (clusterCount > SingleGroupMaxClusters)
            {
                VariableArray<uint32_t> clusterIndices;
                VariableArray<uint32_t> clusterIndexCounts;
                // 隣り合うクラスタを判定する頂点は、位置が同じものを同じ頂点として数える(UV の継ぎ目で頂点が分かれていても隣とみなす)
                for (const BuildCluster& cluster : current)
                {
                    for (const uint32_t vertex : cluster.Indices)
                    {
                        clusterIndices.push_back(mesh.PositionRemap[vertex]);
                    }
                    clusterIndexCounts.push_back(static_cast<uint32_t>(cluster.Indices.size()));
                }
                groupCount = meshopt_partitionClusters(groupOfCluster.data(), clusterIndices.data(),
                                                       clusterIndices.size(), clusterIndexCounts.data(), clusterCount,
                                                       mesh.Positions.data(), mesh.VertexCount(), sizeof(float) * 3,
                                                       GroupTargetSize);
            }

            VariableArray<LevelGroup> groups(groupCount);
            for (size_t c = 0; c < clusterCount; ++c)
            {
                groups[groupOfCluster[c]].Members.push_back(static_cast<uint32_t>(c));
            }

            const VariableArray<uint8_t> lockByPosition = ComputeGroupBoundaryLocks(mesh, current, groupOfCluster);
            scratch.LevelInputEdges.clear();
            scratch.ClaimedEdges.clear();
            for (const BuildCluster& cluster : current)
            {
                VariableArray<uint64_t> clusterEdges;
                CollectPositionEdges(mesh, nullptr, cluster.Indices.data(), cluster.Indices.size(), clusterEdges);
                scratch.LevelInputEdges.insert(scratch.LevelInputEdges.end(), clusterEdges.begin(), clusterEdges.end());
            }
            std::sort(scratch.LevelInputEdges.begin(), scratch.LevelInputEdges.end());
            for (LevelGroup& group : groups)
            {
                if (group.Members.empty())
                {
                    continue;
                }
                if (!SimplifyGroup(mesh, current, lockByPosition, scratch, group, output, error))
                {
                    return false;
                }
                output.Groups.push_back(std::move(group));
            }
            return true;
        }

        // 切り口(誤差のしきい値)に含まれるクラスタ: 自分の誤差 <= しきい値 < 親の誤差
        bool IsInCut(const BuiltCluster& cluster, float threshold)
        {
            return cluster.Cluster.Error <= threshold && threshold < cluster.ParentError;
        }
    } // namespace

    bool BakeMeshLodDag(const CookedMeshVertex* vertices,
                        size_t vertexCount,
                        const uint32_t* indices,
                        size_t indexCount,
                        CookMeshDagResult& outResult,
                        AnsiString& error,
                        uint32_t fallbackMinTriangles)
    {
        outResult = CookMeshDagResult{};

        WeldedMesh mesh;
        if (!BuildWeldedMesh(vertices, vertexCount, indices, indexCount, mesh, error))
        {
            return false;
        }

        VariableArray<BuildCluster> current;
        BuildClustersFromTriangles(mesh.Indices.data(), mesh.Indices.size(), mesh.Positions.data(), mesh.VertexCount(),
                                   nullptr, current);
        for (BuildCluster& cluster : current)
        {
            ComputeClusterSphere(mesh, cluster);
            ComputeClusterCone(mesh, cluster);
        }

        SimplifyScratch scratch;
        scratch.LocalOfGlobal.assign(mesh.VertexCount(), InvalidIndex);

        VariableArray<BuiltCluster> built;
        VariableArray<CookedMeshClusterGroup> groups;
        uint32_t level = 0;
        uint32_t permissiveGroups = 0;
        uint32_t rejectedGroups = 0;
        VariableArray<uint32_t> levelTriangles;
        while (true)
        {
            uint64_t currentTriangles = 0;
            for (const BuildCluster& cluster : current)
            {
                currentTriangles += cluster.Indices.size() / 3;
            }

            levelTriangles.push_back(static_cast<uint32_t>(currentTriangles));
            // 1 クラスタまで縮んだか、段の上限に達した段は根にする
            bool bMakeRoot = current.size() <= 1 || level + 1 >= Format::MaxLODLevels;
            LevelOutput output;
            if (!bMakeRoot)
            {
                if (!ProcessLevel(mesh, current, scratch, output, error))
                {
                    return false;
                }
                // 次の段が空、または三角形がほとんど減らないなら、簡略化が進まないのでこの段を根にする
                bMakeRoot = output.Next.empty() ||
                            output.NextTriangles * 100 > currentTriangles * (100 - MinProgressPercent);
            }

            if (bMakeRoot)
            {
                for (BuildCluster& cluster : current)
                {
                    BuiltCluster root;
                    root.Cluster = std::move(cluster);
                    root.Level = level;
                    root.bRoot = true;
                    built.push_back(std::move(root));
                }
                break;
            }

            // この段のクラスタを、グループごとの連続した範囲に並べて確定する
            permissiveGroups += output.PermissiveGroups;
            rejectedGroups += output.RejectedGroups;
            // 次の段のクラスタに付いた作ったグループの番号は段の中の通し番号なので、全体の番号へ直す
            // （下の確定は output.Groups の順に番号を振るので、全体の番号 = 確定前のグループ数 + 段の中の番号）
            const uint32_t firstGroupId = static_cast<uint32_t>(groups.size());
            for (BuildCluster& next : output.Next)
            {
                if (next.SourceGroup != Format::InvalidGroupId)
                {
                    next.SourceGroup += firstGroupId;
                }
            }
            for (LevelGroup& group : output.Groups)
            {
                const uint32_t groupId = static_cast<uint32_t>(groups.size());
                CookedMeshClusterGroup record;
                record.BoundsCenter = group.Center;
                record.BoundsRadius = group.Radius;
                record.Error = group.Error;
                record.ClusterOffset = static_cast<uint32_t>(built.size());
                record.ClusterCount = static_cast<uint32_t>(group.Members.size());
                record.LODLevel = level;
                groups.push_back(record);
                for (const uint32_t member : group.Members)
                {
                    BuiltCluster child;
                    child.Cluster = std::move(current[member]);
                    child.Level = level;
                    child.GroupId = groupId;
                    child.ParentCenter = group.Center;
                    child.ParentRadius = group.Radius;
                    child.ParentError = group.Error;
                    built.push_back(std::move(child));
                }
            }
            current = std::move(output.Next);
            ++level;
        }

        // 書き出しの形へ。クラスタごとに自分の頂点の範囲(最大 128 頂点)とクラスタ内の相対の索引を持たせる。
        Core::Asset::CookedMeshV1WriteInput& out = outResult.Output;
        out.LODLevelCount = level + 1;
        out.Groups = std::move(groups);
        uint32_t rootCount = 0;
        for (const BuiltCluster& entry : built)
        {
            const BuildCluster& source = entry.Cluster;
            if (source.Indices.empty() || source.Indices.size() / 3 > ClusterMaxTriangles)
            {
                error = "クラスタの三角形数が範囲外です";
                return false;
            }

            unsigned int localVertices[ClusterMaxVertices * 2] = {};
            unsigned char localTriangles[ClusterMaxTriangles * 3] = {};
            const size_t localCount = meshopt_extractMeshletIndices(localVertices, localTriangles, source.Indices.data(),
                                                                    source.Indices.size());
            if (localCount == 0 || localCount > ClusterMaxVertices)
            {
                error = "クラスタの頂点数が範囲外です";
                return false;
            }

            CookedMeshCluster record;
            record.BoundsCenter = source.Center;
            record.BoundsRadius = source.Radius;
            record.LODError = source.Error;
            record.GroupId = entry.GroupId;
            record.SourceGroupId = source.SourceGroup;
            record.ParentBoundsCenter = entry.ParentCenter;
            record.ParentBoundsRadius = entry.ParentRadius;
            record.ParentError = entry.ParentError;
            record.LODLevel = entry.Level;
            record.ConeAxis = source.ConeAxis;
            record.ConeCutoff = source.ConeCutoff;
            record.IndexOffset = static_cast<uint32_t>(out.ClusterIndices.size());
            record.IndexCount = static_cast<uint32_t>(source.Indices.size());
            record.VertexOffset = static_cast<uint32_t>(out.Vertices.size());
            record.VertexCount = static_cast<uint32_t>(localCount);
            record.bIsRoot = entry.bRoot;
            rootCount += entry.bRoot ? 1u : 0u;
            for (size_t local = 0; local < localCount; ++local)
            {
                out.Vertices.push_back(mesh.Vertices[localVertices[local]]);
            }
            for (size_t i = 0; i < source.Indices.size(); ++i)
            {
                out.ClusterIndices.push_back(localTriangles[i]);
            }
            out.Clusters.push_back(record);
        }

        // フォールバックの段: 誤差のしきい値で切ったクラスタの三角形の集まり。三角形数が目標以下になる最小のしきい値を探す。
        // 切り口の三角形数 = (自分の誤差 <= T のクラスタの三角形数) - (親の誤差 <= T のクラスタの三角形数)
        struct ErrorEdge
        {
            float Value = 0.0f;
            uint32_t Triangles = 0;
        };
        VariableArray<ErrorEdge> selfEdges;
        VariableArray<ErrorEdge> parentEdges;
        VariableArray<float> candidates;
        uint64_t totalTriangles = 0;
        for (const BuiltCluster& entry : built)
        {
            const uint32_t triangles = static_cast<uint32_t>(entry.Cluster.Indices.size() / 3);
            selfEdges.push_back({entry.Cluster.Error, triangles});
            parentEdges.push_back({entry.ParentError, triangles});
            candidates.push_back(entry.Cluster.Error);
            if (entry.Level == 0)
            {
                totalTriangles += triangles;
            }
        }
        const auto byValue = [](const ErrorEdge& lhs, const ErrorEdge& rhs) { return lhs.Value < rhs.Value; };
        std::sort(selfEdges.begin(), selfEdges.end(), byValue);
        std::sort(parentEdges.begin(), parentEdges.end(), byValue);
        std::sort(candidates.begin(), candidates.end());
        candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());

        // 値の昇順に並べた辺の、しきい値以下の三角形数の累計(先頭に 0 を置いた累計表を二分探索する)
        const auto makeCumulative = [](const VariableArray<ErrorEdge>& edges) {
            VariableArray<uint64_t> cumulative;
            cumulative.reserve(edges.size() + 1);
            cumulative.push_back(0);
            for (const ErrorEdge& edge : edges)
            {
                cumulative.push_back(cumulative.back() + edge.Triangles);
            }
            return cumulative;
        };
        const auto trianglesUpTo = [](const VariableArray<ErrorEdge>& edges, const VariableArray<uint64_t>& cumulative,
                                      float threshold) {
            const auto upper = std::upper_bound(edges.begin(), edges.end(), threshold,
                                                [](float value, const ErrorEdge& edge) { return value < edge.Value; });
            return cumulative[static_cast<size_t>(upper - edges.begin())];
        };
        const VariableArray<uint64_t> selfCumulative = makeCumulative(selfEdges);
        const VariableArray<uint64_t> parentCumulative = makeCumulative(parentEdges);
        const uint64_t target = std::max<uint64_t>(
            std::min<uint64_t>(totalTriangles / FallbackReductionDivisor, FallbackMaxTriangles),
            std::min<uint64_t>(fallbackMinTriangles, FallbackMinOverrideMaxTriangles));
        float threshold = candidates.back();
        for (const float candidate : candidates)
        {
            if (trianglesUpTo(selfEdges, selfCumulative, candidate) -
                    trianglesUpTo(parentEdges, parentCumulative, candidate) <=
                target)
            {
                threshold = candidate;
                break;
            }
        }

        float fallbackError = 0.0f;
        uint32_t fallbackTriangles = 0;
        for (size_t i = 0; i < built.size(); ++i)
        {
            if (!IsInCut(built[i], threshold))
            {
                continue;
            }
            const CookedMeshCluster& record = out.Clusters[i];
            for (uint32_t k = 0; k < record.IndexCount; ++k)
            {
                out.FallbackIndices.push_back(record.VertexOffset + out.ClusterIndices[record.IndexOffset + k]);
            }
            fallbackError = std::max(fallbackError, built[i].Cluster.Error);
            fallbackTriangles += record.IndexCount / 3;
        }
        if (out.FallbackIndices.empty())
        {
            error = "フォールバックの段に三角形が残りませんでした";
            return false;
        }
        out.FallbackError = fallbackError;

        EncloseSpheres(mesh.Positions.data(), nullptr, mesh.VertexCount(), out.TotalBoundsCenter, out.TotalBoundsRadius);

        CookMeshDagStats& stats = outResult.Stats;
        stats.SourceTriangles = static_cast<uint32_t>(totalTriangles);
        stats.WeldedVertices = static_cast<uint32_t>(mesh.VertexCount());
        stats.LODLevelCount = out.LODLevelCount;
        stats.ClusterCount = static_cast<uint32_t>(out.Clusters.size());
        stats.GroupCount = static_cast<uint32_t>(out.Groups.size());
        stats.RootClusterCount = rootCount;
        stats.PermissiveGroupCount = permissiveGroups;
        stats.RejectedGroupCount = rejectedGroups;
        stats.LevelTriangles = std::move(levelTriangles);
        stats.FallbackTriangles = fallbackTriangles;
        stats.FallbackTargetTriangles = static_cast<uint32_t>(target);
        stats.bReachedSingleRoot = rootCount == 1;
        return true;
    }
}
