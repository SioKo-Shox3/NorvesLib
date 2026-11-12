#pragma once

#include "RenderingValidation/RenderingFloatImage.h"

namespace NorvesLib::Core::Rendering
{
    class RenderWorld;
}

namespace NorvesLib::Test::RenderingValidation
{
    class RenderingValidationSceneFixture;

    /** @brief PaleFlat の静的・スキンの画素を、状態表に沿って取得して検査する。 */
    class PaleFlatScenario
    {
    public:
        bool ParseArgument(const Core::Container::String& argument, Core::Container::String& reason);
        bool IsEnabled() const;
        bool Initialize(Core::Rendering::RenderWorld& world, const RenderingValidationSceneFixture& fixture);
        void ApplyState(Core::Rendering::RenderWorld& world);
        bool Evaluate(const Core::Rendering::CapturedFrame& frame, Core::Container::String& reason);
        void Advance();
        bool RequestFollowup(Core::Rendering::FrameCaptureRequest& request) const;

    private:
        bool EvaluateNormals(const RgbaFloatImage& image, Core::Container::String& reason);
        bool EvaluateColor(const RgbaFloatImage& image, Core::Container::String& reason) const;
        bool FindBrightRoi(uint32_t object, const float light[3], int32_t& outX, int32_t& outY) const;
        bool MeasureSun(const RgbaFloatImage& image, float outLuma[2]) const;
        bool EvaluateSun(const RgbaFloatImage& image) const;
        bool MeasureFogDifference(const RgbaFloatImage& image, float& outDifference) const;
        bool EvaluateFog(const RgbaFloatImage& image) const;
        bool EvaluateRotation(const RgbaFloatImage& image) const;
        bool IsSpherePixel(int32_t x, int32_t y, uint32_t object) const;
        bool IsSphereInterior(int32_t x, int32_t y, uint32_t object) const;

        const RenderingValidationSceneFixture* m_Fixture = nullptr;
        RgbaFloatImage m_Normals;
        uint32_t m_Stage = 0;
        uint32_t m_AppliedStage = ~0u;
        uint64_t m_StageStartFrame = 0;
        int32_t m_SkinX = 0;
        int32_t m_SkinY = 0;
        int32_t m_StaticX = 0;
        int32_t m_StaticY = 0;
        float m_SunBaseline[2] = {};
        float m_FogReferenceDifference = 0.0f;
        bool m_bSunBaselineValid = false;
        bool m_bFogReferenceValid = false;
        bool m_bEnabled = false;
        bool m_bInitialCapture = true;
        bool m_bAdvance = false;
        bool m_bStateValid = false;
        bool m_bChecksPassed = true;
    };
} // namespace NorvesLib::Test::RenderingValidation
