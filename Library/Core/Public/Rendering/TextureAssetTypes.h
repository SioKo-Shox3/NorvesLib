#pragma once

#include "Container/Containers.h"
#include "Container/PointerTypes.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    struct CookedTextureAsyncPayload;

    enum class TextureAssetFallbackMode : uint8_t
    {
        FailOnCookedFailure,
        DebugAllowLooseFallback
    };

    enum class TextureLoadSource : uint8_t
    {
        LegacyFile,
        LooseStbi,
        CookedNvtex
    };

    enum class PreparedTextureAssetStatus : uint8_t
    {
        InvalidRequest,
        InvalidPath,
        AbsolutePathUnsupported,
        ManifestInvalid,
        ManifestMissingLooseFallback,
        VariantMissingLooseFallback,
        CookedPackageReadFailed,
        CookedPackageParseFailed,
        CookedEntryMissing,
        CookedEntryHashMismatch,
        CookedTextureParseFailed,
        DebugLooseFallback,
        CookedReady
    };

    struct PreparedTextureAsset
    {
        PreparedTextureAssetStatus Status = PreparedTextureAssetStatus::InvalidRequest;
        Container::String RequestPath;
        Container::String ResolvedFallbackPath;
        Container::AnsiString LogicalPath;
        Container::String CacheKey;
        uint64_t Generation = 0;
        TextureAssetFallbackMode FallbackMode = TextureAssetFallbackMode::FailOnCookedFailure;
        TextureLoadSource Source = TextureLoadSource::LegacyFile;
        Container::TSharedPtr<CookedTextureAsyncPayload> Payload;
        Container::String Reason;

        [[nodiscard]] bool HasCookedPayload() const noexcept;
        [[nodiscard]] bool ShouldUseLooseFallback() const noexcept;
        [[nodiscard]] bool Failed() const noexcept;
    };

    struct PreparedCookedTextureMip0RGBA8UNormLinearSplit
    {
        uint32_t Width = 0;
        uint32_t Height = 0;
        Container::VariableArray<uint8_t> R;
        Container::VariableArray<uint8_t> G;
        Container::VariableArray<uint8_t> B;
        Container::VariableArray<uint8_t> A;
    };
}
