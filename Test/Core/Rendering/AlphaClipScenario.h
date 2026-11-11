#pragma once

#include "Rendering/FrameCaptureTypes.h"
#include "RenderingValidation/RenderingValidationScene.h"

namespace NorvesLib::Test::RenderingValidation
{
    /** @brief 切り抜きの被覆と両面の照明を実際の GBuffer と表示画像で確かめる。 */
    class AlphaClipScenario
    {
    public:
        AlphaClipScenario();
        ~AlphaClipScenario();
        bool ParseArgument(const Core::Container::String& argument, Core::Container::String& reason);
        bool IsEnabled() const;
        Core::Rendering::FrameCaptureSourceKind GetCaptureSource() const;
        bool Initialize(Core::Rendering::RenderWorld& world, const RenderingValidationSceneFixture& fixture);
        void ApplyState(Core::Rendering::RenderWorld& world);
        bool Evaluate(const Core::Rendering::CapturedFrame& frame, Core::Container::String& reason);
        void Advance();
        bool RequestFollowup(Core::Rendering::FrameCaptureRequest& request) const;

    private:
        struct State;
        bool LoadThresholds();
        bool IsHash() const;
        bool EvaluateHash(Core::Container::String& reason);
        Core::Container::TUniquePtr<State> m_State;
        const RenderingValidationSceneFixture* m_Fixture = nullptr;
        AlphaClipFixtureKind m_Kind = AlphaClipFixtureKind::Opaque;
        bool m_bEnabled = false;
        double m_CoverageTarget = 0.0;
        double m_CoverageTolerance = 0.0;
        double m_EmissiveTolerance = 0.0;
        double m_MaximumLumaDelta = 0.0;
        double m_MinimumBackgroundLumaGain = 0.0;
        double m_MinimumTexelPixels = 0.0;
        double m_HashCoverageTolerance = 0.0;
        double m_HashMinimumChangedFraction = 0.0;
        double m_HashFrameCount = 0.0;
        uint32_t m_HashPhase = 0u;
        bool m_bHashEpochStarted = false;
        bool m_bHashComplete = false;
    };
} // namespace NorvesLib::Test::RenderingValidation
