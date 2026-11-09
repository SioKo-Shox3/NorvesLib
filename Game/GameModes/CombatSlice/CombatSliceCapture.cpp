#include "CombatSliceCapture.h"

#include "Debug/CapturedFramePng.h"
#include "Engine/Engine.h"
#include "GameMode/GameModeContext.h"
#include "Logging/LogMacros.h"
#include "Rendering/FrameCaptureTypes.h"
#include "Rendering/RenderWorld.h"

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

namespace Game::GameModes
{
    using namespace NorvesLib::Core;

    bool CombatSliceCapture::Request(GameMode::GameModeContext& ctx, const Container::String& path)
    {
        if (path.empty())
        {
            return false;
        }

        m_Path = path;
        auto& render = ctx.EngineRef.GetRenderWorld();
        Rendering::FrameCaptureRequest request;
        request.SourceKind = Rendering::FrameCaptureSourceKind::BackBuffer;
        const auto result = render.RequestFrameCapture(request);
        if (!result.IsAccepted())
        {
            m_Status = CombatSliceCaptureStatus::Failed;
            if (result.Status == Rendering::FrameCaptureRequestStatus::AlreadyPending)
            {
                NORVES_LOG_ERROR("CombatSlice", "撮影要求に失敗しました: 別の撮影が処理中です: %s", m_Path.c_str());
            }
            else
            {
                NORVES_LOG_ERROR("CombatSlice", "撮影要求に失敗しました: 描画の準備ができていません: %s", m_Path.c_str());
            }
            return false;
        }

        m_RequestId = result.RequestId;
        m_StartFrame = render.GetRenderedFrameCount();
        m_Status = CombatSliceCaptureStatus::Pending;
        return true;
    }

    CombatSliceCaptureStatus CombatSliceCapture::Poll(GameMode::GameModeContext& ctx)
    {
        if (m_Status != CombatSliceCaptureStatus::Pending)
        {
            return m_Status;
        }

        auto& render = ctx.EngineRef.GetRenderWorld();
        Rendering::CapturedFrame frame;
        if (render.TryConsumeCapturedFrame(frame))
        {
            if (frame.RequestId != m_RequestId)
            {
                NORVES_LOG_ERROR("CombatSlice", "撮影結果の要求 ID が一致しません: 期待=%llu 実際=%llu",
                                 static_cast<unsigned long long>(m_RequestId),
                                 static_cast<unsigned long long>(frame.RequestId));
                m_Status = CombatSliceCaptureStatus::Failed;
            }
            else if (!frame.IsSuccess())
            {
                NORVES_LOG_ERROR("CombatSlice", "撮影結果の取得に失敗しました: %s",
                                 Rendering::GetFrameCaptureResultStatusName(frame.Status));
                m_Status = CombatSliceCaptureStatus::Failed;
            }
            else if (!Game::Debug::SaveCapturedFramePng(frame, m_Path))
            {
                NORVES_LOG_ERROR("CombatSlice", "撮影結果を PNG に保存できませんでした: %s", m_Path.c_str());
                m_Status = CombatSliceCaptureStatus::Failed;
            }
            else
            {
                NORVES_LOG_INFO("CombatSlice", "撮影を保存しました: %s", m_Path.c_str());
                m_Status = CombatSliceCaptureStatus::Saved;
            }
        }
        else if (render.GetRenderedFrameCount() - m_StartFrame >= 120)
        {
            NORVES_LOG_ERROR("CombatSlice", "撮影結果の待機が 120 描画フレームに達しました: %s", m_Path.c_str());
            m_Status = CombatSliceCaptureStatus::Failed;
        }
        return m_Status;
    }

    CombatSliceCaptureStatus CombatSliceCapture::GetStatus() const
    {
        return m_Status;
    }

    const Container::String& CombatSliceCapture::GetPath() const
    {
        return m_Path;
    }

    void CombatSliceCapture::Reset()
    {
        m_Status = CombatSliceCaptureStatus::Idle;
        m_Path.clear();
        m_RequestId = 0;
        m_StartFrame = 0;
    }
} // namespace Game::GameModes
