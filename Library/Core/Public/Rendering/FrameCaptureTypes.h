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
        /**
         * @brief 同じフレームのメインSceneViewの GBuffer の生の値（アルベド・法線・粗さ・深度）も読み出す
         *
         * GBufferAovPass（--capture-aov で足す。統計が有効な構成だけ）が書いた 2 枚を写し、結果は CapturedFrame::Aov に入る。
         */
        bool bIncludeAov = false;
    };

    struct FrameCaptureRequestSnapshot
    {
        uint64_t RequestId = 0;
        FrameCaptureSourceKind SourceKind = FrameCaptureSourceKind::PresentationColor;
        uint32_t MinimumPathTracingSamples = 0;
        bool bIncludeSceneColor = false;
        bool bIncludeAov = false;

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
     * 前半はカメラの CameraToneMapCurve・CameraGradingOverride を写したもの。差し替えが無いカメラは View の既定の
     * トーンマップ・グレーディングで描かれ、前半には入らない（bGradingOverride が false）。
     * 後半（ToneMap・Vignette・bBloom・bFxaa）は、ポストのパスがそのフレームで実際に使った値。
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

        /**
         * @brief そのフレームでトーンマップのパスが実際に使った値（tonemapping.frag の定数と同じ意味）
         *
         * カメラの差し替えと View の既定を解いた後の値。bRecorded が false ならパスが走っていない。
         */
        struct ToneMapRecord
        {
            bool bRecorded = false;
            /** @brief tonemapping.frag の operatorType（4: ACES 2.0 SDR LUT、5: NeutralLinear など） */
            uint32_t OperatorType = 0;
            bool bBypass = false;
            /** @brief 0: View の設定の式、1: カメラの差し替えの式 */
            uint32_t GradingMode = 0;
            float ColorFilter[4] = {1.0f, 1.0f, 1.0f, 1.0f};
            float Contrast = 1.0f;
            float ContrastPivot = 0.18f;
            float Saturation = 1.0f;
            float Brightness = 0.0f;
            float Temperature = 0.0f;
            float LookLutIntensity = 0.0f;
            /** @brief トーンマップの中のビネット（強さ・半径・幅） */
            float Vignette[3] = {0.0f, 0.8f, 0.5f};
            float FilmGrainStrength = 0.0f;
            /** @brief 入力をシーンの色でなく表示変換の照合の色の階段にしたか */
            bool bDisplayRamp = false;
        };
        ToneMapRecord ToneMap;

        /** @brief そのフレームで VignettePass が実際に使った値（vignette.frag の定数と同じ意味） */
        struct VignetteRecord
        {
            bool bRecorded = false;
            bool bEnabled = false;
            float Intensity = 0.0f;
            float Radius = 0.8f;
            float Softness = 0.5f;
            float ChromaticAberrationPixels = 0.0f;
        };
        VignetteRecord Vignette;

        /** @brief ブルーム（レンズダートを含む）と FXAA のパスが有効だったか */
        bool bBloom = false;
        bool bFxaa = false;
    };

    /**
     * @brief GBuffer の生の値（AOV）を描いたカメラ（撮影の AOV の書き出しが、深度の意味と投影を残すための値）
     *
     * 座標はワールド（m、+Y が上）。向きは単位ベクトル。
     */
    struct FrameCaptureAovCamera
    {
        bool bValid = false;
        float Position[3] = {0.0f, 0.0f, 0.0f};
        float Forward[3] = {0.0f, 0.0f, -1.0f};
        float Up[3] = {0.0f, 1.0f, 0.0f};
        float Right[3] = {1.0f, 0.0f, 0.0f};
        /** @brief 縦の画角[度]と縦横比（幅 / 高さ） */
        float VerticalFieldOfViewDegrees = 60.0f;
        float AspectRatio = 16.0f / 9.0f;
        float NearPlane = 0.1f;
        float FarPlane = 1000.0f;
        bool bOrthographic = false;
    };

    struct FrameCaptureSourceSet
    {
        FrameCaptureSource PresentationColor;
        FrameCaptureSource SceneColor;
        FrameCaptureSource BackBuffer;
        FrameCaptureSource GBufferVelocity;
        FrameCaptureSource RTGIDiffuseIndirect;
        FrameCaptureSource RTGIHistoryAge;
        /**
         * @brief GBufferAovPass の出力（RGBA32F）。表面 = アルベド（線形 RGB）+ 粗さ、幾何 = ワールドの法線 + 視線方向の深度 m
         *
         * --capture-aov でパスを足した構成で、そのフレームにパスが書いたときだけ入る。
         */
        FrameCaptureSource AovSurface;
        FrameCaptureSource AovGeometry;
        FrameCaptureAovCamera AovCamera;
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
            AovSurface = FrameCaptureSource{};
            AovGeometry = FrameCaptureSource{};
            AovCamera = FrameCaptureAovCamera{};
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

    /** @brief 読み出した 1 枚の画素（行は上から、RowPitchBytes ごと） */
    struct CapturedImage
    {
        uint32_t Width = 0;
        uint32_t Height = 0;
        RHI::Format Format = RHI::Format::UNKNOWN;
        uint32_t BytesPerPixel = 0;
        uint32_t RowPitchBytes = 0;
        Container::VariableArray<uint8_t> Pixels;
    };

    /** @brief FrameCaptureRequest::bIncludeAov で同じフレームから読み出した GBuffer の生の値 */
    struct CapturedAov
    {
        /** @brief 読み出せたか（要求していない・失敗したときは false で、ほかの値は空） */
        bool bValid = false;
        /** @brief RGBA32F。rgb = アルベド（線形）、a = 粗さ。空の画素は rgb = 0・a = 0 */
        CapturedImage Surface;
        /** @brief RGBA32F。xyz = ワールドの法線（GBuffer の値のまま）、w = 視線方向の深度 m。空の画素は xyz = 0・w = +∞ */
        CapturedImage Geometry;
        FrameCaptureAovCamera Camera;
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
        /** @brief 要求したときだけ入る、同じフレームの GBuffer の生の値 */
        CapturedAov Aov;

        bool IsSuccess() const
        {
            return Status == FrameCaptureResultStatus::Success;
        }
    };

} // namespace NorvesLib::Core::Rendering
