#pragma once

#include "Container/Containers.h"
#include <cstdint>

namespace NorvesLib::Core
{
    class JsonWriter;
}

namespace NorvesLib::Core::Scene
{
    // 画像の上と右が指すワールドの水平軸。
    enum class MapAxis : uint8_t
    {
        PositiveX,
        NegativeX,
        PositiveZ,
        NegativeZ
    };

    struct MapTransform
    {
        double WorldMinX = 0.0;
        double WorldMinZ = 0.0;
        double WorldMaxX = 0.0;
        double WorldMaxZ = 0.0;
        double PixelSizeMeters = 1.0;

        // ワールドは右手系の Y 上で、通常のカメラは forward=-Z、up=+Y のとき右が +X。
        // 真上のカメラを forward=-Y、up=+Z とすると、BuildViewRotationMatrix の
        // CreateLookAt と Vulkan の AdjustProjectionForClipSpace の Y 反転により、
        // 撮影画像の上は +Z、右は -X になる。
        MapAxis UpAxis = MapAxis::PositiveZ;
        MapAxis RightAxis = MapAxis::NegativeX;
        uint32_t ImageWidth = 0;
        uint32_t ImageHeight = 0;
    };

    // U は左から右、V は上から下へ、それぞれ 0 から 1 に増える。
    struct MapUV
    {
        double U = 0.0;
        double V = 0.0;
    };

    struct MapWorldXZ
    {
        double X = 0.0;
        double Z = 0.0;
    };

    bool ValidateMapTransform(const MapTransform& transform, Container::String* outError = nullptr);

    // 変換には検証済みの地図を渡す。矩形外の座標も外挿する。
    MapUV WorldToMapUV(const MapTransform& transform, double worldX, double worldZ);
    MapWorldXZ MapUVToWorld(const MapTransform& transform, const MapUV& uv);

    // 検証済みの変換を、開始済みの JSON オブジェクトの中へ書く。
    void WriteMapTransformJsonFields(JsonWriter& writer, const MapTransform& transform);

    // 無効な変換を書き出す場合は空文字列を返す。読み込み失敗時は out を変更しない。
    Container::String WriteMapTransformJson(const MapTransform& transform);
    bool ReadMapTransformJson(const Container::String& text, MapTransform& out, Container::String* outError = nullptr);
} // namespace NorvesLib::Core::Scene
