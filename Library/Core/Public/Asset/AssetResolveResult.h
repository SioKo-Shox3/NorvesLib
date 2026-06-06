#pragma once

#include "Asset/AssetBlob.h"
#include "Asset/AssetManifest.h"
#include "Asset/AssetPath.h"
#include "Asset/AssetReadRequest.h"
#include "Container/String.h"
#include "FileStream/Package.h"

#include <cstdint>

namespace NorvesLib::Core::Asset
{
    enum class AssetResolveStatus : uint8_t
    {
        SuccessCooked,
        SuccessLoose,
        InvalidRequest,
        InvalidManifest,
        LooseReadFailed,
        CookedPackageReadFailed,
        CookedPackageParseFailed,
        CookedEntryMissing,
        CookedEntryHashMismatch
    };

    enum class AssetResolveSource : uint8_t
    {
        None,
        Cooked,
        Loose,
        DebugLooseFallback
    };

    struct AssetResolveRequest
    {
        Container::AnsiString LogicalPath;
        AssetKind Kind = AssetKind::Unknown;
        Container::AnsiString Variant = AssetManifest::DefaultVariant;
        AssetFallbackMode FallbackMode = AssetFallbackMode::FailOnCookedFailure;
    };

    struct AssetResolveResult
    {
        AssetResolveStatus Status = AssetResolveStatus::InvalidRequest;
        AssetResolveSource Source = AssetResolveSource::None;
        Container::AnsiString NormalizedLogicalPath;
        AssetBlob Blob;
        AssetPath LoosePath;
        AssetCookedReference CookedReference;
        AssetFallbackDecision FallbackDecision;
        AssetManifestResolveStatus ManifestStatus = AssetManifestResolveStatus::InvalidRequest;
        AssetReadStatus LooseReadStatus = AssetReadStatus::InvalidRequest;
        AssetReadStatus PackageReadStatus = AssetReadStatus::InvalidRequest;
        ::NorvesLib::FileStream::PackageEntry Entry;
        Container::AnsiString Reason;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return (Status == AssetResolveStatus::SuccessCooked ||
                    Status == AssetResolveStatus::SuccessLoose) &&
                   Blob.IsValid();
        }

        [[nodiscard]] bool UsedCooked() const noexcept
        {
            return Succeeded() && Source == AssetResolveSource::Cooked;
        }

        [[nodiscard]] bool UsedLoose() const noexcept
        {
            return Succeeded() &&
                   (Source == AssetResolveSource::Loose ||
                    Source == AssetResolveSource::DebugLooseFallback);
        }

        [[nodiscard]] bool RequiresExplicitLog() const noexcept
        {
            return FallbackDecision.bRequiresExplicitLog;
        }
    };
}
