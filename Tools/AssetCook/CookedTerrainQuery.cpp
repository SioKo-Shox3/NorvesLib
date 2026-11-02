#include "CookedTerrainQuery.h"

#include "AssetCookOutput.h"
#include "TerrainCliffDetail.h"
#include "Asset/AssetFileReader.h"
#include "Asset/AssetSystem.h"
#include "Asset/CookedMeshFormat.h"
#include "Container/String.h"
#include "Container/VariableArray.h"
#include "Rendering/Terrain/TerrainHeightField.h"
#include "Rendering/Terrain/TerrainPlacementGround.h"
#include "Debug/Stats.h"
#include "Logging/LogMacros.h"
#include "Text/JsonDocument.h"

#include <algorithm>
#include <bit>
#include <compare>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string_view>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace NorvesLib::Tools::AssetCook
{
    namespace
    {
        namespace Asset = NorvesLib::Core::Asset;
        namespace Container = NorvesLib::Core::Container;
        namespace Terrain = NorvesLib::Core::Rendering::Terrain;
        using NorvesLib::Core::JsonDocument;
        using NorvesLib::Core::JsonValue;

        /// 点の結果の状態。高さを返せないときは黙って 0 を返さず、ここのどれかを返す
        enum class SampleStatus : uint8_t
        {
            Ok,
            // X/Z が DEM の格子の外
            OutOfRange,
            // 区画のクック済みのパッケージがマニフェストに無い・読めない
            PackageMissing,
            // パッケージはあるがメッシュを解析できない・クラスタの範囲が壊れている
            MeshInvalid,
            // メッシュは正しいが、鉛直の線と交わる三角形が無い（面の穴）
            NoHit
        };

        const char *StatusText(SampleStatus status)
        {
            switch (status)
            {
            case SampleStatus::Ok:
                return "ok";
            case SampleStatus::OutOfRange:
                return "out_of_range";
            case SampleStatus::PackageMissing:
                return "package_missing";
            case SampleStatus::MeshInvalid:
                return "mesh_invalid";
            case SampleStatus::NoHit:
                return "no_hit";
            }
            return "unknown";
        }

        /// 鉛直の線と三角形の交点 1 つ
        struct SurfaceHit
        {
            double Height = 0.0;
            // 三角形の面の法線（巻きの向きから求めた表の向き。長さ 1）
            double Normal[3] = {};
            // 区画のメッシュのインデックスの列での三角形の番号（インデックスの位置 ÷ 3）
            uint64_t Triangle = 0;
            uint32_t Cluster = 0;
            // 交点を持つ三角形の区画（継ぎ目の近くの点は隣の区画の三角形とも交わる）
            uint32_t TileX = 0;
            uint32_t TileZ = 0;
            // ほかの区画の三角形の交点も同じ高さとしてまとめたか（両側の区画が共有する境の上の点）
            bool bShared = false;
        };

        struct QueryPoint
        {
            double X = 0.0;
            double Z = 0.0;
            uint32_t TileX = 0;
            uint32_t TileZ = 0;
            double DemHeight = 0.0;
            SampleStatus Status = SampleStatus::OutOfRange;
            Container::VariableArray<SurfaceHit> Hits;
        };

        /// 開いた縁の辺の両端の位置（X0, Y0, Z0, X1, Y1, Z1）
        struct BoundaryEdge
        {
            float Ends[6] = {};
        };

        struct TileReport
        {
            uint32_t TileX = 0;
            uint32_t TileZ = 0;
            Container::AnsiString LogicalPath;
            SampleStatus Status = SampleStatus::Ok;
            Container::AnsiString Reason;
            bool bCliffDetail = false;
            bool bCarve = false;
            uint32_t FinestLod = 0;
            uint32_t FinestClusters = 0;
            uint64_t FinestTriangles = 0;
            // 最も細かい段の開いた縁の辺（1 つの三角形だけが使う辺）の両端の位置。--boundary-edges のときだけ集める
            Container::VariableArray<BoundaryEdge> BoundaryEdges;
        };

        /// 同じ高さの交点（三角形の共有する辺・頂点の上を線が通る）を 1 つにまとめる差（m）
        constexpr double kMergeTolerance = 1.0e-4;
        /// 三角形の内外の判定の重心座標の許容（辺・頂点の上の点を落とさない）
        constexpr double kBarycentricTolerance = 1.0e-7;
        /// 区画の境から隣の区画のメッシュも調べる幅（m）。崖の細部の押し出しが頂点を水平へ動かす量（境の上で最大 0.77 m）より広く取る
        constexpr double kSeamReachMeters = 2.0;

        bool ReadFileBytes(const char *path, Container::VariableArray<uint8_t> &outBytes)
        {
            std::ifstream stream(std::filesystem::path(path), std::ios::binary);
            if (!stream)
            {
                return false;
            }
            stream.seekg(0, std::ios::end);
            const std::streamoff size = stream.tellg();
            if (size < 0)
            {
                return false;
            }
            stream.seekg(0, std::ios::beg);
            outBytes.resize(static_cast<size_t>(size));
            return size == 0 || static_cast<bool>(stream.read(reinterpret_cast<char *>(outBytes.data()), size));
        }

        Container::Span<const uint8_t> WithoutBom(const Container::VariableArray<uint8_t> &bytes)
        {
            Container::Span<const uint8_t> text(bytes.data(), bytes.size());
            if (text.size() >= 3 && text[0] == 0xefu && text[1] == 0xbbu && text[2] == 0xbfu)
            {
                text = {text.data() + 3, text.size() - 3};
            }
            return text;
        }

        /// 点の一覧を読む。{"points": [[x, z], ...]} か、要素が {"x": .., "z": ..} の配列（最上位の配列も可）
        bool ReadPoints(const char *path, Container::VariableArray<QueryPoint> &outPoints)
        {
            Container::VariableArray<uint8_t> bytes;
            if (!ReadFileBytes(path, bytes))
            {
                std::cerr << "AssetCook error: 点の一覧のファイルを開けません: " << path << "\n";
                return false;
            }
            JsonDocument document;
            if (!JsonDocument::TryParseUtf8(WithoutBom(bytes), document))
            {
                std::cerr << "AssetCook error: 点の一覧が JSON として読めません: " << path << "\n";
                return false;
            }
            JsonValue list = document.GetRoot();
            if (list.IsObject())
            {
                list = list.FindMember("points");
            }
            if (!list.IsArray())
            {
                std::cerr << "AssetCook error: 点の一覧は {\"points\": [[x, z], ...]} の形で渡してください: " << path << "\n";
                return false;
            }
            for (size_t index = 0; index < list.GetArraySize(); ++index)
            {
                const JsonValue element = list.GetArrayElement(index);
                JsonValue x;
                JsonValue z;
                if (element.IsArray() && element.GetArraySize() == 2)
                {
                    x = element.GetArrayElement(0);
                    z = element.GetArrayElement(1);
                }
                else if (element.IsObject())
                {
                    x = element.FindMember("x");
                    z = element.FindMember("z");
                }
                if (!x.IsNumber() || !z.IsNumber() || !std::isfinite(x.AsNumber()) || !std::isfinite(z.AsNumber()))
                {
                    std::cerr << "AssetCook error: 点の一覧の " << index << " 番目は有限の x・z ではありません\n";
                    return false;
                }
                QueryPoint point;
                point.X = x.AsNumber();
                point.Z = z.AsNumber();
                outPoints.push_back(std::move(point));
            }
            return true;
        }

        /// 点の X/Z を含む DEM の区画を求める。格子の外なら false。区画の境の上の点は東・南の区画に入れる（最後の区画を除く）
        bool FindTile(const Terrain::TerrainGridLayout &layout, double x, double z, uint32_t &outTileX, uint32_t &outTileZ)
        {
            const double column = (x - layout.FirstX) / layout.Spacing;
            const double row = (z - layout.FirstZ) / layout.Spacing;
            if (!(column >= 0.0 && column <= static_cast<double>(layout.Columns - 1u) && row >= 0.0 &&
                  row <= static_cast<double>(layout.Rows - 1u)))
            {
                return false;
            }
            const uint32_t cellX = std::min(static_cast<uint32_t>(column), layout.Columns - 2u);
            const uint32_t cellZ = std::min(static_cast<uint32_t>(row), layout.Rows - 2u);
            outTileX = std::min(cellX / Terrain::WastelandTerrain::kDemTileCells,
                                Terrain::WastelandTerrain::DemTileCount(layout.Columns) - 1u);
            outTileZ = std::min(cellZ / Terrain::WastelandTerrain::kDemTileCells,
                                Terrain::WastelandTerrain::DemTileCount(layout.Rows) - 1u);
            return true;
        }

        /// 区画のメッシュの最も細かい段の三角形と、指した点の鉛直の線の交点を集める。クラスタの範囲が壊れていれば false
        bool IntersectTile(const Asset::CookedMeshData &mesh, Container::VariableArray<QueryPoint> &points,
                           const Container::VariableArray<size_t> &pointIndices, TileReport &report, bool bSupportOnly = false,
                           bool bHighestOnly = false)
        {
            uint32_t finestLod = UINT32_MAX;
            for (const Asset::CookedMeshCluster &cluster : mesh.Clusters)
            {
                finestLod = std::min(finestLod, cluster.LODLevel);
            }
            if (finestLod == UINT32_MAX)
            {
                report.Reason = "クラスタがありません";
                return false;
            }
            report.FinestLod = finestLod;

            // 最も細かい段のクラスタの X/Z の範囲（頂点から求める。境界球は押し出しの後の形を緩く包むだけなので使わない）
            struct ClusterBox
            {
                uint32_t Cluster = 0;
                double MinX = 0.0;
                double MaxX = 0.0;
                double MinZ = 0.0;
                double MaxZ = 0.0;
            };
            Container::VariableArray<ClusterBox> boxes;
            for (uint32_t clusterIndex = 0; clusterIndex < mesh.Clusters.size(); ++clusterIndex)
            {
                const Asset::CookedMeshCluster &cluster = mesh.Clusters[clusterIndex];
                if (cluster.LODLevel != finestLod)
                {
                    continue;
                }
                if (static_cast<uint64_t>(cluster.IndexOffset) + cluster.IndexCount > mesh.Indices.size() ||
                    static_cast<uint64_t>(cluster.VertexOffset) + cluster.VertexCount > mesh.Vertices.size() ||
                    cluster.IndexCount % 3u != 0u || cluster.VertexCount == 0u)
                {
                    report.Reason = "クラスタの範囲がメッシュの外を指します";
                    return false;
                }
                ClusterBox box;
                box.Cluster = clusterIndex;
                box.MinX = box.MinZ = std::numeric_limits<double>::max();
                box.MaxX = box.MaxZ = -std::numeric_limits<double>::max();
                for (uint32_t i = 0; i < cluster.IndexCount; ++i)
                {
                    const uint32_t local = mesh.Indices[cluster.IndexOffset + i];
                    if (local >= cluster.VertexCount)
                    {
                        report.Reason = "クラスタのインデックスが頂点の範囲の外です";
                        return false;
                    }
                    const Asset::CookedMeshVertex &vertex = mesh.Vertices[cluster.VertexOffset + local];
                    if (!std::isfinite(vertex.Position.X) || !std::isfinite(vertex.Position.Y) ||
                        !std::isfinite(vertex.Position.Z))
                    {
                        report.Reason = "頂点の位置が有限ではありません";
                        return false;
                    }
                    box.MinX = std::min(box.MinX, static_cast<double>(vertex.Position.X));
                    box.MaxX = std::max(box.MaxX, static_cast<double>(vertex.Position.X));
                    box.MinZ = std::min(box.MinZ, static_cast<double>(vertex.Position.Z));
                    box.MaxZ = std::max(box.MaxZ, static_cast<double>(vertex.Position.Z));
                }
                ++report.FinestClusters;
                report.FinestTriangles += cluster.IndexCount / 3u;
                boxes.push_back(box);
            }
            if (boxes.empty())
            {
                report.Reason = "最も細かい段のクラスタがありません";
                return false;
            }

            // クラスタの範囲を X/Z の升目（kBucketMeters 角）へ振り分け、点はその升目に掛かるクラスタだけを調べる
            constexpr double kBucketMeters = 8.0;
            double minX = boxes[0].MinX;
            double minZ = boxes[0].MinZ;
            double maxX = boxes[0].MaxX;
            double maxZ = boxes[0].MaxZ;
            for (const ClusterBox &box : boxes)
            {
                minX = std::min(minX, box.MinX);
                minZ = std::min(minZ, box.MinZ);
                maxX = std::max(maxX, box.MaxX);
                maxZ = std::max(maxZ, box.MaxZ);
            }
            const uint32_t bucketsX = static_cast<uint32_t>((maxX - minX) / kBucketMeters) + 1u;
            const uint32_t bucketsZ = static_cast<uint32_t>((maxZ - minZ) / kBucketMeters) + 1u;
            const auto bucketOf = [&](double value, double origin, uint32_t count)
            {
                const double cell = std::floor((value - origin) / kBucketMeters);
                return static_cast<uint32_t>(std::clamp(cell, 0.0, static_cast<double>(count - 1u)));
            };
            Container::VariableArray<Container::VariableArray<uint32_t>> buckets(static_cast<size_t>(bucketsX) * bucketsZ);
            for (uint32_t boxIndex = 0; boxIndex < boxes.size(); ++boxIndex)
            {
                const ClusterBox &box = boxes[boxIndex];
                const uint32_t x0 = bucketOf(box.MinX - 1.0e-6, minX, bucketsX);
                const uint32_t x1 = bucketOf(box.MaxX + 1.0e-6, minX, bucketsX);
                const uint32_t z0 = bucketOf(box.MinZ - 1.0e-6, minZ, bucketsZ);
                const uint32_t z1 = bucketOf(box.MaxZ + 1.0e-6, minZ, bucketsZ);
                for (uint32_t bz = z0; bz <= z1; ++bz)
                {
                    for (uint32_t bx = x0; bx <= x1; ++bx)
                    {
                        buckets[static_cast<size_t>(bz) * bucketsX + bx].push_back(boxIndex);
                    }
                }
            }

            for (const size_t pointIndex : pointIndices)
            {
                QueryPoint &point = points[pointIndex];
                const double px = point.X;
                const double pz = point.Z;
                const Container::VariableArray<uint32_t> &candidates =
                    buckets[static_cast<size_t>(bucketOf(pz, minZ, bucketsZ)) * bucketsX + bucketOf(px, minX, bucketsX)];
                for (const uint32_t boxIndex : candidates)
                {
                    const ClusterBox &box = boxes[boxIndex];
                    if (px < box.MinX - 1.0e-6 || px > box.MaxX + 1.0e-6 || pz < box.MinZ - 1.0e-6 ||
                        pz > box.MaxZ + 1.0e-6)
                    {
                        continue;
                    }
                    const Asset::CookedMeshCluster &cluster = mesh.Clusters[box.Cluster];
                    for (uint32_t i = 0; i < cluster.IndexCount; i += 3u)
                    {
                        const Asset::CookedMeshVertex &a = mesh.Vertices[cluster.VertexOffset + mesh.Indices[cluster.IndexOffset + i]];
                        const Asset::CookedMeshVertex &b =
                            mesh.Vertices[cluster.VertexOffset + mesh.Indices[cluster.IndexOffset + i + 1u]];
                        const Asset::CookedMeshVertex &c =
                            mesh.Vertices[cluster.VertexOffset + mesh.Indices[cluster.IndexOffset + i + 2u]];
                        const double ax = a.Position.X;
                        const double az = a.Position.Z;
                        const double bx = b.Position.X;
                        const double bz = b.Position.Z;
                        const double cx = c.Position.X;
                        const double cz = c.Position.Z;
                        // X/Z の平面へ写した三角形の重心座標。写した面積が 0（鉛直の三角形）は線と平行なので交点を持たない
                        const double denominator = (bz - cz) * (ax - cx) + (cx - bx) * (az - cz);
                        if (std::fabs(denominator) < 1.0e-12)
                        {
                            continue;
                        }
                        const double wa = ((bz - cz) * (px - cx) + (cx - bx) * (pz - cz)) / denominator;
                        const double wb = ((cz - az) * (px - cx) + (ax - cx) * (pz - cz)) / denominator;
                        const double wc = 1.0 - wa - wb;
                        if (wa < -kBarycentricTolerance || wb < -kBarycentricTolerance || wc < -kBarycentricTolerance)
                        {
                            continue;
                        }
                        SurfaceHit hit;
                        hit.Height = wa * a.Position.Y + wb * b.Position.Y + wc * c.Position.Y;
                        const double e1[3] = {bx - ax, static_cast<double>(b.Position.Y) - a.Position.Y, bz - az};
                        const double e2[3] = {cx - ax, static_cast<double>(c.Position.Y) - a.Position.Y, cz - az};
                        double n[3] = {e2[1] * e1[2] - e2[2] * e1[1], e2[2] * e1[0] - e2[0] * e1[2],
                                       e2[0] * e1[1] - e2[1] * e1[0]};
                        const double length = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                        for (uint32_t axis = 0; axis < 3; ++axis)
                        {
                            hit.Normal[axis] = length > 0.0 ? n[axis] / length : 0.0;
                        }
                        hit.Triangle = (static_cast<uint64_t>(cluster.IndexOffset) + i) / 3u;
                        hit.Cluster = box.Cluster;
                        hit.TileX = report.TileX;
                        hit.TileZ = report.TileZ;
                        if (bSupportOnly || bHighestOnly)
                        {
                            // 接地用は上向きの面から指定した高さに最も近い交点、または最上面だけを保持する。
                            if ((bSupportOnly && hit.Normal[1] < 0.3) ||
                                (!point.Hits.empty() &&
                                 (bHighestOnly ? point.Hits[0].Height >= hit.Height
                                               : std::fabs(point.Hits[0].Height - point.DemHeight) <=
                                                     std::fabs(hit.Height - point.DemHeight))))
                            {
                                continue;
                            }
                            if (point.Hits.empty())
                            {
                                point.Hits.push_back(hit);
                            }
                            else
                            {
                                point.Hits[0] = hit;
                            }
                        }
                        else
                        {
                            point.Hits.push_back(hit);
                        }
                    }
                }
            }
            return true;
        }

        /// 最も細かい段の三角形の辺のうち、1 つの三角形だけが使う辺（区画の外周の縁）を集める。クラスタは頂点を別に持つので、
        /// 辺は頂点の番号ではなく位置のビットで照らす（同じ元の頂点はクラスタをまたいでビット単位で同じ位置）
        void CollectBoundaryEdges(const Asset::CookedMeshData &mesh, uint32_t finestLod,
                                  Container::VariableArray<BoundaryEdge> &outEdges)
        {
            // 辺の両端の位置のビット（小さい端を先に）
            struct EdgeKey
            {
                uint32_t Bits[6] = {};
                auto operator<=>(const EdgeKey &) const = default;
            };
            const auto bitsOf = [&mesh](const Asset::CookedMeshCluster &cluster, uint32_t index, uint32_t *outBits)
            {
                const Asset::CookedMeshVertex &vertex = mesh.Vertices[cluster.VertexOffset + mesh.Indices[cluster.IndexOffset + index]];
                outBits[0] = std::bit_cast<uint32_t>(vertex.Position.X);
                outBits[1] = std::bit_cast<uint32_t>(vertex.Position.Y);
                outBits[2] = std::bit_cast<uint32_t>(vertex.Position.Z);
            };
            Container::VariableArray<EdgeKey> edges;
            for (const Asset::CookedMeshCluster &cluster : mesh.Clusters)
            {
                if (cluster.LODLevel != finestLod)
                {
                    continue;
                }
                for (uint32_t i = 0; i < cluster.IndexCount; i += 3u)
                {
                    uint32_t corners[3][3] = {};
                    for (uint32_t k = 0; k < 3u; ++k)
                    {
                        bitsOf(cluster, i + k, corners[k]);
                    }
                    for (uint32_t k = 0; k < 3u; ++k)
                    {
                        const uint32_t *first = corners[k];
                        const uint32_t *second = corners[(k + 1u) % 3u];
                        if (std::lexicographical_compare(second, second + 3, first, first + 3))
                        {
                            std::swap(first, second);
                        }
                        EdgeKey key;
                        std::copy(first, first + 3, key.Bits);
                        std::copy(second, second + 3, key.Bits + 3);
                        edges.push_back(key);
                    }
                }
            }
            std::sort(edges.begin(), edges.end());
            for (size_t begin = 0; begin < edges.size();)
            {
                size_t end = begin + 1u;
                while (end < edges.size() && edges[end] == edges[begin])
                {
                    ++end;
                }
                if (end - begin == 1u)
                {
                    BoundaryEdge edge;
                    for (uint32_t k = 0; k < 6u; ++k)
                    {
                        edge.Ends[k] = std::bit_cast<float>(edges[begin].Bits[k]);
                    }
                    outEdges.push_back(edge);
                }
                begin = end;
            }
        }

        /// 点の交点を上から順に並べ、共有する辺・頂点の上で重なった同じ高さの交点を 1 つにまとめる
        /// （点を含む区画の三角形を先に、同じ区画なら番号の小さい三角形を残す）
        void MergeHits(QueryPoint &point)
        {
            const auto rank = [&point](const SurfaceHit &hit)
            { return hit.TileX == point.TileX && hit.TileZ == point.TileZ ? 0u : 1u; };
            const auto before = [&rank](const SurfaceHit &lhs, const SurfaceHit &rhs)
            {
                if (rank(lhs) != rank(rhs))
                {
                    return rank(lhs) < rank(rhs);
                }
                if (lhs.TileZ != rhs.TileZ || lhs.TileX != rhs.TileX)
                {
                    return lhs.TileZ != rhs.TileZ ? lhs.TileZ < rhs.TileZ : lhs.TileX < rhs.TileX;
                }
                return lhs.Triangle < rhs.Triangle;
            };
            std::sort(point.Hits.begin(), point.Hits.end(),
                      [&before](const SurfaceHit &lhs, const SurfaceHit &rhs)
                      { return lhs.Height != rhs.Height ? lhs.Height > rhs.Height : before(lhs, rhs); });
            Container::VariableArray<SurfaceHit> merged;
            for (const SurfaceHit &hit : point.Hits)
            {
                if (!merged.empty() && merged.back().Height - hit.Height <= kMergeTolerance)
                {
                    const bool bShared =
                        merged.back().bShared || hit.TileX != merged.back().TileX || hit.TileZ != merged.back().TileZ;
                    if (before(hit, merged.back()))
                    {
                        const double height = merged.back().Height;
                        merged.back() = hit;
                        merged.back().Height = height;
                    }
                    merged.back().bShared = bShared;
                    continue;
                }
                merged.push_back(hit);
            }
            point.Hits = std::move(merged);
        }

        /// 文字列を UTF-16 の符号単位へ直す。この実行ファイルの narrow な文字列（リテラル・argv・Core の理由）は
        /// 実行文字コード（ANSI のコードページ）なので先にそれで読み、読めなければ UTF-8（マニフェスト由来）で読む。
        /// どちらでも読めなければ false
        bool DecodeNarrowText(std::string_view text, Container::VariableArray<wchar_t> &outUnits)
        {
            outUnits.clear();
            if (text.empty())
            {
                return true;
            }
#if defined(_WIN32)
            for (const UINT codePage : {static_cast<UINT>(CP_ACP), static_cast<UINT>(CP_UTF8)})
            {
                const int length = MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS, text.data(),
                                                       static_cast<int>(text.size()), nullptr, 0);
                if (length <= 0)
                {
                    continue;
                }
                outUnits.resize(static_cast<size_t>(length));
                if (MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                                        outUnits.data(), length) == length)
                {
                    return true;
                }
            }
            outUnits.clear();
#endif
            return false;
        }

        /// JSON の文字列を書く。ASCII の外は \uXXXX にし、出力を常に ASCII（したがって正しい UTF-8）に保つ
        void WriteJsonString(std::string_view text)
        {
            const auto writeEscaped = [](unsigned int code)
            {
                char escaped[8] = {};
                std::snprintf(escaped, sizeof(escaped), "\\u%04x", code);
                std::cout << escaped;
            };
            Container::VariableArray<wchar_t> units;
            const bool bDecoded = DecodeNarrowText(text, units);
            std::cout << '"';
            const size_t count = bDecoded ? units.size() : text.size();
            for (size_t index = 0; index < count; ++index)
            {
                // 読めない文字列は ASCII の外の 1 バイトごとに置換文字（U+FFFD）にする
                const unsigned int code = bDecoded ? static_cast<unsigned int>(units[index])
                                                   : static_cast<unsigned char>(text[index]);
                if (code == '"' || code == '\\')
                {
                    std::cout << '\\' << static_cast<char>(code);
                }
                else if (code < 0x20u)
                {
                    writeEscaped(code);
                }
                else if (code >= 0x80u)
                {
                    writeEscaped(bDecoded ? code : 0xFFFDu);
                }
                else
                {
                    std::cout << static_cast<char>(code);
                }
            }
            std::cout << '"';
        }

        void WriteNumber(double value)
        {
            char text[40] = {};
            std::snprintf(text, sizeof(text), "%.10g", value);
            std::cout << text;
        }
    } // namespace

    bool RunCookedTerrainSampleCommand(int argc, char **argv, int &outExitCode)
    {
        if (argc < 2 || std::string_view(argv[1]) != "--sample-cooked-terrain")
        {
            return false;
        }
        outExitCode = 1;
        const char *manifestPath = nullptr;
        const char *pointsPath = nullptr;
        // 遠景の段の名前（例: castle）。渡すと DEM の区画の代わりに、その段の区画を読む
        const char *horizonLevel = nullptr;
        // 読んだ区画ごとに最も細かい段の開いた縁の辺も書き出す（継ぎ目の両側の縁が同じ辺かを照らすため）
        bool bBoundaryEdges = false;
        for (int index = 2; index < argc; ++index)
        {
            const std::string_view option(argv[index]);
            if (option == "--boundary-edges")
            {
                bBoundaryEdges = true;
                continue;
            }
            if ((option == "--manifest" || option == "--points") && index + 1 < argc)
            {
                (option == "--manifest" ? manifestPath : pointsPath) = argv[++index];
                continue;
            }
            if (option == "--horizon-level" && index + 1 < argc)
            {
                horizonLevel = argv[++index];
                continue;
            }
            std::cerr << "AssetCook error: --sample-cooked-terrain の引数が不正です: " << argv[index] << "\n";
            return true;
        }
        if (manifestPath == nullptr || pointsPath == nullptr)
        {
            std::cerr << "AssetCook error: --sample-cooked-terrain には --manifest <manifest.json> と "
                         "--points <点の一覧の JSON> を渡してください\n";
            return true;
        }

        Container::VariableArray<QueryPoint> points;
        if (!ReadPoints(pointsPath, points))
        {
            return true;
        }

        Container::AnsiString loadError;
        const auto field = Terrain::WastelandTerrain::LoadDemGrid(loadError);
        if (!field)
        {
            std::cerr << "AssetCook error: " << loadError.c_str() << "\n";
            return true;
        }
        const Terrain::TerrainGridLayout layout = field->GetGridLayout();
        if (layout.Columns < 2u || layout.Rows < 2u || !(layout.Spacing > 0.0))
        {
            std::cerr << "AssetCook error: DEM の格子の大きさが不正です\n";
            return true;
        }

        Container::VariableArray<uint8_t> manifestBytes;
        if (!ReadFileBytes(manifestPath, manifestBytes))
        {
            std::cerr << "AssetCook error: マニフェストを開けません: " << manifestPath << "\n";
            return true;
        }
        const Container::Span<const uint8_t> manifestText = WithoutBom(manifestBytes);
        const std::filesystem::path manifestParent = std::filesystem::absolute(std::filesystem::path(manifestPath)).parent_path();
        Asset::AssetSystem system{Container::AnsiString(manifestParent.generic_string().c_str())};
        if (!system.LoadManifestFromJsonText(
                Detail::ToCoreString(Container::AnsiStringView(reinterpret_cast<const char *>(manifestText.data()),
                                                               manifestText.size())),
                manifestPath))
        {
            std::cerr << "AssetCook error: マニフェストを読めません: " << manifestPath << "\n";
            return true;
        }

        // 点を区画ごとに分け、区画ごとに 1 回だけパッケージを読む
        const uint32_t tilesX = Terrain::WastelandTerrain::DemTileCount(layout.Columns);
        const uint32_t tilesZ = Terrain::WastelandTerrain::DemTileCount(layout.Rows);
        Container::VariableArray<Container::VariableArray<size_t>> tilePoints(static_cast<size_t>(tilesX) * tilesZ);
        for (size_t index = 0; index < points.size(); ++index)
        {
            QueryPoint &point = points[index];
            if (!FindTile(layout, point.X, point.Z, point.TileX, point.TileZ))
            {
                point.Status = SampleStatus::OutOfRange;
                continue;
            }
            point.Status = SampleStatus::NoHit;
            tilePoints[static_cast<size_t>(point.TileZ) * tilesX + point.TileX].push_back(index);
            // 崖の細部の押し出しは区画の境の頂点も X/Z へ動かす（境の頂点は両側の区画で同じ位置）。境が隣へずれた所の面は
            // 隣の区画のメッシュにあるので、境から kSeamReachMeters 以内の点は隣の区画の三角形とも交わりを調べる
            for (uint32_t tileZ = point.TileZ > 0u ? point.TileZ - 1u : 0u; tileZ <= std::min(point.TileZ + 1u, tilesZ - 1u);
                 ++tileZ)
            {
                for (uint32_t tileX = point.TileX > 0u ? point.TileX - 1u : 0u;
                     tileX <= std::min(point.TileX + 1u, tilesX - 1u); ++tileX)
                {
                    if (tileX == point.TileX && tileZ == point.TileZ)
                    {
                        continue;
                    }
                    uint32_t firstColumn = 0;
                    uint32_t cellsX = 0;
                    uint32_t firstRow = 0;
                    uint32_t cellsZ = 0;
                    Terrain::WastelandTerrain::DemTileCellRange(tileX, layout.Columns, firstColumn, cellsX);
                    Terrain::WastelandTerrain::DemTileCellRange(tileZ, layout.Rows, firstRow, cellsZ);
                    const double minX = layout.FirstX + static_cast<double>(firstColumn) * layout.Spacing;
                    const double maxX = layout.FirstX + static_cast<double>(firstColumn + cellsX) * layout.Spacing;
                    const double minZ = layout.FirstZ + static_cast<double>(firstRow) * layout.Spacing;
                    const double maxZ = layout.FirstZ + static_cast<double>(firstRow + cellsZ) * layout.Spacing;
                    if (point.X >= minX - kSeamReachMeters && point.X <= maxX + kSeamReachMeters &&
                        point.Z >= minZ - kSeamReachMeters && point.Z <= maxZ + kSeamReachMeters)
                    {
                        tilePoints[static_cast<size_t>(tileZ) * tilesX + tileX].push_back(index);
                    }
                }
            }
        }

        // 区画のパッケージを読み、指した点の交点を集める。失敗の種類を返す
        const auto intersectPackage = [&](TileReport &report, const Container::VariableArray<size_t> &indices)
        {
            const Asset::AssetResolveResult resolved =
                system.ResolveAsset(report.LogicalPath.c_str(), Asset::AssetKind::Model);
            if (!resolved.Succeeded() || !resolved.UsedCooked())
            {
                report.Reason = resolved.Reason;
                return resolved.Status == Asset::AssetResolveStatus::CookedPackageParseFailed ? SampleStatus::MeshInvalid
                                                                                                : SampleStatus::PackageMissing;
            }
            const Asset::CookedMeshParseResult parsed = Asset::ParseCookedMesh(resolved.Blob);
            if (!parsed.Succeeded())
            {
                char reason[64] = {};
                std::snprintf(reason, sizeof(reason), "メッシュを解析できません status=%u",
                              static_cast<unsigned int>(parsed.Status));
                report.Reason = reason;
                return SampleStatus::MeshInvalid;
            }
            if (!IntersectTile(parsed.Mesh, points, indices, report))
            {
                return SampleStatus::MeshInvalid;
            }
            if (bBoundaryEdges)
            {
                CollectBoundaryEdges(parsed.Mesh, report.FinestLod, report.BoundaryEdges);
            }
            return SampleStatus::Ok;
        };

        Container::VariableArray<TileReport> tiles;
        if (horizonLevel != nullptr)
        {
            // 遠景の段: 段の名前で始まる区画（<段>_<x>_<z>）のすべてで全部の点の交点を調べる。区画は X/Z の範囲を格子で
            // 割らないので、点の区画は決めない（区画の失敗はその区画の交点が無いだけで、区画の状態に書く）
            Container::VariableArray<Terrain::TerrainHorizonMesh> meshes;
            if (!Terrain::LoadTerrainHorizonMeshes(Terrain::WastelandTerrain::kHorizonDescriptorPath, meshes, loadError))
            {
                std::cerr << "AssetCook error: " << loadError.c_str() << "\n";
                return true;
            }
            Container::VariableArray<size_t> all;
            for (size_t index = 0; index < points.size(); ++index)
            {
                points[index].Status = SampleStatus::NoHit;
                points[index].TileX = UINT32_MAX;
                points[index].TileZ = UINT32_MAX;
                all.push_back(index);
            }
            const std::string_view prefix(horizonLevel);
            for (const Terrain::TerrainHorizonMesh &entry : meshes)
            {
                const std::string_view name(entry.Name.c_str(), entry.Name.size());
                unsigned int tileX = 0;
                unsigned int tileZ = 0;
                if (name.size() <= prefix.size() + 1u || name.substr(0, prefix.size()) != prefix ||
                    name[prefix.size()] != '_' ||
                    std::sscanf(entry.Name.c_str() + prefix.size() + 1u, "%u_%u", &tileX, &tileZ) != 2)
                {
                    continue;
                }
                TileReport report;
                report.TileX = tileX;
                report.TileZ = tileZ;
                report.LogicalPath = Container::AnsiString(Terrain::WastelandTerrain::kHorizonCookedMeshLogicalPathPrefix) +
                                     entry.Name + Container::AnsiString(".generated");
                report.Status = intersectPackage(report, all);
                tiles.push_back(std::move(report));
            }
            if (tiles.empty())
            {
                std::cerr << "AssetCook error: 遠景の段 " << horizonLevel << " の区画が記述にありません\n";
                return true;
            }
        }
        for (uint32_t tileZ = 0; tileZ < (horizonLevel != nullptr ? 0u : tilesZ); ++tileZ)
        {
            for (uint32_t tileX = 0; tileX < tilesX; ++tileX)
            {
                const Container::VariableArray<size_t> &indices = tilePoints[static_cast<size_t>(tileZ) * tilesX + tileX];
                if (indices.empty())
                {
                    continue;
                }
                TileReport report;
                report.TileX = tileX;
                report.TileZ = tileZ;
                char suffix[32] = {};
                std::snprintf(suffix, sizeof(suffix), "%u_%u.generated", tileX, tileZ);
                report.LogicalPath = Container::AnsiString(Terrain::WastelandTerrain::kDemCookedMeshLogicalPathPrefix) +
                                     Container::AnsiString(suffix);
                uint32_t firstColumn = 0;
                uint32_t cellsX = 0;
                uint32_t firstRow = 0;
                uint32_t cellsZ = 0;
                Terrain::WastelandTerrain::DemTileCellRange(tileX, layout.Columns, firstColumn, cellsX);
                Terrain::WastelandTerrain::DemTileCellRange(tileZ, layout.Rows, firstRow, cellsZ);
                report.bCliffDetail = TerrainCliffTileIntersects(layout, firstColumn, cellsX, firstRow, cellsZ);
                report.bCarve = TerrainCliffCarveTouches(layout, firstColumn, cellsX, firstRow, cellsZ);

                const SampleStatus failure = intersectPackage(report, indices);
                if (failure != SampleStatus::Ok)
                {
                    // 失敗は点を含む区画のときだけ点の状態にする（隣の区画の失敗は、その区画の交点が無いだけ）
                    report.Status = failure;
                    for (const size_t index : indices)
                    {
                        if (points[index].TileX == tileX && points[index].TileZ == tileZ)
                        {
                            points[index].Status = failure;
                        }
                    }
                }
                tiles.push_back(std::move(report));
            }
        }
        for (QueryPoint &point : points)
        {
            if (point.Status != SampleStatus::NoHit)
            {
                point.Hits.clear();
                continue;
            }
            MergeHits(point);
            point.Status = point.Hits.empty() ? SampleStatus::NoHit : SampleStatus::Ok;
        }

        // 結果（機械が照合する JSON。高さ・位置は m、エンジンのワールド座標）
        std::cout << "{\n  \"format\": \"cooked_terrain_samples.v1\",\n  \"manifest\": ";
        WriteJsonString(manifestPath);
        std::cout << ",\n  \"tiles\": [";
        for (size_t index = 0; index < tiles.size(); ++index)
        {
            const TileReport &tile = tiles[index];
            std::cout << (index == 0 ? "\n" : ",\n") << "    {\"tile\": [" << tile.TileX << ", " << tile.TileZ
                      << "], \"logical_path\": ";
            WriteJsonString(tile.LogicalPath.c_str());
            std::cout << ", \"status\": \"" << StatusText(tile.Status) << "\", \"cliff_detail\": "
                      << (tile.bCliffDetail ? "true" : "false") << ", \"carve\": " << (tile.bCarve ? "true" : "false")
                      << ", \"finest_lod\": " << tile.FinestLod << ", \"finest_clusters\": " << tile.FinestClusters
                      << ", \"finest_triangles\": " << tile.FinestTriangles << ", \"reason\": ";
            WriteJsonString(tile.Reason.c_str());
            if (bBoundaryEdges)
            {
                std::cout << ", \"boundary_edges\": [";
                for (size_t edgeIndex = 0; edgeIndex < tile.BoundaryEdges.size(); ++edgeIndex)
                {
                    std::cout << (edgeIndex == 0 ? "[" : ", [");
                    for (uint32_t k = 0; k < 6u; ++k)
                    {
                        // float の位置をそのまま読み戻せる桁（9 桁）で書く
                        char text[32] = {};
                        std::snprintf(text, sizeof(text), "%.9g", static_cast<double>(tile.BoundaryEdges[edgeIndex].Ends[k]));
                        std::cout << (k == 0 ? "" : ", ") << text;
                    }
                    std::cout << "]";
                }
                std::cout << "]";
            }
            std::cout << "}";
        }
        std::cout << "\n  ],\n  \"points\": [";
        for (size_t index = 0; index < points.size(); ++index)
        {
            const QueryPoint &point = points[index];
            std::cout << (index == 0 ? "\n" : ",\n") << "    {\"index\": " << index << ", \"x\": ";
            WriteNumber(point.X);
            std::cout << ", \"z\": ";
            WriteNumber(point.Z);
            std::cout << ", \"status\": \"" << StatusText(point.Status) << "\"";
            if (point.Status != SampleStatus::OutOfRange && point.TileX != UINT32_MAX)
            {
                std::cout << ", \"tile\": [" << point.TileX << ", " << point.TileZ << "]";
            }
            std::cout << ", \"hits\": [";
            for (size_t hitIndex = 0; hitIndex < point.Hits.size(); ++hitIndex)
            {
                const SurfaceHit &hit = point.Hits[hitIndex];
                std::cout << (hitIndex == 0 ? "" : ", ") << "{\"height\": ";
                WriteNumber(hit.Height);
                std::cout << ", \"normal\": [";
                WriteNumber(hit.Normal[0]);
                std::cout << ", ";
                WriteNumber(hit.Normal[1]);
                std::cout << ", ";
                WriteNumber(hit.Normal[2]);
                std::cout << "], \"triangle\": " << hit.Triangle << ", \"cluster\": " << hit.Cluster << ", \"hit_tile\": ["
                          << hit.TileX << ", " << hit.TileZ << "], \"shared\": " << (hit.bShared ? "true" : "false") << "}";
            }
            std::cout << "]}";
        }
        std::cout << "\n  ]\n}\n";
        outExitCode = 0;
        return true;
    }

    namespace
    {
        struct PlacementCoordinate
        {
            int32_t X = 0;
            int32_t Z = 0;
        };

        struct PlacementBlock : PlacementCoordinate
        {
            float Heights[Terrain::PlacementGroundFormat::kSamples] = {};
            uint8_t ReliableCells[Terrain::PlacementGroundFormat::kMaskBytes] = {};
        };

        struct PlacementTile
        {
            TileReport Report;
            double MinX = 0.0;
            double MaxX = 0.0;
            double MinZ = 0.0;
            double MaxZ = 0.0;
            uint32_t Blocks = 0;
        };

        struct PlacementBounds
        {
            double MinX = 0.0;
            double MaxX = 0.0;
            double MinZ = 0.0;
            double MaxZ = 0.0;
        };

        struct PlacementRegion
        {
            const char *Id;
            PlacementBounds Bounds;
        };

        // Scripts/Terrain/ShapeRegions.json の range_groups["cliff-gen"] と
        // TerrainCliffDetail.cpp の kCastleRegions[0]〜[4] の範囲。
        constexpr PlacementRegion kCastlePlacementRegions[] = {
            {"cliff_castle_0_main_butte", {-3450.0, -2100.0, -4450.0, -3950.0}},
            {"cliff_castle_1_far_cliffs", {-3650.0, -1900.0, -6340.0, -4650.0}},
            {"cliff_castle_2_left_7_9km", {-6250.0, -5150.0, -8250.0, -6250.0}},
            {"cliff_castle_3_left_5_7km", {-5350.0, -4250.0, -7000.0, -5500.0}},
            {"cliff_castle_4_left_edge_9km", {-6875.0, -6150.0, -8625.0, -7875.0}},
        };
        constexpr double kCastleRegionPadding = 48.0;

        struct PlacementSummary
        {
            uint64_t NanCount = 0;
            uint64_t SampledCount = 0;
            uint64_t UnreliableCells = 0;
            uint64_t CenterQueryCells = 0;
            uint64_t ConfidenceQueryPoints = 0;
            uint64_t ConfidenceProbeSamples = 0;
            uint64_t QuarterQueryCells = 0;
            float ConfidenceMs = 0.0f;
            Container::VariableArray<double> Differences;
        };

        bool IsCastleName(const JsonValue &value)
        {
            constexpr char expected[] = "castle";
            if (!value.IsString() || value.AsString().size() != sizeof(expected) - 1u)
            {
                return false;
            }
            for (size_t i = 0; i < sizeof(expected) - 1u; ++i)
            {
                if (value.AsString().data()[i] != expected[i])
                {
                    return false;
                }
            }
            return true;
        }

        bool ReadPlacementBounds(const JsonValue &object, PlacementBounds &bounds)
        {
            const JsonValue values[] = {object.FindMember("x_min"), object.FindMember("x_max"),
                                        object.FindMember("z_min"), object.FindMember("z_max")};
            for (const JsonValue &value : values)
            {
                if (!value.IsNumber() || !std::isfinite(value.AsNumber()))
                {
                    return false;
                }
            }
            bounds = {values[0].AsNumber(), values[1].AsNumber(), values[2].AsNumber(), values[3].AsNumber()};
            return bounds.MinX < bounds.MaxX && bounds.MinZ < bounds.MaxZ;
        }

        /// 遠景の記述はアセットの根から読み、通常の遠景メッシュの検証も通す。
        bool LoadCastlePlacementLayout(PlacementBounds &bounds, Container::VariableArray<PlacementTile> &tiles,
                                       Container::AnsiString &outError)
        {
            Container::VariableArray<Terrain::TerrainHorizonMesh> meshes;
            if (!Terrain::LoadTerrainHorizonMeshes(Terrain::WastelandTerrain::kHorizonDescriptorPath, meshes, outError))
            {
                return false;
            }
            Asset::AssetFileReader reader;
            Asset::AssetReadRequest request;
            request.InputPath = Terrain::WastelandTerrain::kHorizonDescriptorPath;
            request.bAllowAbsolutePath = false;
            request.MaxReadBytes = 4u * 1024u * 1024u;
            const Asset::AssetReadResult descriptor = reader.Read(request);
            if (!descriptor.Succeeded())
            {
                outError = "遠景の地形の記述を読めません";
                return false;
            }
            auto text = descriptor.Blob.GetSpan();
            if (text.size() >= 3u && text[0] == 0xefu && text[1] == 0xbbu && text[2] == 0xbfu)
            {
                text = {text.data() + 3, text.size() - 3};
            }
            JsonDocument document;
            if (!JsonDocument::TryParseUtf8(text, document))
            {
                outError = "遠景の地形の記述が JSON として読めません";
                return false;
            }
            const JsonValue root = document.GetRoot();
            const JsonValue levels = root.FindMember("levels");
            const JsonValue entries = root.FindMember("meshes");
            if (!levels.IsArray() || !entries.IsArray() || entries.GetArraySize() != meshes.size())
            {
                outError = "遠景の地形の段または区画の並びが不正です";
                return false;
            }
            bool bFound = false;
            for (size_t i = 0; i < levels.GetArraySize(); ++i)
            {
                const JsonValue level = levels.GetArrayElement(i);
                if (!IsCastleName(level.FindMember("name")))
                {
                    continue;
                }
                if (bFound || !ReadPlacementBounds(level, bounds))
                {
                    outError = "castle の段の範囲が不正です";
                    return false;
                }
                bFound = true;
            }
            if (!bFound || (bounds.MaxX - bounds.MinX) / Terrain::PlacementGroundFormat::kCastleBlockMeters > INT32_MAX ||
                (bounds.MaxZ - bounds.MinZ) / Terrain::PlacementGroundFormat::kCastleBlockMeters > INT32_MAX)
            {
                outError = "castle の段の範囲がないか、大きすぎます";
                return false;
            }
            for (size_t i = 0; i < entries.GetArraySize(); ++i)
            {
                const JsonValue entry = entries.GetArrayElement(i);
                if (!IsCastleName(entry.FindMember("level")))
                {
                    continue;
                }
                PlacementBounds tileBounds;
                if (!ReadPlacementBounds(entry.FindMember("bounds"), tileBounds))
                {
                    outError = "castle の区画の範囲が不正です";
                    return false;
                }
                PlacementTile tile;
                tile.MinX = tileBounds.MinX;
                tile.MaxX = tileBounds.MaxX;
                tile.MinZ = tileBounds.MinZ;
                tile.MaxZ = tileBounds.MaxZ;
                tile.Report.LogicalPath = Container::AnsiString(Terrain::WastelandTerrain::kHorizonCookedMeshLogicalPathPrefix) +
                                          meshes[i].Name + Container::AnsiString(".generated");
                tiles.push_back(std::move(tile));
            }
            if (tiles.empty())
            {
                outError = "castle の段の区画が記述にありません";
                return false;
            }
            return true;
        }

        bool PlacementOverlaps(double x0, double x1, double z0, double z1, const PlacementBounds &bounds, double padding)
        {
            return x1 >= bounds.MinX - padding && x0 <= bounds.MaxX + padding &&
                   z1 >= bounds.MinZ - padding && z0 <= bounds.MaxZ + padding;
        }

        /// 生の構造体を書かず、整数と浮動小数点のビットを little-endian で書く。
        void WritePlacementUInt(std::ostream &stream, uint64_t value, size_t width)
        {
            for (size_t i = 0; i < width; ++i)
            {
                stream.put(static_cast<char>((value >> (8u * i)) & 255u));
            }
        }

        void WritePlacementText(std::ostream &stream, const Container::AnsiString &text)
        {
            WritePlacementUInt(stream, text.size(), 4);
            stream.write(text.data(), static_cast<std::streamsize>(text.size()));
        }

        /// 完全なマニフェストを読む独立の段。単一区画の焼き直しの途中では呼ばない。
        bool WritePlacementGround(const char *manifestPath, const char *outputPath, bool bSkipIfCurrent,
                                  Container::AnsiString &outError)
        {
            using namespace Terrain::PlacementGroundFormat;
            const auto reject = [&](const char *reason)
            {
                outError = reason;
                return false;
            };
            const auto field = Terrain::WastelandTerrain::LoadDemGrid(outError);
            if (!field)
            {
                return false;
            }
            const auto layout = field->GetGridLayout();
            const double maxX = layout.FirstX + (layout.Columns - 1u) * layout.Spacing;
            const double maxZ = layout.FirstZ + (layout.Rows - 1u) * layout.Spacing;
            if (layout.Columns < 2u || layout.Rows < 2u || !std::isfinite(layout.Spacing) || layout.Spacing <= 0.0 ||
                !std::isfinite(layout.FirstX) || !std::isfinite(layout.FirstZ) || !std::isfinite(maxX) ||
                !std::isfinite(maxZ) || (maxX - layout.FirstX) / kBlockMeters > INT32_MAX ||
                (maxZ - layout.FirstZ) / kBlockMeters > INT32_MAX)
            {
                return reject("DEM の格子の範囲が不正です");
            }
            Container::VariableArray<uint8_t> manifestBytes;
            if (!ReadFileBytes(manifestPath, manifestBytes))
            {
                return reject("マニフェストを開けません");
            }
            const auto manifestText = WithoutBom(manifestBytes);
            const auto manifestParent = std::filesystem::absolute(std::filesystem::path(manifestPath)).parent_path();
            const auto destination = std::filesystem::absolute(std::filesystem::path(outputPath));
            std::filesystem::create_directories(destination.parent_path());
            Asset::AssetSystem system{Container::AnsiString(manifestParent.generic_string().c_str())};
            if (!system.LoadManifestFromJsonText(
                    Detail::ToCoreString(Container::AnsiStringView(reinterpret_cast<const char *>(manifestText.data()),
                                                                   manifestText.size())),
                    manifestPath))
            {
                return reject("マニフェストを読めません");
            }
#if NORVES_ENABLE_STATS
            // CLI の要約も既存の計測マクロから取る。Release には計測を持ち込まない。
            const Container::AnsiString tracePath((destination.generic_string() + ".trace.csv").c_str());
            struct TraceGuard
            {
                bool bActive;
                ~TraceGuard()
                {
                    if (bActive)
                    {
                        NorvesLib::Debug::StatsManager::Get().StopTrace();
                    }
                }
            } trace{NorvesLib::Debug::StatsManager::Get().StartTrace(Detail::ToCoreString(tracePath))};
            if (!trace.bActive)
            {
                return reject("接地用の高さ格子の計測ファイルを開けません");
            }
#endif
            NORVES_STAT_TIME_START(PlacementGround);
            const uint32_t tilesX = Terrain::WastelandTerrain::DemTileCount(layout.Columns);
            const uint32_t tilesZ = Terrain::WastelandTerrain::DemTileCount(layout.Rows);
            Container::VariableArray<PlacementTile> tiles(static_cast<size_t>(tilesX) * tilesZ);
            const uint32_t blocksX = static_cast<uint32_t>(std::ceil((maxX - layout.FirstX) / kBlockMeters));
            const uint32_t blocksZ = static_cast<uint32_t>(std::ceil((maxZ - layout.FirstZ) / kBlockMeters));
            Container::VariableArray<uint8_t> candidates(static_cast<size_t>(blocksX) * blocksZ, 0);
            for (uint32_t tz = 0; tz < tilesZ; ++tz)
            {
                for (uint32_t tx = 0; tx < tilesX; ++tx)
                {
                    auto &tile = tiles[static_cast<size_t>(tz) * tilesX + tx];
                    tile.Report.TileX = tx;
                    tile.Report.TileZ = tz;
                    char suffix[32] = {};
                    std::snprintf(suffix, sizeof(suffix), "%u_%u.generated", tx, tz);
                    tile.Report.LogicalPath =
                        Container::AnsiString(Terrain::WastelandTerrain::kDemCookedMeshLogicalPathPrefix) + suffix;
                    uint32_t firstColumn = 0, cellsX = 0, firstRow = 0, cellsZ = 0;
                    Terrain::WastelandTerrain::DemTileCellRange(tx, layout.Columns, firstColumn, cellsX);
                    Terrain::WastelandTerrain::DemTileCellRange(tz, layout.Rows, firstRow, cellsZ);
                    tile.MinX = layout.FirstX + firstColumn * layout.Spacing;
                    tile.MaxX = tile.MinX + cellsX * layout.Spacing;
                    tile.MinZ = layout.FirstZ + firstRow * layout.Spacing;
                    tile.MaxZ = tile.MinZ + cellsZ * layout.Spacing;
                    tile.Report.bCliffDetail =
                        TerrainCliffTileIntersects(layout, firstColumn, cellsX, firstRow, cellsZ);
                    tile.Report.bCarve = TerrainCliffCarveTouches(layout, firstColumn, cellsX, firstRow, cellsZ);
                    if (!tile.Report.bCliffDetail && !tile.Report.bCarve)
                    {
                        continue;
                    }
                    // 境から押し出された面の分も候補に含める。平らな隣の塊は差の判定で落ちる。
                    const auto bx0 = static_cast<uint32_t>(
                        std::clamp(std::floor((tile.MinX - kSeamReachMeters - layout.FirstX) / kBlockMeters), 0.0,
                                   static_cast<double>(blocksX)));
                    const auto bz0 = static_cast<uint32_t>(
                        std::clamp(std::floor((tile.MinZ - kSeamReachMeters - layout.FirstZ) / kBlockMeters), 0.0,
                                   static_cast<double>(blocksZ)));
                    const auto bx1 = static_cast<uint32_t>(
                        std::clamp(std::ceil((tile.MaxX + kSeamReachMeters - layout.FirstX) / kBlockMeters), 0.0,
                                   static_cast<double>(blocksX)));
                    const auto bz1 = static_cast<uint32_t>(
                        std::clamp(std::ceil((tile.MaxZ + kSeamReachMeters - layout.FirstZ) / kBlockMeters), 0.0,
                                   static_cast<double>(blocksZ)));
                    for (uint32_t bz = bz0; bz < bz1; ++bz)
                    {
                        for (uint32_t bx = bx0; bx < bx1; ++bx)
                        {
                            candidates[static_cast<size_t>(bz) * blocksX + bx] = 1;
                        }
                    }
                }
            }
            // 8 m の原点を持つ区画に塊を一度だけ割り当てる。500 m の区画の端は 8 m に揃わない。
            Container::VariableArray<Container::VariableArray<PlacementCoordinate>> batches(tiles.size());
            for (uint32_t bz = 0; bz < blocksZ; ++bz)
            {
                for (uint32_t bx = 0; bx < blocksX; ++bx)
                {
                    if (candidates[static_cast<size_t>(bz) * blocksX + bx] == 0)
                    {
                        continue;
                    }
                    uint32_t tx = 0, tz = 0;
                    if (!FindTile(layout, layout.FirstX + bx * kBlockMeters, layout.FirstZ + bz * kBlockMeters, tx, tz))
                    {
                        return reject("接地用の高さ格子の塊を区画へ割り当てられません");
                    }
                    PlacementCoordinate block;
                    block.X = static_cast<int32_t>(bx);
                    block.Z = static_cast<int32_t>(bz);
                    batches[static_cast<size_t>(tz) * tilesX + tx].push_back(block);
                }
            }
            PlacementBounds castleBounds;
            Container::VariableArray<PlacementTile> castleTiles;
            if (!LoadCastlePlacementLayout(castleBounds, castleTiles, outError))
            {
                return false;
            }
            for (const PlacementRegion &region : kCastlePlacementRegions)
            {
                if (region.Bounds.MinX - kCastleRegionPadding < castleBounds.MinX ||
                    region.Bounds.MaxX + kCastleRegionPadding > castleBounds.MaxX ||
                    region.Bounds.MinZ - kCastleRegionPadding < castleBounds.MinZ ||
                    region.Bounds.MaxZ + kCastleRegionPadding > castleBounds.MaxZ)
                {
                    return reject("castle の段が接地用の範囲を覆っていません");
                }
            }
            const uint32_t castleBlocksX = static_cast<uint32_t>(std::ceil((castleBounds.MaxX - castleBounds.MinX) / kCastleBlockMeters));
            const uint32_t castleBlocksZ = static_cast<uint32_t>(std::ceil((castleBounds.MaxZ - castleBounds.MinZ) / kCastleBlockMeters));
            Container::VariableArray<Container::VariableArray<PlacementCoordinate>> castleBatches(castleTiles.size());
            Container::VariableArray<uint8_t> castleSourceNeeded(castleTiles.size(), 0);
            for (uint32_t bz = 0; bz < castleBlocksZ; ++bz)
            {
                for (uint32_t bx = 0; bx < castleBlocksX; ++bx)
                {
                    const double x0 = castleBounds.MinX + bx * kCastleBlockMeters;
                    const double z0 = castleBounds.MinZ + bz * kCastleBlockMeters;
                    const double x1 = std::min(x0 + kCastleBlockMeters, castleBounds.MaxX);
                    const double z1 = std::min(z0 + kCastleBlockMeters, castleBounds.MaxZ);
                    bool bCandidate = false;
                    for (const PlacementRegion &region : kCastlePlacementRegions)
                    {
                        if (PlacementOverlaps(x0, x1, z0, z1, region.Bounds, kCastleRegionPadding))
                        {
                            bCandidate = true;
                            break;
                        }
                    }
                    if (!bCandidate)
                    {
                        continue;
                    }
                    size_t owner = castleTiles.size();
                    for (size_t id = 0; id < castleTiles.size(); ++id)
                    {
                        const auto &tile = castleTiles[id];
                        if (owner == castleTiles.size() && x0 >= tile.MinX && x0 <= tile.MaxX &&
                            z0 >= tile.MinZ && z0 <= tile.MaxZ)
                        {
                            owner = id;
                        }
                        if (x1 >= tile.MinX - kSeamReachMeters && x0 <= tile.MaxX + kSeamReachMeters &&
                            z1 >= tile.MinZ - kSeamReachMeters && z0 <= tile.MaxZ + kSeamReachMeters)
                        {
                            castleSourceNeeded[id] = 1;
                        }
                    }
                    if (owner == castleTiles.size())
                    {
                        return reject("castle の接地用の塊を区画へ割り当てられません");
                    }
                    PlacementCoordinate block;
                    block.X = static_cast<int32_t>(bx);
                    block.Z = static_cast<int32_t>(bz);
                    castleBatches[owner].push_back(block);
                }
            }
            Container::VariableArray<Terrain::TerrainPlacementGroundSource> sources;
            Container::VariableArray<uint8_t> sourceNeeded(tiles.size(), 0);
            // 標本化の前に問い合わせる区画を決める。中心の点は同じ塊の四隅の範囲内にある。
            for (const auto &batch : batches)
            {
                for (const auto &block : batch)
                {
                    const double x0 = layout.FirstX + block.X * kBlockMeters;
                    const double z0 = layout.FirstZ + block.Z * kBlockMeters;
                    const double x1 = std::min(x0 + kBlockMeters, maxX);
                    const double z1 = std::min(z0 + kBlockMeters, maxZ);
                    for (size_t id = 0; id < tiles.size(); ++id)
                    {
                        const auto &tile = tiles[id];
                        if (x1 >= tile.MinX - kSeamReachMeters && x0 <= tile.MaxX + kSeamReachMeters &&
                            z1 >= tile.MinZ - kSeamReachMeters && z0 <= tile.MaxZ + kSeamReachMeters)
                        {
                            sourceNeeded[id] = 1;
                        }
                    }
                }
            }
            const auto addSources = [&](const auto &sourceTiles, const auto &needed)
            {
                for (size_t id = 0; id < sourceTiles.size(); ++id)
                {
                    if (needed[id] == 0)
                    {
                        continue;
                    }
                    const auto resolved = system.FindCookedVariant(sourceTiles[id].Report.LogicalPath.c_str(), Asset::AssetKind::Model);
                    if (!resolved.ShouldUseCooked())
                    {
                        outError = sourceTiles[id].Report.LogicalPath + ": 区画のクック済みの参照がありません";
                        return false;
                    }
                    Terrain::TerrainPlacementGroundSource source;
                    source.LogicalPath = resolved.Reference.LogicalPath;
                    source.CookedPackage = resolved.Reference.CookedPackage;
                    source.CookedHash = resolved.Reference.CookedHash;
                    sources.push_back(std::move(source));
                }
                return true;
            };
            if (!addSources(tiles, sourceNeeded))
            {
                return false;
            }
            const size_t nearSourceCount = sources.size();
            if (!addSources(castleTiles, castleSourceNeeded))
            {
                return false;
            }
            if (bSkipIfCurrent && std::filesystem::exists(destination))
            {
                Container::AnsiString loadError;
                const auto current = Terrain::TerrainPlacementGround::Load(
                    Container::AnsiString(destination.generic_string().c_str()), loadError);
                if (current && current->GetLayerCount() == 2u && current->IsCurrent({sources.data(), sources.size()}))
                {
                    float elapsedMs = 0.0f;
                    NORVES_STAT_TIME_END(PlacementGround, elapsedMs);
                    std::cout << "{\"skipped\":true,\"format_version\":" << kVersion
                              << ",\"source_count\":" << nearSourceCount
                              << ",\"file_bytes\":" << std::filesystem::file_size(destination) << ",\"seconds\":";
#if NORVES_ENABLE_STATS
                    WriteNumber(elapsedMs / 1000.0);
#else
                    std::cout << "null";
#endif
                    std::cout << "}\n";
                    return true;
                }
            }
            const auto intersectPoints = [&](Container::VariableArray<QueryPoint> &points, bool)
            {
                Container::VariableArray<Container::VariableArray<size_t>> tilePoints(tiles.size());
                for (size_t pointIndex = 0; pointIndex < points.size(); ++pointIndex)
                {
                    auto &point = points[pointIndex];
                    if (!FindTile(layout, point.X, point.Z, point.TileX, point.TileZ))
                    {
                        continue;
                    }
                    for (uint32_t tz = point.TileZ > 0 ? point.TileZ - 1 : 0;
                         tz <= std::min(point.TileZ + 1, tilesZ - 1); ++tz)
                    {
                        for (uint32_t tx = point.TileX > 0 ? point.TileX - 1 : 0;
                             tx <= std::min(point.TileX + 1, tilesX - 1); ++tx)
                        {
                            const size_t id = static_cast<size_t>(tz) * tilesX + tx;
                            const auto &tile = tiles[id];
                            if (point.X >= tile.MinX - kSeamReachMeters && point.X <= tile.MaxX + kSeamReachMeters &&
                                point.Z >= tile.MinZ - kSeamReachMeters && point.Z <= tile.MaxZ + kSeamReachMeters)
                            {
                                tilePoints[id].push_back(pointIndex);
                            }
                        }
                    }
                }
                for (size_t id = 0; id < tiles.size(); ++id)
                {
                    if (tilePoints[id].empty())
                    {
                        continue;
                    }
                    auto &report = tiles[id].Report;
                    const auto resolved = system.ResolveAsset(report.LogicalPath.c_str(), Asset::AssetKind::Model);
                    if (!resolved.Succeeded() || !resolved.UsedCooked())
                    {
                        outError = report.LogicalPath + ": " + resolved.Reason;
                        return false;
                    }
                    const auto parsed = Asset::ParseCookedMesh(resolved.Blob);
                    if (!parsed.Succeeded() || !IntersectTile(parsed.Mesh, points, tilePoints[id], report, true) ||
                        report.FinestLod != 0u)
                    {
                        outError = report.LogicalPath + ": LOD 0 のメッシュまたはクラスタの範囲が不正です ";
                        outError += report.Reason;
                        return false;
                    }
                }
                return true;
            };
            const auto intersectCastlePoints = [&](Container::VariableArray<QueryPoint> &points, bool bProbe)
            {
                if (points.empty())
                {
                    return true;
                }
                double minX = points[0].X, maxX = points[0].X;
                double minZ = points[0].Z, maxZ = points[0].Z;
                for (const auto &point : points)
                {
                    minX = std::min(minX, point.X);
                    maxX = std::max(maxX, point.X);
                    minZ = std::min(minZ, point.Z);
                    maxZ = std::max(maxZ, point.Z);
                }
                for (auto &tile : castleTiles)
                {
                    if (maxX < tile.MinX - kSeamReachMeters || minX > tile.MaxX + kSeamReachMeters ||
                        maxZ < tile.MinZ - kSeamReachMeters || minZ > tile.MaxZ + kSeamReachMeters)
                    {
                        continue;
                    }
                    Container::VariableArray<size_t> indices;
                    for (size_t i = 0; i < points.size(); ++i)
                    {
                        const auto &point = points[i];
                        if (point.X >= tile.MinX - kSeamReachMeters && point.X <= tile.MaxX + kSeamReachMeters &&
                            point.Z >= tile.MinZ - kSeamReachMeters && point.Z <= tile.MaxZ + kSeamReachMeters)
                        {
                            indices.push_back(i);
                        }
                    }
                    if (indices.empty())
                    {
                        continue;
                    }
                    auto &report = tile.Report;
                    const auto resolved = system.ResolveAsset(report.LogicalPath.c_str(), Asset::AssetKind::Model);
                    if (!resolved.Succeeded() || !resolved.UsedCooked())
                    {
                        outError = report.LogicalPath + ": " + resolved.Reason;
                        return false;
                    }
                    const auto parsed = Asset::ParseCookedMesh(resolved.Blob);
                    // 頂点の標本は最上面、信頼の探りは近景と同じく補間値に最も近い上向きの面を使う。
                    if (!parsed.Succeeded() || !IntersectTile(parsed.Mesh, points, indices, report, bProbe, !bProbe) ||
                        report.FinestLod != 0u)
                    {
                        outError = report.LogicalPath + ": LOD 0 のメッシュまたはクラスタの範囲が不正です ";
                        outError += report.Reason;
                        return false;
                    }
                }
                return true;
            };
            const auto processBatches = [&](auto &layerTiles, auto &layerBatches, auto &queryPoints,
                                            double firstX, double firstZ, float blockMeters, float spacing, bool bCastle,
                                            Container::VariableArray<PlacementBlock> &layerWritten, PlacementSummary &summary)
            {
                auto &tiles = layerTiles;
                auto &batches = layerBatches;
                auto &written = layerWritten;
                auto &differences = summary.Differences;
                auto &nanCount = summary.NanCount;
                auto &sampledCount = summary.SampledCount;
                auto &unreliableCells = summary.UnreliableCells;
                auto &centerQueryCells = summary.CenterQueryCells;
                auto &confidenceQueryPoints = summary.ConfidenceQueryPoints;
                auto &confidenceProbeSamples = summary.ConfidenceProbeSamples;
                auto &quarterQueryCells = summary.QuarterQueryCells;
                auto &confidenceMs = summary.ConfidenceMs;
                for (size_t owner = 0; owner < batches.size(); ++owner)
                {
                    auto &batch = batches[owner];
                    if (batch.empty())
                    {
                        continue;
                    }
                    const size_t firstWritten = written.size();
                    Container::VariableArray<QueryPoint> points;
                    points.reserve(batch.size() * kSamples);
                    for (const auto &block : batch)
                    {
                        for (uint32_t iz = 0; iz < kSide; ++iz)
                        {
                            for (uint32_t ix = 0; ix < kSide; ++ix)
                            {
                                QueryPoint point;
                                point.X = firstX + block.X * blockMeters + ix * spacing;
                                point.Z = firstZ + block.Z * blockMeters + iz * spacing;
                                if (!bCastle)
                                {
                                    point.DemHeight = field->SampleHeight(static_cast<float>(point.X), static_cast<float>(point.Z));
                                }
                                points.push_back(std::move(point));
                            }
                        }
                    }
                    sampledCount += points.size();
                    if (!queryPoints(points, false))
                    {
                        return false;
                    }
                    for (size_t index = 0; index < batch.size(); ++index)
                    {
                        PlacementBlock block;
                        block.X = batch[index].X;
                        block.Z = batch[index].Z;
                        bool bChanged = false;
                        for (uint32_t i = 0; i < kSamples; ++i)
                        {
                            const auto &point = points[index * kSamples + i];
                            block.Heights[i] = point.Hits.empty() ? std::numeric_limits<float>::quiet_NaN()
                                                                  : static_cast<float>(point.Hits[0].Height);
                            if (!point.Hits.empty() && (bCastle || std::fabs(point.Hits[0].Height - point.DemHeight) > 0.01))
                            {
                                bChanged = true;
                            }
                        }
                        if (!bChanged)
                        {
                            continue;
                        }
                        for (uint32_t i = 0; i < kSamples; ++i)
                        {
                            const float height = block.Heights[i];
                            if (std::isnan(height))
                            {
                                ++nanCount;
                            }
                            else if (!bCastle)
                            {
                                differences.push_back(
                                    std::fabs(static_cast<double>(height) - points[index * kSamples + i].DemHeight));
                            }
                        }
                        written.push_back(block);
                        ++tiles[owner].Blocks;
                    }
                    // 四隅の探索で使った大きい列は信頼判定の探索の前に手放す。
                    Container::VariableArray<QueryPoint>().swap(points);
                    NORVES_STAT_TIME_START(PlacementConfidence);
                    struct CellProbes
                    {
                        size_t Block = 0;
                        uint32_t Cell = 0;
                        size_t Points[9] = {};
                        uint32_t PointCount = 5u;
                    };
                    Container::VariableArray<CellProbes> cells;
                    Container::VariableArray<QueryPoint> probes;
                    for (size_t blockIndex = firstWritten; blockIndex < written.size(); ++blockIndex)
                    {
                        auto &block = written[blockIndex];
                        // 隣り合う升目の辺の中点は同じ高さ・同じ交点なので、塊の中では一度だけ問い合わせる。
                        size_t horizontal[kSide * kCellSide];
                        size_t vertical[kSide * kCellSide];
                        const size_t missing = std::numeric_limits<size_t>::max();
                        std::fill(horizontal, horizontal + kSide * kCellSide, missing);
                        std::fill(vertical, vertical + kSide * kCellSide, missing);
                        const auto appendProbe = [&](double x, double z, double height)
                        {
                            const size_t index = probes.size();
                            QueryPoint point;
                            point.X = x;
                            point.Z = z;
                            point.DemHeight = height;
                            probes.push_back(std::move(point));
                            return index;
                        };
                        const auto edgeProbe = [&](size_t &index, double x, double z, double height)
                        {
                            if (index == missing)
                            {
                                index = appendProbe(x, z, height);
                            }
                            return index;
                        };
                        for (uint32_t cell = 0; cell < kCells; ++cell)
                        {
                            const uint32_t ix = cell % kCellSide;
                            const uint32_t iz = cell / kCellSide;
                            const uint32_t i = iz * kSide + ix;
                            const double a = block.Heights[i];
                            const double b = block.Heights[i + 1u];
                            const double c = block.Heights[i + kSide];
                            const double d = block.Heights[i + kSide + 1u];
                            if (!std::isfinite(a) || !std::isfinite(b) || !std::isfinite(c) || !std::isfinite(d))
                            {
                                ++unreliableCells;
                                continue;
                            }
                            const double x0 = firstX + block.X * blockMeters + ix * spacing;
                            const double z0 = firstZ + block.Z * blockMeters + iz * spacing;
                            const double half = 0.5 * spacing;
                            CellProbes entry;
                            entry.Block = blockIndex;
                            entry.Cell = cell;
                            entry.Points[0] = appendProbe(x0 + half, z0 + half, (a + b + c + d) * 0.25);
                            entry.Points[1] = edgeProbe(horizontal[iz * kCellSide + ix], x0 + half, z0, (a + b) * 0.5);
                            entry.Points[2] = edgeProbe(horizontal[(iz + 1u) * kCellSide + ix], x0 + half,
                                                        z0 + spacing, (c + d) * 0.5);
                            entry.Points[3] = edgeProbe(vertical[iz * kSide + ix], x0, z0 + half, (a + c) * 0.5);
                            entry.Points[4] = edgeProbe(vertical[iz * kSide + ix + 1u], x0 + spacing,
                                                        z0 + half, (b + d) * 0.5);
                            // 傾きの大きい升目では、中央と辺の中点の間に細い段がある例を確認した。
                            if (std::max({a, b, c, d}) - std::min({a, b, c, d}) > 0.1)
                            {
                                const auto quarterProbe = [&](double u, double v)
                                {
                                    const double value = (1.0 - v) * ((1.0 - u) * a + u * b) +
                                                         v * ((1.0 - u) * c + u * d);
                                    return appendProbe(x0 + u * spacing, z0 + v * spacing, value);
                                };
                                entry.Points[5] = quarterProbe(0.25, 0.25);
                                entry.Points[6] = quarterProbe(0.75, 0.25);
                                entry.Points[7] = quarterProbe(0.25, 0.75);
                                entry.Points[8] = quarterProbe(0.75, 0.75);
                                entry.PointCount = 9u;
                                ++quarterQueryCells;
                            }
                            confidenceProbeSamples += entry.PointCount;
                            cells.push_back(entry);
                        }
                    }
                    centerQueryCells += cells.size();
                    confidenceQueryPoints += probes.size();
                    if (!queryPoints(probes, true))
                    {
                        return false;
                    }
                    for (const CellProbes &cell : cells)
                    {
                        bool reliable = true;
                        for (uint32_t probe = 0; probe < cell.PointCount; ++probe)
                        {
                            const QueryPoint &point = probes[cell.Points[probe]];
                            if (point.Hits.empty() || std::fabs(point.Hits[0].Height - point.DemHeight) > 0.02)
                            {
                                reliable = false;
                                break;
                            }
                        }
                        if (reliable)
                        {
                            written[cell.Block].ReliableCells[cell.Cell / 8u] |= static_cast<uint8_t>(1u << (cell.Cell % 8u));
                        }
                        else
                        {
                            ++unreliableCells;
                        }
                    }
                    float batchConfidenceMs = 0.0f;
                    NORVES_STAT_TIME_END(PlacementConfidence, batchConfidenceMs);
                    confidenceMs += batchConfidenceMs;
                    // 区画ごとの作業メモリは次の区画へ持ち越さない。
                    Container::VariableArray<PlacementCoordinate>().swap(batch);
                }
                return true;
            };
            Container::VariableArray<PlacementBlock> written;
            Container::VariableArray<PlacementBlock> castleWritten;
            PlacementSummary nearSummary;
            PlacementSummary castleSummary;
            if (!processBatches(tiles, batches, intersectPoints, layout.FirstX, layout.FirstZ, kBlockMeters, kSpacing,
                                false, written, nearSummary))
            {
                return false;
            }
            NORVES_STAT_TIME_START(PlacementCastle);
            if (!processBatches(castleTiles, castleBatches, intersectCastlePoints, castleBounds.MinX, castleBounds.MinZ,
                                kCastleBlockMeters, kCastleSpacing, true, castleWritten, castleSummary))
            {
                return false;
            }
            float castleMs = 0.0f;
            NORVES_STAT_TIME_END(PlacementCastle, castleMs);
            auto &differences = nearSummary.Differences;
            const uint64_t nanCount = nearSummary.NanCount;
            const uint64_t sampledCount = nearSummary.SampledCount;
            const uint64_t unreliableCells = nearSummary.UnreliableCells;
            const uint64_t centerQueryCells = nearSummary.CenterQueryCells;
            const uint64_t confidenceQueryPoints = nearSummary.ConfidenceQueryPoints;
            const uint64_t confidenceProbeSamples = nearSummary.ConfidenceProbeSamples;
            const uint64_t quarterQueryCells = nearSummary.QuarterQueryCells;
            const float confidenceMs = nearSummary.ConfidenceMs;
            if (written.size() > UINT32_MAX || castleWritten.size() > UINT32_MAX || sources.size() > UINT32_MAX)
            {
                return reject("接地用の高さ格子の個数が形式の上限を超えています");
            }
            std::sort(written.begin(), written.end(),
                      [](const auto &a, const auto &b)
                      {
                          return a.Z < b.Z || (a.Z == b.Z && a.X < b.X);
                      });
            std::sort(castleWritten.begin(), castleWritten.end(),
                      [](const auto &a, const auto &b)
                      {
                          return a.Z < b.Z || (a.Z == b.Z && a.X < b.X);
                      });
            std::sort(differences.begin(), differences.end());
            // 一時ファイルを閉じてから置き換え、途中の書き込みを完成した格子として残さない。
            const auto temporary = std::filesystem::path(destination.native() + L".tmp");
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            if (!stream)
            {
                return reject("接地用の高さ格子の出力ファイルを開けません");
            }
            WritePlacementUInt(stream, kMagic, 4);
            WritePlacementUInt(stream, kVersion, 4);
            WritePlacementUInt(stream, kSide, 4);
            WritePlacementUInt(stream, 2u, 4);
            WritePlacementUInt(stream, sources.size(), 4);
            for (const auto &source : sources)
            {
                if (source.LogicalPath.size() > UINT32_MAX || source.CookedPackage.size() > UINT32_MAX)
                {
                    return reject("接地用の高さ格子の参照名が形式の上限を超えています");
                }
                WritePlacementText(stream, source.LogicalPath);
                WritePlacementText(stream, source.CookedPackage);
                WritePlacementUInt(stream, source.CookedHash, 8);
            }
            const auto writeLayer = [&](uint32_t flags, float blockMeters, float spacing,
                                        double firstX, double firstZ, double layerMaxX, double layerMaxZ,
                                        const Container::VariableArray<PlacementBlock> &blocks)
            {
                WritePlacementUInt(stream, flags, 4);
                WritePlacementUInt(stream, std::bit_cast<uint32_t>(blockMeters), 4);
                WritePlacementUInt(stream, std::bit_cast<uint32_t>(spacing), 4);
                for (const double value : {firstX, firstZ, layerMaxX, layerMaxZ})
                {
                    WritePlacementUInt(stream, std::bit_cast<uint64_t>(value), 8);
                }
                WritePlacementUInt(stream, blocks.size(), 4);
                for (const auto &block : blocks)
                {
                    WritePlacementUInt(stream, std::bit_cast<uint32_t>(block.X), 4);
                    WritePlacementUInt(stream, std::bit_cast<uint32_t>(block.Z), 4);
                    for (const float height : block.Heights)
                    {
                        WritePlacementUInt(stream, std::bit_cast<uint32_t>(height), 4);
                    }
                    stream.write(reinterpret_cast<const char *>(block.ReliableCells), kMaskBytes);
                }
            };
            writeLayer(kLayerFallbackToHeightField, kBlockMeters, kSpacing, layout.FirstX, layout.FirstZ, maxX, maxZ, written);
            writeLayer(0u, kCastleBlockMeters, kCastleSpacing, castleBounds.MinX, castleBounds.MinZ,
                       castleBounds.MaxX, castleBounds.MaxZ, castleWritten);
            stream.close();
            if (!stream)
            {
                return reject("接地用の高さ格子の書き込みを完了できません");
            }
            if (!MoveFileExW(temporary.c_str(), destination.c_str(),
                             MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            {
                return reject("接地用の高さ格子の出力ファイルを置き換えられません");
            }
            Container::AnsiString loadError;
            const auto loaded = Terrain::TerrainPlacementGround::Load(
                Container::AnsiString(destination.generic_string().c_str()), loadError);
            if (!loaded)
            {
                outError = "書き出した接地用の高さ格子を読み直せません: ";
                outError += loadError;
                return false;
            }
            struct RegionSummary
            {
                uint64_t CoveredBlocks = 0;
                uint64_t ExpectedBlocks = 0;
                uint64_t BakedSamples = 0;
                uint64_t Samples = 0;
            };
            RegionSummary regionSummaries[sizeof(kCastlePlacementRegions) / sizeof(kCastlePlacementRegions[0])] = {};
            Container::VariableArray<uint8_t> castleBlockMap(static_cast<size_t>(castleBlocksX) * castleBlocksZ, 0);
            for (const auto &block : castleWritten)
            {
                castleBlockMap[static_cast<size_t>(block.Z) * castleBlocksX + block.X] = 1;
            }
            for (size_t i = 0; i < sizeof(kCastlePlacementRegions) / sizeof(kCastlePlacementRegions[0]); ++i)
            {
                const auto &region = kCastlePlacementRegions[i];
                auto &summary = regionSummaries[i];
                for (uint32_t bz = 0; bz < castleBlocksZ; ++bz)
                {
                    for (uint32_t bx = 0; bx < castleBlocksX; ++bx)
                    {
                        const double x0 = castleBounds.MinX + bx * kCastleBlockMeters;
                        const double z0 = castleBounds.MinZ + bz * kCastleBlockMeters;
                        if (PlacementOverlaps(x0, std::min(x0 + kCastleBlockMeters, castleBounds.MaxX),
                                              z0, std::min(z0 + kCastleBlockMeters, castleBounds.MaxZ), region.Bounds, 0.0))
                        {
                            ++summary.ExpectedBlocks;
                            summary.CoveredBlocks += castleBlockMap[static_cast<size_t>(bz) * castleBlocksX + bx];
                        }
                    }
                }
                for (double z = region.Bounds.MinZ; z <= region.Bounds.MaxZ; z += 4.0)
                {
                    for (double x = region.Bounds.MinX; x <= region.Bounds.MaxX; x += 4.0)
                    {
                        float height = 0.0f;
                        if (loaded->Sample(static_cast<float>(x), static_cast<float>(z), height) ==
                            Terrain::PlacementGroundSample::Baked)
                        {
                            ++summary.BakedSamples;
                        }
                        ++summary.Samples;
                    }
                }
            }
            size_t castleTileCount = 0;
            for (const uint8_t needed : castleSourceNeeded)
            {
                castleTileCount += needed != 0;
            }
            const auto fileBytes = std::filesystem::file_size(destination);
            float elapsedMs = 0.0f;
            NORVES_STAT_TIME_END(PlacementGround, elapsedMs);
            std::cout << "{\"skipped\":false,\"format_version\":" << kVersion << ",\"block_count\":" << written.size()
                      << ",\"point_count\":" << written.size() * kSamples << ",\"sampled_point_count\":" << sampledCount
                      << ",\"nan_point_count\":" << nanCount << ",\"max_abs_difference\":";
            WriteNumber(differences.empty() ? 0.0 : differences.back());
            std::cout << ",\"p99_abs_difference\":";
            WriteNumber(differences.empty()
                            ? 0.0
                            : differences[static_cast<size_t>(std::ceil(0.99 * differences.size())) - 1u]);
            std::cout << ",\"file_bytes\":" << fileBytes << ",\"seconds\":";
#if NORVES_ENABLE_STATS
            WriteNumber(elapsedMs / 1000.0);
#else
            std::cout << "null";
#endif
            std::cout << ",\"unreliable_cell_count\":" << unreliableCells << ",\"unreliable_cell_ratio\":";
            WriteNumber(written.empty() ? 0.0 : static_cast<double>(unreliableCells) / (written.size() * kCells));
            std::cout << ",\"center_query_cell_count\":" << centerQueryCells
                      << ",\"quarter_query_cell_count\":" << quarterQueryCells
                      << ",\"confidence_probe_sample_count\":" << confidenceProbeSamples
                      << ",\"confidence_query_point_count\":" << confidenceQueryPoints << ",\"confidence_seconds\":";
#if NORVES_ENABLE_STATS
            WriteNumber(confidenceMs / 1000.0);
#else
            std::cout << "null";
#endif
            std::cout << ",\"source_count\":" << nearSourceCount << ",\"tiles\":[";
            bool bFirst = true;
            for (const auto &tile : tiles)
            {
                if (!tile.Report.bCliffDetail && !tile.Report.bCarve && tile.Blocks == 0)
                {
                    continue;
                }
                std::cout << (bFirst ? "" : ",") << "{\"tile\":[" << tile.Report.TileX << "," << tile.Report.TileZ
                          << "],\"block_count\":" << tile.Blocks << "}";
                bFirst = false;
            }
            std::cout << "],\"castle\":{\"block_count\":" << castleWritten.size()
                      << ",\"point_count\":" << castleWritten.size() * kSamples
                      << ",\"nan_point_count\":" << castleSummary.NanCount
                      << ",\"unreliable_cell_count\":" << castleSummary.UnreliableCells
                      << ",\"unreliable_cell_ratio\":";
            WriteNumber(castleWritten.empty() ? 0.0
                                               : static_cast<double>(castleSummary.UnreliableCells) / (castleWritten.size() * kCells));
            std::cout << ",\"seconds\":";
#if NORVES_ENABLE_STATS
            WriteNumber(castleMs / 1000.0);
#else
            std::cout << "null";
#endif
            std::cout << ",\"tile_count\":" << castleTileCount << ",\"regions\":[";
            for (size_t i = 0; i < sizeof(kCastlePlacementRegions) / sizeof(kCastlePlacementRegions[0]); ++i)
            {
                const auto &summary = regionSummaries[i];
                std::cout << (i == 0 ? "" : ",") << "{\"id\":";
                WriteJsonString(kCastlePlacementRegions[i].Id);
                std::cout << ",\"covered_block_count\":" << summary.CoveredBlocks
                          << ",\"expected_block_count\":" << summary.ExpectedBlocks << ",\"baked_sample_ratio\":";
                WriteNumber(summary.Samples == 0 ? 0.0 : static_cast<double>(summary.BakedSamples) / summary.Samples);
                std::cout << "}";
            }
            std::cout << "]}}\n";
            return true;
        }
    } // namespace

    bool RunPlacementGroundWriteCommand(int argc, char **argv, int &outExitCode)
    {
        if (argc < 2 || Container::AnsiStringView(argv[1]).compare("--write-placement-ground") != 0)
        {
            return false;
        }
        outExitCode = 1;
        const char *manifest = nullptr;
        const char *output = nullptr;
        bool bSkipIfCurrent = false;
        for (int i = 2; i < argc; ++i)
        {
            const Container::AnsiStringView option(argv[i]);
            if (option.compare("--skip-if-current") == 0 && !bSkipIfCurrent)
            {
                bSkipIfCurrent = true;
                continue;
            }
            if (i + 1 < argc && ((option.compare("--manifest") == 0 && manifest == nullptr) ||
                                 (option.compare("--out") == 0 && output == nullptr)))
            {
                (option.compare("--manifest") == 0 ? manifest : output) = argv[++i];
            }
            else
            {
                std::cerr << "AssetCook error: --write-placement-ground の引数が不正です\n";
                return true;
            }
        }
        if (manifest == nullptr || output == nullptr || *manifest == 0 || *output == 0)
        {
            std::cerr << "AssetCook error: --manifest <manifest.json> と --out <path> を渡してください\n";
            return true;
        }
        Container::AnsiString error;
        try
        {
            if (WritePlacementGround(manifest, output, bSkipIfCurrent, error))
            {
                outExitCode = 0;
                return true;
            }
        }
        catch (const std::exception &)
        {
            error = "接地用の高さ格子の読み書きまたはメモリの確保に失敗しました";
        }
        NORVES_LOG_ERROR("AssetCook", "%s", error.c_str());
        std::cerr << "AssetCook error: " << error.c_str() << "\n";
        return true;
    }
} // namespace NorvesLib::Tools::AssetCook
