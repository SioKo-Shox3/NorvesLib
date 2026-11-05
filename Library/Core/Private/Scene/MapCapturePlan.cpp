#include "Scene/MapCapturePlan.h"

#include "Rendering/AutoExposure.h"
#include "Text/JsonWriter.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace NorvesLib::Core::Scene
{
    namespace
    {
        bool SetError(Container::String* outError, const TCHAR* message)
        {
            if (outError != nullptr)
            {
                *outError = message;
            }
            return false;
        }

        bool GetPixelDimension(double minimum, double maximum, double pixelSize, uint32_t& dimension)
        {
            const double span = maximum - minimum;
            const double pixels = std::isfinite(span) ? span / pixelSize : maximum / pixelSize - minimum / pixelSize;
            if (!std::isfinite(pixels))
            {
                return false;
            }
            const double rounded = std::round(pixels);
            if (rounded < 1.0 || rounded > static_cast<double>(UINT32_MAX) || std::abs(pixels - rounded) > 1e-4)
            {
                return false;
            }
            dimension = static_cast<uint32_t>(rounded);
            return true;
        }

        bool ToFiniteFloat(double value, float& result)
        {
            if (!std::isfinite(value) || std::abs(value) > static_cast<double>((std::numeric_limits<float>::max)()))
            {
                return false;
            }
            result = static_cast<float>(value);
            return std::isfinite(result);
        }

        MapWorldXZ PixelToWorld(const MapTransform& transform, double x, double y)
        {
            return MapUVToWorld(transform, {x / transform.ImageWidth, y / transform.ImageHeight});
        }
    }

    bool BuildMapCapturePlan(const MapCaptureSpec& spec, MapCapturePlan& outPlan, Container::String* outError)
    {
        if (!std::isfinite(spec.WorldMinX) || !std::isfinite(spec.WorldMinZ) ||
            !std::isfinite(spec.WorldMaxX) || !std::isfinite(spec.WorldMaxZ) ||
            !std::isfinite(spec.PixelSizeMeters) || !std::isfinite(spec.MinHeightMeters) ||
            !std::isfinite(spec.MaxHeightMeters) || !std::isfinite(spec.FixedEV100))
        {
            return SetError(outError, TEXT("撮影の設定には有限の数値を指定してください。"));
        }
        if (spec.PixelSizeMeters <= 0.0 || spec.WorldMinX >= spec.WorldMaxX || spec.WorldMinZ >= spec.WorldMaxZ)
        {
            return SetError(outError, TEXT("画素サイズと撮影範囲は正の大きさにしてください。"));
        }
        if (spec.MinHeightMeters > spec.MaxHeightMeters || spec.OutputDirectory.empty())
        {
            return SetError(outError, TEXT("高さの範囲と出力先を確認してください。"));
        }
        const uint64_t marginTwice = static_cast<uint64_t>(spec.TileMarginPixels) * 2u;
        if (marginTwice >= spec.BackBufferWidth || marginTwice >= spec.BackBufferHeight)
        {
            return SetError(outError, TEXT("バックバッファの寸法と余白を確認してください。"));
        }

        MapCapturePlan plan;
        plan.Spec = spec;
        plan.Transform.WorldMinX = spec.WorldMinX;
        plan.Transform.WorldMinZ = spec.WorldMinZ;
        plan.Transform.WorldMaxX = spec.WorldMaxX;
        plan.Transform.WorldMaxZ = spec.WorldMaxZ;
        plan.Transform.PixelSizeMeters = spec.PixelSizeMeters;
        // CreateLookAt と Vulkan の投影の Y 反転により、真上の画像は上が +Z、右が -X になる。
        plan.Transform.UpAxis = MapAxis::PositiveZ;
        plan.Transform.RightAxis = MapAxis::NegativeX;
        if (!GetPixelDimension(spec.WorldMinX, spec.WorldMaxX, spec.PixelSizeMeters, plan.Transform.ImageWidth) ||
            !GetPixelDimension(spec.WorldMinZ, spec.WorldMaxZ, spec.PixelSizeMeters, plan.Transform.ImageHeight) ||
            !ValidateMapTransform(plan.Transform))
        {
            return SetError(outError, TEXT("撮影範囲を画素の整数寸法で表せません。"));
        }

        const uint32_t innerWidth = spec.BackBufferWidth - static_cast<uint32_t>(marginTwice);
        const uint32_t innerHeight = spec.BackBufferHeight - static_cast<uint32_t>(marginTwice);
        plan.Columns = (plan.Transform.ImageWidth - 1u) / innerWidth + 1u;
        plan.Rows = (plan.Transform.ImageHeight - 1u) / innerHeight + 1u;
        const uint64_t tileCount = static_cast<uint64_t>(plan.Columns) * plan.Rows;
        if (tileCount > plan.Tiles.max_size())
        {
            return SetError(outError, TEXT("タイル数が表現できる範囲を超えています。"));
        }

        const double clearance = (std::max)(10.0, 0.05 * (spec.MaxHeightMeters - spec.MinHeightMeters));
        const double cameraY = spec.MaxHeightMeters + clearance;
        float cameraYFloat = 0.0f;
        if (!ToFiniteFloat(spec.BackBufferWidth * spec.PixelSizeMeters, plan.OrthoWidth) || plan.OrthoWidth <= 0.0f ||
            !ToFiniteFloat(spec.BackBufferHeight * spec.PixelSizeMeters, plan.OrthoHeight) || plan.OrthoHeight <= 0.0f ||
            !ToFiniteFloat(0.5 * clearance, plan.NearPlane) || plan.NearPlane <= 0.0f ||
            !ToFiniteFloat((cameraY - spec.MinHeightMeters) + clearance, plan.FarPlane) ||
            plan.FarPlane <= plan.NearPlane || !ToFiniteFloat(cameraY, cameraYFloat))
        {
            return SetError(outError, TEXT("カメラの位置または投影の値を表現できません。"));
        }

        plan.Tiles.reserve(static_cast<size_t>(tileCount));
        for (uint32_t row = 0; row < plan.Rows; ++row)
        {
            for (uint32_t column = 0; column < plan.Columns; ++column)
            {
                MapCaptureTile tile;
                tile.Column = column;
                tile.Row = row;
                tile.MapPixelX = column * innerWidth;
                tile.MapPixelY = row * innerHeight;
                tile.CropX = spec.TileMarginPixels;
                tile.CropY = spec.TileMarginPixels;
                tile.CropWidth = (std::min)(innerWidth, plan.Transform.ImageWidth - tile.MapPixelX);
                tile.CropHeight = (std::min)(innerHeight, plan.Transform.ImageHeight - tile.MapPixelY);
                const MapWorldXZ first = PixelToWorld(plan.Transform, tile.MapPixelX, tile.MapPixelY);
                const MapWorldXZ last = PixelToWorld(plan.Transform,
                    static_cast<double>(tile.MapPixelX) + tile.CropWidth,
                    static_cast<double>(tile.MapPixelY) + tile.CropHeight);
                tile.WorldMinX = (std::min)(first.X, last.X);
                tile.WorldMinZ = (std::min)(first.Z, last.Z);
                tile.WorldMaxX = (std::max)(first.X, last.X);
                tile.WorldMaxZ = (std::max)(first.Z, last.Z);
                const MapWorldXZ center = PixelToWorld(plan.Transform,
                    static_cast<double>(tile.MapPixelX) + innerWidth * 0.5,
                    static_cast<double>(tile.MapPixelY) + innerHeight * 0.5);
                float centerFloat = 0.0f;
                if (!ToFiniteFloat(center.X, centerFloat) || !ToFiniteFloat(center.Z, centerFloat))
                {
                    return SetError(outError, TEXT("タイルのカメラ位置を表現できません。"));
                }
                tile.CameraPositionX = center.X;
                tile.CameraPositionY = cameraY;
                tile.CameraPositionZ = center.Z;
                Container::StringBuilder fileName;
                fileName.AppendFormat(TEXT("tile_c%02u_r%02u.png"), column, row);
                tile.FileName = fileName.ToString();
                plan.Tiles.push_back(std::move(tile));
            }
        }
        outPlan = std::move(plan);
        if (outError != nullptr)
        {
            outError->clear();
        }
        return true;
    }

    Rendering::CameraProxy MakeMapCaptureCamera(const MapCapturePlan& plan, const MapCaptureTile& tile)
    {
        Rendering::CameraProxy camera;
        camera.PositionX = static_cast<float>(tile.CameraPositionX);
        camera.PositionY = static_cast<float>(tile.CameraPositionY);
        camera.PositionZ = static_cast<float>(tile.CameraPositionZ);
        camera.ForwardX = 0.0f;
        camera.ForwardY = -1.0f;
        camera.ForwardZ = 0.0f;
        camera.UpX = 0.0f;
        camera.UpY = 0.0f;
        camera.UpZ = 1.0f;
        camera.RightX = -1.0f;
        camera.RightY = 0.0f;
        camera.RightZ = 0.0f;
        camera.Projection = Rendering::ProjectionType::Orthographic;
        camera.AspectRatio = static_cast<float>(plan.Spec.BackBufferWidth) / static_cast<float>(plan.Spec.BackBufferHeight);
        camera.OrthoWidth = plan.OrthoWidth;
        camera.OrthoHeight = plan.OrthoHeight;
        camera.NearPlane = plan.NearPlane;
        camera.FarPlane = plan.FarPlane;
        camera.Viewport.Width = static_cast<float>(plan.Spec.BackBufferWidth);
        camera.Viewport.Height = static_cast<float>(plan.Spec.BackBufferHeight);
        camera.CullingMask = Rendering::RenderLayer::MapVisible;
        camera.ExposureMode = Rendering::CameraExposureMode::Manual;
        camera.EV100 = plan.Spec.FixedEV100;
        camera.Exposure = Rendering::AutoExposurePreExposureFromEV100(camera.EV100);
        camera.PreExposure = camera.Exposure;
        camera.InvPreExposure = 1.0f / camera.Exposure;
        camera.ProjectionJitterNdcX = 0.0f;
        camera.ProjectionJitterNdcY = 0.0f;
        return camera;
    }

    Container::String WriteMapCaptureManifestJson(const MapCapturePlan& plan)
    {
        if (!ValidateMapTransform(plan.Transform))
        {
            return {};
        }
        JsonWriter writer(true);
        writer.BeginObject();
        writer.WriteString("format", "norves.map_capture_manifest");
        writer.WriteUInt64("version", 1);
        writer.BeginObject("settings");
        writer.WriteNumber("pixel_size_m", plan.Spec.PixelSizeMeters);
        writer.WriteUInt64("tile_margin_px", plan.Spec.TileMarginPixels);
        writer.WriteUInt64("back_buffer_width", plan.Spec.BackBufferWidth);
        writer.WriteUInt64("back_buffer_height", plan.Spec.BackBufferHeight);
        writer.WriteNumber("min_height_m", plan.Spec.MinHeightMeters);
        writer.WriteNumber("max_height_m", plan.Spec.MaxHeightMeters);
        writer.WriteNumber("ev100", plan.Spec.FixedEV100);
        writer.WriteUInt64("settle_frames", plan.Spec.SettleFrames);
        writer.WriteString("output_directory", plan.Spec.OutputDirectory);
        writer.WriteString("projection", "orthographic");
        writer.BeginArray("forward");
        writer.WriteNumberElement(0);
        writer.WriteNumberElement(-1);
        writer.WriteNumberElement(0);
        writer.EndArray();
        writer.BeginArray("up");
        writer.WriteNumberElement(0);
        writer.WriteNumberElement(0);
        writer.WriteNumberElement(1);
        writer.EndArray();
        writer.WriteString("culling_mask", "MapVisible");
        writer.WriteNumber("ortho_width", plan.OrthoWidth);
        writer.WriteNumber("ortho_height", plan.OrthoHeight);
        writer.WriteNumber("near", plan.NearPlane);
        writer.WriteNumber("far", plan.FarPlane);
        writer.EndObject();
        writer.BeginObject("map_transform");
        WriteMapTransformJsonFields(writer, plan.Transform);
        writer.EndObject();
        writer.BeginObject("grid");
        writer.WriteUInt64("columns", plan.Columns);
        writer.WriteUInt64("rows", plan.Rows);
        writer.WriteUInt64("map_width_px", plan.Transform.ImageWidth);
        writer.WriteUInt64("map_height_px", plan.Transform.ImageHeight);
        writer.EndObject();
        writer.BeginArray("tiles");
        for (const MapCaptureTile& tile : plan.Tiles)
        {
            writer.BeginObject();
            writer.WriteUInt64("column", tile.Column);
            writer.WriteUInt64("row", tile.Row);
            writer.WriteString("file", tile.FileName);
            writer.BeginObject("world_rect");
            writer.WriteNumber("min_x", tile.WorldMinX);
            writer.WriteNumber("min_z", tile.WorldMinZ);
            writer.WriteNumber("max_x", tile.WorldMaxX);
            writer.WriteNumber("max_z", tile.WorldMaxZ);
            writer.EndObject();
            writer.BeginObject("map_pixel");
            writer.WriteUInt64("x", tile.MapPixelX);
            writer.WriteUInt64("y", tile.MapPixelY);
            writer.EndObject();
            writer.BeginObject("crop");
            writer.WriteUInt64("x", tile.CropX);
            writer.WriteUInt64("y", tile.CropY);
            writer.WriteUInt64("width", tile.CropWidth);
            writer.WriteUInt64("height", tile.CropHeight);
            writer.EndObject();
            writer.BeginArray("camera_position");
            writer.WriteNumberElement(tile.CameraPositionX);
            writer.WriteNumberElement(tile.CameraPositionY);
            writer.WriteNumberElement(tile.CameraPositionZ);
            writer.EndArray();
            writer.EndObject();
        }
        writer.EndArray();
        writer.EndObject();
        return writer.IsComplete() ? writer.ToString() : Container::String{};
    }
} // namespace NorvesLib::Core::Scene
