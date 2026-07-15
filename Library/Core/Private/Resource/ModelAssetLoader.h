#pragma once

#include "Asset/CookedMeshFormat.h"
#include "Rendering/RenderResourceContexts.h"
#include "Rendering/RenderTypes.h"
#include "ModelStaging.h"

namespace NorvesLib::Core::Asset
{
    class AssetSystem;
}

namespace NorvesLib::Core::Resource
{
    [[nodiscard]] bool BuildModelStagingFromCookedMesh(
        const Asset::CookedMeshData& cooked,
        const Container::String& debugName,
        const Container::String& resolvedPath,
        ModelStaging::ModelStagingData& outStaging);

    [[nodiscard]] Rendering::ModelHandle LoadCookedModel(
        const Asset::AssetSystem& assetSystem,
        const Container::String& logicalPath,
        Rendering::ModelLoadResourceContext resources);
} // namespace NorvesLib::Core::Resource
