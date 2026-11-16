// 明示配置の検査・安定した並べ替え・量子化を行う。
#include "ScatterPlacementList.h"

#include "Asset/AssetManifest.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <exception>
#include <limits>
#include <utility>

namespace NorvesLib::Tools::AssetCook
{
    namespace Asset = Core::Asset;
    using Core::Container::AnsiString;
    using Core::Container::Span;
    using Core::Container::VariableArray;
    using Core::JsonValue;
    using Error = ScatterPlacementListError;

    const char* GetScatterPlacementListErrorName(Error error)
    {
        switch (error)
        {
        case Error::Json: return "json";
        case Error::InvalidSpecies: return "invalid_species";
        case Error::UnknownSpecies: return "unknown_species";
        case Error::NonFinite: return "non_finite";
        case Error::ScaleOutOfRange: return "scale_out_of_range";
        case Error::TintOutOfRange: return "tint_out_of_range";
        case Error::InvalidSeed: return "invalid_seed";
        case Error::InvalidCollision: return "invalid_collision";
        case Error::InvalidChunkSize: return "invalid_chunk_size";
        case Error::OutOfGrid: return "out_of_grid";
        case Error::TerrainMismatch: return "terrain_mismatch";
        case Error::Quantization: return "quantization";
        case Error::TooManyInstances: return "too_many_instances";
        }
        return "json";
    }

    namespace
    {
        constexpr double Max = std::numeric_limits<float>::max();
        constexpr double Pi = 3.1415926535897932384626433832795;

        bool NumberToken(const char* first, const char* last, bool& bUnderflow)
        {
            const char* cursor = first;
            if (cursor != last && *cursor == '-')
            {
                ++cursor;
            }
            const char* integer = cursor;
            if (cursor != last && *cursor == '0')
            {
                ++cursor;
            }
            else
            {
                while (cursor != last && *cursor >= '0' && *cursor <= '9')
                {
                    ++cursor;
                }
            }
            if (cursor == integer)
            {
                return false;
            }
            const int64_t integerDigits = cursor - integer;
            int64_t firstNonZero = -1, digitIndex = 0;
            for (const char* digit = integer; digit != cursor; ++digit, ++digitIndex)
            {
                if (*digit != '0' && firstNonZero < 0)
                {
                    firstNonZero = digitIndex;
                }
            }
            if (cursor != last && *cursor == '.')
            {
                const char* fraction = ++cursor;
                while (cursor != last && *cursor >= '0' && *cursor <= '9')
                {
                    if (*cursor != '0' && firstNonZero < 0)
                    {
                        firstNonZero = digitIndex;
                    }
                    ++digitIndex;
                    ++cursor;
                }
                if (cursor == fraction)
                {
                    return false;
                }
            }
            int64_t exponent = 0;
            if (cursor != last && (*cursor == 'e' || *cursor == 'E'))
            {
                ++cursor;
                const bool bNegative = cursor != last && *cursor == '-';
                if (cursor != last && (*cursor == '+' || *cursor == '-'))
                {
                    ++cursor;
                }
                const char* digits = cursor;
                while (cursor != last && *cursor >= '0' && *cursor <= '9')
                {
                    exponent = std::min<int64_t>(1000000000, exponent * 10 + (*cursor++ - '0'));
                }
                if (cursor == digits)
                {
                    return false;
                }
                if (bNegative)
                {
                    exponent = -exponent;
                }
            }
            bUnderflow = firstNonZero < 0 || integerDigits - firstNonZero - 1 + exponent < 0;
            return cursor == last;
        }

        struct Reader
        {
            const AnsiString& Marker;
            AnsiString& Message;
            AnsiString Context;

            bool Fail(Error code, const char8_t* detail)
            {
                Message = reinterpret_cast<const char*>(u8"配置の一覧 ");
                Message += GetScatterPlacementListErrorName(code);
                Message += ": ";
                Message += reinterpret_cast<const char*>(detail);
                if (!Context.empty())
                {
                    Message += reinterpret_cast<const char*>(u8"（");
                    Message += Context;
                    Message += reinterpret_cast<const char*>(u8"）");
                }
                return false;
            }

            bool Keys(JsonValue value, std::initializer_list<const char*> keys)
            {
                AnsiString detail;
                if (!ScatterSpecInput::Keys(value, keys, detail))
                {
                    return Fail(Error::Json, u8"オブジェクトの型、未知または重複した鍵を確認してください");
                }
                return true;
            }

            bool Text(JsonValue value, AnsiString& out, Error invalidText = Error::Json)
            {
                AnsiString detail;
                if (!ScatterSpecInput::Text(value, out, detail))
                {
                    return Fail(value.IsString() ? invalidText : Error::Json, u8"文字列は空でない NUL を含まない Unicode が必要です");
                }
                return true;
            }

            bool Numeric(JsonValue value, double& out, Error nonFinite, bool bFloat = true)
            {
                if (value.IsString() && ScatterSpecInput::KeyEquals(value.AsString(), Marker.c_str()))
                {
                    return Fail(nonFinite, u8"数値を有限値で表せません");
                }
                if (!value.IsNumber())
                {
                    return Fail(Error::Json, u8"数値が必要です");
                }
                out = value.AsNumber();
                if (!std::isfinite(out) || (bFloat && !std::isfinite(static_cast<float>(out))))
                {
                    return Fail(nonFinite, u8"数値を有限な float で表せません");
                }
                return true;
            }

            bool Range(JsonValue value, double min, double max, double& out, Error code)
            {
                if (!Numeric(value, out, code))
                {
                    return false;
                }
                return (out >= min && out <= max) || Fail(code, u8"数値が許可された範囲外です");
            }

            bool Vector(JsonValue value, double (&out)[3], Error code)
            {
                if (!value.IsArray() || value.GetArraySize() != 3)
                {
                    return Fail(Error::Json, u8"3 要素の配列が必要です");
                }
                for (size_t axis = 0; axis < 3; ++axis)
                {
                    if (!Numeric(value.GetArrayElement(axis), out[axis], code))
                    {
                        return false;
                    }
                }
                return true;
            }

            bool Boolean(JsonValue object, const char* key, bool& out)
            {
                if (!object.HasMember(key))
                {
                    return true;
                }
                const auto value = object.FindMember(key);
                if (!value.IsBoolean())
                {
                    return Fail(Error::Json, u8"真偽値は true または false が必要です");
                }
                out = value.AsBool();
                return true;
            }

            bool UInt64(JsonValue value, uint64_t& out)
            {
                AnsiString detail;
                return ScatterSpecInput::UInt64(value, out, detail) || Fail(Error::InvalidSeed, u8"seed は範囲内の10進整数が必要です");
            }
        };

        bool Collision(Reader& reader, JsonValue object, Asset::CookedScatterCollisionShape& out)
        {
            AnsiString shape;
            if (!reader.Text(object.FindMember("shape"), shape, Error::InvalidCollision))
            {
                return false;
            }
            double a[3], b[3] = {}, radius = 0;
            if (shape == "capsule")
            {
                out.Type = Asset::ScatterCollisionShapeType::Capsule;
                if (!reader.Keys(object, {"shape", "a", "b", "radius"}) ||
                    !reader.Vector(object.FindMember("a"), a, Error::InvalidCollision) ||
                    !reader.Vector(object.FindMember("b"), b, Error::InvalidCollision))
                {
                    return false;
                }
            }
            else if (shape == "sphere" || shape == "box")
            {
                out.Type = shape == "sphere" ? Asset::ScatterCollisionShapeType::Sphere : Asset::ScatterCollisionShapeType::Box;
                if (!reader.Keys(object, shape == "sphere" ? std::initializer_list<const char*>{"shape", "center", "radius"} :
                                                           std::initializer_list<const char*>{"shape", "center", "half_extents"}) ||
                    !reader.Vector(object.FindMember("center"), a, Error::InvalidCollision))
                {
                    return false;
                }
                if (shape == "box")
                {
                    if (!reader.Vector(object.FindMember("half_extents"), b, Error::InvalidCollision))
                    {
                        return false;
                    }
                    for (double extent : b)
                    {
                        if (extent <= 0 || static_cast<float>(extent) <= 0)
                        {
                            return reader.Fail(Error::InvalidCollision, u8"箱の半分の寸法は正値が必要です");
                        }
                    }
                }
            }
            else
            {
                return reader.Fail(Error::InvalidCollision, u8"衝突の形は capsule・sphere・box が必要です");
            }
            if (shape != "box" && !reader.Range(object.FindMember("radius"), 0, Max, radius, Error::InvalidCollision))
            {
                return false;
            }
            if (shape != "box" && (radius <= 0 || static_cast<float>(radius) <= 0))
            {
                return reader.Fail(Error::InvalidCollision, u8"半径は正の float で表せる値が必要です");
            }
            for (size_t axis = 0; axis < 3; ++axis)
            {
                out.A[axis] = static_cast<float>(a[axis]);
                out.B[axis] = static_cast<float>(b[axis]);
            }
            out.Radius = static_cast<float>(radius);
            return true;
        }

        bool Species(Reader& reader, JsonValue object, Asset::CookedScatterWriteDesc& output)
        {
            if (!reader.Keys(object, {"name", "render_path", "lods", "mesh_radius_m", "scale_range", "slope_follow",
                                      "brightness_variation", "cast_shadow", "ray_tracing", "collision", "wind"}))
            {
                return false;
            }
            Asset::CookedScatterSpecies species;
            AnsiString render;
            if (!reader.Text(object.FindMember("name"), species.Name, Error::InvalidSpecies))
            {
                return false;
            }
            reader.Context = species.Name;
            for (const auto& previous : output.Species)
            {
                if (previous.Name == species.Name)
                {
                    return reader.Fail(Error::InvalidSpecies, u8"種名が重複しています");
                }
            }
            if (!reader.Text(object.FindMember("render_path"), render, Error::InvalidSpecies))
            {
                return false;
            }
            if (render != "mega" && render != "foliage")
            {
                return reader.Fail(Error::InvalidSpecies, u8"render_path は mega または foliage が必要です");
            }
            species.RenderPath = render == "mega" ? Asset::ScatterRenderPath::Mega : Asset::ScatterRenderPath::Foliage;
            const auto lods = object.FindMember("lods");
            if (!lods.IsArray())
            {
                return reader.Fail(Error::Json, u8"lods は配列が必要です");
            }
            if (lods.GetArraySize() < 1 || lods.GetArraySize() > Asset::CookedScatterFormatV0::MaxLodCount)
            {
                return reader.Fail(Error::InvalidSpecies, u8"lods は1〜4件が必要です");
            }
            species.LodCount = static_cast<uint32_t>(lods.GetArraySize());
            for (size_t i = 0; i < species.LodCount; ++i)
            {
                if (!reader.Text(lods.GetArrayElement(i), species.LodPaths[i], Error::InvalidSpecies))
                {
                    return false;
                }
            }
            double radius, scaleMin, scaleMax, slope = 0, brightness = 0, stiffness = 1;
            const auto scales = object.FindMember("scale_range");
            if (!scales.IsArray() || scales.GetArraySize() != 2)
            {
                return reader.Fail(Error::Json, u8"scale_range は2要素の配列が必要です");
            }
            if (!reader.Range(object.FindMember("mesh_radius_m"), 0, Max, radius, Error::InvalidSpecies) ||
                !reader.Range(scales.GetArrayElement(0), std::numeric_limits<float>::min(), Max, scaleMin, Error::InvalidSpecies) ||
                !reader.Range(scales.GetArrayElement(1), std::numeric_limits<float>::min(), Max, scaleMax, Error::InvalidSpecies) ||
                (object.HasMember("slope_follow") && !reader.Range(object.FindMember("slope_follow"), 0, 1, slope, Error::InvalidSpecies)) ||
                (object.HasMember("brightness_variation") && !reader.Range(object.FindMember("brightness_variation"), 0, 1, brightness, Error::InvalidSpecies)))
            {
                return false;
            }
            const double spacing = 2 * radius * scaleMax;
            if (radius <= 0 || static_cast<float>(radius) <= 0 || scaleMin > scaleMax ||
                !std::isfinite(spacing) || spacing > Max || static_cast<float>(spacing) <= 0)
            {
                return reader.Fail(Error::InvalidSpecies, u8"拡大範囲または最大直径が種表の範囲外です");
            }
            species.MeshRadiusMeters = static_cast<float>(radius);
            species.MinSpacingMeters = static_cast<float>(spacing);
            species.ScaleMin = static_cast<float>(scaleMin);
            species.ScaleMax = static_cast<float>(scaleMax);
            species.SlopeFollow = static_cast<float>(slope);
            species.BrightnessVariation = static_cast<float>(brightness);
            bool bShadow = true, bRayTracing = true;
            if (!reader.Boolean(object, "cast_shadow", bShadow) || !reader.Boolean(object, "ray_tracing", bRayTracing))
            {
                return false;
            }
            species.Flags = (bShadow ? 0u : Asset::ScatterSpeciesFlagNoCastShadow) |
                            (bRayTracing ? 0u : Asset::ScatterSpeciesFlagNoRayTracing);
            if (object.HasMember("wind"))
            {
                const auto wind = object.FindMember("wind");
                uint64_t phase = 0;
                if (!reader.Keys(wind, {"stiffness", "phase_seed"}) ||
                    (wind.HasMember("stiffness") && !reader.Range(wind.FindMember("stiffness"), 0, Max, stiffness, Error::InvalidSpecies)) ||
                    (wind.HasMember("phase_seed") && !reader.UInt64(wind.FindMember("phase_seed"), phase)))
                {
                    return false;
                }
                if (phase > UINT32_MAX)
                {
                    return reader.Fail(Error::InvalidSpecies, u8"wind.phase_seed は uint32 の範囲が必要です");
                }
                species.WindPhaseSeed = static_cast<uint32_t>(phase);
            }
            species.WindStiffness = static_cast<float>(stiffness);
            if (object.HasMember("collision"))
            {
                const auto collision = object.FindMember("collision");
                if (!collision.IsArray())
                {
                    return reader.Fail(Error::Json, u8"collision は配列が必要です");
                }
                if (collision.GetArraySize() > Asset::CookedScatterFormatV0::MaxCollisionShapesPerSpecies)
                {
                    return reader.Fail(Error::InvalidCollision, u8"衝突の形は種ごとに16件以下が必要です");
                }
                species.CollisionCount = static_cast<uint32_t>(collision.GetArraySize());
                if (species.CollisionCount != 0)
                {
                    species.Flags |= Asset::ScatterSpeciesFlagCollision;
                    species.CollisionFirst = static_cast<uint32_t>(output.CollisionShapes.size());
                }
                for (size_t i = 0; i < collision.GetArraySize(); ++i)
                {
                    Asset::CookedScatterCollisionShape shape;
                    if (!Collision(reader, collision.GetArrayElement(i), shape))
                    {
                        return false;
                    }
                    output.CollisionShapes.push_back(shape);
                }
            }
            output.Species.push_back(std::move(species));
            return true;
        }

        struct Instance
        {
            Asset::ScatterInstanceValue Value;
            uint32_t Species = 0;
            uint32_t ChunkX = 0;
            uint32_t ChunkZ = 0;
            uint32_t Seed = 0;
            size_t InputIndex = 0;
        };

        float Bound(double value, bool bUpper)
        {
            float result = static_cast<float>(value);
            if ((bUpper && result < value) || (!bUpper && result > value))
            {
                result = std::nextafter(result, bUpper ? std::numeric_limits<float>::infinity() : -std::numeric_limits<float>::infinity());
            }
            return result;
        }

        void InstanceContext(Reader& reader, size_t index)
        {
            char number[32];
            std::snprintf(number, sizeof(number), "%zu", index);
            reader.Context = reinterpret_cast<const char*>(u8"個体の番号 ");
            reader.Context += number;
        }

        bool Cook(Reader& reader, const std::filesystem::path& path, Span<const uint8_t> bytes,
                  JsonValue root, ScatterSpecCookResult& result)
        {
            if (!reader.Keys(root, {"version", "kind", "terrain", "chunk_size_m", "seed", "height_mode", "species", "instances"}))
            {
                return false;
            }
            double version;
            AnsiString kind, mode = "absolute", terrainPath, expected;
            Asset::CookedScatterWriteDesc output;
            if (!reader.Range(root.FindMember("version"), 0, 0, version, Error::Json) ||
                !reader.Text(root.FindMember("kind"), kind) || kind != "placement" ||
                !reader.UInt64(root.FindMember("seed"), output.Seed) ||
                (root.HasMember("height_mode") && !reader.Text(root.FindMember("height_mode"), mode)))
            {
                return false;
            }
            if (mode != "absolute" && mode != "terrain")
            {
                return reader.Fail(Error::Json, u8"height_mode は absolute または terrain が必要です");
            }
            const auto reference = root.FindMember("terrain");
            if (!reader.Keys(reference, {"path", "cooked_hash"}) || !reader.Text(reference.FindMember("path"), terrainPath) ||
                !reader.Text(reference.FindMember("cooked_hash"), expected))
            {
                return false;
            }
            uint64_t recordedHash = 0;
            if (expected.size() != 16)
            {
                return reader.Fail(Error::Json, u8"cooked_hash は16桁の16進数が必要です");
            }
            for (char c : expected)
            {
                const int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
                if (digit < 0)
                {
                    return reader.Fail(Error::Json, u8"cooked_hash は16桁の16進数が必要です");
                }
                recordedHash = (recordedHash << 4) | static_cast<uint64_t>(digit);
            }
            Asset::CookedTerrainData terrain;
            VariableArray<std::filesystem::path> inputs;
            AnsiString detail;
            bool bMismatch = false;
            if (!ScatterSpecInput::LoadTerrain(path, terrainPath, recordedHash, terrain, output.TerrainCookedHash, inputs, detail, &bMismatch))
            {
                reader.Fail(bMismatch ? Error::TerrainMismatch : Error::Json, u8"参照地形を読み込めません");
                reader.Message += ": ";
                reader.Message += detail;
                return false;
            }
            double chunkSize;
            if (!reader.Numeric(root.FindMember("chunk_size_m"), chunkSize, Error::InvalidChunkSize))
            {
                return false;
            }
            const double ratio = static_cast<double>(terrain.TileCells) * terrain.CellSizeM / chunkSize;
            if (chunkSize <= 0 || static_cast<float>(chunkSize) <= 0 || !std::isfinite(ratio) || ratio < 1 - 1e-9 ||
                std::abs(ratio - std::round(ratio)) > 1e-9 * std::abs(ratio))
            {
                return reader.Fail(Error::InvalidChunkSize, u8"チャンクの一辺は地形タイルの一辺の約数となる有限の正値が必要です");
            }
            const double gridX = std::ceil(static_cast<double>(terrain.CellCountX) * terrain.CellSizeM / chunkSize);
            const double gridZ = std::ceil(static_cast<double>(terrain.CellCountZ) * terrain.CellSizeM / chunkSize);
            if (!std::isfinite(gridX) || !std::isfinite(gridZ) || gridX < 1 || gridZ < 1 || gridX > INT32_MAX || gridZ > INT32_MAX)
            {
                return reader.Fail(Error::InvalidChunkSize, u8"チャンク格子を形式の範囲で表せません");
            }
            output.OriginX = terrain.OriginX;
            output.OriginZ = terrain.OriginZ;
            output.GridCountX = static_cast<uint32_t>(gridX);
            output.GridCountZ = static_cast<uint32_t>(gridZ);
            output.ChunkSizeMeters = static_cast<float>(chunkSize);
            const auto species = root.FindMember("species");
            if (!species.IsArray())
            {
                return reader.Fail(Error::Json, u8"species は配列が必要です");
            }
            if (species.GetArraySize() < 1 || species.GetArraySize() > Asset::CookedScatterFormatV0::MaxSpeciesCount)
            {
                return reader.Fail(Error::InvalidSpecies, u8"species は1〜64件が必要です");
            }
            // 拡大範囲との照合には、種表へ float に丸める前の入力値を使う。
            VariableArray<double> scaleMin, scaleMax;
            for (size_t i = 0; i < species.GetArraySize(); ++i)
            {
                reader.Context.clear();
                if (!Species(reader, species.GetArrayElement(i), output))
                {
                    return false;
                }
                const auto scales = species.GetArrayElement(i).FindMember("scale_range");
                scaleMin.push_back(scales.GetArrayElement(0).AsNumber());
                scaleMax.push_back(scales.GetArrayElement(1).AsNumber());
            }
            reader.Context.clear();
            const auto instances = root.FindMember("instances");
            if (!instances.IsArray())
            {
                return reader.Fail(Error::Json, u8"instances は配列が必要です");
            }
            if (instances.GetArraySize() < 1 || instances.GetArraySize() > 4000000)
            {
                return reader.Fail(Error::TooManyInstances, u8"個体数は1〜4,000,000件が必要です");
            }
            ScatterTerrainSampler sampler;
            if (mode == "terrain" && !sampler.Initialize(terrain, detail))
            {
                return reader.Fail(Error::Json, u8"地形の標本器を初期化できません");
            }
            VariableArray<Instance> values;
            values.reserve(instances.GetArraySize());
            bool bHasSeeds = false;
            result.Stats.Species.resize(output.Species.size());
            for (size_t i = 0; i < instances.GetArraySize(); ++i)
            {
                InstanceContext(reader, i);
                const auto object = instances.GetArrayElement(i);
                if (!reader.Keys(object, {"species", "position", "yaw_deg", "scale", "tint", "seed"}))
                {
                    return false;
                }
                AnsiString name;
                if (!reader.Text(object.FindMember("species"), name))
                {
                    return false;
                }
                Instance instance;
                instance.InputIndex = i;
                while (instance.Species < output.Species.size() && output.Species[instance.Species].Name != name)
                {
                    ++instance.Species;
                }
                if (instance.Species == output.Species.size())
                {
                    reader.Context += " / ";
                    reader.Context += name;
                    return reader.Fail(Error::UnknownSpecies, u8"参照した種が種表にありません");
                }
                double position[3], yaw = 0, scale = 1, tint[3] = {1, 1, 1};
                if (!reader.Vector(object.FindMember("position"), position, Error::NonFinite) ||
                    (object.HasMember("yaw_deg") && !reader.Numeric(object.FindMember("yaw_deg"), yaw, Error::NonFinite, false)) ||
                    (object.HasMember("scale") && !reader.Numeric(object.FindMember("scale"), scale, Error::NonFinite)) ||
                    (object.HasMember("tint") && !reader.Vector(object.FindMember("tint"), tint, Error::NonFinite)))
                {
                    return false;
                }
                if (scale < scaleMin[instance.Species] || scale > scaleMax[instance.Species])
                {
                    return reader.Fail(Error::ScaleOutOfRange, u8"拡大が種の scale_range の範囲外です");
                }
                for (size_t axis = 0; axis < 3; ++axis)
                {
                    if (tint[axis] < 0 || tint[axis] > 2)
                    {
                        return reader.Fail(Error::TintOutOfRange, u8"色味の各成分は0〜2が必要です");
                    }
                    instance.Value.Tint[axis] = static_cast<float>(tint[axis] / 2);
                }
                if (object.HasMember("seed"))
                {
                    const auto seed = object.FindMember("seed");
                    if (!seed.IsIntegerLiteral() || !std::isfinite(seed.AsNumber()) || seed.AsNumber() < 1 || seed.AsNumber() > UINT32_MAX)
                    {
                        return reader.Fail(Error::InvalidSeed, u8"個体の seed は1〜4294967295の整数が必要です");
                    }
                    instance.Seed = seed.AsUInt32();
                    bHasSeeds = true;
                }
                const double x = std::floor((position[0] - output.OriginX) / chunkSize);
                const double z = std::floor((position[2] - output.OriginZ) / chunkSize);
                if (!std::isfinite(x) || !std::isfinite(z) || x < 0 || z < 0 || x >= gridX || z >= gridZ)
                {
                    return reader.Fail(Error::OutOfGrid, u8"個体が地形のチャンク格子外です");
                }
                instance.ChunkX = static_cast<uint32_t>(x);
                instance.ChunkZ = static_cast<uint32_t>(z);
                if (mode == "terrain")
                {
                    double height;
                    if (!sampler.SampleHeight(position[0], position[2], height) ||
                        !sampler.SampleNormal(position[0], position[2], instance.Value.Normal))
                    {
                        return reader.Fail(Error::OutOfGrid, u8"個体の位置で地形を標本化できません");
                    }
                    position[1] += height;
                    if (!std::isfinite(position[1]) || !std::isfinite(static_cast<float>(position[1])))
                    {
                        return reader.Fail(Error::NonFinite, u8"地形からの高さを加えた Y を有限値で表せません");
                    }
                }
                instance.Value.PositionX = position[0];
                instance.Value.PositionY = position[1];
                instance.Value.PositionZ = position[2];
                // 大きい角度も先に度で巡回させ、ラジアンへの変換であふれさせない。
                yaw = std::fmod(yaw, 360.0);
                if (yaw < 0)
                {
                    yaw += 360.0;
                }
                instance.Value.YawRadians = static_cast<float>(yaw * Pi / 180.0);
                instance.Value.Scale = static_cast<float>(scale);
                instance.Value.Rank = static_cast<uint8_t>(MixScatterSeed(MixScatterSeed(output.Seed + 0x9e3779b97f4a7c15ull) ^
                                                                         ScatterSpeciesKey(name) ^ i) >> 56);
                ++result.Stats.Species[instance.Species].InstanceCount;
                values.push_back(instance);
            }
            std::sort(values.begin(), values.end(), [](const auto& a, const auto& b)
            {
                if (a.ChunkZ != b.ChunkZ)
                {
                    return a.ChunkZ < b.ChunkZ;
                }
                if (a.ChunkX != b.ChunkX)
                {
                    return a.ChunkX < b.ChunkX;
                }
                if (a.Species != b.Species)
                {
                    return a.Species < b.Species;
                }
                if (a.Value.Rank != b.Value.Rank)
                {
                    return a.Value.Rank < b.Value.Rank;
                }
                return a.InputIndex < b.InputIndex;
            });
            for (size_t first = 0; first < values.size();)
            {
                size_t end = first;
                Asset::CookedScatterChunk chunk;
                chunk.ChunkX = values[first].ChunkX;
                chunk.ChunkZ = values[first].ChunkZ;
                while (end < values.size() && values[end].ChunkX == chunk.ChunkX && values[end].ChunkZ == chunk.ChunkZ)
                {
                    const auto& value = values[end].Value;
                    const double positions[] = {value.PositionX, value.PositionY, value.PositionZ};
                    for (size_t axis = 0; axis < 3; ++axis)
                    {
                        const float lower = Bound(positions[axis], false), upper = Bound(positions[axis], true);
                        chunk.AabbMin[axis] = end == first ? lower : std::min(chunk.AabbMin[axis], lower);
                        chunk.AabbMax[axis] = end == first ? upper : std::max(chunk.AabbMax[axis], upper);
                    }
                    ++end;
                }
                chunk.MinY = chunk.AabbMin[1];
                chunk.MaxY = chunk.AabbMax[1];
                output.Chunks.push_back(chunk);
                size_t cursor = first;
                for (size_t s = 0; s < output.Species.size(); ++s)
                {
                    Asset::CookedScatterRange range{output.Instances.size(), 0};
                    while (cursor < end && values[cursor].Species == s)
                    {
                        const auto& instance = values[cursor++];
                        Asset::InstanceRecord16 record;
                        if (!Asset::EncodeScatterInstance(instance.Value, chunk, output.Species[s], output.ChunkSizeMeters,
                                                          output.OriginX, output.OriginZ, record))
                        {
                            InstanceContext(reader, instance.InputIndex);
                            return reader.Fail(Error::Quantization, u8"個体を NVSCT のレコードへ量子化できません");
                        }
                        output.Instances.push_back(record);
                        if (bHasSeeds)
                        {
                            output.VariationSeeds.push_back(instance.Seed);
                        }
                        ++range.Count;
                    }
                    output.SpeciesRanges.push_back(range);
                }
                first = end;
            }
            reader.Context.clear();
            const auto status = Asset::WriteCookedScatter(output, result.Bytes);
            if (status != Asset::CookedScatterWriteStatus::Success)
            {
                return reader.Fail(status == Asset::CookedScatterWriteStatus::InvalidCollision ? Error::InvalidCollision : Error::Quantization,
                                   u8"NVSCT の表を書き出せません");
            }
            if (!ScatterSpecInput::SourceHash(bytes, output.TerrainCookedHash, inputs, result.SourceHash, detail))
            {
                return reader.Fail(Error::Json, u8"source_hash 用の入力を読み込めません");
            }
            result.PayloadHash = Asset::ComputeCookedScatterFileHash({result.Bytes.data(), result.Bytes.size()});
            result.ChunkCount = static_cast<uint32_t>(output.Chunks.size());
            result.InstanceCount = output.Instances.size();
            for (const auto& item : output.Species)
            {
                result.SpeciesNames.push_back(item.Name);
            }
            return true;
        }
    } // namespace

    bool ParseScatterPlacementListDocument(Span<const uint8_t> bytes, Core::JsonDocument& document,
                                           AnsiString& marker, bool& bPlacement, AnsiString& error)
    {
        bPlacement = false;
        marker = "!non_finite!";
        const size_t offset = bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf ? 3 : 0;
        // 入力の文字列と衝突しない目印を選ぶ。エスケープ済みの文字列も復号して照合する。
        for (size_t cursor = offset; cursor < bytes.size();)
        {
            if (bytes[cursor++] != '"')
            {
                continue;
            }
            const size_t begin = cursor - 1;
            while (cursor < bytes.size())
            {
                const uint8_t c = bytes[cursor++];
                if (c == '\\' && cursor < bytes.size())
                {
                    ++cursor;
                }
                else if (c == '"')
                {
                    break;
                }
            }
            Core::JsonDocument text;
            if (Core::JsonDocument::TryParseUtf8({bytes.data() + begin, cursor - begin}, text) &&
                ScatterSpecInput::KeyEquals(text.GetRoot().AsString(), marker.c_str()))
            {
                marker += "!";
                cursor = offset;
            }
        }
        VariableArray<uint8_t> normalized;
        size_t depth = 0;
        bool bExact = false;
        bool bKindValue = false;
        for (size_t cursor = offset; cursor < bytes.size();)
        {
            const size_t begin = cursor;
            const uint8_t c = bytes[cursor++];
            if (c == '"')
            {
                while (cursor < bytes.size())
                {
                    const uint8_t unit = bytes[cursor++];
                    if (unit == '\\' && cursor < bytes.size())
                    {
                        ++cursor;
                    }
                    else if (unit == '"')
                    {
                        break;
                    }
                }
                Core::JsonDocument text;
                if (Core::JsonDocument::TryParseUtf8({bytes.data() + begin, cursor - begin}, text))
                {
                    const auto& name = text.GetRoot().AsString();
                    bPlacement |= bKindValue && ScatterSpecInput::KeyEquals(name, "placement");
                    size_t next = cursor;
                    while (next < bytes.size() && (bytes[next] == ' ' || bytes[next] == '\t' || bytes[next] == '\r' || bytes[next] == '\n'))
                    {
                        ++next;
                    }
                    const bool bKey = next < bytes.size() && bytes[next] == ':';
                    bKindValue = bKey && depth == 1 && ScatterSpecInput::KeyEquals(name, "kind");
                    bExact = bKey && ((depth == 1 && ScatterSpecInput::KeyEquals(name, "seed")) || ScatterSpecInput::KeyEquals(name, "phase_seed"));
                }
            }
            else if (c == '-' || (c >= '0' && c <= '9'))
            {
                while (cursor < bytes.size() && ((bytes[cursor] >= '0' && bytes[cursor] <= '9') || bytes[cursor] == '.' ||
                       bytes[cursor] == 'e' || bytes[cursor] == 'E' || bytes[cursor] == '+' || bytes[cursor] == '-'))
                {
                    ++cursor;
                }
                const char* first = reinterpret_cast<const char*>(bytes.data() + begin);
                const char* last = reinterpret_cast<const char*>(bytes.data() + cursor);
                double value;
                const auto converted = std::from_chars(first, last, value, std::chars_format::general);
                bool bUnderflow = false;
                const bool bValidNumber = NumberToken(first, last, bUnderflow);
                bool bDigits = true;
                for (size_t i = begin; i < cursor; ++i)
                {
                    bDigits &= bytes[i] >= '0' && bytes[i] <= '9';
                }
                if (bValidNumber && ((bExact && bDigits) || (converted.ec == std::errc::result_out_of_range && converted.ptr == last)))
                {
                    if (!(bExact && bDigits) && bUnderflow)
                    {
                        normalized.push_back('0');
                        bExact = false;
                        continue;
                    }
                    normalized.push_back('"');
                    if (bExact && bDigits)
                    {
                        for (size_t i = begin; i < cursor; ++i)
                        {
                            normalized.push_back(bytes[i]);
                        }
                    }
                    else
                    {
                        for (char unit : marker)
                        {
                            normalized.push_back(static_cast<uint8_t>(unit));
                        }
                    }
                    normalized.push_back('"');
                    bExact = false;
                    continue;
                }
                bExact = false;
            }
            else if (c == '{' || c == '[')
            {
                ++depth;
            }
            else if ((c == '}' || c == ']') && depth > 0)
            {
                --depth;
            }
            for (size_t i = begin; i < cursor; ++i)
            {
                normalized.push_back(bytes[i]);
            }
        }
        if (!Core::JsonDocument::TryParseUtf8({normalized.data(), normalized.size()}, document))
        {
            Reader reader{marker, error, {}};
            return reader.Fail(Error::Json, u8"JSON の構文が不正です");
        }
        return true;
    }

    bool CookScatterPlacementList(const std::filesystem::path& path, Span<const uint8_t> bytes, JsonValue root,
                                  const AnsiString& nonFiniteMarker, ScatterSpecCookResult& out, AnsiString& error)
    {
        Reader reader{nonFiniteMarker, error, {}};
        try
        {
            ScatterSpecCookResult result;
            if (!Cook(reader, path, bytes, root, result))
            {
                return false;
            }
            out = std::move(result);
            return true;
        }
        catch (const std::exception&)
        {
            return reader.Fail(Error::Json, u8"配置の構築に失敗しました。入力と利用可能なメモリを確認してください");
        }
    }
} // namespace NorvesLib::Tools::AssetCook
