#include "Debug/CaptureSettle.h"

#include "Core/Public/Engine/Engine.h"
#include "Core/Public/Engine/NorvesEngine.h"
#include "Core/Public/Engine/SkeletalAssetSession.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Rendering/RenderWorld.h"

namespace Game::Debug
{
    void CaptureSettle::Reset(uint64_t settledRenderedFrames)
    {
        m_SettledRenderedFrames = settledRenderedFrames;
        m_bObservedPendingAssets = false;
        m_bBaselineLatched = false;
        m_BaselineRenderedFrame = 0;
        m_SettleUntilRenderedFrame = 0;
    }

    void CaptureSettle::RestartAfterMove(uint64_t renderedFrameCount, uint64_t waitRenderedFrames)
    {
        m_bObservedPendingAssets = true;
        m_bBaselineLatched = false;
        m_SettleUntilRenderedFrame = renderedFrameCount + waitRenderedFrames;
    }

    bool CaptureSettle::IsLoading(NorvesLib::Core::Rendering::RenderWorld& renderWorld, bool bOtherPending,
                                  uint64_t renderedFrameCount) const
    {
        // ApplicationProcessor の --capture-png と同じ読み込み中の判定に、移動後の待ちを足す。
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

    void CaptureSettle::OnPreRender(NorvesLib::Core::Rendering::RenderWorld& renderWorld, bool bOtherPending)
    {
        // ApplicationProcessor と同じく、描画の前にも読み込み中を見て、基準を数え直す。
        if (IsLoading(renderWorld, bOtherPending, renderWorld.GetRenderedFrameCount()))
        {
            m_bObservedPendingAssets = true;
            m_bBaselineLatched = false;
        }
    }

    bool CaptureSettle::OnPostRender(NorvesLib::Core::Rendering::RenderWorld& renderWorld, bool bOtherPending,
                                     uint64_t renderedFrameCount, const char* logTag, const char* itemName)
    {
        // 読み込み中を一度見た後、読み込み中でない最初の描画を基準にし、エポックを始め直す。
        if (IsLoading(renderWorld, bOtherPending, renderedFrameCount))
        {
            m_bObservedPendingAssets = true;
            m_bBaselineLatched = false;
            return false;
        }
        if (!m_bObservedPendingAssets)
        {
            return false;
        }
        if (!m_bBaselineLatched)
        {
            m_BaselineRenderedFrame = renderedFrameCount;
            m_bBaselineLatched = true;
            LOG_INFO_F("%s view=%s settle baseline rendered=%llu", logTag, itemName,
                       static_cast<unsigned long long>(m_BaselineRenderedFrame));
            if (NorvesLib::Core::Engine::GEngine &&
                NorvesLib::Core::Engine::GEngine->GetDeterministicCapture().IsEnabled())
            {
                // 次のフレームを 0 番として、時間・TAA・RTGI・自動露出を数え直す
                NorvesLib::Core::Engine::GEngine->GetDeterministicCapture().BeginEpoch();
                renderWorld.BeginDeterministicEpoch();
            }
            return false;
        }
        if (renderedFrameCount < m_BaselineRenderedFrame)
        {
            m_BaselineRenderedFrame = renderedFrameCount;
            return false;
        }
        return renderedFrameCount - m_BaselineRenderedFrame >= m_SettledRenderedFrames;
    }
} // namespace Game::Debug
