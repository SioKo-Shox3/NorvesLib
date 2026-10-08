#pragma once
#include "Asset/AssetManifest.h"
#include "Container/Span.h"
#include <filesystem>
namespace NorvesLib::Tools::AssetCook::Detail
{
    // 固定Windows PowerShell5.1のv1 texture集約書式。順序を変更しない。
    [[nodiscard]] bool SerializeLegacyTextureManifest(Core::Container::Span<const Core::Asset::AssetCookedReference> references,
        Core::Container::AnsiString& output, Core::Container::AnsiString& error);
    // 新規directory専用。存在する宛先を空directoryも含めて置換しない。
    [[nodiscard]] bool PublishNewTextureAssetSet(const std::filesystem::path& stage,
        const std::filesystem::path& destination, Core::Container::AnsiString& error);
}
