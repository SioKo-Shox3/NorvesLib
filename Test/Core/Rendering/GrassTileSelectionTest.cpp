// 草のタイル選別の可視性、密度、距離帯と再現性を確かめる。
#include "Rendering/GrassTileSelector.h"
#include "Rendering/CameraViewConstants.h"
#include "Math/MatrixUtils.h"

#include <cmath>
#include <cstring>
#include <iostream>

using namespace NorvesLib::Core::Rendering;
namespace Container = NorvesLib::Core::Container;
namespace Math = NorvesLib::Math;

int main()
{
    GrassFieldRenderData field;
    field.ChunkSizeMeters = 32.0f;
    field.LayerAResolution = 4;
    field.ContentRevision = 1;
    constexpr uint32_t bytesPerChunk = 4 * 4 * 4;
    // 前方の左右のチャンクを、密度ゼロと表の欠落に分ける。
    for (int32_t chunkZ = -4; chunkZ <= 3; ++chunkZ)
    {
        for (int32_t chunkX = -4; chunkX <= 3; ++chunkX)
        {
            if (chunkX == -1 && chunkZ == 1)
            {
                continue;
            }
            GrassFieldChunkRenderData chunk;
            chunk.ChunkX = chunkX;
            chunk.ChunkZ = chunkZ;
            chunk.LayerAByteOffset = static_cast<uint32_t>(field.LayerAPixels.size());
            field.Chunks.push_back(chunk);
            const uint8_t density = chunkX == 0 && chunkZ == 1 ? 0 : 255;
            for (uint32_t pixel = 0; pixel < bytesPerChunk / 4; ++pixel)
            {
                field.LayerAPixels.push_back(density);
                field.LayerAPixels.push_back(255);
                field.LayerAPixels.push_back(128);
                field.LayerAPixels.push_back(0);
            }
        }
    }

    CameraProxy camera;
    camera.PositionX = 0.0f;
    camera.PositionY = 2.0f;
    camera.PositionZ = 0.0f;
    camera.ForwardX = 0.0f;
    camera.ForwardY = 0.0f;
    camera.ForwardZ = 1.0f;
    camera.UpX = 0.0f;
    camera.UpY = 1.0f;
    camera.UpZ = 0.0f;
    camera.FieldOfView = 90.0f;
    camera.NearPlane = 0.1f;
    camera.FarPlane = 200.0f;
    const auto constants = CameraViewConstants::BuildForDevice(camera, 1.0f, nullptr);
    const auto frustum = Math::MatrixUtils::ExtractClipSpaceFrustumPlanes(
        constants.CullingViewProjectionMatrix, Math::ClipSpaceDepthRange::ZeroToOne);
    GrassTileSelectionInput input;
    input.CameraPosition = Math::Vector3(0.0f, 2.0f, 0.0f);
    frustum.CopyToShaderData(input.FrustumPlanes);

    // 密度ゼロ・欠落の中央と有密度の遠景が視錐台内にある配置を保証する。
    const Math::Vector3 visiblePoints[] = {
        Math::Vector3(16.0f, 0.5f, 48.0f),
        Math::Vector3(-16.0f, 0.5f, 48.0f),
        Math::Vector3(4.0f, 0.5f, 84.0f)
    };
    for (const auto& point : visiblePoints)
    {
        for (const auto& plane : input.FrustumPlanes)
        {
            if (plane[0] * point.x + plane[1] * point.y + plane[2] * point.z + plane[3] <= 0.0f)
            {
                std::cout << "失敗: 試験用チャンクの中央が視錐台内にありません\n";
                return 1;
            }
        }
    }

    const GrassGenerateSettings settings;
    GrassTileSelector selector;
    Container::VariableArray<GrassTileInfo> tiles;
    const uint32_t count = selector.Select(field, settings, input, tiles);
    if (tiles.empty() || count != tiles.size())
    {
        std::cout << "失敗: 選別結果が空、または戻り値とタイル数が一致しません\n";
        return 1;
    }

    uint32_t bandCounts[3] = {};
    bool bHasVisibleDenseTile = false;
    for (const auto& tile : tiles)
    {
        // 草丈とタイルの保守的な境界球を考慮しても、十分後方にある矩形は見えない。
        if (tile.OriginZ + settings.TileSizeMeters <= -16.0f)
        {
            std::cout << "失敗: 視錐台外の後方タイルが選ばれました\n";
            return 1;
        }
        const int32_t chunkX = static_cast<int32_t>(std::floor(tile.OriginX / field.ChunkSizeMeters));
        const int32_t chunkZ = static_cast<int32_t>(std::floor(tile.OriginZ / field.ChunkSizeMeters));
        if (chunkX == 0 && chunkZ == 1)
        {
            std::cout << "失敗: 最大密度がゼロのチャンクのタイルが選ばれました\n";
            return 1;
        }
        if ((chunkX == -1 && chunkZ == 1) || chunkX < -4 || chunkX > 3 || chunkZ < -4 || chunkZ > 3)
        {
            std::cout << "失敗: 表に無いチャンクのタイルが選ばれました\n";
            return 1;
        }

        // カメラを矩形に射影し、最近点までの水平距離を独立に求める。
        const float nearestX = std::fmax(tile.OriginX, std::fmin(input.CameraPosition.x,
            tile.OriginX + settings.TileSizeMeters));
        const float nearestZ = std::fmax(tile.OriginZ, std::fmin(input.CameraPosition.z,
            tile.OriginZ + settings.TileSizeMeters));
        const float dx = nearestX - input.CameraPosition.x;
        const float dz = nearestZ - input.CameraPosition.z;
        const float distanceSquared = dx * dx + dz * dz;
        if (distanceSquared > 120.0f * 120.0f)
        {
            std::cout << "失敗: 矩形への水平距離が120 mを超えるタイルが選ばれました\n";
            return 1;
        }
        const uint32_t expectedBand = distanceSquared <= 25.0f * 25.0f ? 0u :
            (distanceSquared <= 60.0f * 60.0f ? 1u : 2u);
        if (tile.Band != expectedBand)
        {
            std::cout << "失敗: タイルの距離帯が矩形への水平最近点距離と一致しません\n";
            return 1;
        }
        ++bandCounts[tile.Band];
        bHasVisibleDenseTile |= tile.TileX == 0 && tile.TileZ == 10;
    }
    if (!bHasVisibleDenseTile || bandCounts[0] == 0 || bandCounts[1] == 0 || bandCounts[2] == 0)
    {
        std::cout << "失敗: 視錐台内の有密度タイル、または近・中・遠のいずれかの帯がありません\n";
        return 1;
    }

    Container::VariableArray<GrassTileInfo> repeatedTiles;
    Container::VariableArray<GrassTileInfo> separateTiles;
    GrassTileSelector separateSelector;
    selector.Select(field, settings, input, repeatedTiles);
    separateSelector.Select(field, settings, input, separateTiles);
    if (tiles.size() != repeatedTiles.size() || tiles.size() != separateTiles.size() ||
        std::memcmp(tiles.data(), repeatedTiles.data(), tiles.size() * sizeof(GrassTileInfo)) != 0 ||
        std::memcmp(tiles.data(), separateTiles.data(), tiles.size() * sizeof(GrassTileInfo)) != 0)
    {
        std::cout << "失敗: 同一入力の再選別または別インスタンスの結果がバイト一致しません\n";
        return 1;
    }
    std::cout << "GrassTileSelectionTest: 可視性・密度・距離帯・再現性を確認しました（近・中・遠: "
              << bandCounts[0] << "・" << bandCounts[1] << "・" << bandCounts[2] << "）\n";
    return 0;
}
