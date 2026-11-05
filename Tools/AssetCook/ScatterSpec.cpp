// 地形への参照と全入力のハッシュを確かめてから散布を焼く。
#include "ScatterSpec.h"

#include "TerrainSpec.h"
#include "Asset/AssetManifest.h"
#include "Asset/CookedSkeletalNameCodec.h"
#include "Text/JsonDocument.h"

#include <cmath>
#include <exception>
#include <fstream>
#include <initializer_list>
#include <limits>

namespace NorvesLib::Tools::AssetCook
{
    namespace Asset = Core::Asset;
    using Core::Container::AnsiString;
    using Core::Container::Span;
    using Core::Container::VariableArray;
    using Core::JsonValue;

    namespace
    {
        bool Fail(AnsiString& error, const char8_t* message)
        {
            error = reinterpret_cast<const char*>(message);
            return false;
        }

        bool Read(const std::filesystem::path& path, VariableArray<uint8_t>& bytes, AnsiString& error)
        {
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            const auto size = file.tellg();
            if (!file || size < 0 || static_cast<uint64_t>(size) > std::numeric_limits<size_t>::max())
            {
                return Fail(error, u8"散布または参照地形の入力ファイルを読み込めません");
            }
            bytes.resize(static_cast<size_t>(size));
            file.seekg(0);
            if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
            {
                return Fail(error, u8"散布または参照地形の入力ファイルを最後まで読み込めません");
            }
            return true;
        }

        void HashFile(uint64_t& hash, Span<const uint8_t> bytes)
        {
            // 草の場と同様に、ファイル境界を長さの little endian 8 バイトで区切る。
            for (unsigned i = 0; i < 8; ++i)
            {
                hash ^= static_cast<uint8_t>(static_cast<uint64_t>(bytes.size()) >> (8 * i));
                hash *= Asset::CookedTextureFormatV0::Fnv1a64Prime;
            }
            for (uint8_t byte : bytes)
            {
                hash ^= byte;
                hash *= Asset::CookedTextureFormatV0::Fnv1a64Prime;
            }
        }

        bool Text(JsonValue value, AnsiString& out, AnsiString& error)
        {
            if (!value.IsString() || value.AsString().empty())
            {
                return Fail(error, u8"散布の文字列は空でない文字列で指定してください");
            }
            const auto& text = value.AsString();
            const Span<const Core::Container::String::value_type> units(text.data(), text.size());
            const auto measured = Asset::MeasureSkeletalNameEncoding(2, units);
            if (!measured.Succeeded())
            {
                return Fail(error, u8"散布の文字列は NUL を含まない Unicode が必要です");
            }
            VariableArray<uint8_t> bytes(measured.ByteCount);
            if (!Asset::EncodeSkeletalWireName(2, units, {bytes.data(), bytes.size()}).Succeeded())
            {
                return Fail(error, u8"散布の文字列を UTF-8 に変換できません");
            }
            out.clear();
            out.append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            return true;
        }

        bool KeyEquals(const Core::Container::String& name, const char* key)
        {
            size_t i = 0;
            while (key[i] != 0 && i < name.size() && name[i] == key[i])
            {
                ++i;
            }
            return i == name.size() && key[i] == 0;
        }

        bool Keys(JsonValue object, std::initializer_list<const char*> allowed, AnsiString& error)
        {
            if (!object.IsObject())
            {
                return Fail(error, u8"散布の設定は JSON オブジェクトで指定してください");
            }
            for (size_t i = 0; i < object.GetObjectSize(); ++i)
            {
                const auto& name = object.GetMemberName(i);
                bool bKnown = false;
                for (const char* key : allowed)
                {
                    bKnown |= KeyEquals(name, key);
                }
                if (!bKnown)
                {
                    return Fail(error, u8"散布 spec に未知のキーがあります");
                }
                for (size_t j = 0; j < i; ++j)
                {
                    if (name == object.GetMemberName(j))
                    {
                        return Fail(error, u8"散布 spec のキーが重複しています");
                    }
                }
            }
            return true;
        }

        bool Number(JsonValue value, double min, double max, double& out, AnsiString& error)
        {
            if (!value.IsNumber() || !std::isfinite(value.AsNumber()) || value.AsNumber() < min || value.AsNumber() > max)
            {
                return Fail(error, u8"散布の数値が未指定または範囲外です");
            }
            out = value.AsNumber();
            return true;
        }

        bool OptionalNumber(JsonValue object, const char* key, double min, double max, double& out, AnsiString& error)
        {
            return !object.HasMember(key) || Number(object.FindMember(key), min, max, out, error);
        }

        bool Boolean(JsonValue object, const char* key, bool& out, AnsiString& error)
        {
            if (!object.HasMember(key))
            {
                return true;
            }
            const auto value = object.FindMember(key);
            if (!value.IsBoolean())
            {
                return Fail(error, u8"散布の真偽値は true または false で指定してください");
            }
            out = value.AsBool();
            return true;
        }

        bool Pair(JsonValue value, double min, double max, double& first, double& last, AnsiString& error)
        {
            if (!value.IsArray() || value.GetArraySize() != 2 ||
                !Number(value.GetArrayElement(0), min, max, first, error) ||
                !Number(value.GetArrayElement(1), min, max, last, error) || first > last)
            {
                return Fail(error, u8"散布の範囲は範囲内の [最小値, 最大値] で指定してください");
            }
            return true;
        }

        bool UInt64(JsonValue value, uint64_t& out, AnsiString& error)
        {
            AnsiString text;
            if (!Text(value, text, error))
            {
                return Fail(error, u8"散布の seed・phase_seed は uint64 の10進整数が必要です");
            }
            uint64_t result = 0;
            for (char c : text)
            {
                if (c < '0' || c > '9' || result > (UINT64_MAX - static_cast<unsigned>(c - '0')) / 10)
                {
                    return Fail(error, u8"散布の seed・phase_seed は uint64 の10進整数が必要です");
                }
                result = result * 10 + static_cast<unsigned>(c - '0');
            }
            out = result;
            return true;
        }

        bool ParseDocument(Span<const uint8_t> bytes, Core::JsonDocument& document, AnsiString& error)
        {
            size_t offset = bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf ? 3 : 0;
            Core::JsonDocument original;
            if (!Core::JsonDocument::TryParseUtf8({bytes.data() + offset, bytes.size() - offset}, original))
            {
                return Fail(error, u8"散布 spec の JSON が不正です");
            }
            // JsonValue の数値は double。整数 token は丸める前に文字列へ写して uint64 全域を保つ。
            // 鍵のエスケープも通常の JSON 解析で復号し、文字列の値や他の数値には触れない。
            VariableArray<uint8_t> exact;
            size_t cursor = offset;
            while (cursor < bytes.size())
            {
                const size_t begin = cursor;
                if (bytes[cursor++] != '"')
                {
                    exact.push_back(bytes[begin]);
                    continue;
                }
                while (cursor < bytes.size())
                {
                    const auto c = bytes[cursor++];
                    if (c == '\\' && cursor < bytes.size())
                    {
                        ++cursor;
                    }
                    else if (c == '"')
                    {
                        break;
                    }
                }
                for (size_t i = begin; i < cursor; ++i)
                {
                    exact.push_back(bytes[i]);
                }
                Core::JsonDocument keyDocument;
                if (!Core::JsonDocument::TryParseUtf8({bytes.data() + begin, cursor - begin}, keyDocument))
                {
                    return Fail(error, u8"散布 spec の JSON 文字列が不正です");
                }
                const auto& key = keyDocument.GetRoot().AsString();
                if (!KeyEquals(key, "seed") && !KeyEquals(key, "phase_seed"))
                {
                    continue;
                }
                size_t number = cursor;
                const auto skip = [&]()
                {
                    while (number < bytes.size() && (bytes[number] == ' ' || bytes[number] == '\t' || bytes[number] == '\r' || bytes[number] == '\n'))
                    {
                        ++number;
                    }
                };
                skip();
                if (number == bytes.size() || bytes[number] != ':')
                {
                    continue;
                }
                ++number;
                skip();
                size_t end = number;
                while (end < bytes.size() && bytes[end] >= '0' && bytes[end] <= '9')
                {
                    ++end;
                }
                if (end == number)
                {
                    continue;
                }
                for (; cursor < number; ++cursor)
                {
                    exact.push_back(bytes[cursor]);
                }
                exact.push_back('"');
                for (; cursor < end; ++cursor)
                {
                    exact.push_back(bytes[cursor]);
                }
                exact.push_back('"');
            }
            if (!Core::JsonDocument::TryParseUtf8({exact.data(), exact.size()}, document))
            {
                return Fail(error, u8"散布 spec の JSON が不正です");
            }
            return true;
        }

        bool Species(JsonValue value, ScatterSpeciesRule& rule, AnsiString& error)
        {
            if (!Keys(value, {"name", "render_path", "lods", "mesh_radius_m", "min_spacing_m", "density", "base_density_per_m2",
                              "height_band_m", "slope_band_deg", "exclude", "scale_range", "slope_follow", "brightness_variation",
                              "dry_from_mask", "collision", "wind"}, error))
            {
                return false;
            }
            AnsiString render;
            constexpr double Max = std::numeric_limits<float>::max();
            if (!Text(value.FindMember("name"), rule.Name, error) || !Text(value.FindMember("render_path"), render, error) ||
                !Number(value.FindMember("mesh_radius_m"), 0, Max, rule.MeshRadiusM, error) ||
                !OptionalNumber(value, "min_spacing_m", 0, Max, rule.MinSpacingM, error) ||
                !Number(value.FindMember("base_density_per_m2"), 0, Max, rule.BaseDensityPerM2, error) ||
                !Pair(value.FindMember("scale_range"), std::numeric_limits<float>::min(), Max, rule.ScaleMin, rule.ScaleMax, error) ||
                !Number(value.FindMember("slope_follow"), 0, 1, rule.SlopeFollow, error) ||
                !Number(value.FindMember("brightness_variation"), 0, 1, rule.BrightnessVariation, error) ||
                !Boolean(value, "dry_from_mask", rule.bUseDryGrass, error) || !Boolean(value, "collision", rule.bCollision, error))
            {
                return false;
            }
            if (render != "mega" && render != "foliage")
            {
                return Fail(error, u8"散布の render_path は mega または foliage が必要です");
            }
            rule.RenderPath = render == "mega" ? Asset::ScatterRenderPath::Mega : Asset::ScatterRenderPath::Foliage;
            const auto lods = value.FindMember("lods");
            if (!lods.IsArray() || lods.GetArraySize() == 0 || lods.GetArraySize() > Asset::CookedScatterFormatV0::MaxLodCount)
            {
                return Fail(error, u8"散布の lods は1〜4個のメッシュ論理パスが必要です");
            }
            for (size_t i = 0; i < lods.GetArraySize(); ++i)
            {
                AnsiString path;
                if (!Text(lods.GetArrayElement(i), path, error))
                {
                    return false;
                }
                rule.LodMeshLogicalPaths.push_back(std::move(path));
            }
            const auto density = value.FindMember("density");
            AnsiString source;
            if (!Text(density.FindMember("source"), source, error))
            {
                return false;
            }
            if (source == "constant")
            {
                if (!Keys(density, {"source", "value"}, error) || !Number(density.FindMember("value"), 0, 1, rule.DensitySource.Constant, error))
                {
                    return false;
                }
            }
            else if (source == "layer")
            {
                AnsiString layer, channel;
                if (!Keys(density, {"source", "layer", "channel"}, error) || !Text(density.FindMember("layer"), layer, error) ||
                    !Text(density.FindMember("channel"), channel, error))
                {
                    return false;
                }
                if (layer.size() != 4 || channel.size() != 1 ||
                    (channel[0] != 'r' && channel[0] != 'g' && channel[0] != 'b' && channel[0] != 'a'))
                {
                    return Fail(error, u8"散布の密度層は4文字、channel は r/g/b/a が必要です");
                }
                rule.DensitySource.bUseLayer = true;
                rule.DensitySource.FourCC = Asset::MakeAssetPackageFourCC(layer[0], layer[1], layer[2], layer[3]);
                rule.DensitySource.Channel = channel[0] == 'r' ? 0 : channel[0] == 'g' ? 1 : channel[0] == 'b' ? 2 : 3;
            }
            else
            {
                return Fail(error, u8"散布の density.source は constant または layer が必要です");
            }
            rule.bHeightBandEnabled = value.HasMember("height_band_m");
            rule.bSlopeBandEnabled = value.HasMember("slope_band_deg");
            if ((rule.bHeightBandEnabled && !Pair(value.FindMember("height_band_m"), -Max, Max, rule.HeightMinM, rule.HeightMaxM, error)) ||
                (rule.bSlopeBandEnabled && !Pair(value.FindMember("slope_band_deg"), 0, 90, rule.SlopeMinDeg, rule.SlopeMaxDeg, error)))
            {
                return false;
            }
            if (value.HasMember("exclude"))
            {
                const auto exclude = value.FindMember("exclude");
                if (!Keys(exclude, {"road_threshold", "wetness"}, error))
                {
                    return false;
                }
                rule.bExcludeRoad = exclude.HasMember("road_threshold");
                rule.bWetnessBandEnabled = exclude.HasMember("wetness");
                if (!OptionalNumber(exclude, "road_threshold", 0, 1, rule.RoadThreshold, error) ||
                    (rule.bWetnessBandEnabled && !Pair(exclude.FindMember("wetness"), 0, 1, rule.WetnessMin, rule.WetnessMax, error)))
                {
                    return false;
                }
            }
            if (value.HasMember("wind"))
            {
                const auto wind = value.FindMember("wind");
                if (!Keys(wind, {"stiffness", "phase_seed"}, error) ||
                    !OptionalNumber(wind, "stiffness", 0, Max, rule.WindStiffness, error) ||
                    (wind.HasMember("phase_seed") && !UInt64(wind.FindMember("phase_seed"), rule.WindPhaseSeed, error)))
                {
                    return false;
                }
                if (rule.WindPhaseSeed > UINT32_MAX)
                {
                    return Fail(error, u8"散布の wind.phase_seed は uint32 の範囲が必要です");
                }
            }
            return true;
        }

        bool Cook(const std::filesystem::path& path, ScatterSpecCookResult& out, AnsiString& error)
        {
            VariableArray<uint8_t> specBytes;
            Core::JsonDocument document;
            if (!Read(path, specBytes, error) || !ParseDocument({specBytes.data(), specBytes.size()}, document, error))
            {
                return false;
            }
            const auto root = document.GetRoot();
            double version = 0;
            ScatterBakeDesc desc;
            if (!Keys(root, {"version", "terrain", "chunk_size_m", "seed", "species"}, error) ||
                !Number(root.FindMember("version"), 0, 0, version, error) ||
                !Number(root.FindMember("chunk_size_m"), std::numeric_limits<float>::min(), std::numeric_limits<float>::max(), desc.Params.ChunkSizeM, error) ||
                !UInt64(root.FindMember("seed"), desc.Params.Seed, error))
            {
                return false;
            }
            const auto reference = root.FindMember("terrain");
            AnsiString terrainPathText, expected;
            if (!Keys(reference, {"path", "cooked_hash"}, error) || !Text(reference.FindMember("path"), terrainPathText, error) ||
                !Text(reference.FindMember("cooked_hash"), expected, error))
            {
                return false;
            }
            uint64_t recordedHash = 0;
            if (expected.size() != 16)
            {
                return Fail(error, u8"地形の cooked_hash は16桁の16進数で指定してください");
            }
            for (char c : expected)
            {
                const int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
                if (digit < 0)
                {
                    return Fail(error, u8"地形の cooked_hash は16桁の16進数で指定してください");
                }
                recordedHash = (recordedHash << 4) | static_cast<uint64_t>(digit);
            }
            const auto species = root.FindMember("species");
            if (!species.IsArray() || species.GetArraySize() == 0 || species.GetArraySize() > Asset::CookedScatterFormatV0::MaxSpeciesCount)
            {
                return Fail(error, u8"散布の species は1〜64件の配列が必要です");
            }
            for (size_t i = 0; i < species.GetArraySize(); ++i)
            {
                ScatterSpeciesRule rule;
                if (!Species(species.GetArrayElement(i), rule, error))
                {
                    return false;
                }
                for (const auto& previous : desc.Species)
                {
                    if (previous.Name == rule.Name)
                    {
                        return Fail(error, u8"散布の種名が重複しています");
                    }
                }
                desc.Species.push_back(std::move(rule));
            }
            const auto* first = reinterpret_cast<const char8_t*>(terrainPathText.data());
            const auto terrainPath = (path.parent_path() / std::filesystem::path(first, first + terrainPathText.size())).lexically_normal();
            VariableArray<uint8_t> terrainBytes;
            VariableArray<std::filesystem::path> inputs;
            if (terrainPath.extension() == ".nvtrn")
            {
                if (!Read(terrainPath, terrainBytes, error))
                {
                    return false;
                }
                inputs.push_back(terrainPath);
            }
            else
            {
                TerrainSpecCookResult terrainResult;
                if (!CookTerrainSpecFile(terrainPath, terrainResult, error))
                {
                    return false;
                }
                terrainBytes = std::move(terrainResult.Build.NvtrnBytes);
                inputs = std::move(terrainResult.InputFiles);
            }
            // manifest の cooked_hash と同じ、NVTRN 全体に対する FNV-1a64。
            desc.TerrainCookedHash = Asset::ComputeAssetPackagePayloadHash(terrainBytes.data(), terrainBytes.size());
            if (recordedHash != desc.TerrainCookedHash)
            {
                error = reinterpret_cast<const char*>(u8"地形の cooked_hash が食い違います: 記録=");
                error += Asset::FormatAssetHashHex(recordedHash);
                error += reinterpret_cast<const char*>(u8" 実際=");
                error += Asset::FormatAssetHashHex(desc.TerrainCookedHash);
                return false;
            }
            const auto parsed = Asset::ParseCookedTerrain(Asset::AssetBlob::CopyBytes({terrainBytes.data(), terrainBytes.size()}));
            if (!parsed.Succeeded())
            {
                return Fail(error, u8"参照地形の NVTRN を解析できません");
            }
            desc.Params.OriginX = parsed.Terrain.OriginX;
            desc.Params.OriginZ = parsed.Terrain.OriginZ;
            ScatterSpecCookResult result;
            result.SourceHash = Asset::CookedTextureFormatV0::Fnv1a64OffsetBasis;
            HashFile(result.SourceHash, {specBytes.data(), specBytes.size()});
            uint8_t hashBytes[8];
            for (unsigned i = 0; i < 8; ++i)
            {
                hashBytes[i] = static_cast<uint8_t>(desc.TerrainCookedHash >> (8 * i));
            }
            HashFile(result.SourceHash, {hashBytes, 8});
            for (const auto& input : inputs)
            {
                VariableArray<uint8_t> bytes;
                if (!Read(input, bytes, error))
                {
                    return false;
                }
                HashFile(result.SourceHash, {bytes.data(), bytes.size()});
            }
            if (!BakeScatter(desc, parsed.Terrain, {}, result.Bytes, &result.Stats, error))
            {
                return false;
            }
            const auto scatter = Asset::ParseCookedScatter(Asset::AssetBlob::CopyBytes({result.Bytes.data(), result.Bytes.size()}));
            if (!scatter.Succeeded())
            {
                return Fail(error, u8"焼いた散布の NVSCT を解析できません");
            }
            result.PayloadHash = scatter.Scatter.PayloadHash;
            result.ChunkCount = static_cast<uint32_t>(scatter.Scatter.Chunks.size());
            for (size_t i = 0; i < desc.Species.size(); ++i)
            {
                result.SpeciesNames.push_back(desc.Species[i].Name);
                result.InstanceCount += result.Stats.Species[i].InstanceCount;
            }
            out = std::move(result);
            return true;
        }
    } // namespace

    bool CookScatterSpecFile(const std::filesystem::path& path, ScatterSpecCookResult& out, AnsiString& error)
    {
        out = {};
        error.clear();
        try
        {
            return Cook(std::filesystem::absolute(path).lexically_normal(), out, error);
        }
        catch (const std::exception&)
        {
            return Fail(error, u8"散布の構築に失敗しました。入力の寸法・パスと利用可能なメモリを確認してください");
        }
    }
} // namespace NorvesLib::Tools::AssetCook
