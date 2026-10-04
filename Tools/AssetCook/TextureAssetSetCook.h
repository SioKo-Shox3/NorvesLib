#pragma once
#include "Container/String.h"
#include <filesystem>
namespace NorvesLib::Tools::AssetCook
{
    struct TextureAssetSetCookRequest
    {
        std::filesystem::path SpecPath, SourceRoot, RuntimeRoot, ManifestPath;
    };
    // v1 textureの全件cookを新規rootへ公開する。既存root/増分はこの境界では拒否する。
    [[nodiscard]] bool CookTextureAssetSet(const TextureAssetSetCookRequest& request, Core::Container::AnsiString& error);
    // --asset-setを含む場合だけ処理し、呼出元に終了codeを返す。
    [[nodiscard]] bool RunTextureAssetSetCommand(int argc, const char* const* argv, int& exitCode);
}
