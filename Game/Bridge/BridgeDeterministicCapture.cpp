#include "BridgeDeterministicCapture.h"

#include "Engine/Engine.h"
#include "Rendering/FrameCaptureThumbnailEncoder.h"

#include <utility>

namespace Game::Bridge
{
    namespace
    {
        using namespace NorvesLib::Core;
        using namespace NorvesLib::Core::Rendering;

        constexpr uint64_t FnvOffsetBasis = 14695981039346656037ull;
        constexpr uint64_t FnvPrime = 1099511628211ull;

        uint64_t HashBytes(uint64_t hash, const uint8_t* bytes, size_t size)
        {
            for (size_t index = 0; index < size; ++index)
            {
                hash = (hash ^ bytes[index]) * FnvPrime;
            }
            return hash;
        }

        bool IsCurrentEngine(void* context)
        {
            return context != nullptr && context == Engine::GEngine;
        }

        bool EngineDeterministicEnabled(void* context)
        {
            return IsCurrentEngine(context) && Engine::GEngine->GetDeterministicCapture().IsEnabled();
        }

        bool EngineSceneReady(void* context)
        {
            return IsCurrentEngine(context) && Engine::GEngine->GetRenderWorld().IsInitialized()
                && Engine::GEngine->GetDeterministicCapture().IsSceneReady()
                && !Engine::GEngine->GetRenderWorld().HasPendingAsyncAssets();
        }

        void EngineBeginEpoch(void* context)
        {
            if (IsCurrentEngine(context))
            {
                Engine::GEngine->GetDeterministicCapture().BeginEpoch();
                auto& renderWorld = Engine::GEngine->GetRenderWorld();
                renderWorld.SetDeterministicCapture(true);
                renderWorld.BeginDeterministicEpoch();
            }
        }

        uint64_t EngineEpochFrames(void* context)
        {
            return IsCurrentEngine(context) ? Engine::GEngine->GetDeterministicCapture().GetEpochFrames() : 0;
        }

        uint64_t EngineNextFrameNumber(void* context)
        {
            // EndFrame の提出数は GT が所有し、RT の完了時刻や診断の有効・無効に依存しない。
            return IsCurrentEngine(context) ? Engine::GEngine->GetRenderWorld().GetStats().FrameNumber : 0;
        }

        FrameCaptureRequestResult EngineRequestFrameCapture(void* context)
        {
            if (!IsCurrentEngine(context))
            {
                return {};
            }
            FrameCaptureRequest request;
            request.SourceKind = FrameCaptureSourceKind::PresentationColor;
            return Engine::GEngine->GetRenderWorld().RequestFrameCapture(request);
        }

        bool EngineTryConsumeCapturedFrame(void* context, CapturedFrame& frame)
        {
            return IsCurrentEngine(context) && Engine::GEngine->GetRenderWorld().TryConsumeCapturedFrame(frame);
        }

        bool IsValidSource(const BridgeDeterministicFrameSource& source)
        {
            return source.IsDeterministicEnabled != nullptr && source.IsSceneReady != nullptr
                && source.BeginEpoch != nullptr && source.GetEpochFrames != nullptr
                && source.RequestFrameCapture != nullptr && source.TryConsumeCapturedFrame != nullptr
                && source.GetNextFrameNumber != nullptr;
        }

        const char* CaptureFailureCode(FrameCaptureResultStatus status)
        {
            switch (status)
            {
            case FrameCaptureResultStatus::UnsupportedFormat:
                return "unsupported_format";
            case FrameCaptureResultStatus::InvalidDimensions:
                return "invalid_dimensions";
            case FrameCaptureResultStatus::SourceUnavailable:
                return "capture_source_unavailable";
            case FrameCaptureResultStatus::SourceMissingTransferSrc:
                return "capture_source_missing_transfer_src";
            case FrameCaptureResultStatus::ReadbackBufferCreateFailed:
                return "readback_buffer_create_failed";
            case FrameCaptureResultStatus::MapFailed:
                return "readback_map_failed";
            default:
                return "capture_failed";
            }
        }

        const char* EncodeFailureCode(FrameCaptureThumbnailStatus status)
        {
            switch (status)
            {
            case FrameCaptureThumbnailStatus::UnsupportedFormat:
                return "unsupported_format";
            case FrameCaptureThumbnailStatus::InvalidDimensions:
                return "invalid_dimensions";
            case FrameCaptureThumbnailStatus::InvalidPixelData:
                return "invalid_pixel_data";
            case FrameCaptureThumbnailStatus::EncodedBytesTooLarge:
                return "png_too_large";
            default:
                return "png_encode_failed";
            }
        }
    } // namespace

    NorvesLib::Core::Container::String ValidateBridgeDeterministicCaptureRequest(
        const BridgeDeterministicCaptureRequest& request)
    {
        if (request.SettleFrames < 1 || request.SettleFrames > 600)
        {
            return "invalid_settle_frames";
        }
        if (request.MaxWidth < 1 || request.MaxWidth > BridgeDeterministicCaptureMaxWidth
            || request.MaxHeight < 1 || request.MaxHeight > BridgeDeterministicCaptureMaxHeight)
        {
            return "invalid_size";
        }
        return {};
    }

    BridgeDeterministicFrameSource MakeEngineDeterministicFrameSource()
    {
        BridgeDeterministicFrameSource source;
        source.Context = NorvesLib::Core::Engine::GEngine;
        source.IsDeterministicEnabled = EngineDeterministicEnabled;
        source.IsSceneReady = EngineSceneReady;
        source.BeginEpoch = EngineBeginEpoch;
        source.GetEpochFrames = EngineEpochFrames;
        source.RequestFrameCapture = EngineRequestFrameCapture;
        source.TryConsumeCapturedFrame = EngineTryConsumeCapturedFrame;
        source.GetNextFrameNumber = EngineNextFrameNumber;
        return source;
    }

    NorvesLib::Core::Container::String BridgeDeterministicCaptureSession::Begin(
        const BridgeDeterministicCaptureRequest& request,
        const BridgeDeterministicFrameSource& source)
    {
        if (IsInProgress())
        {
            return "capture_in_progress";
        }
        m_Result = {};
        const auto validationCode = ValidateBridgeDeterministicCaptureRequest(request);
        if (!validationCode.empty())
        {
            Fail(validationCode.c_str());
            return m_Result.ErrorCode;
        }
        if (!IsValidSource(source))
        {
            Fail("invalid_frame_source");
            return m_Result.ErrorCode;
        }
        if (!source.IsDeterministicEnabled(source.Context))
        {
            Fail("deterministic_capture_disabled");
            return m_Result.ErrorCode;
        }
        m_Request = request;
        m_Source = source;
        m_WaitFrames = 0;
        m_RequestId = 0;
        m_EpochFirstFrameNumber = 0;
        m_LastEpochFrames = 0;
        m_RequestedFrameNumber = 0;
        m_Result.Status = BridgeDeterministicCaptureStatus::WaitingForScene;
        BeginEpochIfReady();
        return {};
    }

    bool BridgeDeterministicCaptureSession::IsInProgress() const
    {
        return m_Result.Status == BridgeDeterministicCaptureStatus::WaitingForScene
            || m_Result.Status == BridgeDeterministicCaptureStatus::Settling
            || m_Result.Status == BridgeDeterministicCaptureStatus::WaitingForCapture;
    }

    void BridgeDeterministicCaptureSession::Fail(const char* code)
    {
        m_Result.Status = BridgeDeterministicCaptureStatus::Failed;
        m_Result.ErrorCode = code;
    }

    void BridgeDeterministicCaptureSession::BeginEpochIfReady()
    {
        if (m_Source.IsSceneReady(m_Source.Context))
        {
            m_Source.BeginEpoch(m_Source.Context);
            m_EpochFirstFrameNumber = m_Source.GetNextFrameNumber(m_Source.Context);
            m_LastEpochFrames = 0;
            m_Result.Status = BridgeDeterministicCaptureStatus::Settling;
        }
    }

    BridgeDeterministicCaptureStatus BridgeDeterministicCaptureSession::Tick()
    {
        using namespace NorvesLib::Core::Rendering;
        if (!IsInProgress())
        {
            return m_Result.Status;
        }
        if (++m_WaitFrames > m_Request.SettleFrames + 240u)
        {
            Fail("capture_timeout");
            return m_Result.Status;
        }
        if (!m_Source.IsDeterministicEnabled(m_Source.Context))
        {
            Fail("deterministic_capture_disabled");
            return m_Result.Status;
        }
        if (m_Result.Status == BridgeDeterministicCaptureStatus::WaitingForScene)
        {
            BeginEpochIfReady();
            return m_Result.Status;
        }
        if (!m_Source.IsSceneReady(m_Source.Context))
        {
            // 読み込みが再開した状態では、異なるシーンを同じ撮影として成功させない。
            Fail("scene_not_ready");
            return m_Result.Status;
        }
        const uint64_t epochFrames = m_Source.GetEpochFrames(m_Source.Context);
        const uint64_t nextFrameNumber = m_Source.GetNextFrameNumber(m_Source.Context);
        if (epochFrames < m_LastEpochFrames || nextFrameNumber < m_EpochFirstFrameNumber
            || nextFrameNumber - m_EpochFirstFrameNumber != epochFrames + 1u)
        {
            Fail("epoch_changed");
            return m_Result.Status;
        }
        m_LastEpochFrames = epochFrames;

        if (m_Result.Status == BridgeDeterministicCaptureStatus::Settling)
        {
            if (epochFrames < m_Request.SettleFrames)
            {
                return m_Result.Status;
            }
            if (epochFrames > m_Request.SettleFrames)
            {
                Fail("settle_frame_missed");
                return m_Result.Status;
            }
            const FrameCaptureRequestResult requestResult = m_Source.RequestFrameCapture(m_Source.Context);
            if (!requestResult.IsAccepted())
            {
                Fail(requestResult.Status == FrameCaptureRequestStatus::AlreadyPending
                    ? "capture_already_pending" : "capture_not_initialized");
                return m_Result.Status;
            }
            if (requestResult.RequestId == 0)
            {
                Fail("invalid_capture_request_id");
                return m_Result.Status;
            }
            m_RequestId = requestResult.RequestId;
            m_RequestedFrameNumber = nextFrameNumber;
            m_Result.Status = BridgeDeterministicCaptureStatus::WaitingForCapture;
        }

        CapturedFrame frame;
        if (m_Source.TryConsumeCapturedFrame(m_Source.Context, frame))
        {
            // 過去の要求の結果は捨て、今回の要求 ID の結果だけで完了する。
            if (frame.RequestId != m_RequestId)
            {
                return m_Result.Status;
            }
            if (!frame.IsSuccess())
            {
                Fail(CaptureFailureCode(frame.Status));
            }
            else if (frame.FrameNumber != m_RequestedFrameNumber)
            {
                Fail("capture_frame_mismatch");
            }
            else
            {
                Complete(frame);
            }
        }
        return m_Result.Status;
    }

    void BridgeDeterministicCaptureSession::Complete(const NorvesLib::Core::Rendering::CapturedFrame& frame)
    {
        using namespace NorvesLib::Core::Rendering;
        FrameCaptureThumbnailOptions options;
        options.MaxWidth = m_Request.MaxWidth;
        options.MaxHeight = m_Request.MaxHeight;
        options.MaxEncodedBytes = BridgeDeterministicCaptureMaxPngBytes;
        auto encoded = EncodeCapturedFrameThumbnail(frame, options);
        if (!encoded.IsSuccess())
        {
            Fail(EncodeFailureCode(encoded.Status));
            return;
        }

        // エンコーダが形式・寸法・行幅と画素バッファの長さを検証した後に生バイトを読む。
        uint64_t pixelHash = FnvOffsetBasis;
        const size_t tightRowBytes = static_cast<size_t>(frame.Width) * frame.BytesPerPixel;
        for (uint32_t row = 0; row < frame.Height; ++row)
        {
            pixelHash = HashBytes(pixelHash, frame.Pixels.data() + static_cast<size_t>(row) * frame.RowPitchBytes,
                tightRowBytes);
        }
        m_Result.EpochFrameNumber = frame.FrameNumber - m_EpochFirstFrameNumber;
        m_Result.PixelHash = pixelHash;
        m_Result.PngHash = HashBytes(FnvOffsetBasis, encoded.PngBytes.data(), encoded.PngBytes.size());
        m_Result.Width = encoded.Width;
        m_Result.Height = encoded.Height;
        m_Result.PngBytes = std::move(encoded.PngBytes);
        m_Result.Status = BridgeDeterministicCaptureStatus::Completed;
    }
} // namespace Game::Bridge
