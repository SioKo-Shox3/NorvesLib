#include "Scene/MapTransform.h"

#include "Text/JsonDocument.h"
#include "Text/JsonWriter.h"
#include <cmath>

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

        bool IsValidAxis(MapAxis axis)
        {
            switch (axis)
            {
            case MapAxis::PositiveX:
            case MapAxis::NegativeX:
            case MapAxis::PositiveZ:
            case MapAxis::NegativeZ:
                return true;
            default:
                return false;
            }
        }

        bool IsXAxis(MapAxis axis)
        {
            return axis == MapAxis::PositiveX || axis == MapAxis::NegativeX;
        }

        bool IsPositiveAxis(MapAxis axis)
        {
            return axis == MapAxis::PositiveX || axis == MapAxis::PositiveZ;
        }

        double PixelExtent(double minimum, double maximum, double pixelSize)
        {
            const double span = maximum - minimum;
            if (std::isfinite(span))
            {
                return span / pixelSize;
            }
            // 両端が有限でも幅が double の範囲を超える場合は、先に画素単位へ変換する。
            return maximum / pixelSize - minimum / pixelSize;
        }

        double ToCoordinate(double value, double minimum, double maximum, MapAxis axis, bool bVertical)
        {
            const bool bIncreasing = IsPositiveAxis(axis) != bVertical;
            const double span = maximum - minimum;
            const double offset = bIncreasing ? value - minimum : maximum - value;
            if (std::isfinite(span) && std::isfinite(offset))
            {
                return offset / span;
            }
            // 幅や外挿距離の引き算があふれる場合は、分子と分母を半分にして計算する。
            const double halfSpan = maximum * 0.5 - minimum * 0.5;
            const double halfOffset = bIncreasing ? value * 0.5 - minimum * 0.5 : maximum * 0.5 - value * 0.5;
            return halfOffset / halfSpan;
        }

        double FromCoordinate(double value, double minimum, double maximum, MapAxis axis, bool bVertical)
        {
            return IsPositiveAxis(axis) != bVertical ? std::lerp(minimum, maximum, value)
                                                    : std::lerp(maximum, minimum, value);
        }

        const char* AxisToString(MapAxis axis)
        {
            switch (axis)
            {
            case MapAxis::PositiveX: return "+x";
            case MapAxis::NegativeX: return "-x";
            case MapAxis::PositiveZ: return "+z";
            case MapAxis::NegativeZ: return "-z";
            default: return "";
            }
        }

        bool ReadAxis(const JsonValue& value, MapAxis& outAxis)
        {
            if (!value.IsString())
            {
                return false;
            }
            const Container::String& text = value.AsString();
            if (text == TEXT("+x"))
            {
                outAxis = MapAxis::PositiveX;
            }
            else if (text == TEXT("-x"))
            {
                outAxis = MapAxis::NegativeX;
            }
            else if (text == TEXT("+z"))
            {
                outAxis = MapAxis::PositiveZ;
            }
            else if (text == TEXT("-z"))
            {
                outAxis = MapAxis::NegativeZ;
            }
            else
            {
                return false;
            }
            return true;
        }

        bool ReadNumber(const JsonValue& value, double& outValue)
        {
            if (!value.IsNumber())
            {
                return false;
            }
            outValue = value.AsNumber();
            return true;
        }

        bool ReadDimension(const JsonValue& value, uint32_t& outValue)
        {
            if (!value.IsNumber())
            {
                return false;
            }
            const double number = value.AsNumber();
            if (!std::isfinite(number) || number < 0.0 || number > static_cast<double>(UINT32_MAX) ||
                std::floor(number) != number)
            {
                return false;
            }
            outValue = static_cast<uint32_t>(number);
            return true;
        }
    }

    bool ValidateMapTransform(const MapTransform& transform, Container::String* outError)
    {
        if (!std::isfinite(transform.WorldMinX) || !std::isfinite(transform.WorldMinZ) ||
            !std::isfinite(transform.WorldMaxX) || !std::isfinite(transform.WorldMaxZ) ||
            !std::isfinite(transform.PixelSizeMeters))
        {
            return SetError(outError, TEXT("矩形と画素サイズには有限の数値を指定してください。"));
        }
        if (transform.PixelSizeMeters <= 0.0)
        {
            return SetError(outError, TEXT("画素サイズは正の値にしてください。"));
        }
        if (transform.WorldMaxX <= transform.WorldMinX || transform.WorldMaxZ <= transform.WorldMinZ)
        {
            return SetError(outError, TEXT("矩形の最大値は最小値より大きくしてください。"));
        }
        if (transform.ImageWidth == 0 || transform.ImageHeight == 0)
        {
            return SetError(outError, TEXT("画像の幅と高さは正の値にしてください。"));
        }
        if (!IsValidAxis(transform.UpAxis) || !IsValidAxis(transform.RightAxis) ||
            IsXAxis(transform.UpAxis) == IsXAxis(transform.RightAxis))
        {
            return SetError(outError, TEXT("画像の上と右には異なる水平軸を指定してください。"));
        }

        const double pixelsX = PixelExtent(transform.WorldMinX, transform.WorldMaxX, transform.PixelSizeMeters);
        const double pixelsZ = PixelExtent(transform.WorldMinZ, transform.WorldMaxZ, transform.PixelSizeMeters);
        const double widthPixels = IsXAxis(transform.RightAxis) ? pixelsX : pixelsZ;
        const double heightPixels = IsXAxis(transform.UpAxis) ? pixelsX : pixelsZ;
        if (!std::isfinite(widthPixels) || !std::isfinite(heightPixels) ||
            std::abs(widthPixels - static_cast<double>(transform.ImageWidth)) > 1e-3 ||
            std::abs(heightPixels - static_cast<double>(transform.ImageHeight)) > 1e-3)
        {
            return SetError(outError, TEXT("画像の寸法と矩形の大きさが一致しません。"));
        }
        if (outError != nullptr)
        {
            outError->clear();
        }
        return true;
    }

    MapUV WorldToMapUV(const MapTransform& transform, double worldX, double worldZ)
    {
        const bool bRightX = IsXAxis(transform.RightAxis);
        const bool bUpX = IsXAxis(transform.UpAxis);
        return {
            ToCoordinate(bRightX ? worldX : worldZ, bRightX ? transform.WorldMinX : transform.WorldMinZ,
                         bRightX ? transform.WorldMaxX : transform.WorldMaxZ, transform.RightAxis, false),
            ToCoordinate(bUpX ? worldX : worldZ, bUpX ? transform.WorldMinX : transform.WorldMinZ,
                         bUpX ? transform.WorldMaxX : transform.WorldMaxZ, transform.UpAxis, true)
        };
    }

    MapWorldXZ MapUVToWorld(const MapTransform& transform, const MapUV& uv)
    {
        MapWorldXZ result;
        const bool bRightX = IsXAxis(transform.RightAxis);
        const bool bUpX = IsXAxis(transform.UpAxis);
        const double right = FromCoordinate(uv.U, bRightX ? transform.WorldMinX : transform.WorldMinZ,
                                            bRightX ? transform.WorldMaxX : transform.WorldMaxZ,
                                            transform.RightAxis, false);
        const double up = FromCoordinate(uv.V, bUpX ? transform.WorldMinX : transform.WorldMinZ,
                                         bUpX ? transform.WorldMaxX : transform.WorldMaxZ,
                                         transform.UpAxis, true);
        if (bRightX)
        {
            result.X = right;
            result.Z = up;
        }
        else
        {
            result.X = up;
            result.Z = right;
        }
        return result;
    }

    Container::String WriteMapTransformJson(const MapTransform& transform)
    {
        if (!ValidateMapTransform(transform))
        {
            return {};
        }
        JsonWriter writer(true);
        writer.BeginObject();
        writer.WriteString("format", "norves.map_transform");
        writer.WriteUInt64("version", 1);
        writer.BeginObject("world_rect");
        writer.WriteNumber("min_x", transform.WorldMinX);
        writer.WriteNumber("min_z", transform.WorldMinZ);
        writer.WriteNumber("max_x", transform.WorldMaxX);
        writer.WriteNumber("max_z", transform.WorldMaxZ);
        writer.EndObject();
        writer.WriteNumber("pixel_size_m", transform.PixelSizeMeters);
        writer.WriteUInt64("image_width", transform.ImageWidth);
        writer.WriteUInt64("image_height", transform.ImageHeight);
        writer.WriteString("up_axis", AxisToString(transform.UpAxis));
        writer.WriteString("right_axis", AxisToString(transform.RightAxis));
        writer.EndObject();
        return writer.IsComplete() ? writer.ToString() : Container::String{};
    }

    bool ReadMapTransformJson(const Container::String& text, MapTransform& out, Container::String* outError)
    {
        JsonDocument document;
        if (!JsonDocument::TryParse(text, document))
        {
            return SetError(outError, TEXT("地図変換の JSON が不正です。"));
        }
        const JsonValue root = document.GetRoot();
        const JsonValue format = root.FindMember("format");
        const JsonValue version = root.FindMember("version");
        const JsonValue rect = root.FindMember("world_rect");
        if (!root.IsObject() || !format.IsString() || format.AsString() != TEXT("norves.map_transform") ||
            !version.IsNumber() || version.AsNumber() != 1.0 || !rect.IsObject())
        {
            return SetError(outError, TEXT("地図変換の形式またはバージョンが不正です。"));
        }

        MapTransform parsed;
        if (!ReadNumber(rect.FindMember("min_x"), parsed.WorldMinX) ||
            !ReadNumber(rect.FindMember("min_z"), parsed.WorldMinZ) ||
            !ReadNumber(rect.FindMember("max_x"), parsed.WorldMaxX) ||
            !ReadNumber(rect.FindMember("max_z"), parsed.WorldMaxZ) ||
            !ReadNumber(root.FindMember("pixel_size_m"), parsed.PixelSizeMeters) ||
            !ReadDimension(root.FindMember("image_width"), parsed.ImageWidth) ||
            !ReadDimension(root.FindMember("image_height"), parsed.ImageHeight) ||
            !ReadAxis(root.FindMember("up_axis"), parsed.UpAxis) ||
            !ReadAxis(root.FindMember("right_axis"), parsed.RightAxis))
        {
            return SetError(outError, TEXT("地図変換の項目が欠けているか、値の型または軸が不正です。"));
        }
        if (!ValidateMapTransform(parsed, outError))
        {
            return false;
        }
        out = parsed;
        return true;
    }
} // namespace NorvesLib::Core::Scene
