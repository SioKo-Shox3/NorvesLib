#include "Resource/ModelAssetResolver.h"

#include "Asset/AssetSystem.h"
#include "Container/StringView.h"

namespace NorvesLib::Core::Resource
{
    Asset::AssetResolveResult ResolveCookedModel(
        const Asset::AssetSystem& assetSystem,
        const Container::String& logicalPath)
    {
        return assetSystem.ResolveAsset(
            Container::AnsiStringView(logicalPath.data(), logicalPath.size()),
            Asset::AssetKind::Model,
            Asset::AssetManifest::DefaultVariant,
            Asset::AssetFallbackMode::FailOnCookedFailure);
    }
} // namespace NorvesLib::Core::Resource
