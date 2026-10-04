#pragma once
#include "Resource/ImportSettings.h"
#include "Resource/MaterialImportSettings.h"
#include "Resource/MaterialSelection.h"
#include "Container/VariableArray.h"

namespace NorvesLib::Core::AssetImport
{
    using MaterialNameBytes = Container::VariableArray<uint8_t>;
    struct MaterialOverrideSettings
    {
        MaterialSelectorKind Kind = MaterialSelectorKind::UniqueName;
        uint32_t SourceIndex = 0;
        MaterialNameBytes Name;
        bool bExpectedNamePresent = false;
        MaterialSettingsLayer Settings;
        bool bSurfacePresent = false;
        MaterialNameBytes SurfaceName;
    };
    struct ImportSettingsDocument
    {
        ImportSettings Geometry;
        MaterialSourceProfile Profile = MaterialSourceProfile::Source;
        MaterialSettingsLayer AssetMaterial;
        bool bAssetMaterialSpecified = false;
        Container::VariableArray<MaterialOverrideSettings> Materials;
    };
    struct SourceMaterialIdentity
    {
        uint32_t SourceIndex = 0;
        MaterialNameBytes Name;
    };
    using SourceMaterialCatalog = Container::VariableArray<SourceMaterialIdentity>;
    struct ResolvedMaterialImportEntry
    {
        uint32_t SourceIndex = 0;
        MaterialNameBytes Name;
        ResolvedMaterialSettings Settings;
        bool bSurfacePresent = false;
        MaterialNameBytes SurfaceName;
    };
    struct ResolvedMaterialImportPlan
    {
        ImportSettings Geometry;
        // 元材質を省略したprimitive用。catalogへ仮想material0を追加しない。
        ResolvedMaterialSettings ImplicitDefault;
        // 元index順に所有する。JSON/入力catalogの寿命に依存しない。
        Container::VariableArray<ResolvedMaterialImportEntry> Materials;
        uint32_t DuplicateNameGroups = 0;
        uint32_t FirstDuplicateMaterialIndex = UINT32_MAX;
        uint32_t SecondDuplicateMaterialIndex = UINT32_MAX;
    };
    enum class MaterialImportPlanStatus : uint8_t
    {
        Success, InvalidGeometry, InvalidConfiguration, SelectionFailed, SettingsFailed
    };
    struct MaterialImportPlanOutcome
    {
        MaterialImportPlanStatus Status = MaterialImportPlanStatus::Success;
        MaterialSelectionResult Selection;
        MaterialSettingsStatus Settings = MaterialSettingsStatus::Success;
        size_t EntryIndex = SIZE_MAX;
        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == MaterialImportPlanStatus::Success;
        }
    };
    // JSON/値所有planだけのAPI。未対応のcookへ設定を捨てて渡してはならない。
    // すべて成功時だけoutを置換する。SurfaceNameの永続化/Collider適用はGR81で接続する。
    [[nodiscard]] SettingsResult ParseImportSettingsDocument(const JsonValue& root, ImportSettingsDocument& out);
    [[nodiscard]] SettingsResult ParseMaterialOverrideSettings(const JsonValue& value, MaterialOverrideSettings& out);
    [[nodiscard]] SettingsResult ReadSourceMaterialCatalog(const JsonValue& root, SourceMaterialCatalog& out);
    [[nodiscard]] MaterialImportPlanOutcome ResolveMaterialImportPlan(const SourceMaterialCatalog& catalog,
        const ImportSettingsDocument& document, EmissiveScale assetSet, ResolvedMaterialImportPlan& out);
}
