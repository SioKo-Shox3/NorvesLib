#include "RenderingValidation/RenderingValidationApplication.h"

#include "Engine/Engine.h"
#include "Logging/LogMacros.h"
#include "Rendering/RenderWorld.h"

namespace NorvesLib::Test::RenderingValidation
{
    namespace
    {
        bool StartsWith(const Core::Container::String& value, const TCHAR* prefix)
        {
            const Core::Container::String prefixText(prefix);
            return value.size() >= prefixText.size() && value.substr(0, prefixText.size()) == prefixText;
        }

        // 10進の正の整数（上限付き）を読む。
        bool TryParsePositiveInteger(const Core::Container::String& text, uint32_t maximum,
                                     uint32_t& outValue)
        {
            if (text.empty() || text.size() > 9u)
            {
                return false;
            }
            uint32_t value = 0u;
            for (const auto character : text)
            {
                if (character < TEXT('0') || character > TEXT('9'))
                {
                    return false;
                }
                value = value * 10u + static_cast<uint32_t>(character - TEXT('0'));
            }
            if (value == 0u || value > maximum)
            {
                return false;
            }
            outValue = value;
            return true;
        }

        // パストレーサーの取得1回に掛かる描画フレーム数（保留で待つ分）の見積もり。
        uint64_t ComputePathTracingCaptureFrames(const RenderingValidationRunConfig& config)
        {
            if (!config.bPathTracing)
            {
                return 0u;
            }
            const uint64_t perFrame = config.PathTracingSamplesPerFrame == 0u
                ? 1u : config.PathTracingSamplesPerFrame;
            return (static_cast<uint64_t>(config.PathTracingSamples) + perFrame - 1u) / perFrame;
        }
    }

    bool RenderingValidationApplicationHandler::OnPreInitialize(
        const Core::Container::VariableArray<Core::Container::String>& args)
    {
        m_RunConfig = RenderingValidationRunConfig{};
        m_bCaptureRequested = false;
        m_bExitRequested = false;
        m_CaptureRequestRenderedFrame = 0;
        m_LastAcceptedRequestId = 0;
        m_LastAcceptedRequestStageToken = 0;
        m_NextStageToken = 0;
        bool bSceneSpecified = false;
        bool bCaptureSourceSpecified = false;
        for (const Core::Container::String& argument : args)
        {
            if (argument == TEXT("--trace") || StartsWith(argument, TEXT("--trace-file=")))
            {
                continue;
            }
            if (StartsWith(argument, TEXT("--scene=")))
            {
                if (bSceneSpecified)
                {
                    LOG_ERROR("描画検証の scene 引数が重複しています");
                    return false;
                }
                bSceneSpecified = true;
                const Core::Container::String value = argument.substr(Core::Container::String(TEXT("--scene=")).size());
                if (value == TEXT("indoor"))
                {
                    m_RunConfig.Scene = SceneKind::Indoor;
                }
                else if (value == TEXT("outdoor"))
                {
                    m_RunConfig.Scene = SceneKind::Outdoor;
                }
                else
                {
                    LOG_ERROR("描画検証の scene 値が不正です");
                    return false;
                }
                continue;
            }
            if (StartsWith(argument, TEXT("--capture-source=")))
            {
                if (bCaptureSourceSpecified)
                {
                    LOG_ERROR("描画検証の capture-source 引数が重複しています");
                    return false;
                }
                bCaptureSourceSpecified = true;
                const Core::Container::String value =
                    argument.substr(Core::Container::String(TEXT("--capture-source=")).size());
                if (value == TEXT("presentation"))
                {
                    m_RunConfig.CaptureSource =
                        Core::Rendering::FrameCaptureSourceKind::PresentationColor;
                }
                else if (value == TEXT("scene-color"))
                {
                    m_RunConfig.CaptureSource = Core::Rendering::FrameCaptureSourceKind::SceneColor;
                }
                else if (value == TEXT("back-buffer"))
                {
                    m_RunConfig.CaptureSource = Core::Rendering::FrameCaptureSourceKind::BackBuffer;
                }
                else if (value == TEXT("gbuffer-velocity"))
                {
                    m_RunConfig.CaptureSource = Core::Rendering::FrameCaptureSourceKind::GBufferVelocity;
                }
                else
                {
                    LOG_ERROR("描画検証の capture-source 値が不正です");
                    return false;
                }
                continue;
            }

            if (StartsWith(argument, TEXT("--renderer=")))
            {
                const Core::Container::String value =
                    argument.substr(Core::Container::String(TEXT("--renderer=")).size());
                if (value == TEXT("raster"))
                {
                    m_RunConfig.bPathTracing = false;
                }
                else if (value == TEXT("path-tracing"))
                {
                    m_RunConfig.bPathTracing = true;
                }
                else
                {
                    LOG_ERROR("描画検証の renderer 値が不正です");
                    return false;
                }
                continue;
            }
            if (StartsWith(argument, TEXT("--path-tracing-samples-per-frame=")))
            {
                // エンジン側が同じ引数で試料数を設定する。ここでは待ち時間の見積もりにだけ使う。
                if (!TryParsePositiveInteger(
                        argument.substr(Core::Container::String(
                            TEXT("--path-tracing-samples-per-frame=")).size()),
                        1024u, m_RunConfig.PathTracingSamplesPerFrame))
                {
                    LOG_ERROR("描画検証の path-tracing-samples-per-frame 値が不正です");
                    return false;
                }
                continue;
            }
            if (StartsWith(argument, TEXT("--path-tracing-transport=")) ||
                StartsWith(argument, TEXT("--path-tracing-pixel-sampling=")) ||
                StartsWith(argument, TEXT("--path-tracing-sample-batch=")) ||
                StartsWith(argument, TEXT("--path-tracing-debug-output=")) ||
                StartsWith(argument, TEXT("--raster-direct-brdf=")))
            {
                // エンジンが同じ引数で輸送範囲・画素内の標本位置・PTの検証出力・ラスタの直接光の
                // BRDFを設定する。
                continue;
            }
            if (StartsWith(argument, TEXT("--path-tracing-samples=")))
            {
                if (!TryParsePositiveInteger(
                        argument.substr(Core::Container::String(
                            TEXT("--path-tracing-samples=")).size()),
                        1u << 24u, m_RunConfig.PathTracingSamples))
                {
                    LOG_ERROR("描画検証の path-tracing-samples 値が不正です");
                    return false;
                }
                continue;
            }

            Core::Container::String reason;
            if (!ParseAdditionalArgument(argument, reason))
            {
                LOG_ERROR("描画検証に未対応の引数があります");
                return false;
            }
        }

        if (!bSceneSpecified)
        {
            LOG_ERROR("描画検証の scene 引数がありません");
            return false;
        }
        return true;
    }

bool RenderingValidationApplicationHandler::OnInitialize()
{
	if (Core::Engine::GEngine == nullptr)
	{
		return false;
	}
	if (!m_Fixture.Initialize(Core::Engine::GEngine->GetWorld(),
		Core::Engine::GEngine->GetRenderResources(),
		m_RunConfig.Scene,
		m_RunConfig.Seed))
	{
		return false;
	}
	Core::Engine::GEngine->GetRenderWorld().GetRenderingCoordinator().SetRTGIEnabled(false);
    if (m_RunConfig.bPathTracing &&
        Core::Engine::GEngine->GetRenderWorld().GetRenderingCoordinator().GetMainViewRenderer() !=
            Core::Rendering::RenderingMainViewRenderer::PathTracing)
    {
        LOG_ERROR("描画検証でパストレーサーを要求しましたが、メインSceneViewがラスタのままです");
        return false;
    }
	return true;
}

    bool RenderingValidationApplicationHandler::ShouldAdvanceSimulation() const
    {
        return !m_bCaptureRequested;
    }

    void RenderingValidationApplicationHandler::OnPreRender()
    {
        if (m_bExitRequested || Core::Engine::GEngine == nullptr)
        {
            return;
        }

        Core::Rendering::RenderWorld& renderWorld = Core::Engine::GEngine->GetRenderWorld();
        m_Fixture.ApplyCamera(renderWorld);
        ApplyCaptureStageState(renderWorld);
        if (!m_bCaptureRequested && m_Fixture.IsCaptureStateStable() &&
            !renderWorld.HasPendingAsyncAssets())
        {
            Core::Rendering::FrameCaptureRequest captureRequest;
            captureRequest.SourceKind = m_RunConfig.CaptureSource;
            captureRequest.MinimumPathTracingSamples =
                m_RunConfig.bPathTracing ? m_RunConfig.PathTracingSamples : 0u;
            const Core::Rendering::FrameCaptureRequestResult request =
                renderWorld.RequestFrameCapture(captureRequest);
            if (!request.IsAccepted())
            {
                Fail(request.Status == Core::Rendering::FrameCaptureRequestStatus::AlreadyPending
                         ? "描画検証の capture 要求時に別の要求が残っています"
                         : "描画検証の capture 機能が初期化されていません");
                return;
            }
            m_bCaptureRequested = true;
            m_CaptureRequestRenderedFrame = renderWorld.GetRenderedFrameCount();
            m_LastAcceptedRequestId = request.RequestId;
            m_LastAcceptedRequestStageToken = ++m_NextStageToken;
            LOG_INFO("RenderingValidation capture request accepted: request=%llu stage_token=%llu frame=%llu",
                     static_cast<unsigned long long>(m_LastAcceptedRequestId),
                     static_cast<unsigned long long>(m_LastAcceptedRequestStageToken),
                     static_cast<unsigned long long>(m_CaptureRequestRenderedFrame));
        }
    }

    void RenderingValidationApplicationHandler::OnPostRender()
    {
        if (m_bExitRequested || Core::Engine::GEngine == nullptr)
        {
            return;
        }

        Core::Rendering::RenderWorld& renderWorld = Core::Engine::GEngine->GetRenderWorld();
        const uint64_t renderedFrames = renderWorld.GetRenderedFrameCount();
        if (m_bCaptureRequested)
        {
            Core::Rendering::CapturedFrame frame;
            if (renderWorld.TryConsumeCapturedFrame(frame))
            {
                Core::Container::String reason;
                if (!frame.IsSuccess())
                {
                    Fail("描画検証の capture 結果が失敗しました");
                    return;
                }
                if (m_RunConfig.bPathTracing)
                {
                    LOG_INFO("RenderingValidation path tracing capture: request=%llu frame=%llu samples=%u",
                             static_cast<unsigned long long>(frame.RequestId),
                             static_cast<unsigned long long>(frame.FrameNumber),
                             frame.PathTracingSampleCount);
                    if (frame.PathTracingSampleCount < m_RunConfig.PathTracingSamples)
                    {
                        Fail("描画検証の capture がパストレーサーの試料数に届く前に取得されました");
                        return;
                    }
                }
                const bool bAccepted = EvaluateCapturedFrame(frame, reason);
                if (bAccepted)
                {
                    AdvanceCaptureStage();
                    ApplyCaptureStageState(renderWorld);
                }
                Core::Rendering::FrameCaptureRequest followupRequest;
                if (bAccepted && RequestFollowupCapture(frame, followupRequest))
                {
                    followupRequest.MinimumPathTracingSamples =
                        m_RunConfig.bPathTracing ? m_RunConfig.PathTracingSamples : 0u;
                    const Core::Rendering::FrameCaptureRequestResult followupResult =
                        renderWorld.RequestFrameCapture(followupRequest);
                    if (!followupResult.IsAccepted())
                    {
                        Fail("描画検証の follow-up capture 要求時に別の要求が残っています");
                        return;
                    }
                    m_bCaptureRequested = true;
                    m_CaptureRequestRenderedFrame = renderedFrames;
                    m_LastAcceptedRequestId = followupResult.RequestId;
                    m_LastAcceptedRequestStageToken = ++m_NextStageToken;
                    LOG_INFO("RenderingValidation follow-up request accepted: request=%llu stage_token=%llu frame=%llu",
                             static_cast<unsigned long long>(m_LastAcceptedRequestId),
                             static_cast<unsigned long long>(m_LastAcceptedRequestStageToken),
                             static_cast<unsigned long long>(m_CaptureRequestRenderedFrame));
                    return;
                }
                m_bExitRequested = true;
                Core::Engine::GEngine->RequestExit(bAccepted ? 0 : 1);
                if (!bAccepted)
                {
                    LOG_ERROR("描画検証の capture 評価に失敗しました: %s", reason.c_str());
                }
                return;
            }
            if (renderedFrames >= m_CaptureRequestRenderedFrame + 32 +
                                      ComputePathTracingCaptureFrames(m_RunConfig))
            {
                Fail("描画検証の capture が要求後32フレーム以内に完了しませんでした");
                return;
            }
        }
        // パストレーサーは取得ごとに試料の累積を待つため、全体の期限は取得ごとの期限に任せる。
        if (!m_RunConfig.bPathTracing && renderedFrames >= 600)
        {
            Fail("描画検証が全体600フレームの期限を超えました");
        }
    }

    void RenderingValidationApplicationHandler::OnPreShutdown()
    {
        if (Core::Engine::GEngine != nullptr)
        {
            m_Fixture.Shutdown(Core::Engine::GEngine->GetRenderResources());
        }
    }

    bool RenderingValidationApplicationHandler::ParseAdditionalArgument(
        const Core::Container::String& argument,
        Core::Container::String& outFailureReason)
    {
        (void)argument;
        outFailureReason = TEXT("unsupported rendering validation argument");
        return false;
    }

    const RenderingValidationRunConfig& RenderingValidationApplicationHandler::GetRunConfig() const
    {
        return m_RunConfig;
    }

    bool RenderingValidationApplicationHandler::RequestFollowupCapture(
        const Core::Rendering::CapturedFrame& /*frame*/,
        Core::Rendering::FrameCaptureRequest& /*outRequest*/)
    {
        return false;
    }

    void RenderingValidationApplicationHandler::ApplyCaptureStageState(
        Core::Rendering::RenderWorld& /*renderWorld*/)
    {
    }

    void RenderingValidationApplicationHandler::AdvanceCaptureStage()
    {
    }

    uint64_t RenderingValidationApplicationHandler::GetLastAcceptedRequestId() const
    {
        return m_LastAcceptedRequestId;
    }

    uint64_t RenderingValidationApplicationHandler::GetLastAcceptedRequestStageToken() const
    {
        return m_LastAcceptedRequestStageToken;
    }

    const RenderingValidationSceneFixture& RenderingValidationApplicationHandler::GetFixture() const
    {
        return m_Fixture;
    }

    void RenderingValidationApplicationHandler::Fail(const char* summary)
    {
        if (!m_bExitRequested && Core::Engine::GEngine != nullptr)
        {
            LOG_ERROR("%s", summary);
            m_bExitRequested = true;
            Core::Engine::GEngine->RequestExit(1);
        }
    }
}
