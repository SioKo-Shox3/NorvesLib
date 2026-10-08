#include "TextureAssetSetSpec.h"
#include "Text/JsonDocument.h"
#include "Text/UnicodeText.h"
#include "Asset/CookedSkeletalNameCodec.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace NorvesLib::Tools::AssetCook
{
    namespace
    {
        using Core::JsonValue;
        using Core::Container::AnsiString;
        using Core::Container::AnsiStringView;
        using Error = TextureAssetSetError;
        bool IsLegacyWhitespace(uint32_t scalar)
        {
            // .NET IsNullOrWhiteSpace/Trimと同じUnicode White_Space集合。
            return (scalar >= 9 && scalar <= 13) || scalar == 32 || scalar == 0x85 || scalar == 0xa0 ||
                scalar == 0x1680 || (scalar >= 0x2000 && scalar <= 0x200a) || scalar == 0x2028 ||
                scalar == 0x2029 || scalar == 0x202f || scalar == 0x205f || scalar == 0x3000;
        }
        bool TrimBounds(AnsiStringView text, size_t& begin, size_t& end)
        {
            begin = text.size(); end = 0;
            size_t cursor = 0;
            const bool valid = Core::TextDetail::ForEachUnicodeScalar<char>({text.data(), text.size()}, [&](uint32_t scalar)
            {
                const size_t bytes = scalar < 0x80 ? 1 : scalar < 0x800 ? 2 : scalar < 0x10000 ? 3 : 4;
                if (!IsLegacyWhitespace(scalar))
                {
                    begin = std::min(begin, cursor);
                    end = cursor + bytes;
                }
                cursor += bytes;
            });
            return valid && begin < end;
        }
        bool CopyText(const Core::Container::String& source, AnsiString& out)
        {
            const Core::Container::Span<const Core::Container::String::value_type> units{source.data(), source.size()};
            const auto measured = Core::Asset::MeasureSkeletalNameEncoding(2, units);
            if (!measured.Succeeded()) return false;
            Core::Container::VariableArray<uint8_t> encoded;
            encoded.resize(measured.ByteCount);
            if (!Core::Asset::EncodeSkeletalWireName(2, units, {encoded.data(), encoded.size()}).Succeeded()) return false;
            out.clear();
            if (!encoded.empty()) out.append(reinterpret_cast<const char*>(encoded.data()), encoded.size());
            return true;
        }
        bool KeyEquals(const Core::Container::String& key, const char* expected)
        {
            const size_t length = std::strlen(expected);
            if (key.size() != length) return false;
            for (size_t i = 0; i < length; ++i) if (key[i] != expected[i]) return false;
            return true;
        }
        Error Field(const JsonValue& object, const char* key, JsonValue& out)
        {
            for (size_t i = 0; i < object.GetObjectSize(); ++i)
            {
                const auto& name = object.GetMemberName(i);
                for (const auto unit : name) if (unit == 0) return Error::InvalidValue;
                if (KeyEquals(name, key))
                {
                    if (out.IsValid()) return Error::DuplicateField;
                    out = object.GetMemberValue(i);
                }
            }
            return out.IsValid() ? Error::None : Error::MissingField;
        }
        Error StringField(const JsonValue& object, const char* key, AnsiString& out, const AnsiString* fallback = nullptr)
        {
            JsonValue value;
            auto error = Field(object, key, value);
            if (error == Error::MissingField && fallback) { out = *fallback; return Error::None; }
            if (error != Error::None) return error;
            if (!value.IsString()) return Error::InvalidType;
            if (!CopyText(value.AsString(), out) || out.empty()) return Error::InvalidValue;
            size_t begin = 0, end = 0;
            return TrimBounds(out, begin, end) ? Error::None : Error::InvalidValue;
        }
        bool OnlyKeys(const JsonValue& object, const char* const* keys, size_t count)
        {
            for (size_t i = 0; i < object.GetObjectSize(); ++i)
            {
                bool known = false;
                for (size_t k = 0; k < count; ++k)
                {
                    if (KeyEquals(object.GetMemberName(i), keys[k]))
                    {
                        known = true;
                    }
                }
                if (!known)
                {
                    return false;
                }
            }
            return true;
        }
        Error BudgetObject(const JsonValue& object, CookAssetBudget& out)
        {
            if (!object.IsObject())
            {
                return Error::InvalidType;
            }
            const char* keys[] = {"max_triangles", "max_joints", "max_texture_bytes", "max_cooked_bytes", "max_bytes"};
            uint64_t* fields[] = {&out.MaxTriangles, &out.MaxJoints, &out.MaxTextureBytes, &out.MaxCookedBytes,
                                  &out.MaxTextureBytes};
            for (size_t i = 0; i < object.GetObjectSize(); ++i)
            {
                bool known = false;
                for (const auto* key : keys)
                {
                    if (KeyEquals(object.GetMemberName(i), key))
                    {
                        known = true;
                    }
                }
                if (!known)
                {
                    return Error::InvalidValue;
                }
            }
            for (size_t i = 0; i < 5; ++i)
            {
                JsonValue value;
                const auto status = Field(object, keys[i], value);
                if (status == Error::MissingField)
                {
                    continue;
                }
                if (status != Error::None)
                {
                    return status;
                }
                if (!value.IsIntegerLiteral() || value.AsNumber() < 0 || value.AsNumber() > 9007199254740991.0)
                {
                    return Error::InvalidValue;
                }
                *fields[i] = uint64_t(value.AsNumber());
            }
            if (object.HasMember("max_bytes") && object.HasMember("max_texture_bytes"))
            {
                return Error::DuplicateField;
            }
            return Error::None;
        }
        bool PrintableAscii(AnsiStringView value)
        {
            for (const unsigned char c : value) if (c < 32 || c >= 127) return false;
            return true;
        }
        int CompareIgnoreAsciiCase(AnsiStringView a, AnsiStringView b);
        Error NormalizeRelative(AnsiString& value, bool stripAssets)
        {
            // 旧CLIのmanifestはASCII契約。Windowsの別名やADSを出力名に持ち込まない。
            size_t begin = 0, end = 0;
            if (!TrimBounds(value, begin, end)) return Error::UnsafePath;
            AnsiString normalized;
            for (size_t i = begin; i < end; ++i) normalized.push_back(value[i] == '\\' ? '/' : value[i]);
            if (normalized.empty() || !PrintableAscii(normalized) || normalized[0] == '/') return Error::UnsafePath;
            size_t segment = 0;
            for (size_t i = 0; i <= normalized.size(); ++i)
            {
                if (i < normalized.size() && normalized[i] == ':') return Error::UnsafePath;
                if (i == normalized.size() || normalized[i] == '/')
                {
                    const size_t length = i - segment;
                    if (length == 0 || (length == 1 && normalized[segment] == '.') ||
                        (length == 2 && normalized[segment] == '.' && normalized[segment + 1] == '.') ||
                        normalized[i - 1] == '.' || normalized[i - 1] == ' ') return Error::UnsafePath;
                    segment = i + 1;
                }
            }
            if (stripAssets)
            {
                if (CompareIgnoreAsciiCase(normalized, "Assets") == 0) return Error::UnsafePath;
                constexpr char prefix[] = "Assets/";
                if (normalized.size() >= sizeof(prefix) - 1 && std::memcmp(normalized.data(), prefix, sizeof(prefix) - 1) == 0)
                {
                    AnsiString stripped;
                    for (size_t i = sizeof(prefix) - 1; i < normalized.size(); ++i) stripped.push_back(normalized[i]);
                    normalized = std::move(stripped);
                }
            }
            value = std::move(normalized);
            return Error::None;
        }
        int CompareIgnoreAsciiCase(AnsiStringView a, AnsiStringView b)
        {
            const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c; };
            for (size_t i = 0; i < std::min(a.size(), b.size()); ++i)
            {
                const char left = lower(a[i]), right = lower(b[i]);
                if (left != right) return left < right ? -1 : 1;
            }
            return a.size() == b.size() ? 0 : (a.size() < b.size() ? -1 : 1);
        }
    }
    TextureAssetSetResult ParseTextureAssetSetSpec(const Core::JsonValue& root, TextureAssetSetSpec& out,
                                                   bool bAllowVersion2)
    {
        if (!root.IsObject()) return {Error::InvalidRoot};
        TextureAssetSetSpec candidate;
        JsonValue version;
        auto error = Field(root, "version", version);
        if (error != Error::None) return {error, static_cast<size_t>(-1), "version"};
        if (!version.IsIntegerLiteral() || (version.AsNumber() != 1 && !(bAllowVersion2 && version.AsNumber() == 2)))
        {
            return {Error::InvalidVersion, static_cast<size_t>(-1), "version"};
        }
        candidate.Version = uint32_t(version.AsNumber());
        if (candidate.Version == 2)
        {
            const char* keys[] = {"version", "name",    "package_root",       "default_variant",
                                  "assets",  "budgets", "emissiveNitsPerUnit"};
            if (!OnlyKeys(root, keys, 7))
            {
                return {Error::InvalidValue, static_cast<size_t>(-1), "unsupported_spec_field"};
            }
        }
        struct NamedText { const char* Key; AnsiString* Value; };
        const NamedText top[] = {{"name", &candidate.Name}, {"package_root", &candidate.PackageRoot}, {"default_variant", &candidate.DefaultVariant}};
        for (const auto& field : top)
        {
            error = StringField(root, field.Key, *field.Value);
            if (error != Error::None) return {error, static_cast<size_t>(-1), field.Key};
        }
        error = NormalizeRelative(candidate.PackageRoot, false);
        if (error != Error::None) return {error, static_cast<size_t>(-1), "package_root"};
        if (!PrintableAscii(candidate.DefaultVariant)) return {Error::InvalidValue, static_cast<size_t>(-1), "default_variant"};
        JsonValue defaultBudgets;
        if (candidate.Version == 2 && root.HasMember("emissiveNitsPerUnit"))
        {
            JsonValue value;
            if (Field(root, "emissiveNitsPerUnit", value) != Error::None || !value.IsNumber() ||
                !std::isfinite(value.AsNumber()) || value.AsNumber() <= 0)
            {
                return {Error::InvalidValue, SIZE_MAX, "emissiveNitsPerUnit"};
            }
            candidate.Emission = {true, value.AsNumber()};
        }
        if (candidate.Version == 2 && root.HasMember("budgets"))
        {
            JsonValue budgets;
            error = Field(root, "budgets", budgets);
            if (error != Error::None || !budgets.IsObject())
            {
                return {Error::InvalidValue, static_cast<size_t>(-1), "budgets"};
            }
            const char* budgetKeys[] = {"defaults", "total"};
            if (!OnlyKeys(budgets, budgetKeys, 2))
            {
                return {Error::InvalidValue, SIZE_MAX, "budgets"};
            }
            if (budgets.HasMember("defaults"))
            {
                error = Field(budgets, "defaults", defaultBudgets);
                if (error != Error::None || !defaultBudgets.IsObject())
                {
                    return {Error::InvalidValue, static_cast<size_t>(-1), "budgets.defaults"};
                }
            }
            if (defaultBudgets.IsObject())
            {
                const char* kinds[] = {"texture", "model", "skeletal", "animation", "audio", "raw"};
                if (!OnlyKeys(defaultBudgets, kinds, 6))
                {
                    return {Error::InvalidValue, SIZE_MAX, "budgets.defaults"};
                }
                for (const auto* kind : kinds)
                {
                    if (defaultBudgets.HasMember(kind))
                    {
                        JsonValue value;
                        CookAssetBudget checked;
                        if (Field(defaultBudgets, kind, value) != Error::None ||
                            BudgetObject(value, checked) != Error::None ||
                            (std::strcmp(kind, "texture") != 0 && value.HasMember("max_bytes")))
                        {
                            return {Error::InvalidValue, SIZE_MAX, "budgets.defaults"};
                        }
                    }
                }
            }
            if (budgets.HasMember("total"))
            {
                JsonValue total;
                error = Field(budgets, "total", total);
                if (error != Error::None || BudgetObject(total, candidate.TotalBudget) != Error::None ||
                    total.HasMember("max_bytes"))
                {
                    return {Error::InvalidValue, static_cast<size_t>(-1), "budgets.total"};
                }
            }
        }
        JsonValue textures;
        error = Field(root, candidate.Version == 1 ? "textures" : "assets", textures);
        if (error != Error::None) return {error, static_cast<size_t>(-1), "textures"};
        if (!textures.IsArray()) return {Error::InvalidType, static_cast<size_t>(-1), "textures"};
        if (textures.GetArraySize() == 0) return {Error::InvalidValue, static_cast<size_t>(-1), "textures"};
        if (candidate.Version == 2 && textures.GetArraySize() > 1024)
        {
            return {Error::InvalidValue, static_cast<size_t>(-1), "assets"};
        }
        candidate.Textures.reserve(textures.GetArraySize());
        for (size_t index = 0; index < textures.GetArraySize(); ++index)
        {
            const auto item = textures.GetArrayElement(index);
            if (!item.IsObject()) return {Error::InvalidType, index, "textures"};
            TextureAssetSetEntry entry;
            if (candidate.Version == 2)
            {
                const char* keys[] = {"kind",          "logical_path", "source_path", "format",   "package_name",
                                      "entry_name",    "variant",      "entry_type",  "budget",   "joint_nodes",
                                      "skeleton_path", "role_profile", "source_clip", "clip_name"};
                if (!OnlyKeys(item, keys, sizeof(keys) / sizeof(keys[0])))
                {
                    return {Error::InvalidValue, index, "unsupported_asset_field"};
                }
            }
            if (candidate.Version == 2)
            {
                error = StringField(item, "kind", entry.Kind);
                if (error != Error::None)
                {
                    return {error, index, "kind"};
                }
                if (entry.Kind == "texture")
                {
                    entry.EntryType = "Tex0";
                }
                else if (entry.Kind == "model")
                {
                    entry.EntryType = "Msh0";
                }
                else if (entry.Kind == "skeletal")
                {
                    entry.EntryType = "Skl0";
                }
                else if (entry.Kind == "animation")
                {
                    entry.EntryType = "Anm1";
                }
                else if (entry.Kind == "audio")
                {
                    entry.EntryType = "Aud0";
                }
                else if (entry.Kind == "raw")
                {
                    entry.EntryType = "Raw ";
                }
                else
                {
                    return {Error::InvalidValue, index, "kind"};
                }
                const auto fallback = entry.EntryType;
                error = StringField(item, "entry_type", entry.EntryType, &fallback);
                if (error != Error::None || entry.EntryType.size() != 4 || !PrintableAscii(entry.EntryType))
                {
                    return {Error::InvalidValue, index, "entry_type"};
                }
            }
            const NamedText fields[] = {{"logical_path", &entry.LogicalPath}, {"source_path", &entry.SourcePath},
                {"format", &entry.Format}, {"package_name", &entry.PackageName}, {"entry_name", &entry.EntryName}, {"variant", &entry.Variant}};
            for (const auto& field : fields)
            {
                error = StringField(item, field.Key, *field.Value, std::strcmp(field.Key, "variant") == 0 ? &candidate.DefaultVariant : nullptr);
                if (error != Error::None) return {error, index, field.Key};
            }
            if (candidate.Version == 2 && (entry.Kind == "model" || entry.Kind == "skeletal") &&
                !item.HasMember("entry_type") && entry.Format.find("nvskel.") == 0)
            {
                entry.EntryType = entry.Format == "nvskel.v1.skinmesh.pnujiw.u32" ? "Skm1" : "Skl0";
            }
            if (candidate.Version == 2)
            {
                if (defaultBudgets.HasMember(entry.Kind.c_str()))
                {
                    JsonValue value;
                    error = Field(defaultBudgets, entry.Kind.c_str(), value);
                    if (error != Error::None || BudgetObject(value, entry.Budget) != Error::None)
                    {
                        return {Error::InvalidValue, index, "budgets.defaults"};
                    }
                }
                if (item.HasMember("budget"))
                {
                    JsonValue value;
                    error = Field(item, "budget", value);
                    if (error != Error::None || BudgetObject(value, entry.Budget) != Error::None ||
                        (entry.Kind != "texture" && value.HasMember("max_bytes")))
                    {
                        return {Error::InvalidValue, index, "budget"};
                    }
                }
            }
            if (candidate.Version == 2 && (item.HasMember("skeleton_path") || item.HasMember("role_profile") ||
                                           item.HasMember("source_clip") || item.HasMember("clip_name")))
            {
                AnsiString empty;
                if (entry.Kind != "animation" ||
                    StringField(item, "skeleton_path", entry.SkeletonPath) != Error::None ||
                    StringField(item, "role_profile", entry.RoleProfilePath) != Error::None ||
                    StringField(item, "source_clip", entry.SourceClip, &empty) != Error::None ||
                    StringField(item, "clip_name", entry.ClipName, &empty) != Error::None ||
                    entry.SkeletonPath.empty() || entry.RoleProfilePath.empty() || entry.SourceClip.size() > 4096 ||
                    entry.ClipName.size() > 4096)
                {
                    return {Error::InvalidValue, index, "retarget_inputs"};
                }
                for (const auto* text :
                     {&entry.SkeletonPath, &entry.RoleProfilePath, &entry.SourceClip, &entry.ClipName})
                {
                    for (const unsigned char c : *text)
                    {
                        if (c < 32 || c == 127)
                        {
                            return {Error::InvalidValue, index, "retarget_inputs"};
                        }
                    }
                }
            }
            if (candidate.Version == 2 && item.HasMember("joint_nodes"))
            {
                JsonValue nodes;
                if (entry.Kind != "animation" || Field(item, "joint_nodes", nodes) != Error::None || !nodes.IsArray() ||
                    nodes.GetArraySize() == 0 || nodes.GetArraySize() > 256)
                {
                    return {Error::InvalidValue, index, "joint_nodes"};
                }
                for (size_t i = 0; i < nodes.GetArraySize(); ++i)
                {
                    const auto value = nodes.GetArrayElement(i);
                    if (!value.IsIntegerLiteral() || value.AsNumber() < 0 || value.AsNumber() > UINT32_MAX)
                    {
                        return {Error::InvalidValue, index, "joint_nodes"};
                    }
                    const auto node = uint32_t(value.AsNumber());
                    if (std::find(entry.JointNodes.begin(), entry.JointNodes.end(), node) != entry.JointNodes.end())
                    {
                        return {Error::InvalidValue, index, "joint_nodes"};
                    }
                    entry.JointNodes.push_back(node);
                }
            }
            error = NormalizeRelative(entry.LogicalPath, true);
            if (error != Error::None) return {error, index, "logical_path"};
            error = NormalizeRelative(entry.EntryName, true);
            if (error != Error::None) return {error, index, "entry_name"};
            error = NormalizeRelative(entry.PackageName, false);
            if (error != Error::None) return {error, index, "package_name"};
            if (!PrintableAscii(entry.Format) || !PrintableAscii(entry.Variant)) return {Error::InvalidValue, index, "format/variant"};
            // sourceはUTF8を保持する。drive-relative/root-relativeの解決と存在検査は実行層で行う。
            for (const unsigned char c : entry.SourcePath) if (c < 32 || c == 127) return {Error::UnsafePath, index, "source_path"};
            candidate.Textures.push_back(std::move(entry));
        }
        // 入力順を変えず、indexだけを並べて大量assetでも二乗時間にしない。
        Core::Container::VariableArray<size_t> order;
        order.reserve(candidate.Textures.size());
        for (size_t i = 0; i < candidate.Textures.size(); ++i) order.push_back(i);
        for (const bool packages : {false, true})
        {
            const auto compare = [&](size_t a, size_t b)
            {
                const auto& left = candidate.Textures[a];
                const auto& right = candidate.Textures[b];
                if (packages) return CompareIgnoreAsciiCase(left.PackageName, right.PackageName);
                const int logical = CompareIgnoreAsciiCase(left.LogicalPath, right.LogicalPath);
                if (logical != 0)
                {
                    return logical;
                }
                if (candidate.Version == 2)
                {
                    const int kind = CompareIgnoreAsciiCase(left.Kind, right.Kind);
                    if (kind != 0)
                    {
                        return kind;
                    }
                }
                return CompareIgnoreAsciiCase(left.Variant, right.Variant);
            };
            std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { const int result = compare(a, b); return result != 0 ? result < 0 : a < b; });
            for (size_t i = 1; i < order.size(); ++i)
            {
                if (compare(order[i - 1], order[i]) == 0)
                    return {packages ? Error::DuplicatePackage : Error::DuplicateLogicalKey,
                        order[i], packages ? "package_name" : "logical_path/variant"};
            }
        }
        out = std::move(candidate);
        return {};
    }
}
