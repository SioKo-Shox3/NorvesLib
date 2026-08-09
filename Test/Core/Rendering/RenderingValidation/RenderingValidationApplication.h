#pragma once

#include "RenderingValidation/RenderingValidationScene.h"
#include "Application/ApplicationHandlerBase.h"
#include "Rendering/FrameCaptureTypes.h"

namespace NorvesLib::Test::RenderingValidation
{
    class RenderingValidationApplicationHandler : public Core::Application::ApplicationHandlerBase
    {
    public:
        bool OnPreInitialize(const Core::Container::VariableArray<Core::Container::String>& args) override;
        bool OnInitialize() override;
        bool ShouldAdvanceSimulation() const override;
        void OnPreRender() override;
        void OnPostRender() override;
        void OnPreShutdown() override;

    protected:
        virtual bool ParseAdditionalArgument(
            const Core::Container::String& argument,
            Core::Container::String& outFailureReason);
        virtual bool EvaluateCapturedFrame(
            const Core::Rendering::CapturedFrame& frame,
            Core::Container::String& outFailureReason) = 0;
        const RenderingValidationRunConfig& GetRunConfig() const;

    private:
        void Fail(const char* summary);

        RenderingValidationRunConfig m_RunConfig;
        RenderingValidationSceneFixture m_Fixture;
        bool m_bCaptureRequested = false;
        bool m_bExitRequested = false;
        uint64_t m_CaptureRequestRenderedFrame = 0;
    };
}
