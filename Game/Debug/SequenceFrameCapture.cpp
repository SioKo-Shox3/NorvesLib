#include "Debug/SequenceFrameCapture.h"

#include "Debug/CapturedFramePng.h"

#include "Core/Public/Engine/Engine.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Rendering/FrameCaptureTypes.h"
#include "Core/Public/Rendering/RenderWorld.h"

#include <algorithm>

namespace Game::Debug
{
    namespace
    {
        using NorvesLib::Core::Container::String;
        using NorvesLib::Core::Container::VariableArray;

        // 撮影を要求してから結果が戻るまで待つ描画フレーム数の上限（--capture-png と同じ）。
        constexpr uint64_t kResultTimeoutRenderedFrames = 120;

        void AppendDecimal(String& text, uint64_t value)
        {
            char digits[24] = {};
            size_t count = 0;
            do
            {
                digits[count++] = static_cast<char>('0' + value % 10u);
                value /= 10u;
            } while (value > 0u && count < sizeof(digits));
            while (count > 0)
            {
                text += static_cast<TCHAR>(digits[--count]);
            }
        }

        // 決定的な撮影（--capture-deterministic）では、ApplicationProcessor と同じく、GameMode が読み込み後の
        // 組み立て（大きな球の生成など）を終えるまでも読み込み中として数える（落ち着いた時点を --capture-png と揃える）。
        bool IsAssetLoading(NorvesLib::Core::Rendering::RenderWorld& renderWorld)
        {
            if (renderWorld.HasPendingAsyncAssets())
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

        void RequestFailureExit()
        {
            if (NorvesLib::Core::Engine::GEngine)
            {
                NorvesLib::Core::Engine::GEngine->RequestExit(1);
            }
        }
    } // namespace

    void SequenceFrameCapture::Configure(const String& pathPrefix, const VariableArray<uint64_t>& renderedFrames)
    {
        m_PathPrefix = pathPrefix;
        m_RenderedFrames = renderedFrames;
        std::sort(m_RenderedFrames.begin(), m_RenderedFrames.end());
        m_RenderedFrames.erase(std::unique(m_RenderedFrames.begin(), m_RenderedFrames.end()), m_RenderedFrames.end());
        m_NextIndex = 0;
        m_bObservedPendingAssets = false;
        m_bBaselineLatched = false;
        m_BaselineRenderedFrame = 0;
        m_bCaptureRequested = false;
        m_CaptureRequestedRenderedFrame = 0;
        m_bFailed = false;
    }

    void SequenceFrameCapture::OnPreRender(NorvesLib::Core::Rendering::RenderWorld& renderWorld)
    {
        if (!IsEnabled() || !renderWorld.IsInitialized())
        {
            return;
        }
        // ApplicationProcessor と同じく、描画の前にも読み込み中のアセットを見て、落ち着いた時点を数え直す。
        if (IsAssetLoading(renderWorld))
        {
            m_bObservedPendingAssets = true;
            m_bBaselineLatched = false;
        }
    }

    void SequenceFrameCapture::OnPostRender(NorvesLib::Core::Rendering::RenderWorld& renderWorld)
    {
        if (!IsEnabled() || m_bFailed || m_NextIndex >= m_RenderedFrames.size() || !renderWorld.IsInitialized())
        {
            return;
        }

        const uint64_t renderedFrameCount = renderWorld.GetRenderedFrameCount();
        if (!m_bCaptureRequested)
        {
            // 落ち着いた時点（--capture-png の数え方と同じ）: 読み込み中を一度見た後、読み込み中でない最初の描画。
            if (IsAssetLoading(renderWorld))
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
                LOG_INFO_F("SEQUENCE_CAPTURE baseline rendered=%llu",
                           static_cast<unsigned long long>(m_BaselineRenderedFrame));
                return;
            }
            const uint64_t targetFrames = m_RenderedFrames[m_NextIndex];
            if (renderedFrameCount < m_BaselineRenderedFrame + targetFrames)
            {
                return;
            }

            NorvesLib::Core::Rendering::FrameCaptureRequest request;
            request.SourceKind = NorvesLib::Core::Rendering::FrameCaptureSourceKind::BackBuffer;
            if (!renderWorld.RequestFrameCapture(request).IsAccepted())
            {
                LOG_ERROR_F("SEQUENCE_CAPTURE failed: capture request rejected target=%llu",
                            static_cast<unsigned long long>(targetFrames));
                m_bFailed = true;
                RequestFailureExit();
                return;
            }
            m_bCaptureRequested = true;
            m_CaptureRequestedRenderedFrame = renderedFrameCount;
            LOG_INFO_F("SEQUENCE_CAPTURE requested target=%llu rendered=%llu settled=%llu",
                       static_cast<unsigned long long>(targetFrames),
                       static_cast<unsigned long long>(renderedFrameCount),
                       static_cast<unsigned long long>(renderedFrameCount - m_BaselineRenderedFrame));
            return;
        }

        NorvesLib::Core::Rendering::CapturedFrame capturedFrame;
        if (!renderWorld.TryConsumeCapturedFrame(capturedFrame))
        {
            if (renderedFrameCount - m_CaptureRequestedRenderedFrame > kResultTimeoutRenderedFrames)
            {
                LOG_ERROR_F("SEQUENCE_CAPTURE failed: no result within %llu rendered frames",
                            static_cast<unsigned long long>(kResultTimeoutRenderedFrames));
                m_bFailed = true;
                RequestFailureExit();
            }
            return;
        }

        const uint64_t targetFrames = m_RenderedFrames[m_NextIndex];
        String path = m_PathPrefix;
        AppendDecimal(path, targetFrames);
        path += TEXT(".png");
        m_bCaptureRequested = false;
        if (!SaveCapturedFramePng(capturedFrame, path))
        {
            LOG_ERROR_F("SEQUENCE_CAPTURE failed: status=%u format=%u width=%u height=%u path=%s",
                        static_cast<unsigned int>(capturedFrame.Status),
                        static_cast<unsigned int>(capturedFrame.Format),
                        capturedFrame.Width,
                        capturedFrame.Height,
                        path.c_str());
            m_bFailed = true;
            RequestFailureExit();
            return;
        }
        LOG_INFO_F("SEQUENCE_CAPTURE saved target=%llu frame=%llu width=%u height=%u path=%s",
                   static_cast<unsigned long long>(targetFrames),
                   static_cast<unsigned long long>(capturedFrame.FrameNumber),
                   capturedFrame.Width,
                   capturedFrame.Height,
                   path.c_str());
        ++m_NextIndex;
    }
} // namespace Game::Debug
