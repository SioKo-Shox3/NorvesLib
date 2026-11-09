#include "Rendering/GrassTileSelector.h"

#include <cassert>
#include <cmath>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        size_t FindChunkIndex(const GrassFieldRenderData& field, int32_t cx, int32_t cz)
        {
            size_t first = 0;
            size_t last = field.Chunks.size();
            while (first < last)
            {
                const size_t middle = first + ((last - first) >> 1);
                const auto& chunk = field.Chunks[middle];
                if (chunk.ChunkZ < cz || (chunk.ChunkZ == cz && chunk.ChunkX < cx))
                {
                    first = middle + 1;
                }
                else
                {
                    last = middle;
                }
            }
            if (first < field.Chunks.size() && field.Chunks[first].ChunkX == cx && field.Chunks[first].ChunkZ == cz)
            {
                return first;
            }
            return field.Chunks.size();
        }

        float DistanceToInterval(float position, float minimum, float maximum)
        {
            if (position < minimum)
            {
                return minimum - position;
            }
            return position > maximum ? position - maximum : 0.0f;
        }
    } // namespace

    void GrassTileSelector::UpdateDensityCache(const GrassFieldRenderData& field)
    {
        if (m_CachedField == &field && m_CachedContentRevision == field.ContentRevision &&
            m_CachedChunks == field.Chunks.data() && m_CachedLayerAPixels == field.LayerAPixels.data() &&
            m_ChunkMaxDensities.size() == field.Chunks.size() &&
            m_CachedLayerAByteCount == field.LayerAPixels.size() && m_CachedLayerAResolution == field.LayerAResolution)
        {
            return;
        }

        m_ChunkMaxDensities.clear();
        m_ChunkMaxDensities.resize(field.Chunks.size(), 0);
        const uint64_t pixelCount = static_cast<uint64_t>(field.LayerAResolution) * field.LayerAResolution;
        for (size_t chunkIndex = 0; chunkIndex < field.Chunks.size(); ++chunkIndex)
        {
            const uint64_t offset = field.Chunks[chunkIndex].LayerAByteOffset;
            // CPU の生成の写しと同じく、画素が欠けたチャンクは使わない。
            if (offset > field.LayerAPixels.size() || pixelCount > (field.LayerAPixels.size() - offset) / 4)
            {
                continue;
            }
            uint8_t maximum = 0;
            for (uint64_t pixel = 0; pixel < pixelCount; ++pixel)
            {
                const uint8_t density = field.LayerAPixels[static_cast<size_t>(offset + pixel * 4)];
                if (density > maximum)
                {
                    maximum = density;
                }
            }
            m_ChunkMaxDensities[chunkIndex] = maximum;
        }
        m_CachedField = &field;
        m_CachedChunks = field.Chunks.data();
        m_CachedLayerAPixels = field.LayerAPixels.data();
        m_CachedContentRevision = field.ContentRevision;
        m_CachedLayerAByteCount = field.LayerAPixels.size();
        m_CachedLayerAResolution = field.LayerAResolution;
    }

    uint32_t GrassTileSelector::Select(const GrassFieldRenderData& field, const GrassGenerateSettings& settings,
        const GrassTileSelectionInput& input, Container::VariableArray<GrassTileInfo>& outTiles)
    {
        outTiles.clear();
        assert(input.WindowSize > 0);
        if (input.WindowSize == 0 || field.Chunks.empty())
        {
            return 0;
        }
        UpdateDensityCache(field);

        const float tileSize = settings.TileSizeMeters;
        const uint32_t tilesPerChunk = static_cast<uint32_t>(std::round(field.ChunkSizeMeters / tileSize));
        assert(tilesPerChunk > 0 && tilesPerChunk <= 0x7fffffffu);
        const double maxDistance = settings.BandMaxDistanceMeters[2];
        // 下端にちょうど接する隣のタイルも含め、帯の距離の等号を保つ。
        const int32_t minTileX = static_cast<int32_t>(std::ceil((input.CameraPosition.x - maxDistance) / tileSize) - 1.0);
        const int32_t minTileZ = static_cast<int32_t>(std::ceil((input.CameraPosition.z - maxDistance) / tileSize) - 1.0);
        const int32_t maxTileX = static_cast<int32_t>(std::floor((input.CameraPosition.x + maxDistance) / tileSize));
        const int32_t maxTileZ = static_cast<int32_t>(std::floor((input.CameraPosition.z + maxDistance) / tileSize));

        float maxHeight = field.Species.empty() ? 1.0f : field.Species[0].HeightMaxMeters;
        for (const auto& species : field.Species)
        {
            if (species.HeightMaxMeters > maxHeight)
            {
                maxHeight = species.HeightMaxMeters;
            }
        }
        const float halfTile = tileSize * 0.5f;
        const float halfHeight = maxHeight * 0.5f;
        const float centerY = settings.GroundHeightMeters + halfHeight;
        const float radius = std::sqrt(2.0f * halfTile * halfTile + halfHeight * halfHeight) + maxHeight;

        // ループ変数だけ広げ、int32_t の最大座標でも終端の加算を安全に行う。
        for (int64_t tileZ = minTileZ; tileZ <= maxTileZ; ++tileZ)
        {
            for (int64_t tileX = minTileX; tileX <= maxTileX; ++tileX)
            {
                const float originX = static_cast<float>(tileX) * tileSize;
                const float originZ = static_cast<float>(tileZ) * tileSize;
                const float dx = DistanceToInterval(input.CameraPosition.x, originX, originX + tileSize);
                const float dz = DistanceToInterval(input.CameraPosition.z, originZ, originZ + tileSize);
                const float distance = std::sqrt(dx * dx + dz * dz);
                uint32_t band = 0;
                while (band < kGrassLodCount && distance > settings.BandMaxDistanceMeters[band])
                {
                    ++band;
                }
                if (band == kGrassLodCount)
                {
                    continue;
                }

                const int32_t cx = GrassFloorDiv(static_cast<int32_t>(tileX), tilesPerChunk);
                const int32_t cz = GrassFloorDiv(static_cast<int32_t>(tileZ), tilesPerChunk);
                const size_t chunkIndex = FindChunkIndex(field, cx, cz);
                if (chunkIndex == field.Chunks.size() || m_ChunkMaxDensities[chunkIndex] == 0)
                {
                    continue;
                }

                const float centerX = originX + halfTile;
                const float centerZ = originZ + halfTile;
                bool bOutside = false;
                for (const auto& plane : input.FrustumPlanes)
                {
                    if (plane[0] * centerX + plane[1] * centerY + plane[2] * centerZ + plane[3] < -radius)
                    {
                        bOutside = true;
                        break;
                    }
                }
                if (bOutside)
                {
                    continue;
                }

                GrassTileInfo tile{};
                tile.OriginX = originX;
                tile.OriginZ = originZ;
                tile.GroundY = settings.GroundHeightMeters;
                tile.Band = band;
                tile.LayerIndex = GrassWindowLayerIndex(cx, cz, input.WindowSize);
                tile.Flags = 0;
                tile.TileX = static_cast<int32_t>(tileX);
                tile.TileZ = static_cast<int32_t>(tileZ);
                outTiles.push_back(tile);
            }
        }
        return static_cast<uint32_t>(outTiles.size());
    }
} // namespace NorvesLib::Core::Rendering
