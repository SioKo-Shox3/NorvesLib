#pragma once
// 明示v1 library cook入口。従来CLI/model形式の選択は変更しない。
#include "Asset/CookedClipBankV1.h"
#include "Resource/SkeletalGltfDecode.h"
namespace NorvesLib::Tools::AssetCook
{
    struct RigClipBankCookResult
    {
        Core::Container::VariableArray<uint8_t> Bytes;
        Core::Skeletal::RigV1Report Report;
    };
    [[nodiscard]] bool CookRigClipBankV1NativePath(
        Core::Container::Span<const uint8_t> source, const std::filesystem::path& sourcePath,
        Core::Container::AnsiStringView format, RigClipBankCookResult& out, Core::Skeletal::RigV1Report& report,
        const Core::Skeletal::RigV1Limits& limits = {},
        const Core::AssetImport::LoadedImportSettings* settings = nullptr,
        const Core::Skeletal::SkeletalGltfDecodeOptions* options = nullptr,
        Core::Skeletal::RigImportProfile profile = Core::Skeletal::RigImportProfile::DirectTrs128,
        const Core::Skeletal::RigClipSourceSelection* clipSource = nullptr,
        const Core::Skeletal::RigClipAnalysisOptions* analysisOptions = nullptr);
} // namespace NorvesLib::Tools::AssetCook
