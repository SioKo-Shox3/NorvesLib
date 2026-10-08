#pragma once
#include "Asset/AssetManifest.h"
#include "Asset/CookedSkinMeshV1.h"
namespace NorvesLib::Tools::AssetCook::Detail
{
    [[nodiscard]] bool InspectCookedRigPayload(Core::Container::AnsiStringView format,
                                               Core::Container::Span<const uint8_t> bytes, uint32_t profile,
                                               Core::Asset::AssetRigSplitMetadata& out,
                                               Core::Container::AnsiString& error);
} // namespace NorvesLib::Tools::AssetCook::Detail
