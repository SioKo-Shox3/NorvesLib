#pragma once

#include "Rendering/RenderTypes.h"
#include "Rendering/GpuResourceTypes.h"
#include "Rendering/TextureAssetTypes.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include "Thread/Task.h"
#include "Delegate/Delegate.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    struct TextureAsyncResult
    {
        Container::String Path;
        Container::String ResolvedPath;
        Container::String CacheKey;
        Container::AnsiString LogicalPath;
        TextureCreateInfo CreateInfo;
        Container::VariableArray<uint8_t> PixelData;
        Container::TSharedPtr<CookedTextureAsyncPayload> CookedTexture;
        TextureLoadSource Source = TextureLoadSource::LegacyFile;
        TextureAssetFallbackMode FallbackMode = TextureAssetFallbackMode::FailOnCookedFailure;
        uint64_t AssetGeneration = 0;
        bool bSuccess = false;
    };

    struct TextureAsyncRequest
    {
        uint32_t RequestId = 0;
        Container::String Path;
        Container::String CacheKey;
        Thread::TaskPtr Task;
        TextureAsyncResult Result;
        Container::VariableArray<NorvesLib::Core::Delegate<void, TextureHandle>> Callbacks;
    };
}
