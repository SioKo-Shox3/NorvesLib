#pragma once

#include "Rendering/FrameCaptureTypes.h"
#include "RenderingValidation/RenderingValidationScene.h"

namespace NorvesLib::Test::RenderingValidation
{
    /** @brief 静的・スキンの板が床へ落とす切り抜きの影を比較する。 */
    class AlphaClipShadowScenario
    {
    public:
        bool ParseArgument(const Core::Container::String& argument, Core::Container::String& reason);
        bool ParseMethodArgument(const Core::Container::String& argument, Core::Container::String& reason);
        bool IsEnabled() const;
        bool HasValidMethods() const;
        bool Initialize(Core::Rendering::RenderWorld& world, const RenderingValidationSceneFixture& fixture);
        void ApplyState(Core::Rendering::RenderWorld& world);
        bool Evaluate(const Core::Rendering::CapturedFrame& frame, Core::Container::String& reason) const;

    private:
        bool LoadThresholds();
        const char* MethodName() const;

        Core::Rendering::CameraProxy m_Camera;
        bool m_bEnabled = false;
        bool m_bPoint = false;
        bool m_bSunMethodSpecified = false;
        bool m_bSunVsm = false;
        bool m_bPointMethodSpecified = false;
        bool m_bPointVsm = false;
        double m_InnerFraction = 0.0;
        double m_OpaqueMinimum = 0.0;
        double m_MaskedMinimum = 0.0;
        double m_MaskedMaximum = 0.0;
        double m_MinimumDifference = 0.0;
        double m_MinimumLightRatio = 0.0;
    };
}
