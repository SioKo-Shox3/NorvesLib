#pragma once

#include "RHI/RHITypes.h"
#include "Container/Containers.h"
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    enum class FrameCaptureSourceKind : uint8_t
    {
        PresentationColor,
        SceneColor,
        BackBuffer
    };

    struct FrameCaptureRequest
    {
        FrameCaptureSourceKind SourceKind = FrameCaptureSourceKind::PresentationColor;
    };

    struct FrameCaptureRequestSnapshot
    {
        uint64_t RequestId = 0;
        FrameCaptureSourceKind SourceKind = FrameCaptureSourceKind::PresentationColor;

        bool IsValid() const
        {
            return RequestId != 0;
        }
    };

    enum class FrameCaptureRequestStatus : uint8_t
    {
        Accepted,
        AlreadyPending,
        NotInitialized
    };

    enum class FrameCaptureResultStatus : uint8_t
    {
        Success,
        SourceUnavailable,
        SourceMissingTransferSrc,
        UnsupportedFormat,
        InvalidDimensions,
        ReadbackBufferCreateFailed,
        MapFailed
    };

    struct FrameCaptureRequestResult
    {
        FrameCaptureRequestStatus Status = FrameCaptureRequestStatus::NotInitialized;
        uint64_t RequestId = 0;

        bool IsAccepted() const
        {
            return Status == FrameCaptureRequestStatus::Accepted;
        }
    };

    struct FrameCaptureSource
    {
        RHI::TexturePtr Texture;
        RHI::ResourceState CurrentState = RHI::ResourceState::ShaderResource;
        RHI::ResourceState RestoreState = RHI::ResourceState::ShaderResource;
        uint64_t FrameNumber = 0;
        RHI::PresentationColorSpace ColorSpace = RHI::PresentationColorSpace::Unknown;
        RHI::PresentationTransfer Transfer = RHI::PresentationTransfer::Unknown;
        bool bHardwareSrgbEncode = false;
        bool bShaderSrgbEncode = false;
    };

    struct FrameCaptureSourceSet
    {
        FrameCaptureSource PresentationColor;
        FrameCaptureSource SceneColor;
        FrameCaptureSource BackBuffer;

        const FrameCaptureSource* Find(FrameCaptureSourceKind kind) const
        {
            switch (kind)
            {
            case FrameCaptureSourceKind::PresentationColor:
                return &PresentationColor;
            case FrameCaptureSourceKind::SceneColor:
                return &SceneColor;
            case FrameCaptureSourceKind::BackBuffer:
                return &BackBuffer;
            default:
                return nullptr;
            }
        }

        void Reset()
        {
            PresentationColor = FrameCaptureSource{};
            SceneColor = FrameCaptureSource{};
            BackBuffer = FrameCaptureSource{};
        }
    };

    struct CapturedFrame
    {
        FrameCaptureResultStatus Status = FrameCaptureResultStatus::SourceUnavailable;
        uint64_t RequestId = 0;
        uint64_t FrameNumber = 0;
        uint32_t Width = 0;
        uint32_t Height = 0;
        RHI::Format Format = RHI::Format::UNKNOWN;
        uint32_t BytesPerPixel = 0;
        uint32_t RowPitchBytes = 0;
        RHI::PresentationColorSpace ColorSpace = RHI::PresentationColorSpace::Unknown;
        RHI::PresentationTransfer Transfer = RHI::PresentationTransfer::Unknown;
        bool bHardwareSrgbEncode = false;
        bool bShaderSrgbEncode = false;
        Container::VariableArray<uint8_t> Pixels;

        bool IsSuccess() const
        {
            return Status == FrameCaptureResultStatus::Success;
        }
    };

} // namespace NorvesLib::Core::Rendering
