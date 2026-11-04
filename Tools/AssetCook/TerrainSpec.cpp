// 地形スペックを解析し、参照入力を地形データへまとめる。
#include "TerrainSpec.h"
#include "NativeCookPath.h"
#include "Asset/CookedSkeletalNameCodec.h"
#include "Asset/CookedTextureFormat.h"
#include "Text/JsonDocument.h"
#include "Text/UnicodeText.h"
#include <charconv>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <new>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace NorvesLib::Tools::AssetCook
{
    namespace
    {
        using Core::JsonValue;
        using Core::Container::AnsiString;
        using Core::Container::Span;
        using Core::Container::String;
        using Core::Container::VariableArray;

        void AppendUtf8(uint32_t scalar, AnsiString& out)
        {
            if (scalar < 0x80)
            {
                out.push_back(static_cast<char>(scalar));
            }
            else if (scalar < 0x800)
            {
                out.push_back(static_cast<char>(0xc0 | (scalar >> 6)));
                out.push_back(static_cast<char>(0x80 | (scalar & 0x3f)));
            }
            else if (scalar < 0x10000)
            {
                out.push_back(static_cast<char>(0xe0 | (scalar >> 12)));
                out.push_back(static_cast<char>(0x80 | ((scalar >> 6) & 0x3f)));
                out.push_back(static_cast<char>(0x80 | (scalar & 0x3f)));
            }
            else
            {
                out.push_back(static_cast<char>(0xf0 | (scalar >> 18)));
                out.push_back(static_cast<char>(0x80 | ((scalar >> 12) & 0x3f)));
                out.push_back(static_cast<char>(0x80 | ((scalar >> 6) & 0x3f)));
                out.push_back(static_cast<char>(0x80 | (scalar & 0x3f)));
            }
        }

        void AppendDiagnosticText(const String& text, AnsiString& out)
        {
            constexpr char Hex[] = "0123456789abcdef";
            const bool bValid = Core::TextDetail::ForEachUnicodeScalar<String::value_type>(
                {text.data(), text.size()}, [&](uint32_t scalar)
                {
                    if (scalar < 0x20)
                    {
                        out.append("\\u00");
                        out.push_back(Hex[(scalar >> 4) & 15]);
                        out.push_back(Hex[scalar & 15]);
                    }
                    else if (scalar == '"' || scalar == '\\')
                    {
                        out.push_back('\\');
                        out.push_back(static_cast<char>(scalar));
                    }
                    else
                    {
                        AppendUtf8(scalar, out);
                    }
                });
            if (!bValid)
            {
                out.append("<不正な Unicode>");
            }
        }

        AnsiString DescribeValue(const JsonValue& value)
        {
            AnsiString description;
            if (!value.IsValid())
            {
                description = "<未指定>";
            }
            else if (value.IsString())
            {
                description.push_back('"');
                AppendDiagnosticText(value.AsString(), description);
                description.push_back('"');
            }
            else if (value.IsNumber())
            {
                char buffer[64];
                const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), value.AsNumber(),
                                                     std::chars_format::general, std::numeric_limits<double>::max_digits10);
                if (converted.ec == std::errc{})
                {
                    description.append(buffer, static_cast<size_t>(converted.ptr - buffer));
                }
                else
                {
                    description = "<数値>";
                }
            }
            else if (value.IsBoolean())
            {
                description = value.AsBool() ? "true" : "false";
            }
            else if (value.IsNull())
            {
                description = "null";
            }
            else if (value.IsArray())
            {
                description = value.GetArraySize() == 0 ? "[]" : "[...]";
            }
            else
            {
                description = value.GetObjectSize() == 0 ? "{}" : "{...}";
            }
            return description;
        }

        AnsiString DescribePath(const std::filesystem::path& path)
        {
            AnsiString text;
            if (!Detail::EncodeCookPathUtf8(path, text))
            {
                text = "<パスの Unicode が不正>";
            }
            return text;
        }

        bool FailField(const std::filesystem::path& path, const AnsiString& field, const JsonValue& value,
                       const char* guidance, AnsiString& error)
        {
            error = "地形スペック「";
            error += DescribePath(path);
            error += "」の ";
            error += field;
            error += " = ";
            error += DescribeValue(value);
            error += "。";
            error += guidance;
            return false;
        }

        bool FailStage(const char* stage, const std::filesystem::path& path, const AnsiString& detail, AnsiString& error)
        {
            error = "地形の";
            error += stage;
            error += "段階、ファイル「";
            error += DescribePath(path);
            error += "」: ";
            error += detail;
            return false;
        }

        bool KeyEquals(const String& name, const char* key)
        {
            const size_t length = std::strlen(key);
            if (name.size() != length)
            {
                return false;
            }
            for (size_t i = 0; i < length; ++i)
            {
                if (name[i] != key[i])
                {
                    return false;
                }
            }
            return true;
        }

        bool CheckKeys(const JsonValue& object, const char* parent, const char* const* keys, size_t count,
                       const std::filesystem::path& path, AnsiString& error)
        {
            for (size_t i = 0; i < object.GetObjectSize(); ++i)
            {
                const auto& name = object.GetMemberName(i);
                AnsiString field = parent;
                if (!field.empty())
                {
                    field += ".";
                }
                AppendDiagnosticText(name, field);
                bool bKnown = false;
                for (size_t k = 0; k < count; ++k)
                {
                    if (KeyEquals(name, keys[k]))
                    {
                        bKnown = true;
                        break;
                    }
                }
                if (!bKnown)
                {
                    AnsiString guidance = "未知のキーです。許されるキー: ";
                    for (size_t k = 0; k < count; ++k)
                    {
                        if (k != 0)
                        {
                            guidance += ", ";
                        }
                        guidance += keys[k];
                    }
                    guidance += "。未知のキーを削除してください。";
                    return FailField(path, field, object.GetMemberValue(i), guidance.c_str(), error);
                }
                for (size_t previous = 0; previous < i; ++previous)
                {
                    if (name == object.GetMemberName(previous))
                    {
                        return FailField(path, field, object.GetMemberValue(i),
                                         "同じキーが重複しています。指定を一つにしてください。", error);
                    }
                }
            }
            return true;
        }

        bool ReadText(const JsonValue& value, const std::filesystem::path& path, const char* field,
                      AnsiString& out, AnsiString& error)
        {
            if (!value.IsString() || value.AsString().empty())
            {
                return FailField(path, field, value, "空でない文字列を指定してください。", error);
            }
            const auto& text = value.AsString();
            const Span<const String::value_type> units{text.data(), text.size()};
            const auto measured = Core::Asset::MeasureSkeletalNameEncoding(2, units);
            if (!measured.Succeeded())
            {
                return FailField(path, field, value, "NUL を含まない有効な Unicode 文字列を指定してください。", error);
            }
            VariableArray<uint8_t> bytes(measured.ByteCount);
            if (!Core::Asset::EncodeSkeletalWireName(2, units, {bytes.data(), bytes.size()}).Succeeded())
            {
                return FailField(path, field, value, "有効な Unicode 文字列を指定してください。", error);
            }
            out.clear();
            if (!bytes.empty())
            {
                out.append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            }
            return true;
        }

        bool ReadNumber(const JsonValue& value, const std::filesystem::path& path, const char* field,
                        double& out, AnsiString& error)
        {
            if (!value.IsNumber() || !std::isfinite(value.AsNumber()))
            {
                return FailField(path, field, value, "有限の数値を指定してください。", error);
            }
            out = value.AsNumber();
            return true;
        }

        bool ReadInteger(const JsonValue& value, const std::filesystem::path& path, const char* field,
                         uint32_t minimum, uint32_t maximum, uint32_t& out, const char* guidance, AnsiString& error)
        {
            if (!value.IsIntegerLiteral() || !std::isfinite(value.AsNumber()) ||
                value.AsNumber() < static_cast<double>(minimum) || value.AsNumber() > static_cast<double>(maximum))
            {
                return FailField(path, field, value, guidance, error);
            }
            out = static_cast<uint32_t>(value.AsNumber());
            return true;
        }

        bool ReadSpecPath(const JsonValue& value, const std::filesystem::path& specPath, const AnsiString& field,
                          std::filesystem::path& out, AnsiString& error)
        {
            AnsiString text;
            if (!ReadText(value, specPath, field.c_str(), text, error))
            {
                return false;
            }
            try
            {
                const auto* pathBegin = reinterpret_cast<const char8_t*>(text.data());
                const std::filesystem::path input(pathBegin, pathBegin + text.size());
                if (!input.is_absolute() && (input.has_root_name() || input.has_root_directory()))
                {
                    return FailField(specPath, field, value,
                                     "完全な絶対パスか、スペックの親からの相対パスを指定してください。", error);
                }
                out = (input.is_absolute() ? input : specPath.parent_path() / input).lexically_normal();
            }
            catch (const std::filesystem::filesystem_error&)
            {
                return FailField(specPath, field, value, "ファイルパスとして使える文字列に修正してください。", error);
            }
            return true;
        }

        AnsiString ChildField(const AnsiString& parent, const char* key)
        {
            AnsiString field = parent;
            field += ".";
            field += key;
            return field;
        }

        AnsiString IndexedField(const AnsiString& parent, size_t index)
        {
            AnsiString field = parent;
            char buffer[32];
            const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), index);
            field += "[";
            field.append(buffer, static_cast<size_t>(converted.ptr - buffer));
            field += "]";
            return field;
        }

        bool CheckRecipeObject(const JsonValue& value, const AnsiString& field, const char* const* keys, size_t count,
                               const std::filesystem::path& path, AnsiString& error)
        {
            if (!value.IsObject())
            {
                return FailField(path, field, value, "オブジェクトを指定してください。", error);
            }
            return CheckKeys(value, field.c_str(), keys, count, path, error);
        }

        enum class NumberRange { Any, NonNegative, Positive, Unit };

        struct RecipeNumber
        {
            const char* Key;
            double* Destination;
            NumberRange Range = NumberRange::Any;
        };

        bool ReadRecipeNumbers(const JsonValue& object, const AnsiString& parent, const RecipeNumber* numbers, size_t count,
                               const std::filesystem::path& path, AnsiString& error)
        {
            for (size_t i = 0; i < count; ++i)
            {
                const RecipeNumber& number = numbers[i];
                const JsonValue value = object.FindMember(number.Key);
                if (!value.IsValid())
                {
                    continue;
                }
                const AnsiString field = ChildField(parent, number.Key);
                if (!ReadNumber(value, path, field.c_str(), *number.Destination, error))
                {
                    return false;
                }
                const double result = *number.Destination;
                if ((number.Range == NumberRange::NonNegative && result < 0.0) ||
                    (number.Range == NumberRange::Positive && result <= 0.0) ||
                    (number.Range == NumberRange::Unit && (result < 0.0 || result > 1.0)))
                {
                    const char* guidance = number.Range == NumberRange::NonNegative ? "0 以上の数値を指定してください。" :
                                           number.Range == NumberRange::Positive ? "0 より大きい数値を指定してください。" :
                                           "0 以上 1 以下の数値を指定してください。";
                    return FailField(path, field, value, guidance, error);
                }
            }
            return true;
        }

        bool ParseNoise(const JsonValue& value, const AnsiString& field, const std::filesystem::path& path,
                        TerrainNoiseRule& out, AnsiString& error)
        {
            if (!value.IsValid())
            {
                return true;
            }
            const char* keys[] = {"amplitude", "scale_m"};
            if (!CheckRecipeObject(value, field, keys, sizeof(keys) / sizeof(keys[0]), path, error))
            {
                return false;
            }
            const RecipeNumber numbers[] = {{"amplitude", &out.Amplitude, NumberRange::NonNegative},
                                            {"scale_m", &out.ScaleM, NumberRange::Positive}};
            return ReadRecipeNumbers(value, field, numbers, sizeof(numbers) / sizeof(numbers[0]), path, error);
        }

        bool ParseBand(const JsonValue& value, const AnsiString& field, const std::filesystem::path& path,
                       double defaultMin, double defaultMax, TerrainBandRule& out, AnsiString& error)
        {
            if (!value.IsValid())
            {
                return true;
            }
            const char* keys[] = {"min", "max", "soft"};
            if (!CheckRecipeObject(value, field, keys, sizeof(keys) / sizeof(keys[0]), path, error))
            {
                return false;
            }
            out.bEnabled = true;
            out.Min = defaultMin;
            out.Max = defaultMax;
            const RecipeNumber numbers[] = {{"min", &out.Min}, {"max", &out.Max}, {"soft", &out.Soft, NumberRange::NonNegative}};
            if (!ReadRecipeNumbers(value, field, numbers, sizeof(numbers) / sizeof(numbers[0]), path, error))
            {
                return false;
            }
            if (out.Max < out.Min)
            {
                const bool bHasMax = value.FindMember("max").IsValid();
                const char* key = bHasMax ? "max" : "min";
                return FailField(path, ChildField(field, key), value.FindMember(key),
                                 "min を max 以下にするか、max を min 以上にしてください。", error);
            }
            return true;
        }

        bool ParseWetness(const JsonValue& value, const std::filesystem::path& path, TerrainWetnessRule& out, AnsiString& error)
        {
            if (!value.IsValid())
            {
                return true;
            }
            const AnsiString field = "recipe.wetness";
            const char* keys[] = {"flow_weight", "flow_ref_cells", "low_weight", "low_full_m", "low_zero_m",
                                  "concave_weight", "concave_full_per_m"};
            if (!CheckRecipeObject(value, field, keys, sizeof(keys) / sizeof(keys[0]), path, error))
            {
                return false;
            }
            const RecipeNumber numbers[] = {{"flow_weight", &out.FlowWeight}, {"flow_ref_cells", &out.FlowRefCells, NumberRange::Positive},
                                            {"low_weight", &out.LowWeight}, {"low_full_m", &out.LowFullM},
                                            {"low_zero_m", &out.LowZeroM}, {"concave_weight", &out.ConcaveWeight},
                                            {"concave_full_per_m", &out.ConcaveFullPerM, NumberRange::Positive}};
            if (!ReadRecipeNumbers(value, field, numbers, sizeof(numbers) / sizeof(numbers[0]), path, error))
            {
                return false;
            }
            if (out.LowZeroM <= out.LowFullM)
            {
                const bool bHasZero = value.FindMember("low_zero_m").IsValid();
                const char* key = bHasZero ? "low_zero_m" : "low_full_m";
                return FailField(path, ChildField(field, key), value.FindMember(key),
                                 "low_zero_m を low_full_m より大きくしてください。", error);
            }
            return true;
        }

        bool ParseDryGrass(const JsonValue& value, const std::filesystem::path& path, TerrainDryGrassRule& out, AnsiString& error)
        {
            if (!value.IsValid())
            {
                return true;
            }
            const AnsiString field = "recipe.dry_grass";
            const char* keys[] = {"base", "noise"};
            if (!CheckRecipeObject(value, field, keys, sizeof(keys) / sizeof(keys[0]), path, error))
            {
                return false;
            }
            const RecipeNumber numbers[] = {{"base", &out.Base}};
            return ReadRecipeNumbers(value, field, numbers, sizeof(numbers) / sizeof(numbers[0]), path, error) &&
                   ParseNoise(value.FindMember("noise"), ChildField(field, "noise"), path, out.Noise, error);
        }

        bool ParseRock(const JsonValue& value, const std::filesystem::path& path, TerrainRockRule& out, AnsiString& error)
        {
            if (!value.IsValid())
            {
                return true;
            }
            const AnsiString field = "recipe.rock";
            const char* keys[] = {"slope_start_deg", "slope_full_deg"};
            if (!CheckRecipeObject(value, field, keys, sizeof(keys) / sizeof(keys[0]), path, error))
            {
                return false;
            }
            const RecipeNumber numbers[] = {{"slope_start_deg", &out.SlopeStartDeg}, {"slope_full_deg", &out.SlopeFullDeg}};
            if (!ReadRecipeNumbers(value, field, numbers, sizeof(numbers) / sizeof(numbers[0]), path, error))
            {
                return false;
            }
            if (out.SlopeFullDeg <= out.SlopeStartDeg)
            {
                const bool bHasFull = value.FindMember("slope_full_deg").IsValid();
                const char* key = bHasFull ? "slope_full_deg" : "slope_start_deg";
                return FailField(path, ChildField(field, key), value.FindMember(key),
                                 "slope_full_deg を slope_start_deg より大きくしてください。", error);
            }
            return true;
        }

        bool ParseSplat(const JsonValue& value, const std::filesystem::path& path, TerrainRecipe& out, AnsiString& error)
        {
            if (!value.IsValid())
            {
                return true;
            }
            const AnsiString field = "recipe.splat";
            const char* keys[] = {"layers"};
            if (!CheckRecipeObject(value, field, keys, sizeof(keys) / sizeof(keys[0]), path, error))
            {
                return false;
            }
            const JsonValue layers = value.FindMember("layers");
            if (!layers.IsValid())
            {
                return true;
            }
            const AnsiString layersField = ChildField(field, "layers");
            if (!layers.IsArray() || layers.GetArraySize() > 8)
            {
                return FailField(path, layersField, layers, "0 個以上 8 個以下の層の配列を指定してください。", error);
            }
            for (size_t i = 0; i < layers.GetArraySize(); ++i)
            {
                const JsonValue layer = layers.GetArrayElement(i);
                const AnsiString layerField = IndexedField(layersField, i);
                const char* layerKeys[] = {"name", "weight", "slope_deg", "height_m", "wetness", "noise"};
                if (!CheckRecipeObject(layer, layerField, layerKeys, sizeof(layerKeys) / sizeof(layerKeys[0]), path, error))
                {
                    return false;
                }
                TerrainSplatLayerRule rule;
                const JsonValue nameValue = layer.FindMember("name");
                const AnsiString nameField = ChildField(layerField, "name");
                if (!ReadText(nameValue, path, nameField.c_str(), rule.Name, error))
                {
                    return false;
                }
                bool bValidName = rule.Name.size() <= 24;
                for (char character : rule.Name)
                {
                    if (!((character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '_'))
                    {
                        bValidName = false;
                        break;
                    }
                }
                if (!bValidName)
                {
                    return FailField(path, nameField, nameValue, "名前を英小文字・数字・アンダースコアだけの 1 以上 24 以下の文字数にしてください。", error);
                }
                for (const TerrainSplatLayerRule& previous : out.SplatLayers)
                {
                    if (previous.Name == rule.Name)
                    {
                        return FailField(path, nameField, nameValue, "層の名前が重複しています。異なる名前を指定してください。", error);
                    }
                }
                const RecipeNumber numbers[] = {{"weight", &rule.Weight, NumberRange::NonNegative}};
                if (!ReadRecipeNumbers(layer, layerField, numbers, sizeof(numbers) / sizeof(numbers[0]), path, error) ||
                    !ParseBand(layer.FindMember("slope_deg"), ChildField(layerField, "slope_deg"), path, 0.0, 90.0, rule.SlopeDeg, error) ||
                    !ParseBand(layer.FindMember("height_m"), ChildField(layerField, "height_m"), path, -1e9, 1e9, rule.HeightM, error) ||
                    !ParseBand(layer.FindMember("wetness"), ChildField(layerField, "wetness"), path, 0.0, 1.0, rule.Wetness, error) ||
                    !ParseNoise(layer.FindMember("noise"), ChildField(layerField, "noise"), path, rule.Noise, error))
                {
                    return false;
                }
                out.SplatLayers.push_back(std::move(rule));
            }
            return true;
        }

        bool ParseGrassDensity(const JsonValue& value, const std::filesystem::path& path, TerrainGrassDensityRule& out, AnsiString& error)
        {
            if (!value.IsValid())
            {
                return true;
            }
            const AnsiString field = "recipe.grass_density";
            const char* keys[] = {"base", "slope_start_deg", "slope_zero_deg", "wet_start", "wet_zero", "road_suppress", "rock_suppress"};
            if (!CheckRecipeObject(value, field, keys, sizeof(keys) / sizeof(keys[0]), path, error))
            {
                return false;
            }
            const RecipeNumber numbers[] = {{"base", &out.Base}, {"slope_start_deg", &out.SlopeStartDeg},
                                            {"slope_zero_deg", &out.SlopeZeroDeg}, {"wet_start", &out.WetStart},
                                            {"wet_zero", &out.WetZero}, {"road_suppress", &out.RoadSuppress, NumberRange::Unit},
                                            {"rock_suppress", &out.RockSuppress, NumberRange::Unit}};
            if (!ReadRecipeNumbers(value, field, numbers, sizeof(numbers) / sizeof(numbers[0]), path, error))
            {
                return false;
            }
            if (out.SlopeZeroDeg <= out.SlopeStartDeg)
            {
                const bool bHasZero = value.FindMember("slope_zero_deg").IsValid();
                const char* key = bHasZero ? "slope_zero_deg" : "slope_start_deg";
                return FailField(path, ChildField(field, key), value.FindMember(key),
                                 "slope_zero_deg を slope_start_deg より大きくしてください。", error);
            }
            if (out.WetZero <= out.WetStart)
            {
                const bool bHasZero = value.FindMember("wet_zero").IsValid();
                const char* key = bHasZero ? "wet_zero" : "wet_start";
                return FailField(path, ChildField(field, key), value.FindMember(key),
                                 "wet_zero を wet_start より大きくしてください。", error);
            }
            return true;
        }

        bool ParsePaints(const JsonValue& value, const std::filesystem::path& path, TerrainRecipe& out, AnsiString& error)
        {
            if (!value.IsValid())
            {
                return true;
            }
            const AnsiString field = "recipe.paint";
            if (!value.IsArray())
            {
                return FailField(path, field, value, "ペイントの配列を指定してください。", error);
            }
            for (size_t i = 0; i < value.GetArraySize(); ++i)
            {
                const JsonValue paint = value.GetArrayElement(i);
                const AnsiString paintField = IndexedField(field, i);
                const char* keys[] = {"target", "path", "mode"};
                if (!CheckRecipeObject(paint, paintField, keys, sizeof(keys) / sizeof(keys[0]), path, error))
                {
                    return false;
                }
                TerrainPaintRule rule;
                const JsonValue targetValue = paint.FindMember("target");
                if (targetValue.IsValid())
                {
                    const AnsiString targetField = ChildField(paintField, "target");
                    AnsiString target;
                    if (!ReadText(targetValue, path, targetField.c_str(), target, error))
                    {
                        return false;
                    }
                    if (target == "wetness")
                    {
                        rule.Target = TerrainPaintTarget::Wetness;
                    }
                    else if (target == "dry_grass")
                    {
                        rule.Target = TerrainPaintTarget::DryGrass;
                    }
                    else if (target == "road")
                    {
                        rule.Target = TerrainPaintTarget::Road;
                    }
                    else if (target == "rock")
                    {
                        rule.Target = TerrainPaintTarget::Rock;
                    }
                    else if (target == "grass_density")
                    {
                        rule.Target = TerrainPaintTarget::GrassDensity;
                    }
                    else
                    {
                        bool bFound = false;
                        constexpr char Prefix[] = "splat.";
                        if (target.size() > sizeof(Prefix) - 1 && std::memcmp(target.data(), Prefix, sizeof(Prefix) - 1) == 0)
                        {
                            for (size_t k = 0; k < out.SplatLayers.size(); ++k)
                            {
                                const AnsiString& name = out.SplatLayers[k].Name;
                                if (target.size() == sizeof(Prefix) - 1 + name.size() &&
                                    std::memcmp(target.data() + sizeof(Prefix) - 1, name.data(), name.size()) == 0)
                                {
                                    rule.Target = TerrainPaintTarget::Splat;
                                    rule.SplatLayerIndex = static_cast<uint32_t>(k);
                                    bFound = true;
                                    break;
                                }
                            }
                        }
                        if (!bFound)
                        {
                            return FailField(path, targetField, targetValue,
                                             "wetness・dry_grass・road・rock・grass_density、または存在する splat.<層名> を指定してください。", error);
                        }
                    }
                }
                const JsonValue modeValue = paint.FindMember("mode");
                if (modeValue.IsValid())
                {
                    const AnsiString modeField = ChildField(paintField, "mode");
                    AnsiString mode;
                    if (!ReadText(modeValue, path, modeField.c_str(), mode, error))
                    {
                        return false;
                    }
                    if (mode == "override")
                    {
                        rule.Mode = TerrainPaintMode::Override;
                    }
                    else if (mode == "multiply")
                    {
                        rule.Mode = TerrainPaintMode::Multiply;
                    }
                    else
                    {
                        return FailField(path, modeField, modeValue, "override または multiply を指定してください。", error);
                    }
                }
                if (!ReadSpecPath(paint.FindMember("path"), path, ChildField(paintField, "path"), rule.Path, error))
                {
                    return false;
                }
                out.Paints.push_back(std::move(rule));
            }
            return true;
        }

        bool ParseRecipe(const JsonValue& value, const std::filesystem::path& path, TerrainRecipe& out, AnsiString& error)
        {
            const char* keys[] = {"wetness", "dry_grass", "rock", "splat", "grass_density", "paint"};
            if (!CheckRecipeObject(value, "recipe", keys, sizeof(keys) / sizeof(keys[0]), path, error))
            {
                return false;
            }
            return ParseWetness(value.FindMember("wetness"), path, out.Wetness, error) &&
                   ParseDryGrass(value.FindMember("dry_grass"), path, out.DryGrass, error) &&
                   ParseRock(value.FindMember("rock"), path, out.Rock, error) &&
                   ParseSplat(value.FindMember("splat"), path, out, error) &&
                   ParseGrassDensity(value.FindMember("grass_density"), path, out.GrassDensity, error) &&
                   ParsePaints(value.FindMember("paint"), path, out, error);
        }

        bool RejectPresent(const JsonValue& object, const char* key, const std::filesystem::path& path,
                           const char* field, const char* guidance, AnsiString& error)
        {
            const JsonValue value = object.FindMember(key);
            return !value.IsValid() || FailField(path, field, value, guidance, error);
        }

        bool ReadBinary(const std::filesystem::path& path, VariableArray<uint8_t>& bytes, AnsiString& detail)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input)
            {
                detail = "ファイルを開けません。パスと読み取り権限を確認してください。";
                return false;
            }
            const std::streamoff length = input.tellg();
            if (length < 0 || static_cast<uint64_t>(length) > bytes.max_size() ||
                static_cast<uint64_t>(length) > static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max()))
            {
                detail = "ファイルの大きさを取得できないか、扱える大きさを超えています。入力を確認してください。";
                return false;
            }
            try
            {
                bytes.resize(static_cast<size_t>(length));
            }
            catch (const std::bad_alloc&)
            {
                detail = "ファイルを読むメモリを確保できません。入力を小さくしてください。";
                return false;
            }
            catch (const std::length_error&)
            {
                detail = "ファイルが配列の上限を超えます。入力を小さくしてください。";
                return false;
            }
            input.seekg(0, std::ios::beg);
            if (!bytes.empty())
            {
                input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            }
            if (!input)
            {
                detail = "ファイルを最後まで読めません。入力と読み取り権限を確認してください。";
                return false;
            }
            return true;
        }

        void HashFile(uint64_t& hash, Span<const uint8_t> bytes)
        {
            constexpr uint64_t Prime = Core::Asset::CookedTextureFormatV0::Fnv1a64Prime;
            const uint64_t size = static_cast<uint64_t>(bytes.size());
            for (unsigned i = 0; i < 8; ++i)
            {
                hash ^= static_cast<uint8_t>(size >> (i * 8));
                hash *= Prime;
            }
            for (uint8_t byte : bytes)
            {
                hash ^= byte;
                hash *= Prime;
            }
        }
    } // namespace

    bool ParseTerrainSpec(Span<const uint8_t> specBytes, const std::filesystem::path& specPath,
                          TerrainSpec& outSpec, AnsiString& outError)
    {
        outError.clear();
        std::error_code pathError;
        if (specPath.empty() || !Core::Gltf::IsValidNativeSourcePath(specPath))
        {
            return FailField(specPath, "$", {}, "有効なスペックファイルのパスを指定してください。", outError);
        }
        const auto absoluteSpec = std::filesystem::absolute(specPath, pathError).lexically_normal();
        if (pathError)
        {
            return FailField(specPath, "$", {}, "スペックのパスを絶対パスにできません。パスを確認してください。", outError);
        }
        if (specBytes.size() >= 3 && specBytes[0] == 0xef && specBytes[1] == 0xbb && specBytes[2] == 0xbf)
        {
            specBytes = {specBytes.data() + 3, specBytes.size() - 3};
        }

        Core::JsonDocument document;
        if (!Core::JsonDocument::TryParseUtf8(specBytes, document))
        {
            outError = "地形スペック「";
            outError += DescribePath(absoluteSpec);
            outError += "」の $ = <解析できない入力>。UTF-8 の JSON として構文と文字コードを修正してください。";
            return false;
        }
        const JsonValue root = document.GetRoot();
        if (!root.IsObject())
        {
            return FailField(absoluteSpec, "$", root, "ルートをオブジェクトにしてください。", outError);
        }
        const char* rootKeys[] = {"version", "height_source", "height_mapping", "cell_size_m", "tile_cells",
                                  "fit", "require_bit_depth", "smooth", "seed", "recipe"};
        if (!CheckKeys(root, "", rootKeys, sizeof(rootKeys) / sizeof(rootKeys[0]), absoluteSpec, outError))
        {
            return false;
        }

        uint32_t version = 0;
        if (!ReadInteger(root.FindMember("version"), absoluteSpec, "version", 1, 1, version,
                         "整数 1 を指定してください。", outError))
        {
            return false;
        }

        TerrainSpec candidate;
        candidate.SpecPath = absoluteSpec;
        const JsonValue source = root.FindMember("height_source");
        if (!source.IsObject())
        {
            return FailField(absoluteSpec, "height_source", source, "高さ入力をオブジェクトで指定してください。", outError);
        }
        const char* sourceKeys[] = {"path", "format", "width", "height", "endian", "exr_channel", "flip_z"};
        if (!CheckKeys(source, "height_source", sourceKeys, sizeof(sourceKeys) / sizeof(sourceKeys[0]), absoluteSpec, outError))
        {
            return false;
        }
        if (!ReadSpecPath(source.FindMember("path"), absoluteSpec, "height_source.path", candidate.HeightSourcePath, outError))
        {
            return false;
        }

        AnsiString format;
        const JsonValue formatValue = source.FindMember("format");
        if (!ReadText(formatValue, absoluteSpec, "height_source.format", format, outError))
        {
            return false;
        }
        if (format == "png16")
        {
            candidate.Import.Format = HeightSourceFormat::Png16;
        }
        else if (format == "png8")
        {
            candidate.Import.Format = HeightSourceFormat::Png8;
        }
        else if (format == "raw16")
        {
            candidate.Import.Format = HeightSourceFormat::Raw16;
        }
        else if (format == "raw32f")
        {
            candidate.Import.Format = HeightSourceFormat::Raw32F;
        }
        else if (format == "exr")
        {
            candidate.Import.Format = HeightSourceFormat::Exr;
        }
        else
        {
            return FailField(absoluteSpec, "height_source.format", formatValue,
                             "png16・png8・raw16・raw32f・exr のいずれかを指定してください。", outError);
        }

        const bool bRaw = candidate.Import.Format == HeightSourceFormat::Raw16 ||
                          candidate.Import.Format == HeightSourceFormat::Raw32F;
        const bool bInteger = candidate.Import.Format == HeightSourceFormat::Png16 ||
                              candidate.Import.Format == HeightSourceFormat::Png8 ||
                              candidate.Import.Format == HeightSourceFormat::Raw16;
        if (bRaw)
        {
            if (!ReadInteger(source.FindMember("width"), absoluteSpec, "height_source.width", 2, UINT32_MAX,
                             candidate.Import.RawWidth, "2 以上 4294967295 以下の整数を指定してください。", outError) ||
                !ReadInteger(source.FindMember("height"), absoluteSpec, "height_source.height", 2, UINT32_MAX,
                             candidate.Import.RawHeight, "2 以上 4294967295 以下の整数を指定してください。", outError))
            {
                return false;
            }
            const JsonValue endianValue = source.FindMember("endian");
            if (endianValue.IsValid())
            {
                AnsiString endian;
                if (!ReadText(endianValue, absoluteSpec, "height_source.endian", endian, outError))
                {
                    return false;
                }
                if (endian != "little" && endian != "big")
                {
                    return FailField(absoluteSpec, "height_source.endian", endianValue,
                                     "little または big を指定してください。", outError);
                }
                candidate.Import.bRawBigEndian = endian == "big";
            }
        }
        else if (!RejectPresent(source, "width", absoluteSpec, "height_source.width",
                                "width は raw16・raw32f だけに指定できます。削除してください。", outError) ||
                 !RejectPresent(source, "height", absoluteSpec, "height_source.height",
                                "height は raw16・raw32f だけに指定できます。削除してください。", outError) ||
                 !RejectPresent(source, "endian", absoluteSpec, "height_source.endian",
                                "endian は raw16・raw32f だけに指定できます。削除してください。", outError))
        {
            return false;
        }

        const JsonValue channel = source.FindMember("exr_channel");
        if (candidate.Import.Format == HeightSourceFormat::Exr)
        {
            if (channel.IsValid() && !ReadText(channel, absoluteSpec, "height_source.exr_channel",
                                                candidate.Import.ExrChannel, outError))
            {
                return false;
            }
        }
        else if (channel.IsValid())
        {
            return FailField(absoluteSpec, "height_source.exr_channel", channel,
                             "exr_channel は exr だけに指定できます。削除してください。", outError);
        }
        const JsonValue flip = source.FindMember("flip_z");
        if (flip.IsValid())
        {
            if (!flip.IsBoolean())
            {
                return FailField(absoluteSpec, "height_source.flip_z", flip, "true または false を指定してください。", outError);
            }
            candidate.Import.bFlipZ = flip.AsBool();
        }

        const JsonValue mapping = root.FindMember("height_mapping");
        if (!mapping.IsValid() && bInteger)
        {
            return FailField(absoluteSpec, "height_mapping", mapping,
                             "高さの単位は推測しません。min_m/max_m または scale_m/offset_m を指定してください。", outError);
        }
        if (mapping.IsValid())
        {
            if (!mapping.IsObject())
            {
                return FailField(absoluteSpec, "height_mapping", mapping, "高さ写像をオブジェクトで指定してください。", outError);
            }
            const char* mappingKeys[] = {"min_m", "max_m", "scale_m", "offset_m", "scale", "offset"};
            if (!CheckKeys(mapping, "height_mapping", mappingKeys, sizeof(mappingKeys) / sizeof(mappingKeys[0]),
                           absoluteSpec, outError))
            {
                return false;
            }
            if (bInteger)
            {
                if (!RejectPresent(mapping, "scale", absoluteSpec, "height_mapping.scale",
                                   "整数入力には min_m/max_m または scale_m/offset_m を指定してください。", outError) ||
                    !RejectPresent(mapping, "offset", absoluteSpec, "height_mapping.offset",
                                   "整数入力には min_m/max_m または scale_m/offset_m を指定してください。", outError))
                {
                    return false;
                }
                const JsonValue minimum = mapping.FindMember("min_m");
                const JsonValue maximum = mapping.FindMember("max_m");
                const JsonValue scale = mapping.FindMember("scale_m");
                const JsonValue offset = mapping.FindMember("offset_m");
                const bool bMinMax = minimum.IsValid() || maximum.IsValid();
                const bool bScaleOffset = scale.IsValid() || offset.IsValid();
                if (bMinMax && bScaleOffset)
                {
                    return FailField(absoluteSpec, scale.IsValid() ? "height_mapping.scale_m" : "height_mapping.offset_m",
                                     scale.IsValid() ? scale : offset,
                                     "min_m/max_m と scale_m/offset_m は混在できません。一方の組だけを指定してください。", outError);
                }
                if (bMinMax)
                {
                    double minM = 0.0;
                    double maxM = 0.0;
                    if (!ReadNumber(minimum, absoluteSpec, "height_mapping.min_m", minM, outError) ||
                        !ReadNumber(maximum, absoluteSpec, "height_mapping.max_m", maxM, outError))
                    {
                        return false;
                    }
                    if (maxM < minM)
                    {
                        return FailField(absoluteSpec, "height_mapping.max_m", maximum,
                                         "max_m は min_m 以上にしてください。", outError);
                    }
                    const double divisor = candidate.Import.Format == HeightSourceFormat::Png8 ? 255.0 : 65535.0;
                    const double difference = maxM - minM;
                    candidate.Import.Scale = std::isfinite(difference) ? difference / divisor : maxM / divisor - minM / divisor;
                    candidate.Import.Offset = minM;
                }
                else if (bScaleOffset)
                {
                    if (!ReadNumber(scale, absoluteSpec, "height_mapping.scale_m", candidate.Import.Scale, outError) ||
                        (offset.IsValid() && !ReadNumber(offset, absoluteSpec, "height_mapping.offset_m", candidate.Import.Offset, outError)))
                    {
                        return false;
                    }
                }
                else
                {
                    return FailField(absoluteSpec, "height_mapping", mapping,
                                     "高さの単位は推測しません。min_m/max_m または scale_m/offset_m を指定してください。", outError);
                }
            }
            else
            {
                if (!RejectPresent(mapping, "min_m", absoluteSpec, "height_mapping.min_m",
                                   "浮動小数点入力には scale・offset を指定してください。", outError) ||
                    !RejectPresent(mapping, "max_m", absoluteSpec, "height_mapping.max_m",
                                   "浮動小数点入力には scale・offset を指定してください。", outError) ||
                    !RejectPresent(mapping, "scale_m", absoluteSpec, "height_mapping.scale_m",
                                   "浮動小数点入力には scale・offset を指定してください。", outError) ||
                    !RejectPresent(mapping, "offset_m", absoluteSpec, "height_mapping.offset_m",
                                   "浮動小数点入力には scale・offset を指定してください。", outError))
                {
                    return false;
                }
                const JsonValue scale = mapping.FindMember("scale");
                const JsonValue offset = mapping.FindMember("offset");
                if ((scale.IsValid() && !ReadNumber(scale, absoluteSpec, "height_mapping.scale", candidate.Import.Scale, outError)) ||
                    (offset.IsValid() && !ReadNumber(offset, absoluteSpec, "height_mapping.offset", candidate.Import.Offset, outError)))
                {
                    return false;
                }
            }
        }

        const JsonValue cellSize = root.FindMember("cell_size_m");
        if (!ReadNumber(cellSize, absoluteSpec, "cell_size_m", candidate.CellSizeM, outError))
        {
            return false;
        }
        if (candidate.CellSizeM <= 0)
        {
            return FailField(absoluteSpec, "cell_size_m", cellSize, "有限で正の数値を指定してください。", outError);
        }
        candidate.Import.CellSizeM = candidate.CellSizeM;

        const JsonValue tile = root.FindMember("tile_cells");
        if (tile.IsValid())
        {
            if (!ReadInteger(tile, absoluteSpec, "tile_cells", 8, 1024, candidate.TileCells,
                             "8 以上 1024 以下の 2 の冪の整数を指定してください。", outError))
            {
                return false;
            }
            if ((candidate.TileCells & (candidate.TileCells - 1)) != 0)
            {
                return FailField(absoluteSpec, "tile_cells", tile, "8 以上 1024 以下の 2 の冪の整数を指定してください。", outError);
            }
        }
        candidate.Import.TileCells = candidate.TileCells;

        const JsonValue fit = root.FindMember("fit");
        if (fit.IsValid())
        {
            AnsiString mode;
            if (!ReadText(fit, absoluteSpec, "fit", mode, outError))
            {
                return false;
            }
            if (mode == "error")
            {
                candidate.Import.Fit = HeightFitMode::Error;
            }
            else if (mode == "crop")
            {
                candidate.Import.Fit = HeightFitMode::Crop;
            }
            else if (mode == "resample")
            {
                candidate.Import.Fit = HeightFitMode::Resample;
            }
            else
            {
                return FailField(absoluteSpec, "fit", fit, "error・crop・resample のいずれかを指定してください。", outError);
            }
        }

        const JsonValue bitDepth = root.FindMember("require_bit_depth");
        if (bitDepth.IsValid())
        {
            uint32_t bits = 0;
            if (!ReadInteger(bitDepth, absoluteSpec, "require_bit_depth", 8, 16, bits,
                             "整数 8 または 16 を指定してください。", outError))
            {
                return false;
            }
            if (bits != 8 && bits != 16)
            {
                return FailField(absoluteSpec, "require_bit_depth", bitDepth, "整数 8 または 16 を指定してください。", outError);
            }
            candidate.Import.RequireBitDepth = bits == 16 ? 16 : 0;
        }

        const JsonValue smooth = root.FindMember("smooth");
        if (smooth.IsValid())
        {
            if (!ReadNumber(smooth, absoluteSpec, "smooth", candidate.Import.SmoothSigmaM, outError))
            {
                return false;
            }
            if (candidate.Import.SmoothSigmaM < 0)
            {
                return FailField(absoluteSpec, "smooth", smooth, "有限で 0 以上の数値をメートル単位で指定してください。", outError);
            }
        }

        const JsonValue seed = root.FindMember("seed");
        if (seed.IsValid())
        {
            uint32_t seedValue = 0;
            if (!ReadInteger(seed, absoluteSpec, "seed", 0, UINT32_MAX, seedValue,
                             "0 以上 4294967295 以下の整数を指定してください。", outError))
            {
                return false;
            }
            candidate.Seed = seedValue;
        }

        const JsonValue recipe = root.FindMember("recipe");
        if (recipe.IsValid())
        {
            if (!ParseRecipe(recipe, absoluteSpec, candidate.Recipe, outError))
            {
                return false;
            }
            candidate.bHasRecipe = true;
        }
        outSpec = std::move(candidate);
        return true;
    }

    bool CookTerrainSpecFile(const std::filesystem::path& specPath, TerrainSpecCookResult& outResult, AnsiString& outError)
    {
        outError.clear();
        std::error_code pathError;
        if (specPath.empty() || !Core::Gltf::IsValidNativeSourcePath(specPath))
        {
            return FailStage("スペック読み込み", specPath, "有効なスペックファイルのパスを指定してください。", outError);
        }
        const auto absoluteSpec = std::filesystem::absolute(specPath, pathError).lexically_normal();
        if (pathError)
        {
            return FailStage("スペック読み込み", specPath, "絶対パスにできません。パスを確認してください。", outError);
        }

        VariableArray<uint8_t> specBytes;
        AnsiString detail;
        if (!ReadBinary(absoluteSpec, specBytes, detail))
        {
            return FailStage("スペック読み込み", absoluteSpec, detail, outError);
        }

        TerrainSpecCookResult candidate;
        if (!ParseTerrainSpec({specBytes.data(), specBytes.size()}, absoluteSpec, candidate.Spec, detail))
        {
            return FailStage("スペック解析", absoluteSpec, detail, outError);
        }
        VariableArray<uint8_t> heightBytes;
        if (!ReadBinary(candidate.Spec.HeightSourcePath, heightBytes, detail))
        {
            return FailStage("高さ入力の読み込み", candidate.Spec.HeightSourcePath, detail, outError);
        }
        HeightImportResult imported;
        if (!ImportHeightfield({heightBytes.data(), heightBytes.size()}, candidate.Spec.Import, imported, detail))
        {
            return FailStage("高さ入力の取り込み", candidate.Spec.HeightSourcePath, detail, outError);
        }
        candidate.SourceHash = Core::Asset::CookedTextureFormatV0::Fnv1a64OffsetBasis;
        HashFile(candidate.SourceHash, {specBytes.data(), specBytes.size()});
        HashFile(candidate.SourceHash, {heightBytes.data(), heightBytes.size()});
        candidate.InputFiles.push_back(candidate.Spec.SpecPath);
        candidate.InputFiles.push_back(candidate.Spec.HeightSourcePath);

        TerrainBuildSettings settings{candidate.Spec.CellSizeM, candidate.Spec.TileCells};
        TerrainMaskFields fields;
        if (candidate.Spec.bHasRecipe)
        {
            VariableArray<TerrainPaintImage> paints;
            paints.reserve(candidate.Spec.Recipe.Paints.size());
            for (const TerrainPaintRule& rule : candidate.Spec.Recipe.Paints)
            {
                VariableArray<uint8_t> paintBytes;
                if (!ReadBinary(rule.Path, paintBytes, detail))
                {
                    return FailStage("ペイント入力の読み込み", rule.Path, detail, outError);
                }
                TerrainPaintImage image;
                if (!DecodeTerrainPaintPng({paintBytes.data(), paintBytes.size()}, candidate.Spec.Import.bFlipZ, image, detail))
                {
                    return FailStage("ペイント入力の取り込み", rule.Path, detail, outError);
                }
                HashFile(candidate.SourceHash, {paintBytes.data(), paintBytes.size()});
                candidate.InputFiles.push_back(rule.Path);
                paints.push_back(std::move(image));
            }
            if (!ComputeTerrainMasks(imported.Grid, candidate.Spec.CellSizeM, static_cast<uint32_t>(candidate.Spec.Seed),
                                     candidate.Spec.Recipe, {paints.data(), paints.size()}, fields, detail))
            {
                return FailStage("マスクの計算", candidate.Spec.SpecPath, detail, outError);
            }
            settings.Masks = &fields;
        }
        if (!BuildCookedTerrain(imported.Grid, settings, candidate.Build, detail))
        {
            return FailStage("書き出し", candidate.Spec.HeightSourcePath, detail, outError);
        }

        candidate.SourceWidth = imported.SourceWidth;
        candidate.SourceHeight = imported.SourceHeight;
        candidate.SourceBitDepth = imported.SourceBitDepth;
        candidate.Warnings = std::move(imported.Warnings);
        outResult = std::move(candidate);
        return true;
    }
} // namespace NorvesLib::Tools::AssetCook
