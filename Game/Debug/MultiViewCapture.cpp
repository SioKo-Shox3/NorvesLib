#include "Debug/MultiViewCapture.h"

#include "Debug/CapturedFramePng.h"
#include "Scenes/SceneViewSwitch.h"

#include "Core/Public/Engine/Engine.h"
#include "Core/Public/Engine/NorvesEngine.h"
#include "Core/Public/Engine/SkeletalAssetSession.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Rendering/FrameCaptureHdrWriter.h"
#include "Core/Public/Rendering/FrameCaptureTypes.h"
#include "Core/Public/Rendering/RenderWorld.h"

namespace Game::Debug
{
    namespace
    {
        using NorvesLib::Core::Container::String;
        using NorvesLib::Core::Container::VariableArray;

        // 撮影を要求してから結果が戻るまで待つ描画フレーム数の上限（--capture-png と同じ）。
        constexpr uint64_t kResultTimeoutRenderedFrames = 120;
        // 視点の切り替えを頼んでから GameMode が受けるまで待つ描画フレーム数の上限
        constexpr uint64_t kSwitchTimeoutRenderedFrames = 120;
    } // namespace

    void MultiViewCapture::Configure(const VariableArray<String>& viewNames, const String& outputDirectory,
                                     uint64_t settledRenderedFrames, uint64_t viewSettleRenderedFrames,
                                     Game::Scenes::SceneViewSwitch* pViewSwitch)
    {
        m_ViewNames = viewNames;
        m_OutputDirectory = outputDirectory;
        m_SettledRenderedFrames = settledRenderedFrames;
        m_ViewSettleRenderedFrames = viewSettleRenderedFrames;
        m_pViewSwitch = pViewSwitch;
        m_Phase = Phase::Settling;
        m_ViewIndex = 0;
        m_bObservedPendingAssets = false;
        m_bBaselineLatched = false;
        m_BaselineRenderedFrame = 0;
        m_SettleUntilRenderedFrame = 0;
        m_WaitStartRenderedFrame = 0;
    }

    bool MultiViewCapture::IsLoading(NorvesLib::Core::Rendering::RenderWorld& renderWorld, bool bOtherPending,
                                     uint64_t renderedFrameCount) const
    {
        // ApplicationProcessor の --capture-png と同じ読み込み中の判定（非同期の資産・骨格の資産・ほかの待ち・
        // 決定的な撮影での SceneReady の前）に、視点を置き直した後の待ちを足す。
        if (renderedFrameCount < m_SettleUntilRenderedFrame)
        {
            return true;
        }
        if (bOtherPending || renderWorld.HasPendingAsyncAssets() ||
            NorvesLib::Core::GEngine.GetSkeletalAssetSession().GetPendingCount() != 0)
        {
            return true;
        }
        if (!NorvesLib::Core::Engine::GEngine)
        {
            return false;
        }
        const auto& deterministicCapture = NorvesLib::Core::Engine::GEngine->GetDeterministicCapture();
        return deterministicCapture.IsEnabled() && !deterministicCapture.IsSceneReady();
    }

    void MultiViewCapture::Fail(const char* reason)
    {
        const char* viewName = m_ViewIndex < m_ViewNames.size() ? m_ViewNames[m_ViewIndex].c_str() : "";
        LOG_ERROR_F("CAPTURE_VIEWS failed view=%s reason=%s", viewName, reason);
        m_Phase = Phase::Failed;
        if (NorvesLib::Core::Engine::GEngine)
        {
            NorvesLib::Core::Engine::GEngine->RequestExit(1);
        }
    }

    void MultiViewCapture::OnPreRender(NorvesLib::Core::Rendering::RenderWorld& renderWorld, bool bOtherPending)
    {
        if (!IsEnabled() || m_Phase != Phase::Settling || !renderWorld.IsInitialized())
        {
            return;
        }
        // ApplicationProcessor と同じく、描画の前にも読み込み中を見て、基準を数え直す。
        if (IsLoading(renderWorld, bOtherPending, renderWorld.GetRenderedFrameCount()))
        {
            m_bObservedPendingAssets = true;
            m_bBaselineLatched = false;
        }
    }

    void MultiViewCapture::OnPostRender(NorvesLib::Core::Rendering::RenderWorld& renderWorld, bool bOtherPending)
    {
        if (!IsEnabled() || m_Phase == Phase::Done || m_Phase == Phase::Failed || !renderWorld.IsInitialized())
        {
            return;
        }

        const uint64_t renderedFrameCount = renderWorld.GetRenderedFrameCount();
        const String& viewName = m_ViewNames[m_ViewIndex];

        if (m_Phase == Phase::WaitingSwitch)
        {
            if (m_pViewSwitch->GetState() == Game::Scenes::SceneViewSwitch::State::Failed)
            {
                Fail("view_switch_rejected");
                return;
            }
            if (m_pViewSwitch->GetState() != Game::Scenes::SceneViewSwitch::State::Applied)
            {
                if (renderedFrameCount - m_WaitStartRenderedFrame > kSwitchTimeoutRenderedFrames)
                {
                    Fail("view_switch_timeout");
                }
                return;
            }
            // 置き直した。ストリーミングが追いつくまで待ってから、基準を探し直す。
            m_Phase = Phase::Settling;
            m_bObservedPendingAssets = true;
            m_bBaselineLatched = false;
            m_SettleUntilRenderedFrame = renderedFrameCount + m_ViewSettleRenderedFrames;
            LOG_INFO_F("CAPTURE_VIEWS view=%s switched rendered=%llu", viewName.c_str(),
                       static_cast<unsigned long long>(renderedFrameCount));
            return;
        }

        if (m_Phase == Phase::Settling)
        {
            // 落ち着いた時点（ApplicationProcessor の --capture-png の数え方と同じ）: 読み込み中を一度見た後、
            // 読み込み中でない最初の描画を基準にし、その時点から決定的な撮影のエポックを始め直す。
            if (IsLoading(renderWorld, bOtherPending, renderedFrameCount))
            {
                m_bObservedPendingAssets = true;
                m_bBaselineLatched = false;
                return;
            }
            if (!m_bObservedPendingAssets)
            {
                return;
            }
            if (!m_bBaselineLatched)
            {
                m_BaselineRenderedFrame = renderedFrameCount;
                m_bBaselineLatched = true;
                LOG_INFO_F("CAPTURE_VIEWS view=%s settle baseline rendered=%llu", viewName.c_str(),
                           static_cast<unsigned long long>(m_BaselineRenderedFrame));
                if (NorvesLib::Core::Engine::GEngine &&
                    NorvesLib::Core::Engine::GEngine->GetDeterministicCapture().IsEnabled())
                {
                    // 次のフレームを 0 番として、時間・TAA・RTGI・自動露出を数え直す
                    NorvesLib::Core::Engine::GEngine->GetDeterministicCapture().BeginEpoch();
                    renderWorld.BeginDeterministicEpoch();
                }
                return;
            }
            if (renderedFrameCount < m_BaselineRenderedFrame)
            {
                m_BaselineRenderedFrame = renderedFrameCount;
                return;
            }
            if (renderedFrameCount - m_BaselineRenderedFrame < m_SettledRenderedFrames)
            {
                return;
            }

            NorvesLib::Core::Rendering::FrameCaptureRequest request;
            request.SourceKind = NorvesLib::Core::Rendering::FrameCaptureSourceKind::BackBuffer;
            request.bIncludeSceneColor = m_bCaptureHdr;
            request.bIncludeAov = m_bCaptureAov;
            request.bIncludeAtmosphere = m_bCaptureAtmosphere;
            request.bIncludeClouds = m_bCaptureClouds;
            if (!renderWorld.RequestFrameCapture(request).IsAccepted())
            {
                Fail("capture_request_rejected");
                return;
            }
            m_Phase = Phase::WaitingResult;
            m_WaitStartRenderedFrame = renderedFrameCount;
            LOG_INFO_F("CAPTURE_VIEWS view=%s requested rendered=%llu", viewName.c_str(),
                       static_cast<unsigned long long>(renderedFrameCount));
            return;
        }

        // Phase::WaitingResult
        NorvesLib::Core::Rendering::CapturedFrame capturedFrame;
        if (!renderWorld.TryConsumeCapturedFrame(capturedFrame))
        {
            if (renderedFrameCount - m_WaitStartRenderedFrame > kResultTimeoutRenderedFrames)
            {
                Fail("no_capture_result");
            }
            return;
        }
        const String path = m_OutputDirectory + TEXT("/") + viewName + TEXT(".png");
        if (!SaveCapturedFramePng(capturedFrame, path))
        {
            LOG_ERROR_F("CAPTURE_VIEWS save failed status=%u format=%u width=%u height=%u path=%s",
                        static_cast<unsigned int>(capturedFrame.Status), static_cast<unsigned int>(capturedFrame.Format),
                        capturedFrame.Width, capturedFrame.Height, path.c_str());
            Fail("save_failed");
            return;
        }
        LOG_INFO_F("CAPTURE_VIEWS view=%s saved frame=%llu width=%u height=%u path=%s", viewName.c_str(),
                   static_cast<unsigned long long>(capturedFrame.FrameNumber), capturedFrame.Width,
                   capturedFrame.Height, path.c_str());
        if (m_bCaptureHdr)
        {
            if (!NorvesLib::Core::Rendering::SaveCapturedSceneColorHdr(capturedFrame, path))
            {
                LOG_ERROR_F("CAPTURE_VIEWS view=%s hdr save failed scene_color=%d format=%u", viewName.c_str(),
                            capturedFrame.SceneColor.bValid ? 1 : 0,
                            static_cast<unsigned int>(capturedFrame.SceneColor.Format));
                Fail("hdr_save_failed");
                return;
            }
            LOG_INFO_F("CAPTURE_VIEWS view=%s hdr saved width=%u height=%u pre_exposure=%g", viewName.c_str(),
                       capturedFrame.SceneColor.Width, capturedFrame.SceneColor.Height,
                       static_cast<double>(capturedFrame.SceneColor.PreExposure));
        }
        if (m_bCaptureAov)
        {
            if (!NorvesLib::Core::Rendering::SaveCapturedAov(capturedFrame, path))
            {
                LOG_ERROR_F("CAPTURE_VIEWS view=%s aov save failed aov=%d", viewName.c_str(),
                            capturedFrame.Aov.bValid ? 1 : 0);
                Fail("aov_save_failed");
                return;
            }
            LOG_INFO_F("CAPTURE_VIEWS view=%s aov saved width=%u height=%u", viewName.c_str(),
                       capturedFrame.Aov.Surface.Width, capturedFrame.Aov.Surface.Height);
        }
        if (m_bCaptureAtmosphere)
        {
            if (!NorvesLib::Core::Rendering::SaveCapturedAtmosphere(capturedFrame, path))
            {
                LOG_ERROR_F("CAPTURE_VIEWS view=%s atmosphere save failed atmosphere=%d", viewName.c_str(),
                            capturedFrame.Atmosphere.bValid ? 1 : 0);
                Fail("atmosphere_save_failed");
                return;
            }
            LOG_INFO_F("CAPTURE_VIEWS view=%s atmosphere saved rays=%u", viewName.c_str(),
                       capturedFrame.Atmosphere.Info.RayCount);
        }
        if (m_bCaptureClouds)
        {
            if (!NorvesLib::Core::Rendering::SaveCapturedClouds(capturedFrame, path))
            {
                LOG_ERROR_F("CAPTURE_VIEWS view=%s cloud save failed clouds=%d", viewName.c_str(),
                            capturedFrame.Clouds.bValid ? 1 : 0);
                Fail("cloud_save_failed");
                return;
            }
            LOG_INFO_F("CAPTURE_VIEWS view=%s clouds saved rays=%u", viewName.c_str(), capturedFrame.Clouds.Info.RayCount);
            if (m_bCaptureCloudOpacity)
            {
                if (!NorvesLib::Core::Rendering::SaveCapturedCloudOpacity(capturedFrame, path))
                {
                    LOG_ERROR_F("CAPTURE_VIEWS view=%s cloud opacity save failed opacity=%d", viewName.c_str(),
                                capturedFrame.Clouds.bOpacityValid ? 1 : 0);
                    Fail("cloud_opacity_save_failed");
                    return;
                }
                LOG_INFO_F("CAPTURE_VIEWS view=%s cloud opacity saved width=%u height=%u", viewName.c_str(),
                           capturedFrame.Clouds.Opacity.Width, capturedFrame.Clouds.Opacity.Height);
            }
        }

        ++m_ViewIndex;
        if (m_ViewIndex >= m_ViewNames.size())
        {
            m_Phase = Phase::Done;
            LOG_INFO_F("CAPTURE_VIEWS done views=%zu", m_ViewNames.size());
            if (NorvesLib::Core::Engine::GEngine)
            {
                NorvesLib::Core::Engine::GEngine->RequestExit(0);
            }
            return;
        }
        if (m_pViewSwitch == nullptr)
        {
            Fail("view_switch_unavailable");
            return;
        }
        m_pViewSwitch->Request(m_ViewNames[m_ViewIndex]);
        m_Phase = Phase::WaitingSwitch;
        m_WaitStartRenderedFrame = renderedFrameCount;
        LOG_INFO_F("CAPTURE_VIEWS view=%s switch requested rendered=%llu", m_ViewNames[m_ViewIndex].c_str(),
                   static_cast<unsigned long long>(renderedFrameCount));
    }
} // namespace Game::Debug
