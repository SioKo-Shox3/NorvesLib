#include "Resource/ImportSettings.h"
#include "Text/JsonDocument.h"
#include <cmath>

namespace NorvesLib::Core::AssetImport
{
    namespace
    {
        bool EqualsAscii(const Container::String& value, const char* name)
        {
            size_t index = 0;
            while (index < value.size() && name[index] != 0)
            {
                if (static_cast<uint32_t>(value[index]) != static_cast<uint8_t>(name[index]))
                {
                    return false;
                }
                ++index;
            }
            return index == value.size() && name[index] == 0;
        }
        template<size_t Count>
        SettingsResult CheckFields(const JsonValue& object, const char* const (&names)[Count])
        {
            if (!object.IsObject())
            {
                return SettingsResult::InvalidType;
            }
            bool seen[Count] = {};
            for (size_t index = 0; index < object.GetObjectSize(); ++index)
            {
                bool bKnown = false;
                for (size_t field = 0; field < Count; ++field)
                {
                    if (EqualsAscii(object.GetMemberName(index), names[field]))
                    {
                        if (seen[field])
                        {
                            return SettingsResult::DuplicateField;
                        }
                        seen[field] = true;
                        bKnown = true;
                        break;
                    }
                }
                if (!bKnown)
                {
                    return SettingsResult::UnknownField;
                }
            }
            return SettingsResult::Success;
        }
        SettingsResult ReadNumber(const JsonValue& value, double& out)
        {
            if (!value.IsNumber())
            {
                return SettingsResult::InvalidType;
            }
            const double number = value.AsNumber();
            if (!std::isfinite(number))
            {
                return SettingsResult::InvalidValue;
            }
            out = number;
            return SettingsResult::Success;
        }
        SettingsResult ReadBool(const JsonValue& value, bool& out)
        {
            if (!value.IsValid())
            {
                return SettingsResult::Success;
            }
            if (!value.IsBoolean())
            {
                return SettingsResult::InvalidType;
            }
            out = value.AsBool();
            return SettingsResult::Success;
        }
        template<class Enum, size_t Count>
        SettingsResult ReadEnum(const JsonValue& value, const char* const (&names)[Count], Enum& out)
        {
            if (!value.IsValid())
            {
                return SettingsResult::Success;
            }
            if (!value.IsString())
            {
                return SettingsResult::InvalidType;
            }
            const auto text = value.AsString();
            for (size_t index = 0; index < Count; ++index)
            {
                if (EqualsAscii(text, names[index]))
                {
                    out = static_cast<Enum>(index);
                    return SettingsResult::Success;
                }
            }
            return SettingsResult::InvalidValue;
        }
        SettingsResult ReadUnits(const JsonValue& units, ImportSettings& settings)
        {
            if (!units.IsValid())
            {
                return SettingsResult::Success;
            }
            constexpr const char* names[] = {"scale", "fit"};
            auto result = CheckFields(units, names);
            if (result != SettingsResult::Success)
            {
                return result;
            }
            const auto scale = units.FindMember("scale");
            const auto fit = units.FindMember("fit");
            if (scale.IsValid() && fit.IsValid())
            {
                return SettingsResult::InvalidValue;
            }
            if (scale.IsValid())
            {
                return ReadNumber(scale, settings.Scale);
            }
            if (!fit.IsValid())
            {
                return SettingsResult::Success;
            }
            constexpr const char* fitFields[] = {"axis", "meters"};
            result = CheckFields(fit, fitFields);
            if (result != SettingsResult::Success)
            {
                return result;
            }
            // fitのaxis/metersは省略不可。NoneはJSONから選べない。
            if (!fit.FindMember("axis").IsString() || !fit.FindMember("meters").IsNumber())
            {
                return SettingsResult::InvalidType;
            }
            constexpr const char* axes[] = {"", "up", "forward", "longest"};
            result = ReadEnum(fit.FindMember("axis"), axes, settings.Fit);
            if (result != SettingsResult::Success || settings.Fit == FitAxis::None)
            {
                return SettingsResult::InvalidValue;
            }
            return ReadNumber(fit.FindMember("meters"), settings.FitMeters);
        }
        SettingsResult ReadAxes(const JsonValue& axes, ImportSettings& settings)
        {
            if (!axes.IsValid())
            {
                return SettingsResult::Success;
            }
            constexpr const char* fields[] = {"up", "forward", "mirrorX"};
            auto result = CheckFields(axes, fields);
            if (result != SettingsResult::Success)
            {
                return result;
            }
            constexpr const char* names[] = {"+X", "-X", "+Y", "-Y", "+Z", "-Z"};
            result = ReadEnum(axes.FindMember("up"), names, settings.Up);
            if (result != SettingsResult::Success)
            {
                return result;
            }
            result = ReadEnum(axes.FindMember("forward"), names, settings.Forward);
            return result == SettingsResult::Success ? ReadBool(axes.FindMember("mirrorX"), settings.bMirrorX) : result;
        }
        SettingsResult ReadOrigin(const JsonValue& origin, ImportSettings& settings)
        {
            if (!origin.IsValid())
            {
                return SettingsResult::Success;
            }
            constexpr const char* fields[] = {"mode", "offset"};
            auto result = CheckFields(origin, fields);
            if (result != SettingsResult::Success)
            {
                return result;
            }
            constexpr const char* modes[] = {"keep", "bounds_center", "bounds_bottom_center", "surface_centroid", "custom"};
            result = ReadEnum(origin.FindMember("mode"), modes, settings.Origin);
            if (result != SettingsResult::Success)
            {
                return result;
            }
            const auto offset = origin.FindMember("offset");
            if (!offset.IsValid())
            {
                return SettingsResult::Success;
            }
            if (!offset.IsArray() || offset.GetArraySize() != 3)
            {
                return SettingsResult::InvalidType;
            }
            for (size_t index = 0; index < 3; ++index)
            {
                result = ReadNumber(offset.GetArrayElement(index), settings.OriginOffset[index]);
                if (result != SettingsResult::Success)
                {
                    return result;
                }
            }
            return SettingsResult::Success;
        }
        SettingsResult ReadMesh(const JsonValue& mesh, ImportSettings& settings)
        {
            if (!mesh.IsValid())
            {
                return SettingsResult::Success;
            }
            constexpr const char* fields[] = {"winding", "flipU", "flipV"};
            auto result = CheckFields(mesh, fields);
            if (result != SettingsResult::Success)
            {
                return result;
            }
            constexpr const char* modes[] = {"keep", "flip", "auto"};
            result = ReadEnum(mesh.FindMember("winding"), modes, settings.Winding);
            if (result != SettingsResult::Success)
            {
                return result;
            }
            result = ReadBool(mesh.FindMember("flipU"), settings.bFlipU);
            return result == SettingsResult::Success ? ReadBool(mesh.FindMember("flipV"), settings.bFlipV) : result;
        }
        SettingsResult CheckMeta(const JsonValue& meta)
        {
            if (!meta.IsValid())
            {
                return SettingsResult::Success;
            }
            constexpr const char* fields[] = {"generator", "seed", "note"};
            const auto result = CheckFields(meta, fields);
            if (result != SettingsResult::Success)
            {
                return result;
            }
            for (const char* name : {"generator", "note"})
            {
                const auto value = meta.FindMember(name);
                if (value.IsValid() && !value.IsString())
                {
                    return SettingsResult::InvalidType;
                }
            }
            const auto seed = meta.FindMember("seed");
            if (seed.IsValid())
            {
                double value = 0;
                const auto seedResult = ReadNumber(seed, value);
                if (seedResult != SettingsResult::Success)
                {
                    return seedResult;
                }
                if (value < 0 || value > 9007199254740991.0 || std::floor(value) != value)
                {
                    return SettingsResult::InvalidValue;
                }
            }
            return SettingsResult::Success;
        }
    } // namespace

    SettingsResult ParseSettings(const JsonValue& root, ImportSettings& outSettings)
    {
        if (!root.IsObject())
        {
            return SettingsResult::InvalidRoot;
        }
        constexpr const char* fields[] = {"version", "meta", "units", "axes", "origin", "mesh",
            "repair", "lod", "material", "collision", "clip"};
        auto result = CheckFields(root, fields);
        if (result != SettingsResult::Success)
        {
            return result;
        }
        const auto version = root.FindMember("version");
        if (!version.IsNumber() || version.AsNumber() != ImportSettingsSchemaVersion)
        {
            return SettingsResult::InvalidVersion;
        }
        ImportSettings candidate;
        result = ReadUnits(root.FindMember("units"), candidate);
        if (result != SettingsResult::Success)
        {
            return result;
        }
        result = ReadAxes(root.FindMember("axes"), candidate);
        if (result != SettingsResult::Success)
        {
            return result;
        }
        result = ReadOrigin(root.FindMember("origin"), candidate);
        if (result != SettingsResult::Success)
        {
            return result;
        }
        result = ReadMesh(root.FindMember("mesh"), candidate);
        if (result != SettingsResult::Success)
        {
            return result;
        }
        result = CheckMeta(root.FindMember("meta"));
        if (result != SettingsResult::Success)
        {
            return result;
        }
        for (const char* name : {"repair", "lod", "material", "collision", "clip"})
        {
            const auto reserved = root.FindMember(name);
            if (reserved.IsValid() && (!reserved.IsObject() || reserved.GetObjectSize() != 0))
            {
                return SettingsResult::UnsupportedFeature;
            }
        }
        result = ValidateSettings(candidate);
        if (result == SettingsResult::Success)
        {
            outSettings = candidate;
        }
        return result;
    }
} // namespace NorvesLib::Core::AssetImport
