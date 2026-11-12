#include "Rendering/Terrain/TerrainMaterialDesc.h"

#include "Asset/AssetFileReader.h"
#include "Rendering/Terrain/TerrainHeightField.h"
#include "Text/JsonDocument.h"
#include "Text/UnicodeText.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace NorvesLib::Core::Rendering::Terrain
{
    namespace
    {
        struct ProvisionalSurfaceEntry
        {
            const char* LayerName;
            const char* SurfaceName;
        };

        // 名前の一覧は企画で未定の仮の値。
        constexpr ProvisionalSurfaceEntry kProvisionalSurfaces[] = {
            {"soil", "Soil"},
            {"meadow", "Soil"},
            {"gravel", "Gravel"},
            {"moss", "Moss"},
            {"mud", "Mud"},
            {"wet_mud", "Mud"},
            {"rock", "Rock"},
            {"rock_face", "Rock"},
            {"sand", "Sand"},
            {"water", "Water"}};

        // JSON の文字列を UTF-8 にし、名前やパスで区切りと誤認される NUL を拒む。
        bool ReadString(const JsonValue& value, Container::AnsiString& out, bool bAllowEmpty = false)
        {
            if (!value.IsString())
            {
                return false;
            }
            const auto& text = value.AsString();
            out.clear();
            bool bHasNull = false;
            const bool bValid = TextDetail::ForEachUnicodeScalar(
                Container::Span<const Container::String::value_type>(text.data(), text.size()),
                [&](uint32_t code)
                {
                    if (code == 0)
                    {
                        bHasNull = true;
                    }
                    else if (code < 0x80)
                    {
                        out += static_cast<char>(code);
                    }
                    else if (code < 0x800)
                    {
                        out += static_cast<char>(0xc0 | (code >> 6));
                        out += static_cast<char>(0x80 | (code & 0x3f));
                    }
                    else if (code < 0x10000)
                    {
                        out += static_cast<char>(0xe0 | (code >> 12));
                        out += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
                        out += static_cast<char>(0x80 | (code & 0x3f));
                    }
                    else
                    {
                        out += static_cast<char>(0xf0 | (code >> 18));
                        out += static_cast<char>(0x80 | ((code >> 12) & 0x3f));
                        out += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
                        out += static_cast<char>(0x80 | (code & 0x3f));
                    }
                });
            return bValid && !bHasNull && (bAllowEmpty || !out.empty());
        }

        bool ReadOptionalPath(const JsonValue& layer, const char* key, Container::AnsiString& out)
        {
            const JsonValue value = layer.FindMember(key);
            return !value.IsValid() || ReadString(value, out, true);
        }

        bool ReadFloat(const JsonValue& value, float& out)
        {
            if (!value.IsNumber())
            {
                return false;
            }
            const double number = value.AsNumber();
            constexpr double kMax = static_cast<double>(std::numeric_limits<float>::max());
            if (!std::isfinite(number) || number < -kMax || number > kMax)
            {
                return false;
            }
            out = static_cast<float>(number);
            return true;
        }

        bool ReadUnitFloat(const JsonValue& value, float& out)
        {
            return ReadFloat(value, out) && value.AsNumber() >= 0.0 && value.AsNumber() <= 1.0;
        }
    } // namespace

    Container::AnsiStringView TerrainMaterialDesc::GetSurfaceName(uint8_t surfaceId) const
    {
        if (surfaceId >= Layers.size())
        {
            return {};
        }
        const auto& name = Layers[surfaceId].SurfaceName;
        return Container::AnsiStringView(name.data(), name.size());
    }

    Container::AnsiStringView GetProvisionalSurfaceName(Container::AnsiStringView layerName)
    {
        for (const auto& entry : kProvisionalSurfaces)
        {
            if (layerName == Container::AnsiStringView(entry.LayerName))
            {
                return Container::AnsiStringView(entry.SurfaceName);
            }
        }
        return Container::AnsiStringView("Soil");
    }

    TerrainMaterialDesc MakeProvisionalTerrainMaterialDesc(const TerrainHeightField& field)
    {
        TerrainMaterialDesc desc;
        desc.Layers.resize(field.GetSplatLayerCount());
        for (uint32_t layer = 0; layer < field.GetSplatLayerCount(); ++layer)
        {
            const auto layerName = field.GetSplatLayerName(layer);
            const auto surfaceName = GetProvisionalSurfaceName(layerName);
            desc.Layers[layer].LayerName.append(layerName.data(), layerName.size());
            desc.Layers[layer].SurfaceName.append(surfaceName.data(), surfaceName.size());
        }
        return desc;
    }

    bool LoadTerrainMaterialDesc(Container::AnsiStringView tablePath, const TerrainHeightField& field,
                                 TerrainMaterialDesc& outDesc, Container::AnsiString& outError)
    {
        constexpr uint64_t kMaxTableBytes = 1024u * 1024u;
        outDesc = MakeProvisionalTerrainMaterialDesc(field);
        outError.clear();
        const auto fail = [&](const char* reason)
        {
            outError = reason;
            outError += "（";
            outError.append(tablePath.data(), tablePath.size());
            outError += "）";
            return false;
        };
        if (field.GetSplatLayerCount() == 0)
        {
            return fail("高さ場にスプラットの層がありません");
        }

        Asset::AssetFileReader reader;
        Asset::AssetReadRequest request;
        request.InputPath.append(tablePath.data(), tablePath.size());
        request.bAllowAbsolutePath = false;
        request.MaxReadBytes = kMaxTableBytes;
        const Asset::AssetReadResult table = reader.Read(request);
        if (!table.Succeeded())
        {
            return fail("地形の層の表を読めません");
        }
        Container::Span<const uint8_t> text(table.Blob.GetData(), table.Blob.GetSize());
        if (text.size() >= 3 && text[0] == 0xefu && text[1] == 0xbbu && text[2] == 0xbfu)
        {
            text = {text.data() + 3, text.size() - 3};
        }
        JsonDocument document;
        if (!JsonDocument::TryParseUtf8(text, document))
        {
            return fail("地形の層の表を JSON として読めません");
        }
        const JsonValue root = document.GetRoot();
        const JsonValue version = root.FindMember("version");
        if (!root.IsObject() || !version.IsIntegerLiteral() || version.AsNumber() != 1.0)
        {
            return fail("地形の層の表の版が不正です");
        }

        TerrainMaterialDesc candidate = outDesc;
        Container::AnsiString cliffName;
        const JsonValue slope = root.FindMember("cliff_slope_deg");
        const JsonValue rotation = root.FindMember("uv_rotation");
        if (!ReadString(root.FindMember("cliff_layer"), cliffName) || !slope.IsArray() || slope.GetArraySize() != 2 ||
            !ReadFloat(slope.GetArrayElement(0), candidate.CliffSlopeDegrees[0]) ||
            !ReadFloat(slope.GetArrayElement(1), candidate.CliffSlopeDegrees[1]) ||
            slope.GetArrayElement(0).AsNumber() < 0.0 || slope.GetArrayElement(1).AsNumber() > 90.0 ||
            candidate.CliffSlopeDegrees[0] >= candidate.CliffSlopeDegrees[1])
        {
            return fail("地形の層の表の崖の名前・傾斜が不正です");
        }
        if (!ReadFloat(root.FindMember("height_blend_depth"), candidate.HeightBlendDepth) ||
            candidate.HeightBlendDepth <= 0.0f || !rotation.IsObject() ||
            !ReadFloat(rotation.FindMember("wavelength_m"), candidate.UvRotationWavelength) ||
            rotation.FindMember("wavelength_m").AsNumber() < 0.0 ||
            !ReadFloat(rotation.FindMember("max_deg"), candidate.UvRotationDegrees) ||
            rotation.FindMember("max_deg").AsNumber() < 0.0)
        {
            return fail("地形の層の表の高さの混合・UV の回転が不正です");
        }

        const JsonValue layers = root.FindMember("layers");
        if (!layers.IsArray())
        {
            return fail("地形の層の表の layers は配列ではありません");
        }
        Container::VariableArray<TerrainSurfaceLayer> tableLayers;
        tableLayers.reserve(layers.GetArraySize());
        for (size_t index = 0; index < layers.GetArraySize(); ++index)
        {
            const JsonValue item = layers.GetArrayElement(index);
            TerrainSurfaceLayer layer;
            const JsonValue color = item.FindMember("color");
            if (!item.IsObject() || !ReadString(item.FindMember("name"), layer.LayerName) ||
                !ReadString(item.FindMember("surface"), layer.SurfaceName) ||
                !ReadFloat(item.FindMember("tile_m"), layer.TileMeters) || layer.TileMeters <= 0.0f ||
                !ReadUnitFloat(item.FindMember("wettability"), layer.Wettability) ||
                !color.IsArray() || color.GetArraySize() != 3 ||
                !ReadUnitFloat(color.GetArrayElement(0), layer.PlaceholderColor[0]) ||
                !ReadUnitFloat(color.GetArrayElement(1), layer.PlaceholderColor[1]) ||
                !ReadUnitFloat(color.GetArrayElement(2), layer.PlaceholderColor[2]) ||
                !ReadUnitFloat(item.FindMember("roughness"), layer.PlaceholderRoughness) ||
                !ReadOptionalPath(item, "albedo", layer.AlbedoPath) ||
                !ReadOptionalPath(item, "normal", layer.NormalPath) ||
                !ReadOptionalPath(item, "arm", layer.ArmPath))
            {
                return fail("地形の層の表の名前・表面・値・テクスチャのパスが不正です");
            }
            tableLayers.push_back(std::move(layer));
        }
        std::sort(tableLayers.begin(), tableLayers.end(), [](const auto& a, const auto& b)
        {
            return a.LayerName < b.LayerName;
        });
        bool bFoundCliff = false;
        for (size_t index = 0; index < tableLayers.size(); ++index)
        {
            if (index > 0 && tableLayers[index - 1].LayerName == tableLayers[index].LayerName)
            {
                return fail("地形の層の表に重複した層の名前があります");
            }
            bFoundCliff = bFoundCliff || tableLayers[index].LayerName == cliffName;
        }
        if (!bFoundCliff)
        {
            return fail("崖の層の名前が地形の層の表にありません");
        }
        for (uint32_t index = 0; index < field.GetSplatLayerCount(); ++index)
        {
            for (const auto& layer : tableLayers)
            {
                if (candidate.Layers[index].LayerName == layer.LayerName)
                {
                    candidate.Layers[index] = layer;
                    if (candidate.CliffLayer < 0 && layer.LayerName == cliffName)
                    {
                        candidate.CliffLayer = static_cast<int32_t>(index);
                    }
                    break;
                }
            }
        }
        outDesc = std::move(candidate);
        return true;
    }
} // namespace NorvesLib::Core::Rendering::Terrain
