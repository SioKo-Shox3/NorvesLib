#pragma once

#include "Asset/CookedMeshFormat.h"
#include "Rendering/RenderResourceContexts.h"
#include "Rendering/RenderTypes.h"
#include "ModelStaging.h"
#include "Container/PointerTypes.h"

namespace NorvesLib::Core::Asset
{
    class AssetSystem;
}

namespace NorvesLib::Core::Resource
{
    struct CookedModelLoadPlan
    {
        Container::TSharedPtr<const Asset::AssetSystem> AssetSystem;
        Container::String RequestPath;
        Container::AnsiString NormalizedLogicalPath;
        Container::String CacheKey;
        uint64_t Generation = 0;
    };

    struct CookedModelCpuLoadResult
    {
        ModelStaging::ModelStagingData Staging;
        Container::String CacheKey;
        uint64_t Generation = 0;
        bool bSuccess = false;
    };

    [[nodiscard]] bool BuildModelStagingFromCookedMesh(
        const Asset::CookedMeshData& cooked,
        const Container::String& debugName,
        const Container::String& resolvedPath,
        ModelStaging::ModelStagingData& outStaging);

    [[nodiscard]] Rendering::ModelHandle LoadCookedModel(
        const Asset::AssetSystem& assetSystem,
        const Container::String& logicalPath,
        Rendering::ModelLoadResourceContext resources);

    [[nodiscard]] bool LoadCookedModelForWorker(const CookedModelLoadPlan& plan,
                                                uint32_t requestId,
                                                CookedModelCpuLoadResult& outResult);
} // namespace NorvesLib::Core::Resource
