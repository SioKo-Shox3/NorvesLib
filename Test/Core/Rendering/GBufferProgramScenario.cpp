#include "GBufferProgramScenario.h"

#include "RenderingValidation/GpuTestEnvironment.h"
#include "RenderingValidation/RenderingFloatImage.h"
#include "RenderingValidation/RenderingValidationScene.h"
#include "Logging/LogMacros.h"
#include "Rendering/RenderWorld.h"
#include "Rendering/SkyAtmosphere.h"
#include "Rendering/VolumetricFog.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace NorvesLib::Test::RenderingValidation
{
    namespace
    {
        constexpr char ProgramName[] = "Test.FixedColor";
        constexpr float FixedEmissive[4] = {0.125f, 0.375f, 0.75f, 0.625f};
        constexpr float StandardEmissive[4] = {0.0f, 0.0f, 0.0f, 0.25f};
        constexpr float Tolerance = 0.001f;
    }

    bool GBufferProgramScenario::ParseArgument(const Core::Container::String& argument,
                                                Core::Container::String& reason)
    {
        if (m_bEnabled || argument != TEXT("--gbuffer-program-scenario=fixed"))
        {
            reason = TEXT("GBuffer プログラムのシナリオが重複しているか値が不正です");
            return false;
        }
        m_bEnabled = true;
        return true;
    }

    bool GBufferProgramScenario::IsEnabled() const
    {
        return m_bEnabled;
    }

    bool GBufferProgramScenario::Initialize(Core::Rendering::RenderWorld& world,
                                             const RenderingValidationSceneFixture& fixture)
    {
        Core::Rendering::GBufferProgramDesc desc;
        desc.Name = ProgramName;
        desc.FragmentShader = NORVES_SOURCE_ROOT "/Test/Core/Rendering/Shaders/gbuffer_program_fixed.frag";
        if (!world.RegisterGBufferProgram(desc) || !fixture.ApplyGBufferProgramFixture(ProgramName))
        {
            LOG_ERROR("GBuffer プログラムの試験用シーンを準備できませんでした");
            return false;
        }
        m_Fixture = &fixture;
        return true;
    }

    void GBufferProgramScenario::ApplyState(Core::Rendering::RenderWorld& world) const
    {
        world.SetMainCamera(m_Fixture->GetShadingModelGBufferCamera());
        auto sky = Core::Rendering::MakeDefaultSkyAtmosphereParameters();
        sky.bEnabled = false;
        world.SetSkyAtmosphere(sky);
        auto fog = Core::Rendering::MakeDefaultVolumetricFogParameters();
        fog.bEnabled = false;
        world.SetVolumetricFogParameters(fog);
    }

    bool GBufferProgramScenario::Evaluate(const Core::Rendering::CapturedFrame& frame,
                                           Core::Container::String& reason) const
    {
        RgbaFloatImage image;
        if (!m_Fixture || frame.Format != NorvesLib::RHI::Format::R16G16B16A16_FLOAT ||
            DecodeCapturedRgba16Float(frame, image) != FloatImageStatus::Success ||
            image.Width != ValidationWidth || image.Height != ValidationHeight)
        {
            reason = TEXT("GBuffer プログラムの発光画像の形式または寸法が不正です");
            return false;
        }

        struct Object
        {
            const char* Name;
            float X;
            const float* Expected;
        };
        const Object objects[] = {
            {"program-a", -2.0f, FixedEmissive},
            {"program-b", 0.0f, FixedEmissive},
            {"standard", 2.0f, StandardEmissive}
        };
        const auto camera = m_Fixture->GetShadingModelGBufferCamera();
        const float focalPixels = static_cast<float>(image.Height) * 0.5f /
            std::tan(camera.FieldOfView * 3.14159265358979323846f / 360.0f);
        bool bPassed = true;
        for (const auto& object : objects)
        {
            const int32_t centerX = static_cast<int32_t>(std::lround(
                static_cast<float>(image.Width) * 0.5f + focalPixels * object.X / camera.PositionZ));
            const int32_t centerY = static_cast<int32_t>(std::lround(
                static_cast<float>(image.Height) * 0.5f - focalPixels * 1.5f / camera.PositionZ));
            float minimum[4] = {
                std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(),
                std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity()};
            float maximum[4] = {
                -std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
                -std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()};
            bool bObjectPassed = true;
            for (int32_t y = centerY - 2; y <= centerY + 2; ++y)
            {
                for (int32_t x = centerX - 2; x <= centerX + 2; ++x)
                {
                    if (x < 0 || y < 0 || x >= static_cast<int32_t>(image.Width) ||
                        y >= static_cast<int32_t>(image.Height))
                    {
                        reason = TEXT("GBuffer プログラムの検査領域が画像の範囲外です");
                        return false;
                    }
                    const size_t offset = (static_cast<size_t>(y) * image.Width + x) * 4u;
                    for (uint32_t channel = 0; channel < 4u; ++channel)
                    {
                        const float actual = image.Values[offset + channel];
                        bObjectPassed = bObjectPassed && std::isfinite(actual) &&
                            std::abs(actual - object.Expected[channel]) <= Tolerance;
                        minimum[channel] = std::min(minimum[channel], actual);
                        maximum[channel] = std::max(maximum[channel], actual);
                    }
                }
            }
            LOG_INFO("GBufferProgramPixel object=%s roi=(%d,%d) pixels=25 expected=(%g,%g,%g,%g) min=(%g,%g,%g,%g) max=(%g,%g,%g,%g) result=%s",
                     object.Name, centerX, centerY,
                     object.Expected[0], object.Expected[1], object.Expected[2], object.Expected[3],
                     minimum[0], minimum[1], minimum[2], minimum[3],
                     maximum[0], maximum[1], maximum[2], maximum[3],
                     bObjectPassed ? "PASS" : "FAIL");
            bPassed = bPassed && bObjectPassed;
        }
        if (!bPassed)
        {
            reason = TEXT("GBuffer プログラムまたは標準材質の発光値が期待値と一致しません");
        }
        return bPassed;
    }
}
