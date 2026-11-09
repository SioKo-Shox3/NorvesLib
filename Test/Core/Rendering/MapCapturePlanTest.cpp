// 地図の格子・被覆・撮影位置・投影の向きと不正な入力の拒否を確かめる。
#include "Scene/MapCapturePlan.h"
#include "Rendering/AutoExposure.h"
#include "Rendering/CameraViewConstants.h"
#include "Math/MatrixUtils.h"
#include "Text/JsonDocument.h"
#include <cmath>
#include <iostream>
#include <limits>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Scene;
using namespace NorvesLib::Core::Rendering;
namespace Math = NorvesLib::Math;

#define CHECK_CAPTURE(condition) \
    do \
    { \
        if (!(condition)) \
        { \
            std::cerr << "検証失敗: " << #condition << " (行 " << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (false)

namespace
{
    MapCaptureSpec MakeSpec()
    {
        MapCaptureSpec spec;
        spec.WorldMinX = -2048.0;
        spec.WorldMinZ = -2048.0;
        spec.WorldMaxX = 2048.0;
        spec.WorldMaxZ = 2048.0;
        spec.PixelSizeMeters = 0.5;
        spec.TileMarginPixels = 64;
        spec.BackBufferWidth = 1280;
        spec.BackBufferHeight = 720;
        spec.MinHeightMeters = 0.0;
        spec.MaxHeightMeters = 300.0;
        spec.FixedEV100 = 14.0f;
        spec.SettleFrames = 8;
        spec.OutputDirectory = TEXT("Maps/Test");
        return spec;
    }

    bool Near(double lhs, double rhs, double tolerance)
    {
        return std::abs(lhs - rhs) <= tolerance;
    }

    bool SameTile(const MapCaptureTile& lhs, const MapCaptureTile& rhs)
    {
        return lhs.Column == rhs.Column && lhs.Row == rhs.Row &&
            lhs.WorldMinX == rhs.WorldMinX && lhs.WorldMinZ == rhs.WorldMinZ &&
            lhs.WorldMaxX == rhs.WorldMaxX && lhs.WorldMaxZ == rhs.WorldMaxZ &&
            lhs.MapPixelX == rhs.MapPixelX && lhs.MapPixelY == rhs.MapPixelY &&
            lhs.CropX == rhs.CropX && lhs.CropY == rhs.CropY &&
            lhs.CropWidth == rhs.CropWidth && lhs.CropHeight == rhs.CropHeight &&
            lhs.CameraPositionX == rhs.CameraPositionX && lhs.CameraPositionY == rhs.CameraPositionY &&
            lhs.CameraPositionZ == rhs.CameraPositionZ && lhs.FileName == rhs.FileName;
    }

    bool TestGridAndCoverage(const MapCapturePlan& plan)
    {
        CHECK_CAPTURE(plan.Columns == 8 && plan.Rows == 14 && plan.Tiles.size() == 112);
        CHECK_CAPTURE(plan.Transform.ImageWidth == 8192 && plan.Transform.ImageHeight == 8192);
        uint64_t pixelArea = 0;
        double worldArea = 0.0;
        for (size_t i = 0; i < plan.Tiles.size(); ++i)
        {
            const MapCaptureTile& tile = plan.Tiles[i];
            CHECK_CAPTURE(tile.Column == i % plan.Columns && tile.Row == i / plan.Columns);
            CHECK_CAPTURE(tile.CropX == 64 && tile.CropY == 64);
            CHECK_CAPTURE(tile.CropWidth > 0 && tile.CropHeight > 0);
            CHECK_CAPTURE(tile.MapPixelX + tile.CropWidth <= 8192 && tile.MapPixelY + tile.CropHeight <= 8192);
            CHECK_CAPTURE(tile.WorldMinX >= -2048.0 && tile.WorldMinZ >= -2048.0 &&
                          tile.WorldMaxX <= 2048.0 && tile.WorldMaxZ <= 2048.0);
            CHECK_CAPTURE(tile.WorldMinX < tile.WorldMaxX && tile.WorldMinZ < tile.WorldMaxZ);
            pixelArea += static_cast<uint64_t>(tile.CropWidth) * tile.CropHeight;
            worldArea += (tile.WorldMaxX - tile.WorldMinX) * (tile.WorldMaxZ - tile.WorldMinZ);
            for (size_t j = 0; j < i; ++j)
            {
                const MapCaptureTile& other = plan.Tiles[j];
                const bool pixelOverlap = tile.MapPixelX < other.MapPixelX + other.CropWidth &&
                    other.MapPixelX < tile.MapPixelX + tile.CropWidth &&
                    tile.MapPixelY < other.MapPixelY + other.CropHeight &&
                    other.MapPixelY < tile.MapPixelY + tile.CropHeight;
                const bool worldOverlap = tile.WorldMinX < other.WorldMaxX && other.WorldMinX < tile.WorldMaxX &&
                    tile.WorldMinZ < other.WorldMaxZ && other.WorldMinZ < tile.WorldMaxZ;
                CHECK_CAPTURE(!pixelOverlap && !worldOverlap);
            }
        }
        CHECK_CAPTURE(pixelArea == uint64_t{8192} * 8192);
        CHECK_CAPTURE(Near(worldArea, 4096.0 * 4096.0, 112.0e-6));
        CHECK_CAPTURE(plan.Tiles[0].FileName == TEXT("tile_c00_r00.png"));
        CHECK_CAPTURE(plan.Tiles[111].FileName == TEXT("tile_c07_r13.png"));
        CHECK_CAPTURE(plan.Tiles[111].CropWidth == 128 && plan.Tiles[111].CropHeight == 496);
        return true;
    }

    bool TestDeterminismAndManifest(const MapCapturePlan& first)
    {
        MapCapturePlan second;
        CHECK_CAPTURE(BuildMapCapturePlan(MakeSpec(), second));
        const Container::String manifest = WriteMapCaptureManifestJson(first);
        CHECK_CAPTURE(!manifest.empty() && manifest == WriteMapCaptureManifestJson(second));
        CHECK_CAPTURE(first.Tiles.size() == second.Tiles.size());
        for (size_t i = 0; i < first.Tiles.size(); ++i)
        {
            CHECK_CAPTURE(SameTile(first.Tiles[i], second.Tiles[i]));
        }
        JsonDocument document;
        CHECK_CAPTURE(JsonDocument::TryParse(manifest, document));
        const JsonValue root = document.GetRoot();
        CHECK_CAPTURE(root.FindMember("format").AsString() == TEXT("norves.map_capture_manifest"));
        CHECK_CAPTURE(root.FindMember("version").AsNumber() == 1.0);
        CHECK_CAPTURE(root.FindMember("tiles").IsArray() && root.FindMember("tiles").GetArraySize() == 112);
        const JsonValue transform = root.FindMember("map_transform");
        CHECK_CAPTURE(transform.FindMember("format").AsString() == TEXT("norves.map_transform"));
        CHECK_CAPTURE(transform.FindMember("version").AsNumber() == 1.0);
        CHECK_CAPTURE(transform.FindMember("world_rect").FindMember("min_x").AsNumber() == -2048.0);
        CHECK_CAPTURE(transform.FindMember("pixel_size_m").AsNumber() == 0.5);
        CHECK_CAPTURE(transform.FindMember("image_width").AsUInt32() == 8192);
        CHECK_CAPTURE(transform.FindMember("image_height").AsUInt32() == 8192);
        CHECK_CAPTURE(transform.FindMember("up_axis").AsString() == TEXT("+z"));
        CHECK_CAPTURE(transform.FindMember("right_axis").AsString() == TEXT("-x"));
        return true;
    }

    bool TestCamera(const MapCapturePlan& plan)
    {
        const CameraProxy camera = MakeMapCaptureCamera(plan, plan.Tiles[0]);
        CHECK_CAPTURE(camera.Projection == ProjectionType::Orthographic);
        CHECK_CAPTURE(camera.ForwardX == 0.0f && camera.ForwardY == -1.0f && camera.ForwardZ == 0.0f);
        CHECK_CAPTURE(camera.UpX == 0.0f && camera.UpY == 0.0f && camera.UpZ == 1.0f);
        CHECK_CAPTURE(camera.RightX == -1.0f && camera.RightY == 0.0f && camera.RightZ == 0.0f);
        CHECK_CAPTURE(camera.AspectRatio == 1280.0f / 720.0f);
        CHECK_CAPTURE(camera.Viewport.Width == 1280.0f && camera.Viewport.Height == 720.0f);
        CHECK_CAPTURE(camera.CullingMask == RenderLayer::MapVisible);
        CHECK_CAPTURE(camera.ExposureMode == CameraExposureMode::Manual && camera.EV100 == 14.0f);
        CHECK_CAPTURE(camera.Exposure == AutoExposurePreExposureFromEV100(14.0f));
        CHECK_CAPTURE(camera.PreExposure == camera.Exposure && camera.InvPreExposure == 1.0f / camera.Exposure);
        CHECK_CAPTURE(camera.ProjectionJitterNdcX == 0.0f && camera.ProjectionJitterNdcY == 0.0f);
        CHECK_CAPTURE(plan.OrthoWidth == 640.0f && plan.OrthoHeight == 360.0f);
        CHECK_CAPTURE(camera.PositionY == 315.0f && plan.NearPlane == 7.5f && plan.FarPlane == 330.0f);
        const Math::Matrix4x4 projection = CameraViewConstants::BuildProjectionMatrix(camera, 1280.0f / 720.0f);
        const Math::Matrix4x4 expected = Math::MatrixUtils::CreateOrthographic(640.0f, 360.0f, plan.NearPlane, plan.FarPlane);
        CHECK_CAPTURE(Math::MatrixUtils::ApproxEqual(projection, expected, 0.0001f));
        CHECK_CAPTURE(Near(projection.GetRow(1).y, 2.0f / 360.0f, 0.0001));
        return true;
    }

    struct Pixel
    {
        double X = 0.0;
        double Y = 0.0;
    };

    Pixel Project(const CameraProxy& camera, double x, double z)
    {
        const Math::Matrix4x4 projection = CameraViewConstants::BuildProjectionMatrix(camera, 1280.0f / 720.0f);
        const Math::Matrix4x4 correction = Math::MatrixUtils::CreateScale(1.0f, -1.0f, -1.0f);
        const Math::Matrix4x4 view = CameraViewConstants::BuildViewMatrix(camera);
        const Math::Vector4 clip = Math::MatrixUtils::TransformPoint(projection * correction * view,
            Math::Vector3(static_cast<float>(x), 150.0f, static_cast<float>(z)));
        return {(static_cast<double>(clip.x) / clip.w + 1.0) * 640.0,
                (static_cast<double>(clip.y) / clip.w + 1.0) * 360.0};
    }

    bool TestProjectedCorners(const MapCapturePlan& plan)
    {
        const size_t indices[] = {0, 7, 104, 111, 60, 55, 107};
        for (size_t index : indices)
        {
            const MapCaptureTile& tile = plan.Tiles[index];
            const CameraProxy camera = MakeMapCaptureCamera(plan, tile);
            const double x[] = {tile.WorldMaxX, tile.WorldMinX, tile.WorldMaxX, tile.WorldMinX};
            const double z[] = {tile.WorldMaxZ, tile.WorldMaxZ, tile.WorldMinZ, tile.WorldMinZ};
            const double expectedX[] = {static_cast<double>(tile.CropX), static_cast<double>(tile.CropX + tile.CropWidth),
                                        static_cast<double>(tile.CropX), static_cast<double>(tile.CropX + tile.CropWidth)};
            const double expectedY[] = {static_cast<double>(tile.CropY), static_cast<double>(tile.CropY),
                                        static_cast<double>(tile.CropY + tile.CropHeight), static_cast<double>(tile.CropY + tile.CropHeight)};
            for (size_t corner = 0; corner < 4; ++corner)
            {
                const Pixel pixel = Project(camera, x[corner], z[corner]);
                CHECK_CAPTURE(Near(pixel.X, expectedX[corner], 0.01));
                CHECK_CAPTURE(Near(pixel.Y, expectedY[corner], 0.01));
            }
        }
        return true;
    }

    bool TestOrientation(const MapCapturePlan& plan)
    {
        const MapWorldXZ points[] = {{1500.0, 1800.0}, {-1900.0, 300.0}, {200.0, -2000.0}};
        for (size_t i = 0; i < 3; ++i)
        {
            const MapWorldXZ point = points[i];
            const MapCaptureTile* found = nullptr;
            for (const MapCaptureTile& tile : plan.Tiles)
            {
                if (point.X >= tile.WorldMinX && point.X <= tile.WorldMaxX &&
                    point.Z >= tile.WorldMinZ && point.Z <= tile.WorldMaxZ)
                {
                    found = &tile;
                    break;
                }
            }
            CHECK_CAPTURE(found != nullptr);
            const Pixel pixel = Project(MakeMapCaptureCamera(plan, *found), point.X, point.Z);
            const double mapX = pixel.X - found->CropX + found->MapPixelX;
            const double mapY = pixel.Y - found->CropY + found->MapPixelY;
            const MapUV uv = WorldToMapUV(plan.Transform, point.X, point.Z);
            CHECK_CAPTURE(Near(mapX, uv.U * 8192.0, 0.01) && Near(mapY, uv.V * 8192.0, 0.01));
            if (i == 0)
            {
                CHECK_CAPTURE(Near(mapX, 1096.0, 0.01) && Near(mapY, 496.0, 0.01));
            }
        }
        return true;
    }

    bool TestInvalidSpecs()
    {
        MapCapturePlan result;
        result.Columns = 42;
        Container::String error;
        MapCaptureSpec spec = MakeSpec();
        spec.PixelSizeMeters = 0.0;
        CHECK_CAPTURE(!BuildMapCapturePlan(spec, result, &error) && !error.empty() && result.Columns == 42);
        spec = MakeSpec(); spec.WorldMaxX += 0.125;
        CHECK_CAPTURE(!BuildMapCapturePlan(spec, result));
        spec = MakeSpec(); spec.TileMarginPixels = 360;
        CHECK_CAPTURE(!BuildMapCapturePlan(spec, result));
        spec = MakeSpec(); spec.MinHeightMeters = 301.0;
        CHECK_CAPTURE(!BuildMapCapturePlan(spec, result));
        spec = MakeSpec(); spec.OutputDirectory.clear();
        CHECK_CAPTURE(!BuildMapCapturePlan(spec, result));
        spec = MakeSpec(); spec.FixedEV100 = (std::numeric_limits<float>::quiet_NaN)();
        CHECK_CAPTURE(!BuildMapCapturePlan(spec, result));
        spec = MakeSpec(); spec.WorldMaxZ = (std::numeric_limits<double>::infinity)();
        CHECK_CAPTURE(!BuildMapCapturePlan(spec, result));
        spec = MakeSpec(); spec.BackBufferWidth = 0;
        CHECK_CAPTURE(!BuildMapCapturePlan(spec, result));
        return true;
    }
}

int main()
{
    MapCapturePlan plan;
    if (!BuildMapCapturePlan(MakeSpec(), plan) || !TestGridAndCoverage(plan) ||
        !TestDeterminismAndManifest(plan) || !TestCamera(plan) || !TestProjectedCorners(plan) ||
        !TestOrientation(plan) || !TestInvalidSpecs())
    {
        return 1;
    }
    std::cout << "MapCapturePlanTest passed\n";
    return 0;
}
