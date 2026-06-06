#pragma once

#include "Asset/AssetBlob.h"
#include "Asset/AssetPath.h"
#include "Container/String.h"
#include <cstdint>

namespace NorvesLib::Core::Asset
{
    enum class AssetReadStatus : uint8_t
    {
        Success,
        InvalidRequest,
        InvalidAssetRoot,
        InvalidPath,
        FileNotFound,
        OpenFailed,
        SizeQueryFailed,
        SizeTooLarge,
        ReadFailed
    };

    struct AssetReadRequest
    {
        Container::AnsiString InputPath;
        Container::AnsiString AssetRoot;
        bool bAllowAbsolutePath = true;
    };

    struct AssetReadResult
    {
        AssetReadStatus Status = AssetReadStatus::InvalidRequest;
        AssetPath Path;
        AssetBlob Blob;
        Container::AnsiString Reason;
        size_t BytesRead = 0;
        int64_t FileSize = -1;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == AssetReadStatus::Success && Blob.IsValid();
        }
    };
}
