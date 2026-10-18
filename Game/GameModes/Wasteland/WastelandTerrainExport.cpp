#include "GameModes/Wasteland/WastelandTerrainExport.h"

#include "Core/Public/Asset/CookedMeshFormat.h"
#include "Core/Public/FileStream/FileStream.h"
#include "Core/Public/Logging/LogMacros.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Container;

namespace Game::GameModes::WastelandTerrainExport
{
    namespace
    {
        bool WriteBytes(const String &path, const void *pData, size_t byteCount)
        {
            NorvesLib::FileStream::FileStreamUniquePtr stream = NorvesLib::FileStream::FileStream::CreateUnique(
                path, NorvesLib::FileStream::FileMode::Write, NorvesLib::FileStream::FileAccess::Write,
                NorvesLib::FileStream::FileShare::None);
            if (!stream)
            {
                return false;
            }
            if (stream->Write(pData, byteCount) != byteCount)
            {
                return false;
            }
            stream->Flush();
            return true;
        }

        /// 論理パスの末尾の名前から拡張子を除いたもの（Assets/Models/WastelandTerrain/WastelandDem_3_4.generated → WastelandDem_3_4）
        String TileNameFromLogicalPath(const String &logicalPath)
        {
            size_t start = 0;
            size_t end = logicalPath.size();
            for (size_t i = 0; i < logicalPath.size(); ++i)
            {
                if (logicalPath[i] == '/' || logicalPath[i] == '\\')
                {
                    start = i + 1;
                    end = logicalPath.size();
                }
                else if (logicalPath[i] == '.')
                {
                    end = i;
                }
            }
            return logicalPath.substr(start, end > start ? end - start : 0);
        }

        void AppendBytes(VariableArray<uint8_t> &bytes, const void *pData, size_t byteCount)
        {
            const uint8_t *begin = static_cast<const uint8_t *>(pData);
            bytes.insert(bytes.end(), begin, begin + byteCount);
        }

        void AppendText(VariableArray<char> &text, const char *pText)
        {
            text.insert(text.end(), pText, pText + std::strlen(pText));
        }
    } // namespace

    bool ExportTile(State &state, const String &logicalPath, const Asset::CookedMeshData &mesh)
    {
        if (!state.IsEnabled())
        {
            return false;
        }
        TileRecord record;
        record.Name = TileNameFromLogicalPath(logicalPath);
        record.LogicalPath = logicalPath;
        record.LodLevelCount = mesh.LODLevelCount;
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            record.BoundsMin[axis] = std::numeric_limits<float>::max();
            record.BoundsMax[axis] = -std::numeric_limits<float>::max();
        }

        // LOD 0 のクラスタが使う頂点だけを、最初に使われた順に詰め直す（クラスタの頂点の範囲は全体の頂点への添字）
        constexpr uint32_t kUnused = std::numeric_limits<uint32_t>::max();
        VariableArray<uint32_t> remap(mesh.Vertices.size(), kUnused);
        VariableArray<uint32_t> usedVertices;
        VariableArray<uint32_t> triangles;
        for (const Asset::CookedMeshCluster &cluster : mesh.Clusters)
        {
            if (cluster.LODLevel != 0)
            {
                continue;
            }
            if (static_cast<uint64_t>(cluster.IndexOffset) + cluster.IndexCount > mesh.Indices.size() ||
                static_cast<uint64_t>(cluster.VertexOffset) + cluster.VertexCount > mesh.Vertices.size() ||
                cluster.IndexCount % 3 != 0)
            {
                LOG_WARNING_F("WASTELAND_TERRAIN_EXPORT_FAILED path=%s クラスタの範囲がメッシュの外を指します",
                              logicalPath.c_str());
                ++state.FailedTiles;
                return false;
            }
            ++record.ClusterCount;
            record.MaxLodError = std::max(record.MaxLodError, cluster.LODError);
            for (uint32_t i = 0; i < cluster.IndexCount; ++i)
            {
                const uint32_t local = mesh.Indices[cluster.IndexOffset + i];
                if (local >= cluster.VertexCount)
                {
                    LOG_WARNING_F("WASTELAND_TERRAIN_EXPORT_FAILED path=%s クラスタのインデックスが頂点の範囲の外です",
                                  logicalPath.c_str());
                    ++state.FailedTiles;
                    return false;
                }
                const uint32_t global = cluster.VertexOffset + local;
                if (remap[global] == kUnused)
                {
                    remap[global] = static_cast<uint32_t>(usedVertices.size());
                    usedVertices.push_back(global);
                }
                triangles.push_back(remap[global]);
            }
        }
        record.VertexCount = usedVertices.size();
        record.TriangleCount = triangles.size() / 3;
        if (record.TriangleCount == 0)
        {
            LOG_WARNING_F("WASTELAND_TERRAIN_EXPORT_FAILED path=%s LOD 0 のクラスタがありません", logicalPath.c_str());
            ++state.FailedTiles;
            return false;
        }

        // 見出し（ASCII）と本体（頂点 24 B・三角形 13 B）
        char header[512] = {};
        const int headerLength = std::snprintf(
            header, sizeof(header),
            "ply\nformat binary_little_endian 1.0\n"
            "comment NorvesLib wasteland terrain LOD0 source=%s\n"
            "comment space=engine_world units=m axes=+X_east,+Y_up,+Z_south\n"
            "element vertex %llu\nproperty float x\nproperty float y\nproperty float z\n"
            "property float nx\nproperty float ny\nproperty float nz\n"
            "element face %llu\nproperty list uchar int vertex_indices\nend_header\n",
            logicalPath.c_str(), static_cast<unsigned long long>(record.VertexCount),
            static_cast<unsigned long long>(record.TriangleCount));
        if (headerLength <= 0 || headerLength >= static_cast<int>(sizeof(header)))
        {
            ++state.FailedTiles;
            return false;
        }
        VariableArray<uint8_t> bytes;
        bytes.reserve(static_cast<size_t>(headerLength) + usedVertices.size() * 24u + triangles.size() / 3u * 13u);
        AppendBytes(bytes, header, static_cast<size_t>(headerLength));
        for (const uint32_t global : usedVertices)
        {
            const Asset::CookedMeshVertex &vertex = mesh.Vertices[global];
            const float values[6] = {vertex.Position.X, vertex.Position.Y, vertex.Position.Z,
                                     vertex.Normal.X,   vertex.Normal.Y,   vertex.Normal.Z};
            AppendBytes(bytes, values, sizeof(values));
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                record.BoundsMin[axis] = std::min(record.BoundsMin[axis], values[axis]);
                record.BoundsMax[axis] = std::max(record.BoundsMax[axis], values[axis]);
            }
        }
        for (size_t i = 0; i + 2 < triangles.size(); i += 3)
        {
            const uint8_t count = 3;
            const int32_t face[3] = {static_cast<int32_t>(triangles[i]), static_cast<int32_t>(triangles[i + 1]),
                                     static_cast<int32_t>(triangles[i + 2])};
            AppendBytes(bytes, &count, sizeof(count));
            AppendBytes(bytes, face, sizeof(face));
        }

        const String path = state.Directory + String("/") + record.Name + String(".ply");
        if (!WriteBytes(path, bytes.data(), bytes.size()))
        {
            LOG_WARNING_F("WASTELAND_TERRAIN_EXPORT_FAILED path=%s 書けません（フォルダ %s があるか確かめてください）",
                          path.c_str(), state.Directory.c_str());
            ++state.FailedTiles;
            return false;
        }
        state.Tiles.push_back(std::move(record));
        return true;
    }

    bool WriteManifest(const State &state)
    {
        if (!state.IsEnabled())
        {
            return false;
        }
        uint64_t totalVertices = 0;
        uint64_t totalTriangles = 0;
        float maxLodError = 0.0f;
        VariableArray<char> json;
        AppendText(json, "{\n  \"format\": \"ply_binary_le_lod0\",\n"
                         "  \"space\": \"engine_world\",\n  \"units\": \"m\",\n"
                         "  \"axes\": {\"x\": \"east\", \"y\": \"up\", \"z\": \"south\"},\n"
                         "  \"origin\": \"Assets/Terrain/ValleyOfTheGods/height.json origin and height_offset_m\",\n"
                         "  \"lod\": \"finest (LOD level 0 clusters, not decimated, not welded)\",\n"
                         "  \"tiles\": [\n");
        char line[1024] = {};
        for (size_t i = 0; i < state.Tiles.size(); ++i)
        {
            const TileRecord &tile = state.Tiles[i];
            totalVertices += tile.VertexCount;
            totalTriangles += tile.TriangleCount;
            maxLodError = std::max(maxLodError, tile.MaxLodError);
            std::snprintf(line, sizeof(line),
                          "    {\"name\": \"%s\", \"file\": \"%s.ply\", \"logical_path\": \"%s\", \"vertices\": %llu, "
                          "\"triangles\": %llu, \"lod0_clusters\": %u, \"lod_levels\": %u, \"lod0_max_error\": %.9g, "
                          "\"bounds_min\": [%.9g, %.9g, %.9g], \"bounds_max\": [%.9g, %.9g, %.9g]}%s\n",
                          tile.Name.c_str(), tile.Name.c_str(), tile.LogicalPath.c_str(),
                          static_cast<unsigned long long>(tile.VertexCount),
                          static_cast<unsigned long long>(tile.TriangleCount), tile.ClusterCount, tile.LodLevelCount,
                          static_cast<double>(tile.MaxLodError), static_cast<double>(tile.BoundsMin[0]),
                          static_cast<double>(tile.BoundsMin[1]), static_cast<double>(tile.BoundsMin[2]),
                          static_cast<double>(tile.BoundsMax[0]), static_cast<double>(tile.BoundsMax[1]),
                          static_cast<double>(tile.BoundsMax[2]), i + 1 < state.Tiles.size() ? "," : "");
            AppendText(json, line);
        }
        std::snprintf(line, sizeof(line),
                      "  ],\n  \"tile_count\": %zu,\n  \"failed_tiles\": %u,\n  \"total_vertices\": %llu,\n"
                      "  \"total_triangles\": %llu\n}\n",
                      state.Tiles.size(), state.FailedTiles, static_cast<unsigned long long>(totalVertices),
                      static_cast<unsigned long long>(totalTriangles));
        AppendText(json, line);

        const String path = state.Directory + String("/manifest.json");
        if (!WriteBytes(path, json.data(), json.size()))
        {
            LOG_WARNING_F("WASTELAND_TERRAIN_EXPORT_FAILED path=%s 一覧を書けません", path.c_str());
            return false;
        }
        LOG_INFO_F("WASTELAND_TERRAIN_EXPORT dir=%s tiles=%zu failed=%u vertices=%llu triangles=%llu lod0_max_error=%.9g",
                   state.Directory.c_str(), state.Tiles.size(), state.FailedTiles,
                   static_cast<unsigned long long>(totalVertices), static_cast<unsigned long long>(totalTriangles),
                   static_cast<double>(maxLodError));
        return state.FailedTiles == 0;
    }
} // namespace Game::GameModes::WastelandTerrainExport
