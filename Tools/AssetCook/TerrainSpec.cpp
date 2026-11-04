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
        AnsiString sourcePathText;
        const JsonValue sourcePathValue = source.FindMember("path");
        if (!ReadText(sourcePathValue, absoluteSpec, "height_source.path", sourcePathText, outError))
        {
            return false;
        }
        try
        {
            const auto* pathBegin = reinterpret_cast<const char8_t*>(sourcePathText.data());
            const std::filesystem::path sourcePath(pathBegin, pathBegin + sourcePathText.size());
            if (!sourcePath.is_absolute() && (sourcePath.has_root_name() || sourcePath.has_root_directory()))
            {
                return FailField(absoluteSpec, "height_source.path", sourcePathValue,
                                 "完全な絶対パスか、スペックの親からの相対パスを指定してください。", outError);
            }
            candidate.HeightSourcePath = (sourcePath.is_absolute() ? sourcePath : absoluteSpec.parent_path() / sourcePath).lexically_normal();
        }
        catch (const std::filesystem::filesystem_error&)
        {
            return FailField(absoluteSpec, "height_source.path", sourcePathValue,
                             "ファイルパスとして使える文字列に修正してください。", outError);
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
            if (!recipe.IsObject())
            {
                return FailField(absoluteSpec, "recipe", recipe, "recipe はオブジェクトで指定してください。", outError);
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
        if (!BuildCookedTerrain(imported.Grid, {candidate.Spec.CellSizeM, candidate.Spec.TileCells}, candidate.Build, detail))
        {
            return FailStage("書き出し", candidate.Spec.HeightSourcePath, detail, outError);
        }

        candidate.SourceHash = Core::Asset::CookedTextureFormatV0::Fnv1a64OffsetBasis;
        HashFile(candidate.SourceHash, {specBytes.data(), specBytes.size()});
        HashFile(candidate.SourceHash, {heightBytes.data(), heightBytes.size()});
        candidate.SourceWidth = imported.SourceWidth;
        candidate.SourceHeight = imported.SourceHeight;
        candidate.SourceBitDepth = imported.SourceBitDepth;
        candidate.InputFiles.push_back(candidate.Spec.SpecPath);
        candidate.InputFiles.push_back(candidate.Spec.HeightSourcePath);
        candidate.Warnings = std::move(imported.Warnings);
        outResult = std::move(candidate);
        return true;
    }
} // namespace NorvesLib::Tools::AssetCook
