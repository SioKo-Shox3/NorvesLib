#include "Resource/ImportSettings.h"
#include "Text/JsonDocument.h"
#include "Resource/MaterialImportDocument.h"
#include "Resource/MaterialImportSettingsJson.h"
#include "Asset/CookedSkeletalNameCodec.h"
#include <utility>
#include <cmath>
#include <cstring>

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
        SettingsResult CheckFields(const JsonValue& object, const char* const (&names)[Count], size_t allowedCount = Count)
        {
            if (!object.IsObject())
            {
                return SettingsResult::InvalidType;
            }
            bool seen[Count] = {};
            for (size_t index = 0; index < object.GetObjectSize(); ++index)
            {
                bool bKnown = false;
                for (size_t field = 0; field < allowedCount; ++field)
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
        SettingsResult ReadClip(const JsonValue& clip, ClipImportSettings& out)
        {
            ClipImportSettings candidate;
            if (!clip.IsValid())
            {
                out = std::move(candidate);
                return SettingsResult::Success;
            }
            constexpr const char* fields[] = {"root_joint", "loop", "loop_threshold", "authored_fps",
                "source_fps", "time_scale", "analysis"};
            auto result = CheckFields(clip, fields);
            if (result != SettingsResult::Success)
            {
                return result;
            }
            const auto rootJoint = clip.FindMember("root_joint");
            if (rootJoint.IsValid())
            {
                if (!rootJoint.IsString())
                {
                    return SettingsResult::InvalidType;
                }
                const auto& text = rootJoint.AsString();
                const Container::Span<const Container::String::value_type> source{text.data(), text.size()};
                const auto measured = Asset::MeasureSkeletalNameEncoding(2, source);
                if (!measured.Succeeded() || measured.ByteCount == 0 || measured.ByteCount > 4096)
                {
                    return SettingsResult::InvalidValue;
                }
                Container::VariableArray<uint8_t> bytes(measured.ByteCount);
                if (!Asset::EncodeSkeletalWireName(2, source, {bytes.data(), bytes.size()}).Succeeded())
                {
                    return SettingsResult::InvalidValue;
                }
                candidate.RootJoint = Container::AnsiString(
                    Container::AnsiStringView(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
                candidate.Keys |= ClipKeyRootJoint;
            }
            const auto loop = clip.FindMember("loop");
            if (loop.IsValid())
            {
                if (loop.IsBoolean())
                {
                    candidate.Loop = loop.AsBool() ? ClipLoopSetting::Loop : ClipLoopSetting::Once;
                }
                else if (loop.IsString())
                {
                    constexpr const char* modes[] = {"auto", "true", "false"};
                    result = ReadEnum(loop, modes, candidate.Loop);
                    if (result != SettingsResult::Success)
                    {
                        return result;
                    }
                }
                else
                {
                    return SettingsResult::InvalidType;
                }
                candidate.Keys |= ClipKeyLoop;
            }
            struct NumberField
            {
                const char* Name;
                uint32_t Key;
                double* Value;
                bool bAllowZero;
            };
            const NumberField numbers[] = {
                {"loop_threshold", ClipKeyLoopThreshold, &candidate.LoopThreshold, true},
                {"authored_fps", ClipKeyAuthoredFps, &candidate.AuthoredFps, false},
                {"source_fps", ClipKeySourceFps, &candidate.SourceFps, false},
                {"time_scale", ClipKeyTimeScale, &candidate.TimeScale, false},
            };
            for (const auto& number : numbers)
            {
                const auto value = clip.FindMember(number.Name);
                if (!value.IsValid())
                {
                    continue;
                }
                result = ReadNumber(value, *number.Value);
                if (result != SettingsResult::Success)
                {
                    return result;
                }
                if (number.bAllowZero ? *number.Value < 0 : *number.Value <= 0)
                {
                    return SettingsResult::InvalidValue;
                }
                candidate.Keys |= number.Key;
            }
            const auto analysis = clip.FindMember("analysis");
            if (analysis.IsValid())
            {
                result = ReadBool(analysis, candidate.bAnalysis);
                if (result != SettingsResult::Success)
                {
                    return result;
                }
                candidate.Keys |= ClipKeyAnalysis;
            }
            constexpr uint32_t fpsKeys = ClipKeyAuthoredFps | ClipKeySourceFps;
            const uint32_t specifiedFps = candidate.Keys & fpsKeys;
            if ((specifiedFps != 0 && specifiedFps != fpsKeys) ||
                (!candidate.bAnalysis && candidate.Keys != ClipKeyAnalysis))
            {
                return SettingsResult::InvalidValue;
            }
            out = std::move(candidate);
            return SettingsResult::Success;
        }
        SettingsResult ReadSkinnedLod(const JsonValue& skinnedLod, SkinnedLodImportSettings& out)
        {
            SkinnedLodImportSettings candidate;
            if (!skinnedLod.IsValid())
            {
                out = std::move(candidate);
                return SettingsResult::Success;
            }
            constexpr const char* fields[] = {"parts", "targets", "thetaRef"};
            auto result = CheckFields(skinnedLod, fields);
            if (result != SettingsResult::Success)
            {
                return result;
            }
            candidate.bRequested = true;
            const auto parts = skinnedLod.FindMember("parts");
            if (parts.IsValid())
            {
                if (!parts.IsArray())
                {
                    return SettingsResult::InvalidType;
                }
                if (parts.GetArraySize() != 2)
                {
                    return SettingsResult::InvalidValue;
                }
                uint32_t limits[2] = {};
                for (size_t index = 0; index < 2; ++index)
                {
                    double value = 0;
                    result = ReadNumber(parts.GetArrayElement(index), value);
                    if (result != SettingsResult::Success)
                    {
                        return result;
                    }
                    if (value < 1 || value > MaximumSkinnedLodParts || std::floor(value) != value)
                    {
                        return SettingsResult::InvalidValue;
                    }
                    limits[index] = static_cast<uint32_t>(value);
                }
                if (limits[0] > limits[1])
                {
                    return SettingsResult::InvalidValue;
                }
                candidate.MinParts = limits[0];
                candidate.MaxParts = limits[1];
            }
            const auto targets = skinnedLod.FindMember("targets");
            if (targets.IsValid())
            {
                if (!targets.IsArray())
                {
                    return SettingsResult::InvalidType;
                }
                if (targets.GetArraySize() == 0 || targets.GetArraySize() > MaximumSkinnedLodTargets)
                {
                    return SettingsResult::InvalidValue;
                }
                candidate.Targets.clear();
                candidate.Targets.reserve(targets.GetArraySize());
                for (size_t index = 0; index < targets.GetArraySize(); ++index)
                {
                    double value = 0;
                    result = ReadNumber(targets.GetArrayElement(index), value);
                    if (result != SettingsResult::Success)
                    {
                        return result;
                    }
                    if (value <= 0 || value > 1 || (index == 0 ? value != 1 : value >= candidate.Targets.back()))
                    {
                        return SettingsResult::InvalidValue;
                    }
                    candidate.Targets.push_back(value);
                }
            }
            const auto thetaRef = skinnedLod.FindMember("thetaRef");
            if (thetaRef.IsValid())
            {
                result = ReadNumber(thetaRef, candidate.ThetaRef);
                if (result != SettingsResult::Success)
                {
                    return result;
                }
                if (candidate.ThetaRef <= 0 || candidate.ThetaRef > 3.141592653589793)
                {
                    return SettingsResult::InvalidValue;
                }
            }
            out = std::move(candidate);
            return SettingsResult::Success;
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

    static SettingsResult ParseGeometrySettings(const JsonValue& root, ImportSettings& outSettings, bool allowMaterial)
    {
        if (!root.IsObject())
        {
            return SettingsResult::InvalidRoot;
        }
        constexpr const char* fields[] = {"version", "meta", "units", "axes", "origin", "mesh",
            "repair", "lod", "material", "collision", "clip", "audio", "skinnedLod", "materials"};
        auto result = CheckFields(root, fields, allowMaterial ? 14 : 13);
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
        for (const char* name : {"repair", "lod", "material", "collision", "clip", "audio", "skinnedLod"})
        {
            if (allowMaterial && (std::strcmp(name, "material") == 0 || std::strcmp(name, "clip") == 0 ||
                std::strcmp(name, "skinnedLod") == 0))
            {
                continue;
            }
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
    SettingsResult ParseSettings(const JsonValue& root, ImportSettings& outSettings)
    {
        return ParseGeometrySettings(root, outSettings, false);
    }
    SettingsResult ParseImportSettingsDocument(const JsonValue& root, ImportSettingsDocument& out)
    {
        ImportSettingsDocument candidate;
        auto status = ParseGeometrySettings(root, candidate.Geometry, true);
        if (status != SettingsResult::Success)
        {
            return status;
        }
        status = ReadClip(root.FindMember("clip"), candidate.Clip);
        if (status != SettingsResult::Success)
        {
            return status;
        }
        status = ReadSkinnedLod(root.FindMember("skinnedLod"), candidate.SkinnedLod);
        if (status != SettingsResult::Success)
        {
            return status;
        }
        const auto material = root.FindMember("material");
        status = ParseAssetMaterialSettings(material, candidate.Profile, candidate.AssetMaterial);
        if (status != SettingsResult::Success)
        {
            return status;
        }
        candidate.bAssetMaterialSpecified = material.IsObject() && material.GetObjectSize() != 0;
        const auto overrides = root.FindMember("materials");
        if (overrides.IsValid())
        {
            if (!overrides.IsArray() || overrides.GetArraySize() > UINT32_MAX)
            {
                return SettingsResult::InvalidType;
            }
            candidate.Materials.reserve(overrides.GetArraySize());
            for (size_t index = 0; index < overrides.GetArraySize(); ++index)
            {
                MaterialOverrideSettings entry;
                status = ParseMaterialOverrideSettings(overrides.GetArrayElement(index), entry);
                if (status != SettingsResult::Success)
                {
                    return status;
                }
                candidate.Materials.push_back(std::move(entry));
            }
        }
        out = std::move(candidate);
        return SettingsResult::Success;
    }
} // namespace NorvesLib::Core::AssetImport
