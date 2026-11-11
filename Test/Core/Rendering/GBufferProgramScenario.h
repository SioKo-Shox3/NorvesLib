#pragma once

#include "Rendering/FrameCaptureTypes.h"

namespace NorvesLib::Core::Rendering
{
    class RenderWorld;
}

namespace NorvesLib::Test::RenderingValidation
{
    class RenderingValidationSceneFixture;

    /** @brief 材質ごとの GBuffer プログラムの描画を画素で確かめる。 */
    class GBufferProgramScenario
    {
    public:
        bool ParseArgument(const Core::Container::String& argument, Core::Container::String& reason);
        bool IsEnabled() const;
        bool Initialize(Core::Rendering::RenderWorld& world, const RenderingValidationSceneFixture& fixture);
        void ApplyState(Core::Rendering::RenderWorld& world) const;
        bool Evaluate(const Core::Rendering::CapturedFrame& frame, Core::Container::String& reason) const;

    private:
        const RenderingValidationSceneFixture* m_Fixture = nullptr;
        bool m_bEnabled = false;
    };
}
