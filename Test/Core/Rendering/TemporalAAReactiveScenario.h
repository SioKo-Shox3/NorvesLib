#pragma once

#include "Rendering/FrameCaptureTypes.h"
#include "Rendering/SceneProxy.h"

namespace NorvesLib::Core::Rendering
{
    class RenderWorld;
}

namespace NorvesLib::Test::RenderingValidation
{
    /** @brief TAA のリアクティブ入力を連続フレームの HDR 値で検証する。 */
    class TemporalAAReactiveScenario
    {
    public:
        TemporalAAReactiveScenario();
        ~TemporalAAReactiveScenario();
        bool ParseArgument(const Core::Container::String& argument, Core::Container::String& reason);
        bool IsEnabled() const;
        bool Initialize(Core::Rendering::RenderWorld& world, bool bOutdoor,
                        Core::Rendering::FrameCaptureSourceKind source);
        void ApplyState(Core::Rendering::RenderWorld& world, const Core::Rendering::CameraProxy& baseCamera);
        bool Evaluate(const Core::Rendering::CapturedFrame& frame, Core::Container::String& reason);
        void Advance();
        bool RequestFollowup(Core::Rendering::FrameCaptureRequest& request) const;

    private:
        struct State;
        Core::Container::TUniquePtr<State> m_State;
        bool m_bEnabled = false;
        bool m_bAdvance = false;
        uint32_t m_Phase = 0;
        uint32_t m_StartedPhase = 0;
    };
} // namespace NorvesLib::Test::RenderingValidation
