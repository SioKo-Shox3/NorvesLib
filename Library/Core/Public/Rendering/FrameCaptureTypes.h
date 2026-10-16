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
        /**
         * @brief 同じフレームのメインSceneViewのシーンの色（ブルーム前の HDR。プリエクスポージャ後の値）も読み出す
         *
         * 結果は CapturedFrame::SceneColor に入る（撮影の HDR の書き出し用。SourceKind が SceneColor のときは使わない）。
         */
        bool bIncludeSceneColor = false;
    };

    struct FrameCaptureRequestSnapshot
    {
        uint64_t RequestId = 0;
        FrameCaptureSourceKind SourceKind = FrameCaptureSourceKind::PresentationColor;
        uint32_t MinimumPathTracingSamples = 0;
        bool bIncludeSceneColor = false;

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

    /** @brief 撮影の結果の状態をログの照合用の名前にする */
    inline const char* GetFrameCaptureResultStatusName(FrameCaptureResultStatus status)
    {
        switch (status)
        {
        case FrameCaptureResultStatus::Success:
            return "Success";
        case FrameCaptureResultStatus::SourceUnavailable:
            return "SourceUnavailable";
        case FrameCaptureResultStatus::SourceMissingTransferSrc:
            return "SourceMissingTransferSrc";
        case FrameCaptureResultStatus::UnsupportedFormat:
            return "UnsupportedFormat";
        case FrameCaptureResultStatus::InvalidDimensions:
            return "InvalidDimensions";
        case FrameCaptureResultStatus::ReadbackBufferCreateFailed:
            return "ReadbackBufferCreateFailed";
        case FrameCaptureResultStatus::MapFailed:
            return "MapFailed";
        }
        return "Unknown";
    }

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

    /**
     * @brief シーンの色を描いたカメラの表示変換の差し替え（撮影の HDR から表示の値を見積もる手掛かり）
     *
     * カメラの CameraToneMapCurve・CameraGradingOverride を写したもの。差し替えが無いカメラは View の既定の
     * トーンマップ・グレーディングで描かれ、ここには入らない（bGradingOverride が false）。
     */
    struct FrameCaptureDisplayInfo
    {
        /** @brief トーンマップの曲線がカメラの差し替えの NeutralLinear か */
        bool bNeutralLinear = false;
        /** @brief カメラのグレーディングの差し替えが有効か（false ならほかの値は使わない） */
        bool bGradingOverride = false;
        float Contrast = 1.0f;
        float ContrastPivot = 0.18f;
        float Saturation = 1.0f;
        float Temperature = 0.0f;
        float VignetteIntensity = 0.0f;
        float VignetteRadius = 0.8f;
        float VignetteSoftness = 0.5f;
        /** @brief 見た目の 3D LUT が掛かるか（グレーディングの後。ここには LUT の中身を写さない） */
        bool bLookLut = false;
        /** @brief 色収差の画素数（0 で掛けない） */
        float ChromaticAberrationPixels = 0.0f;
        /** @brief レンズダートの強さ（0 ならカメラの指定なし） */
        float LensDirtIntensity = 0.0f;
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
        /** @brief SceneColor に掛かっているプリエクスポージャ（割ると放射輝度 cd/m²。0 は不明） */
        float SceneColorPreExposure = 0.0f;
        /** @brief SceneColor を描いたカメラの表示変換の差し替え */
        FrameCaptureDisplayInfo SceneColorDisplay;

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
            SceneColorPreExposure = 0.0f;
            SceneColorDisplay = FrameCaptureDisplayInfo{};
        }
    };

    /** @brief FrameCaptureRequest::bIncludeSceneColor で同じフレームから読み出したシーンの色 */
    struct CapturedSceneColor
    {
        /** @brief 読み出せたか（要求していない・失敗したときは false で、ほかの値は空） */
        bool bValid = false;
        uint32_t Width = 0;
        uint32_t Height = 0;
        RHI::Format Format = RHI::Format::UNKNOWN;
        uint32_t BytesPerPixel = 0;
        uint32_t RowPitchBytes = 0;
        /** @brief 画素に掛かっているプリエクスポージャ（割ると放射輝度 cd/m²） */
        float PreExposure = 0.0f;
        /** @brief 画素を描いたカメラの表示変換の差し替え */
        FrameCaptureDisplayInfo Display;
        Container::VariableArray<uint8_t> Pixels;
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
        /** @brief 要求したときだけ入る、同じフレームのシーンの色（ブルーム前の HDR） */
        CapturedSceneColor SceneColor;

        bool IsSuccess() const
        {
            return Status == FrameCaptureResultStatus::Success;
        }
    };

} // namespace NorvesLib::Core::Rendering
