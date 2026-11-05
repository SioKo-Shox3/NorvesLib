#pragma once

#include "Scene/MapTransform.h"
#include "Rendering/SceneProxy.h"
#include <cstdint>

namespace NorvesLib::Core::Scene
{
    struct MapCaptureSpec
    {
        double WorldMinX = 0.0;
        double WorldMinZ = 0.0;
        double WorldMaxX = 0.0;
        double WorldMaxZ = 0.0;
        double PixelSizeMeters = 0.0;
        uint32_t TileMarginPixels = 0;
        uint32_t BackBufferWidth = 0;
        uint32_t BackBufferHeight = 0;
        double MinHeightMeters = 0.0;
        double MaxHeightMeters = 0.0;
        float FixedEV100 = 0.0f;
        uint32_t SettleFrames = 0;
        Container::String OutputDirectory;
    };

    struct MapCaptureTile
    {
        uint32_t Column = 0;
        uint32_t Row = 0;
        double WorldMinX = 0.0;
        double WorldMinZ = 0.0;
        double WorldMaxX = 0.0;
        double WorldMaxZ = 0.0;
        uint32_t MapPixelX = 0;
        uint32_t MapPixelY = 0;
        uint32_t CropX = 0;
        uint32_t CropY = 0;
        uint32_t CropWidth = 0;
        uint32_t CropHeight = 0;
        double CameraPositionX = 0.0;
        double CameraPositionY = 0.0;
        double CameraPositionZ = 0.0;
        Container::String FileName;
    };

    struct MapCapturePlan
    {
        MapCaptureSpec Spec;
        MapTransform Transform;
        uint32_t Columns = 0;
        uint32_t Rows = 0;
        float OrthoWidth = 0.0f;
        float OrthoHeight = 0.0f;
        float NearPlane = 0.0f;
        float FarPlane = 0.0f;
        Container::VariableArray<MapCaptureTile> Tiles;
    };

    // 失敗時は outPlan を変更せず、理由を指定先へ書く。
    bool BuildMapCapturePlan(const MapCaptureSpec& spec, MapCapturePlan& outPlan, Container::String* outError = nullptr);
    Rendering::CameraProxy MakeMapCaptureCamera(const MapCapturePlan& plan, const MapCaptureTile& tile);
    Container::String WriteMapCaptureManifestJson(const MapCapturePlan& plan);
} // namespace NorvesLib::Core::Scene
