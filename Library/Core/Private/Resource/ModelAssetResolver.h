#pragma once

#include "Asset/AssetResolveResult.h"
#include "Container/String.h"

namespace NorvesLib::Core::Asset
{
    class AssetSystem;
}

namespace NorvesLib::Core::Resource
{
    [[nodiscard]] Asset::AssetResolveResult ResolveCookedModel(
        const Asset::AssetSystem& assetSystem,
        const Container::String& logicalPath);
} // namespace NorvesLib::Core::Resource
