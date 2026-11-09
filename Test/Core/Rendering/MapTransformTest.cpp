#include "Scene/MapTransform.h"
#include <cmath>
#include <iostream>
#include <limits>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Scene;

#define CHECK_MAP(condition) \
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
    MapTransform MakeSquareMap()
    {
        MapTransform transform;
        transform.WorldMinX = -2048.0;
        transform.WorldMinZ = -2048.0;
        transform.WorldMaxX = 2048.0;
        transform.WorldMaxZ = 2048.0;
        transform.PixelSizeMeters = 0.5;
        transform.ImageWidth = 8192;
        transform.ImageHeight = 8192;
        return transform;
    }

    bool SameTransform(const MapTransform& lhs, const MapTransform& rhs)
    {
        return lhs.WorldMinX == rhs.WorldMinX && lhs.WorldMinZ == rhs.WorldMinZ &&
               lhs.WorldMaxX == rhs.WorldMaxX && lhs.WorldMaxZ == rhs.WorldMaxZ &&
               lhs.PixelSizeMeters == rhs.PixelSizeMeters && lhs.UpAxis == rhs.UpAxis &&
               lhs.RightAxis == rhs.RightAxis && lhs.ImageWidth == rhs.ImageWidth &&
               lhs.ImageHeight == rhs.ImageHeight;
    }

    bool ReplaceFirst(Container::String& text, const TCHAR* before, const TCHAR* after)
    {
        const Container::String fragment(before);
        const auto position = text.find(fragment);
        if (position == Container::String::npos)
        {
            return false;
        }
        text.replace(position, fragment.size(), after);
        return true;
    }

    bool TestCoordinateRoundTrip()
    {
        const MapTransform transform = MakeSquareMap();
        CHECK_MAP(ValidateMapTransform(transform));
        const MapWorldXZ points[] = {
            {-2048.0, -2048.0}, {2048.0, 2048.0}, {0.0, 0.0},
            {123.456789, -987.654321}, {-2048.25, 2048.75}
        };
        for (const MapWorldXZ& point : points)
        {
            const MapUV uv = WorldToMapUV(transform, point.X, point.Z);
            const MapWorldXZ result = MapUVToWorld(transform, uv);
            CHECK_MAP(std::abs(result.X - point.X) / transform.PixelSizeMeters < 1e-4);
            CHECK_MAP(std::abs(result.Z - point.Z) / transform.PixelSizeMeters < 1e-4);
        }
        const MapUV coordinates[] = {
            {0.0, 0.0}, {1.0, 1.0}, {0.5, 0.5},
            {0.123456789, 0.987654321}, {-0.0001, 1.0002}
        };
        for (const MapUV& uv : coordinates)
        {
            const MapWorldXZ point = MapUVToWorld(transform, uv);
            const MapUV result = WorldToMapUV(transform, point.X, point.Z);
            CHECK_MAP(std::abs(result.U - uv.U) * transform.ImageWidth < 1e-4);
            CHECK_MAP(std::abs(result.V - uv.V) * transform.ImageHeight < 1e-4);
        }
        // 有限の両端から計算した幅だけが double の範囲を超えても変換できる。
        const double limit = (std::numeric_limits<double>::max)();
        MapTransform extreme;
        extreme.WorldMinX = -limit;
        extreme.WorldMinZ = -limit;
        extreme.WorldMaxX = limit;
        extreme.WorldMaxZ = limit;
        extreme.PixelSizeMeters = limit;
        extreme.ImageWidth = 2;
        extreme.ImageHeight = 2;
        CHECK_MAP(ValidateMapTransform(extreme));
        const MapUV center = WorldToMapUV(extreme, 0.0, 0.0);
        CHECK_MAP(center.U == 0.5 && center.V == 0.5);
        const MapUV corner = WorldToMapUV(extreme, limit, limit);
        CHECK_MAP(corner.U == 0.0 && corner.V == 0.0);
        const MapWorldXZ centerWorld = MapUVToWorld(extreme, {0.5, 0.5});
        CHECK_MAP(centerWorld.X == 0.0 && centerWorld.Z == 0.0);
        const MapWorldXZ cornerWorld = MapUVToWorld(extreme, {0.0, 0.0});
        CHECK_MAP(cornerWorld.X == limit && cornerWorld.Z == limit);
        return true;
    }

    bool TestJsonRoundTrip()
    {
        MapTransform transform = MakeSquareMap();
        transform.WorldMinX = -1234.5678901;
        transform.WorldMinZ = -67.12345678901234;
        transform.PixelSizeMeters = std::nextafter(0.5, 1.0);
        transform.WorldMaxX = transform.WorldMinX + transform.PixelSizeMeters * transform.ImageWidth;
        transform.WorldMaxZ = transform.WorldMinZ + transform.PixelSizeMeters * transform.ImageHeight;
        CHECK_MAP(ValidateMapTransform(transform));

        const MapAxis upAxes[] = {MapAxis::PositiveX, MapAxis::NegativeX, MapAxis::PositiveZ, MapAxis::NegativeZ};
        const MapAxis rightAxes[] = {MapAxis::PositiveZ, MapAxis::NegativeZ, MapAxis::PositiveX, MapAxis::NegativeX};
        for (MapAxis up : upAxes)
        {
            for (MapAxis right : rightAxes)
            {
                transform.UpAxis = up;
                transform.RightAxis = right;
                if (!ValidateMapTransform(transform))
                {
                    continue;
                }
                const Container::String json = WriteMapTransformJson(transform);
                CHECK_MAP(!json.empty());
                MapTransform result;
                Container::String error = TEXT("以前のエラー");
                CHECK_MAP(ReadMapTransformJson(json, result, &error));
                CHECK_MAP(error.empty());
                CHECK_MAP(SameTransform(result, transform));
            }
        }
        // 有限の double の両端でも桁落ちや読み込み不能が起きないことを確かめる。
        MapTransform smallest;
        smallest.WorldMaxX = std::numeric_limits<double>::denorm_min();
        smallest.WorldMaxZ = std::numeric_limits<double>::denorm_min();
        smallest.PixelSizeMeters = std::numeric_limits<double>::denorm_min();
        smallest.ImageWidth = 1;
        smallest.ImageHeight = 1;

        MapTransform largest;
        const double limit = (std::numeric_limits<double>::max)();
        largest.WorldMinX = -limit;
        largest.WorldMinZ = -limit;
        largest.WorldMaxX = limit;
        largest.WorldMaxZ = limit;
        largest.PixelSizeMeters = limit;
        largest.ImageWidth = 2;
        largest.ImageHeight = 2;

        const MapTransform edgeCases[] = {smallest, largest};
        for (const MapTransform& edge : edgeCases)
        {
            CHECK_MAP(ValidateMapTransform(edge));
            const Container::String json = WriteMapTransformJson(edge);
            MapTransform result;
            CHECK_MAP(ReadMapTransformJson(json, result));
            CHECK_MAP(SameTransform(result, edge));
        }
        return true;
    }

    bool TestInvalidValues()
    {
        const MapTransform valid = MakeSquareMap();
        double MapTransform::* fields[] = {
            &MapTransform::WorldMinX, &MapTransform::WorldMinZ, &MapTransform::WorldMaxX,
            &MapTransform::WorldMaxZ, &MapTransform::PixelSizeMeters
        };
        const double invalidNumbers[] = {
            std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
            -std::numeric_limits<double>::infinity()
        };
        for (auto field : fields)
        {
            for (double number : invalidNumbers)
            {
                MapTransform invalid = valid;
                invalid.*field = number;
                CHECK_MAP(!ValidateMapTransform(invalid));
            }
        }
        for (double size : {0.0, -0.5})
        {
            MapTransform invalid = valid;
            invalid.PixelSizeMeters = size;
            CHECK_MAP(!ValidateMapTransform(invalid));
        }

        MapTransform invalid = valid;
        invalid.WorldMaxX = invalid.WorldMinX;
        CHECK_MAP(!ValidateMapTransform(invalid));
        invalid = valid;
        invalid.WorldMinX = invalid.WorldMaxX + 1.0;
        CHECK_MAP(!ValidateMapTransform(invalid));
        invalid = valid;
        invalid.WorldMaxZ = invalid.WorldMinZ;
        CHECK_MAP(!ValidateMapTransform(invalid));
        invalid = valid;
        invalid.WorldMinZ = invalid.WorldMaxZ + 1.0;
        CHECK_MAP(!ValidateMapTransform(invalid));
        invalid = valid;
        invalid.ImageWidth = 0;
        CHECK_MAP(!ValidateMapTransform(invalid));
        invalid = valid;
        invalid.ImageHeight = 0;
        CHECK_MAP(!ValidateMapTransform(invalid));
        invalid = valid;
        ++invalid.ImageWidth;
        CHECK_MAP(!ValidateMapTransform(invalid));
        invalid = valid;
        ++invalid.ImageHeight;
        CHECK_MAP(!ValidateMapTransform(invalid));
        invalid = valid;
        invalid.WorldMaxX += 0.00045;
        CHECK_MAP(ValidateMapTransform(invalid));
        invalid.WorldMaxX += 0.0001;
        CHECK_MAP(!ValidateMapTransform(invalid));
        invalid = valid;
        invalid.RightAxis = MapAxis::NegativeZ;
        CHECK_MAP(!ValidateMapTransform(invalid));
        invalid = valid;
        invalid.UpAxis = MapAxis::PositiveX;
        CHECK_MAP(!ValidateMapTransform(invalid));
        invalid = valid;
        invalid.UpAxis = static_cast<MapAxis>(255);
        CHECK_MAP(!ValidateMapTransform(invalid));
        invalid = valid;
        invalid.RightAxis = static_cast<MapAxis>(255);
        CHECK_MAP(!ValidateMapTransform(invalid));

        const Container::String json = TEXT("{\"format\":\"norves.map_transform\",\"version\":1,")
            TEXT("\"world_rect\":{\"min_x\":-2048,\"min_z\":-2048,\"max_x\":2048,\"max_z\":2048},")
            TEXT("\"pixel_size_m\":0.5,\"image_width\":8192,\"image_height\":8192,")
            TEXT("\"up_axis\":\"+z\",\"right_axis\":\"-x\"}");
        const TCHAR* missingFields[] = {
            TEXT("\"format\":\"norves.map_transform\","), TEXT("\"version\":1,"),
            TEXT("\"world_rect\":{\"min_x\":-2048,\"min_z\":-2048,\"max_x\":2048,\"max_z\":2048},"),
            TEXT("\"min_x\":-2048,"), TEXT("\"min_z\":-2048,"), TEXT("\"max_x\":2048,"),
            TEXT(",\"max_z\":2048"), TEXT("\"pixel_size_m\":0.5,"), TEXT("\"image_width\":8192,"),
            TEXT("\"image_height\":8192,"), TEXT("\"up_axis\":\"+z\","), TEXT(",\"right_axis\":\"-x\"")
        };
        for (const TCHAR* field : missingFields)
        {
            Container::String malformed = json;
            CHECK_MAP(ReplaceFirst(malformed, field, TEXT("")));
            MapTransform result = valid;
            Container::String error;
            CHECK_MAP(!ReadMapTransformJson(malformed, result, &error));
            CHECK_MAP(!error.empty());
            CHECK_MAP(SameTransform(result, valid));
        }
        struct Replacement
        {
            const TCHAR* Before;
            const TCHAR* After;
        };
        const Replacement replacements[] = {
            {TEXT("norves.map_transform"), TEXT("other")},
            {TEXT("\"version\":1"), TEXT("\"version\":2")},
            {TEXT("\"version\":1"), TEXT("\"version\":\"1\"")},
            {TEXT("\"world_rect\":{\"min_x\":-2048,\"min_z\":-2048,\"max_x\":2048,\"max_z\":2048}"),
             TEXT("\"world_rect\":[]")},
            {TEXT("\"min_x\":-2048"), TEXT("\"min_x\":\"-2048\"")},
            {TEXT("\"pixel_size_m\":0.5"), TEXT("\"pixel_size_m\":false")},
            {TEXT("\"image_width\":8192"), TEXT("\"image_width\":\"8192\"")},
            {TEXT("\"image_width\":8192"), TEXT("\"image_width\":8192.5")},
            {TEXT("\"image_width\":8192"), TEXT("\"image_width\":4294967296")},
            {TEXT("\"image_height\":8192"), TEXT("\"image_height\":0")},
            {TEXT("\"up_axis\":\"+z\""), TEXT("\"up_axis\":\"+y\"")},
            {TEXT("\"right_axis\":\"-x\""), TEXT("\"right_axis\":123")},
            {TEXT("\"right_axis\":\"-x\""), TEXT("\"right_axis\":\"-z\"")}
        };
        for (const Replacement& replacement : replacements)
        {
            Container::String malformed = json;
            CHECK_MAP(ReplaceFirst(malformed, replacement.Before, replacement.After));
            MapTransform result = valid;
            Container::String error;
            CHECK_MAP(!ReadMapTransformJson(malformed, result, &error));
            CHECK_MAP(!error.empty());
            CHECK_MAP(SameTransform(result, valid));
        }
        MapTransform result = valid;
        CHECK_MAP(!ReadMapTransformJson(TEXT("{\"format\":"), result));
        CHECK_MAP(SameTransform(result, valid));
        return true;
    }

    bool TestOrientation()
    {
        struct ExpectedPoint
        {
            double X;
            double Z;
            double U;
            double V;
        };
        const ExpectedPoint standard[] = {
            {1024.0, 1536.0, 0.25, 0.125},
            {-2048.0, -1024.0, 1.0, 0.75},
            {0.0, 2048.0, 0.5, 0.0}
        };
        MapTransform transform = MakeSquareMap();
        for (const ExpectedPoint& point : standard)
        {
            const MapUV uv = WorldToMapUV(transform, point.X, point.Z);
            CHECK_MAP(std::abs(uv.U - point.U) < 1e-12);
            CHECK_MAP(std::abs(uv.V - point.V) < 1e-12);
            const MapWorldXZ world = MapUVToWorld(transform, {point.U, point.V});
            CHECK_MAP(std::abs(world.X - point.X) < 1e-9);
            CHECK_MAP(std::abs(world.Z - point.Z) < 1e-9);
        }

        transform.WorldMinX = -200.0;
        transform.WorldMaxX = 200.0;
        transform.WorldMinZ = -500.0;
        transform.WorldMaxZ = 1000.0;
        transform.UpAxis = MapAxis::PositiveX;
        transform.RightAxis = MapAxis::PositiveZ;
        transform.ImageWidth = 3000;
        transform.ImageHeight = 800;
        CHECK_MAP(ValidateMapTransform(transform));
        const ExpectedPoint rotated[] = {
            {100.0, 250.0, 0.5, 0.25},
            {-200.0, 1000.0, 1.0, 1.0},
            {0.0, -500.0, 0.0, 0.5}
        };
        for (const ExpectedPoint& point : rotated)
        {
            const MapUV uv = WorldToMapUV(transform, point.X, point.Z);
            CHECK_MAP(std::abs(uv.U - point.U) < 1e-12);
            CHECK_MAP(std::abs(uv.V - point.V) < 1e-12);
            const MapWorldXZ world = MapUVToWorld(transform, {point.U, point.V});
            CHECK_MAP(std::abs(world.X - point.X) < 1e-9);
            CHECK_MAP(std::abs(world.Z - point.Z) < 1e-9);
        }
        struct ExpectedAxes
        {
            MapAxis Up;
            MapAxis Right;
            double U;
            double V;
        };
        const ExpectedAxes orientations[] = {
            {MapAxis::PositiveX, MapAxis::PositiveZ, 0.4, 0.25},
            {MapAxis::PositiveX, MapAxis::NegativeZ, 0.6, 0.25},
            {MapAxis::NegativeX, MapAxis::PositiveZ, 0.4, 0.75},
            {MapAxis::NegativeX, MapAxis::NegativeZ, 0.6, 0.75},
            {MapAxis::PositiveZ, MapAxis::PositiveX, 0.75, 0.6},
            {MapAxis::PositiveZ, MapAxis::NegativeX, 0.25, 0.6},
            {MapAxis::NegativeZ, MapAxis::PositiveX, 0.75, 0.4},
            {MapAxis::NegativeZ, MapAxis::NegativeX, 0.25, 0.4}
        };
        for (const ExpectedAxes& expected : orientations)
        {
            transform.UpAxis = expected.Up;
            transform.RightAxis = expected.Right;
            const bool bRightX = expected.Right == MapAxis::PositiveX || expected.Right == MapAxis::NegativeX;
            transform.ImageWidth = bRightX ? 800 : 3000;
            transform.ImageHeight = bRightX ? 3000 : 800;
            CHECK_MAP(ValidateMapTransform(transform));
            const MapUV uv = WorldToMapUV(transform, 100.0, 100.0);
            CHECK_MAP(std::abs(uv.U - expected.U) < 1e-12);
            CHECK_MAP(std::abs(uv.V - expected.V) < 1e-12);
            const MapWorldXZ world = MapUVToWorld(transform, {expected.U, expected.V});
            CHECK_MAP(std::abs(world.X - 100.0) < 1e-9);
            CHECK_MAP(std::abs(world.Z - 100.0) < 1e-9);
        }
        return true;
    }
} // namespace

int main()
{
    std::cout << "MapTransformTest start\n";
    if (!TestCoordinateRoundTrip() || !TestJsonRoundTrip() || !TestInvalidValues() || !TestOrientation())
    {
        return 1;
    }
    std::cout << "MapTransformTest passed\n";
    return 0;
}
