#pragma once
#include "SingleAssetCook.h"
#include "RigSplitCook.h"
namespace NorvesLib::Tools::AssetCook::Detail
{
    [[nodiscard]] bool ValidateClipMaterialSettings(Core::Container::Span<const uint8_t> source,
                                                    const Core::AssetImport::LoadedImportSettingsDocument& document,
                                                    Core::AssetImport::EmissiveScale assetSet,
                                                    Core::Container::AnsiString& error);
    [[nodiscard]] bool IsRigSingleFormat(Core::Container::AnsiStringView format);
    [[nodiscard]] bool NormalizeRigSingleRequest(const SingleAssetCookRequest&, SingleAssetCookRequest&,
                                                 Core::Container::AnsiString& error);
    [[nodiscard]] bool BuildRigSingleOutputs(const SingleAssetCookRequest&, Core::Container::Span<const uint8_t>,
                                             bool bInventoryOnly,
                                             Core::Container::VariableArray<RigSplitCookEntry>& out,
                                             Core::Container::AnsiString& error);
    [[nodiscard]] bool CookRigSingleAsset(const SingleAssetCookRequest&, Core::Container::AnsiString& error);
} // namespace NorvesLib::Tools::AssetCook::Detail
