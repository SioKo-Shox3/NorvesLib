#include "GameModes/Wasteland/WastelandCliffScan.h"

#include "Core/Public/Container/Span.h"
#include "Core/Public/Text/JsonDocument.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <utility>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Container;

namespace Game::GameModes::WastelandCliffScan
{
    namespace
    {
        // 四元数の長さが 1 からこれ以上ずれていたら、書き出しの誤りとみなして読まない
        constexpr double kQuaternionLengthTolerance = 1.0e-3;

        bool ReadFileBytes(const String &path, VariableArray<uint8_t> &outBytes)
        {
            std::ifstream stream(path.c_str(), std::ios::binary | std::ios::ate);
            if (!stream)
            {
                return false;
            }
            const std::streamoff size = stream.tellg();
            if (size <= 0)
            {
                return false;
            }
            outBytes.resize(static_cast<size_t>(size));
            stream.seekg(0, std::ios::beg);
            stream.read(reinterpret_cast<char *>(outBytes.data()), size);
            return static_cast<bool>(stream);
        }

        /// 有限の数を count 個並べた配列を読む
        bool ReadNumbers(const JsonValue &value, size_t count, double *outValues)
        {
            if (!value.IsArray() || value.GetArraySize() != count)
            {
                return false;
            }
            for (size_t index = 0; index < count; ++index)
            {
                const JsonValue element = value.GetArrayElement(index);
                if (!element.IsNumber() || !std::isfinite(element.AsNumber()))
                {
                    return false;
                }
                outValues[index] = element.AsNumber();
            }
            return true;
        }

        /// 空でない文字列の項目を読む
        bool ReadText(const JsonValue &object, const char *key, String &outText)
        {
            const JsonValue value = object.FindMember(key);
            if (!value.IsString() || value.AsString().empty())
            {
                return false;
            }
            outText = value.AsString();
            return true;
        }

        String IndexText(const char *list, size_t index)
        {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%s[%zu]", list, index);
            return String(buffer);
        }
    } // namespace

    String TablePath()
    {
#if defined(NORVES_SOURCE_ASSET_DIR)
        // 作業ディレクトリは実行ファイルのディレクトリのことがあるので、リポジトリの Assets の隣を基準に探す
        return String(NORVES_SOURCE_ASSET_DIR) + "/../" + kTableRelativePath;
#else
        return String(kTableRelativePath);
#endif
    }

    bool LoadTable(Table &outTable, String &outError)
    {
        const String path = TablePath();
        VariableArray<uint8_t> bytes;
        if (!ReadFileBytes(path, bytes))
        {
            outError = String("読めません: ") + path;
            return false;
        }
        // Blender の台本は BOM なしで書くが、人がエディタで直すと BOM が付くことがあるので飛ばす
        size_t start = 0;
        if (bytes.size() >= 3 && bytes[0] == 0xEFu && bytes[1] == 0xBBu && bytes[2] == 0xBFu)
        {
            start = 3;
        }
        JsonDocument document;
        String parseError;
        if (!JsonDocument::TryParseUtf8(Span<const uint8_t>(bytes.data() + start, bytes.size() - start), document,
                                        &parseError))
        {
            outError = path + ": " + parseError;
            return false;
        }
        const JsonValue root = document.GetRoot();
        if (!root.IsObject() || root.FindMember("version").AsNumber() != 1.0)
        {
            outError = path + ": version が 1 でありません";
            return false;
        }

        Table table;
        const JsonValue assets = root.FindMember("assets");
        if (!assets.IsArray() || assets.GetArraySize() == 0 || assets.GetArraySize() > kMaxAssets)
        {
            outError = path + ": assets は 1〜8 個の配列にしてください";
            return false;
        }
        for (size_t index = 0; index < assets.GetArraySize(); ++index)
        {
            const JsonValue item = assets.GetArrayElement(index);
            AssetSpec spec;
            if (!item.IsObject() || !ReadText(item, "id", spec.Id) || !ReadText(item, "mesh", spec.MeshPath) ||
                !ReadText(item, "albedo", spec.AlbedoPath) || !ReadText(item, "normal", spec.NormalPath) ||
                !ReadText(item, "orm", spec.OrmPath))
            {
                outError = path + ": " + IndexText("assets", index) + " に id・mesh・albedo・normal・orm の文字列がありません";
                return false;
            }
            for (const AssetSpec &other : table.Assets)
            {
                if (other.Id == spec.Id)
                {
                    outError = path + ": 素材の id が重なっています: " + spec.Id;
                    return false;
                }
            }
            table.Assets.push_back(spec);
        }

        const JsonValue placements = root.FindMember("placements");
        if (!placements.IsArray())
        {
            outError = path + ": placements の配列がありません";
            return false;
        }
        for (size_t index = 0; index < placements.GetArraySize(); ++index)
        {
            const JsonValue item = placements.GetArrayElement(index);
            const String where = IndexText("placements", index);
            Placement placement;
            String assetId;
            if (!item.IsObject() || !ReadText(item, "asset", assetId))
            {
                outError = path + ": " + where + " に asset の文字列がありません";
                return false;
            }
            bool bFound = false;
            for (uint32_t assetIndex = 0; assetIndex < table.Assets.size(); ++assetIndex)
            {
                if (table.Assets[assetIndex].Id == assetId)
                {
                    placement.AssetIndex = assetIndex;
                    bFound = true;
                    break;
                }
            }
            if (!bFound)
            {
                outError = path + ": " + where + " の素材が assets にありません: " + assetId;
                return false;
            }
            if (!ReadText(item, "name", placement.Name))
            {
                placement.Name = where;
            }
            double position[3];
            double rotation[4];
            double scale = 0.0;
            const JsonValue scaleValue = item.FindMember("scale");
            if (!ReadNumbers(item.FindMember("position"), 3, position) ||
                !ReadNumbers(item.FindMember("rotation_xyzw"), 4, rotation) || !scaleValue.IsNumber())
            {
                outError = path + ": " + where + " の position（3 つ）・rotation_xyzw（4 つ）・scale の数がありません";
                return false;
            }
            scale = scaleValue.AsNumber();
            if (!std::isfinite(scale) || scale <= 0.0)
            {
                outError = path + ": " + where + " の scale は正の数にしてください";
                return false;
            }
            const double length = std::sqrt(rotation[0] * rotation[0] + rotation[1] * rotation[1] +
                                            rotation[2] * rotation[2] + rotation[3] * rotation[3]);
            if (std::fabs(length - 1.0) > kQuaternionLengthTolerance)
            {
                outError = path + ": " + where + " の rotation_xyzw の長さが 1 でありません";
                return false;
            }
            placement.Position = NorvesLib::Math::Vector3(static_cast<float>(position[0]), static_cast<float>(position[1]),
                                                          static_cast<float>(position[2]));
            placement.Rotation =
                NorvesLib::Math::Quaternion(static_cast<float>(rotation[0] / length), static_cast<float>(rotation[1] / length),
                                            static_cast<float>(rotation[2] / length), static_cast<float>(rotation[3] / length));
            placement.Scale = static_cast<float>(scale);
            table.Placements.push_back(placement);
        }
        outTable = std::move(table);
        return true;
    }

} // namespace Game::GameModes::WastelandCliffScan
