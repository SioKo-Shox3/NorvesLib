#include "Debug/CameraScriptCapture.h"

#include "Debug/CapturedFramePng.h"

#include "Core/Public/Engine/Engine.h"
#include "Core/Public/Engine/NorvesEngine.h"
#include "Core/Public/Engine/SkeletalAssetSession.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Rendering/FrameCaptureTypes.h"
#include "Core/Public/Rendering/RenderWorld.h"

namespace Game::Debug
{
    namespace
    {
        using NorvesLib::Core::Container::String;

        // 撮影を要求してから結果が戻るまで待つ描画フレーム数の上限（--capture-views と同じ）
        constexpr uint64_t kResultTimeoutFrames = 120;

        unsigned long long EngineFrame()
        {
            return NorvesLib::Core::Engine::GEngine
                       ? static_cast<unsigned long long>(NorvesLib::Core::Engine::GEngine->GetFrameCount())
                       : 0ull;
        }
    } // namespace

    void CameraScriptCapture::Configure(const String& outputDirectory, const String& viewName)
    {
        m_OutputDirectory = outputDirectory;
        m_ViewName = viewName;
        m_ReadyFrames = 0;
        m_bFailed = false;
        m_bWaitingResult = false;
        m_PendingTag = String();
        m_PendingEngineFrame = 0;
        m_WaitedFrames = 0;
    }

    void CameraScriptCapture::Fail(const char* reason)
    {
        LOG_ERROR_F("CAMERA_SCRIPT_CAPTURE failed tag=%s reason=%s", m_PendingTag.c_str(), reason);
        m_bFailed = true;
        m_bWaitingResult = false;
        if (NorvesLib::Core::Engine::GEngine)
        {
            NorvesLib::Core::Engine::GEngine->RequestExit(1);
        }
    }

    void CameraScriptCapture::BeginEpoch()
    {
        if (!NorvesLib::Core::Engine::GEngine || !NorvesLib::Core::Engine::GEngine->GetDeterministicCapture().IsEnabled())
        {
            LOG_ERROR("CAMERA_SCRIPT_EPOCH failed reason=deterministic_capture_disabled（--capture-deterministic と併せて使う）");
            m_PendingTag = String();
            Fail("epoch_without_deterministic_capture");
            return;
        }
        // Tick は BeginFrame の前なので、このフレームのパケットが 0 番になる（MultiViewCapture は描画の後に頼み、次のフレームが 0 番）
        NorvesLib::Core::Engine::GEngine->GetDeterministicCapture().BeginEpoch();
        NorvesLib::Core::Engine::GEngine->GetRenderWorld().BeginDeterministicEpoch();
        LOG_INFO_F("CAMERA_SCRIPT_EPOCH engine_frame=%llu", EngineFrame());
    }

    void CameraScriptCapture::RequestCapture(const String& tag, const float position[3], float yaw, float pitch)
    {
        if (m_bFailed)
        {
            return;
        }
        if (!IsConfigured() || !NorvesLib::Core::Engine::GEngine)
        {
            m_PendingTag = tag;
            Fail("not_configured（--capture-views と併せて使う）");
            return;
        }
        if (m_bWaitingResult)
        {
            // 前の撮影の読み出しが終わっていない（台本の撮影の間を空ける）
            const String previous = m_PendingTag;
            m_PendingTag = tag;
            LOG_ERROR_F("CAMERA_SCRIPT_CAPTURE previous capture still pending tag=%s", previous.c_str());
            Fail("previous_capture_pending");
            return;
        }
        NorvesLib::Core::Rendering::FrameCaptureRequest request;
        request.SourceKind = NorvesLib::Core::Rendering::FrameCaptureSourceKind::BackBuffer;
        m_PendingTag = tag;
        if (!NorvesLib::Core::Engine::GEngine->GetRenderWorld().RequestFrameCapture(request).IsAccepted())
        {
            Fail("capture_request_rejected");
            return;
        }
        m_bWaitingResult = true;
        m_PendingEngineFrame = EngineFrame();
        m_WaitedFrames = 0;
        LOG_INFO_F("CAMERA_SCRIPT_CAPTURE requested tag=%s engine_frame=%llu position=(%.4f, %.4f, %.4f) yaw=%.6f "
                   "pitch=%.6f",
                   tag.c_str(), static_cast<unsigned long long>(m_PendingEngineFrame), static_cast<double>(position[0]),
                   static_cast<double>(position[1]), static_cast<double>(position[2]), static_cast<double>(yaw),
                   static_cast<double>(pitch));
    }

    void CameraScriptCapture::OnPreRender(NorvesLib::Core::Rendering::RenderWorld& renderWorld)
    {
        if (!IsConfigured() || !renderWorld.IsInitialized())
        {
            return;
        }
        // MultiViewCapture の読み込み中の判定のうち、ストリーミングの部分（組み立ての SceneReady は台本の間は偽のまま）
        const bool bPending = renderWorld.HasPendingAsyncAssets() ||
                              NorvesLib::Core::GEngine.GetSkeletalAssetSession().GetPendingCount() != 0;
        m_ReadyFrames = bPending ? 0u : (m_ReadyFrames < kReadyStableFrames ? m_ReadyFrames + 1u : m_ReadyFrames);
    }

    void CameraScriptCapture::OnPostRender(NorvesLib::Core::Rendering::RenderWorld& renderWorld)
    {
        if (!IsConfigured() || !m_bWaitingResult || m_bFailed || !renderWorld.IsInitialized())
        {
            return;
        }
        NorvesLib::Core::Rendering::CapturedFrame capturedFrame;
        if (!renderWorld.TryConsumeCapturedFrame(capturedFrame))
        {
            if (++m_WaitedFrames > kResultTimeoutFrames)
            {
                Fail("no_capture_result");
            }
            return;
        }
        m_bWaitingResult = false;
        const String path = m_OutputDirectory + TEXT("/") + m_ViewName + TEXT(".") + m_PendingTag + TEXT(".png");
        if (!SaveCapturedFramePng(capturedFrame, path))
        {
            LOG_ERROR_F("CAMERA_SCRIPT_CAPTURE save failed status=%u format=%u width=%u height=%u path=%s",
                        static_cast<unsigned int>(capturedFrame.Status), static_cast<unsigned int>(capturedFrame.Format),
                        capturedFrame.Width, capturedFrame.Height, path.c_str());
            Fail("save_failed");
            return;
        }
        LOG_INFO_F("CAMERA_SCRIPT_CAPTURE saved tag=%s frame=%llu requested_engine_frame=%llu width=%u height=%u path=%s",
                   m_PendingTag.c_str(), static_cast<unsigned long long>(capturedFrame.FrameNumber),
                   static_cast<unsigned long long>(m_PendingEngineFrame), capturedFrame.Width, capturedFrame.Height,
                   path.c_str());
    }
} // namespace Game::Debug
