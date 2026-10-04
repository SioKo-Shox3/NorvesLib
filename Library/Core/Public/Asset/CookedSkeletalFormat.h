#pragma once

#include "Asset/AssetBlob.h"
#include "Asset/CookedSkeletalWireFormat.h"
#include "Resource/SkeletalGltfData.h"

#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core::Asset
{
    enum class CookedSkeletalParseStatus : uint8_t
    {
        Success,
        InvalidBlob,
        EmptyBlob,
        HeaderTooSmall,
        BadMagic,
        UnsupportedVersion,
        InvalidHeader,
        FileSizeMismatch,
        SectionOutOfRange,
        PayloadHashMismatch,
        InvalidRecord
    };

    struct CookedSkeletalData
    {
        AssetBlob SourceBlob;
        uint64_t PayloadHash = 0;
        Skeletal::SkeletalGltfData Skeletal;
    };

    struct CookedSkeletalParseResult
    {
        CookedSkeletalParseStatus Status = CookedSkeletalParseStatus::InvalidBlob;
        CookedSkeletalData Data;

        [[nodiscard]] bool Succeeded() const
        {
            return Status == CookedSkeletalParseStatus::Success;
        }
    };

    [[nodiscard]] CookedSkeletalParseResult ParseCookedSkeletal(const AssetBlob& blob);
} // namespace NorvesLib::Core::Asset
