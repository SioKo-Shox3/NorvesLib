#pragma once

#include "Rendering/FrameCaptureTypes.h"
#include "RHI/RHITypes.h"
#include "Container/Containers.h"

#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    inline constexpr uint32_t FrameCaptureThumbnailHardMaxWidth = 640;
    inline constexpr uint32_t FrameCaptureThumbnailHardMaxHeight = 360;
    inline constexpr size_t FrameCaptureThumbnailDefaultMaxEncodedBytes = 256u * 1024u;

    enum class FrameCaptureThumbnailStatus : uint8_t
    {
        Success,
        CapturedFrameNotReady,
        InvalidOptions,
        InvalidDimensions,
        UnsupportedFormat,
        InvalidPixelData,
        EncodeFailed,
        EncodedBytesTooLarge
    };

    struct FrameCaptureThumbnailOptions
    {
        uint32_t MaxWidth = FrameCaptureThumbnailHardMaxWidth;
        uint32_t MaxHeight = FrameCaptureThumbnailHardMaxHeight;
        // Caps retained PNG output bytes; stb may allocate transient memory while encoding.
        size_t MaxEncodedBytes = FrameCaptureThumbnailDefaultMaxEncodedBytes;
    };

    struct FrameCaptureThumbnailResult
    {
        FrameCaptureThumbnailStatus Status = FrameCaptureThumbnailStatus::CapturedFrameNotReady;
        uint64_t RequestId = 0;
        uint64_t FrameNumber = 0;
        uint32_t Width = 0;
        uint32_t Height = 0;
        RHI::Format SourceFormat = RHI::Format::UNKNOWN;
        Container::VariableArray<uint8_t> PngBytes;

        [[nodiscard]] bool IsSuccess() const
        {
            return Status == FrameCaptureThumbnailStatus::Success;
        }
    };

    [[nodiscard]] FrameCaptureThumbnailResult EncodeCapturedFrameThumbnail(
        const CapturedFrame& frame,
        const FrameCaptureThumbnailOptions& options = {});

    [[nodiscard]] const char* GetFrameCaptureThumbnailMimeType();

} // namespace NorvesLib::Core::Rendering

