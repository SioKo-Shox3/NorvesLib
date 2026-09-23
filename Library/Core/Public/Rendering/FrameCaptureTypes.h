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
        BackBuffer,
        GBufferVelocity,
        RTGIDiffuseIndirect, ///< R6 RTGIのデノイズ後の間接光（検証用）
        RTGIHistoryAge       ///< R6 RTGIの書き込み済み履歴age（検証用）
    };

    struct FrameCaptureRequest
    {
        FrameCaptureSourceKind SourceKind = FrameCaptureSourceKind::PresentationColor;
        /**
         * @brief メインSceneViewのパストレーサーがこの試料数を累積するまで取得を待つ（0なら待たない）。
         *
         * 条件を満たさないフレームでは要求を保留したまま次のフレームへ回す。
         */
        uint32_t MinimumPathTracingSamples = 0;
    };

    struct FrameCaptureRequestSnapshot
    {
        uint64_t RequestId = 0;
        FrameCaptureSourceKind SourceKind = FrameCaptureSourceKind::PresentationColor;
        uint32_t MinimumPathTracingSamples = 0;

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
        FrameCaptureSource GBufferVelocity;
        FrameCaptureSource RTGIDiffuseIndirect;
        FrameCaptureSource RTGIHistoryAge;
        /** @brief このフレームにメインSceneViewのパストレーサーが累積した試料数（ラスタなら0） */
        uint32_t PathTracingSampleCount = 0;

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
            case FrameCaptureSourceKind::GBufferVelocity:
                return &GBufferVelocity;
            case FrameCaptureSourceKind::RTGIDiffuseIndirect:
                return &RTGIDiffuseIndirect;
            case FrameCaptureSourceKind::RTGIHistoryAge:
                return &RTGIHistoryAge;
            default:
                return nullptr;
            }
        }

        void Reset()
        {
            PresentationColor = FrameCaptureSource{};
            SceneColor = FrameCaptureSource{};
            BackBuffer = FrameCaptureSource{};
            GBufferVelocity = FrameCaptureSource{};
            RTGIDiffuseIndirect = FrameCaptureSource{};
            RTGIHistoryAge = FrameCaptureSource{};
            PathTracingSampleCount = 0;
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
        /** @brief 取得したフレームでパストレーサーが累積していた試料数（ラスタなら0） */
        uint32_t PathTracingSampleCount = 0;
        Container::VariableArray<uint8_t> Pixels;

        bool IsSuccess() const
        {
            return Status == FrameCaptureResultStatus::Success;
        }
    };

} // namespace NorvesLib::Core::Rendering
