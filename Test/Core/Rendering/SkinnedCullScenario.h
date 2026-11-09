// 主カメラの視錐台によるスキンの絞り込みを GPU で検証する。
#pragma once

#include "RenderingValidation/RenderingValidationScene.h"
#include "Rendering/FrameCaptureTypes.h"

namespace NorvesLib::Test::RenderingValidation
{
    class SkinnedCullScenario
    {
    public:
        bool ParseArgument(const Core::Container::String& argument, Core::Container::String& reason);
        bool IsEnabled() const;
        bool Initialize(const RenderingValidationSceneFixture& fixture, Core::Rendering::RenderWorld& world,
                        SceneKind scene, Core::Rendering::FrameCaptureSourceKind source);
        void ApplyState(const RenderingValidationSceneFixture& fixture, Core::Rendering::RenderWorld& world);
        bool Evaluate(const Core::Rendering::CapturedFrame& frame, Core::Rendering::RenderWorld& world,
                      uint64_t requestId, Core::Container::String& reason);
        void Advance();
        bool RequestFollowup(Core::Rendering::FrameCaptureRequest& request) const;

    private:
        static constexpr uint32_t CaptureCountPerStage = 3u;
        static constexpr uint32_t StageCount = 4u;

        bool m_bEnabled = false;
        bool m_bStageApplied = false;
        bool m_bApplyFailed = false;
        uint32_t m_Stage = 0u;
        uint32_t m_CaptureInStage = 0u;
        uint32_t m_RecordedDraws[StageCount]{};
        uint64_t m_StageStartSequence = 0u;
        uint64_t m_LastFrameNumber = 0u;
        Core::Rendering::CapturedFrame m_BehindFrame;
        Core::Rendering::CapturedFrame m_FrontFrame;
    };
} // namespace NorvesLib::Test::RenderingValidation
