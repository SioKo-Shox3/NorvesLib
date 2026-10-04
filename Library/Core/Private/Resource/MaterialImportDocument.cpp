#include "Resource/MaterialImportDocument.h"
#include "Asset/CookedSkeletalNameCodec.h"
#include "Text/JsonDocument.h"
#include <utility>

namespace NorvesLib::Core::AssetImport
{
    namespace
    {
        SettingsResult ReadUniqueField(const JsonValue& object, const char* name, JsonValue& out)
        {
            for (size_t index = 0; index < object.GetObjectSize(); ++index)
            {
                const auto& key = object.GetMemberName(index);
                for (const auto unit : key)
                {
                    if (unit == 0)
                    {
                        return SettingsResult::InvalidValue;
                    }
                }
                size_t unit = 0;
                while (unit < key.size() && name[unit] != 0 && key[unit] == name[unit])
                {
                    ++unit;
                }
                if (unit == key.size() && name[unit] == 0)
                {
                    if (out.IsValid())
                    {
                        return SettingsResult::DuplicateField;
                    }
                    out = object.GetMemberValue(index);
                }
            }
            return SettingsResult::Success;
        }
    }
    SettingsResult ReadSourceMaterialCatalog(const JsonValue& root, SourceMaterialCatalog& out)
    {
        if (!root.IsObject())
        {
            return SettingsResult::InvalidRoot;
        }
        JsonValue materials;
        auto status = ReadUniqueField(root, "materials", materials);
        if (status != SettingsResult::Success)
        {
            return status;
        }
        SourceMaterialCatalog candidate;
        if (materials.IsValid())
        {
            if (!materials.IsArray() || materials.GetArraySize() > UINT32_MAX)
            {
                return SettingsResult::InvalidType;
            }
            candidate.reserve(materials.GetArraySize());
            for (size_t index = 0; index < materials.GetArraySize(); ++index)
            {
                const auto material = materials.GetArrayElement(index);
                if (!material.IsObject())
                {
                    return SettingsResult::InvalidType;
                }
                JsonValue name;
                status = ReadUniqueField(material, "name", name);
                if (status != SettingsResult::Success)
                {
                    return status;
                }
                SourceMaterialIdentity identity;
                identity.SourceIndex = static_cast<uint32_t>(index);
                if (name.IsValid())
                {
                    if (!name.IsString())
                    {
                        return SettingsResult::InvalidType;
                    }
                    const auto& text = name.AsString();
                    const Container::Span<const Container::String::value_type> source{text.data(), text.size()};
                    const auto measure = Asset::MeasureSkeletalNameEncoding(2, source);
                    if (!measure.Succeeded())
                    {
                        return SettingsResult::InvalidValue;
                    }
                    identity.Name.resize(measure.ByteCount);
                    if (!Asset::EncodeSkeletalWireName(2, source, {identity.Name.data(), identity.Name.size()}).Succeeded())
                    {
                        return SettingsResult::InvalidValue;
                    }
                }
                candidate.push_back(std::move(identity));
            }
        }
        out = std::move(candidate);
        return SettingsResult::Success;
    }
    MaterialImportPlanOutcome ResolveMaterialImportPlan(const SourceMaterialCatalog& catalog,
        const ImportSettingsDocument& document, EmissiveScale assetSet, ResolvedMaterialImportPlan& out)
    {
        MaterialImportPlanOutcome result;
        if (ValidateSettings(document.Geometry) != SettingsResult::Success)
        {
            result.Status = MaterialImportPlanStatus::InvalidGeometry;
            return result;
        }
        ResolvedMaterialImportPlan candidate;
        candidate.Geometry = document.Geometry;
        result.Settings = ResolveMaterialSettings(document.Profile, document.AssetMaterial, {}, assetSet, candidate.ImplicitDefault);
        if (result.Settings != MaterialSettingsStatus::Success)
        {
            result.Status = MaterialImportPlanStatus::SettingsFailed;
            return result;
        }
        size_t scratchCount = 0;
        if (!GetMaterialSelectionScratchCount(catalog.size(), document.Materials.size(), scratchCount))
        {
            result.Status = MaterialImportPlanStatus::InvalidConfiguration;
            return result;
        }
        Container::VariableArray<MaterialIdentityView> identities;
        identities.reserve(catalog.size());
        for (const auto& source : catalog)
        {
            identities.push_back({source.SourceIndex, {source.Name.data(), source.Name.size()}});
        }
        Container::VariableArray<MaterialSelectorView> selectors;
        selectors.reserve(document.Materials.size());
        for (const auto& setting : document.Materials)
        {
            selectors.push_back({setting.Kind, setting.SourceIndex, {setting.Name.data(), setting.Name.size()}, setting.bExpectedNamePresent});
        }
        Container::VariableArray<uint32_t> scratch(scratchCount), rows(selectors.size());
        result.Selection = ResolveMaterialSelection(MaterialIdentityDomain::SourceMaterial,
            {identities.data(), identities.size()}, {selectors.data(), selectors.size()},
            {scratch.data(), scratch.size()}, {rows.data(), rows.size()});
        if (!result.Selection.Succeeded())
        {
            result.Status = MaterialImportPlanStatus::SelectionFailed;
            return result;
        }
        candidate.DuplicateNameGroups = result.Selection.DuplicateNameGroups;
        if (candidate.DuplicateNameGroups != 0)
        {
            candidate.FirstDuplicateMaterialIndex = catalog[result.Selection.FirstDuplicateRow].SourceIndex;
            candidate.SecondDuplicateMaterialIndex = catalog[result.Selection.SecondDuplicateRow].SourceIndex;
        }
        candidate.Materials.resize(catalog.size());
        for (const auto& source : catalog)
        {
            auto& entry = candidate.Materials[source.SourceIndex];
            entry.SourceIndex = source.SourceIndex;
            entry.Name = source.Name;
            entry.Settings = candidate.ImplicitDefault;
        }
        for (size_t index = 0; index < document.Materials.size(); ++index)
        {
            result.EntryIndex = index;
            const auto& setting = document.Materials[index];
            auto& entry = candidate.Materials[catalog[rows[index]].SourceIndex];
            result.Settings = ResolveMaterialSettings(document.Profile, document.AssetMaterial,
                setting.Settings, assetSet, entry.Settings);
            if (result.Settings != MaterialSettingsStatus::Success)
            {
                result.Status = MaterialImportPlanStatus::SettingsFailed;
                return result;
            }
            if (setting.bSurfacePresent)
            {
                if (setting.SurfaceName.empty() || !Asset::MeasureSkeletalNameDecoding<char>(2,
                        {setting.SurfaceName.data(), setting.SurfaceName.size()}).Succeeded())
                {
                    result.Status = MaterialImportPlanStatus::InvalidConfiguration;
                    return result;
                }
                entry.bSurfacePresent = true;
                entry.SurfaceName = setting.SurfaceName;
            }
            else if (!setting.SurfaceName.empty())
            {
                result.Status = MaterialImportPlanStatus::InvalidConfiguration;
                return result;
            }
        }
        result.EntryIndex = SIZE_MAX;
        out = std::move(candidate);
        return result;
    }
}
