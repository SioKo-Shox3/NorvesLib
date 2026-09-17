#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/SkyAtmosphere.h"
#include "Rendering/SkyAtmospherePass.h"
#include "Rendering/ViewRenderContext.h"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

using namespace NorvesLib::Core::Rendering;

namespace
{
#ifndef NORVES_SOURCE_DIR
#error NORVES_SOURCE_DIR must be defined for SkyAtmospherePassContractTest.
#endif

    std::string ReadSource(const char* relativePath)
    {
        std::ifstream file(std::filesystem::path(NORVES_SOURCE_DIR) / relativePath,
                           std::ios::binary);
        assert(file.is_open());
        return std::string((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    }

    void AssertContains(const std::string& source, const char* expected)
    {
        assert(source.find(expected) != std::string::npos);
    }

    void TestNamedResourceContract()
    {
        assert(RenderGraphResourceNames::SkyAtmosphereTransmittance.IsValid());
        assert(RenderGraphResourceNames::SkyAtmosphereRadiance.IsValid());
        assert(RenderGraphResourceNames::SkyAtmosphereSunDisk.IsValid());
        assert(RenderGraphResourceNames::SkyAtmosphereTransmittance !=
               RenderGraphResourceNames::SkyAtmosphereRadiance);
        assert(RenderGraphResourceNames::SkyAtmosphereRadiance !=
               RenderGraphResourceNames::SkyAtmosphereSunDisk);

        SkyAtmospherePass pass;
        assert(std::string(pass.GetName()) == "SkyAtmospherePass");
        assert(pass.GetTransmittanceHandle().IsValid() == false);
        assert(pass.GetRadianceHandle().IsValid() == false);
        assert(pass.GetSunDiskHandle().IsValid() == false);

        SkyAtmosphereRenderResources resources;
        resources.Reset();
        assert(!resources.bSnapshotEnabled);
        assert(!resources.bValid);
        assert(resources.SunDiskPreExposedLuminance == 0.0f);
    }

    void TestSourceWiringContract()
    {
        const std::string passSource =
            ReadSource("Library/Core/Private/Rendering/SkyAtmospherePass.cpp");
        const std::string lightingSource =
            ReadSource("Library/Core/Private/Rendering/LightingPass.cpp");
        const std::string sceneViewSource =
            ReadSource("Library/Core/Private/Rendering/SceneView.cpp");
        const std::string renderingCoordinatorSource =
            ReadSource("Library/Core/Private/Rendering/RenderingCoordinator.cpp");
        const std::string lightingShader = ReadSource("Assets/Shaders/lighting.frag");
        const std::string skyShader = ReadSource("Assets/Shaders/sky_atmosphere.frag");

        AssertContains(passSource, "SanitizeSkyAtmosphereParameters");
        AssertContains(passSource, "EvaluateHillaireSkyReference");
        AssertContains(passSource, "ComputeSunDiskPreExposedLuminance");
        AssertContains(passSource, "SunDiskSafetyFraction");
        AssertContains(passSource, "ImportTexture");
        AssertContains(passSource, "PublishTexture(RenderGraphResourceNames::SkyAtmosphereTransmittance");
        AssertContains(passSource, "PublishTexture(RenderGraphResourceNames::SkyAtmosphereRadiance");
        AssertContains(passSource, "PublishTexture(RenderGraphResourceNames::SkyAtmosphereSunDisk");
        AssertContains(passSource, "builder.PreserveInsertionOrder");

        AssertContains(lightingSource,
                       "TryReadTexture(RenderGraphResourceNames::SkyAtmosphereTransmittance");
        AssertContains(lightingSource,
                       "TryReadTexture(RenderGraphResourceNames::SkyAtmosphereRadiance");
        AssertContains(lightingSource,
                       "TryReadTexture(RenderGraphResourceNames::SkyAtmosphereSunDisk");
        AssertContains(lightingSource, "bSkyAtmosphereRequested");
        AssertContains(lightingSource, "bSkyAtmosphereAvailable");
        AssertContains(lightingSource, "bSkyAtmosphereRequested ? m_DefaultBlackTexture");
        AssertContains(lightingSource, "BindTexture(14");
        AssertContains(lightingSource, "BindTexture(15");
        AssertContains(renderingCoordinatorSource,
                       "viewContext.SnapshotScene = &packet->Scene;");
        AssertContains(renderingCoordinatorSource,
                       "viewContext.SkyAtmosphereSnapshot = packet->Scene.SkyAtmosphere;");

        const std::size_t skyPassPosition = sceneViewSource.find(
            "auto skyAtmospherePass = MakeUnique<SkyAtmospherePass>()");
        const std::size_t lightingPosition = sceneViewSource.find(
            "LightingPassSettings lightingSettings;");
        assert(skyPassPosition != std::string::npos);
        assert(lightingPosition != std::string::npos);
        assert(skyPassPosition < lightingPosition);

        AssertContains(lightingShader,
                       "layout(set = 0, binding = 14) uniform sampler2D skySunDisk");
        AssertContains(lightingShader,
                       "layout(set = 0, binding = 15) uniform sampler2D skyTransmittance");
        AssertContains(lightingShader, "skySunDirectionAndCosRadius");
        AssertContains(lightingShader, "ApplySceneColorPreExposure(skyColor)");
        AssertContains(lightingShader, "sunDiskSample.rgb * sunDiskMask");
        AssertContains(lightingShader, "step(params.skySunDirectionAndCosRadius.w");
        AssertContains(lightingShader, "if (sunDiskSample.a > 0.5)");
        AssertContains(lightingShader, "textureLod(skyTransmittance");
        assert(lightingShader.find("skySample.a") == std::string::npos);

        AssertContains(skyShader, "#version 450");
        AssertContains(skyShader, "transmittanceLut");
        AssertContains(skyShader, "skyRadianceLut");
        AssertContains(skyShader, "sunDiskPreExposed");
    }

    void TestSunDiskFiniteSafetyContract()
    {
        SkyAtmosphereParameters parameters = MakeDefaultSkyAtmosphereParameters();
        parameters.bEnabled = true;
        const float ev15PreExposure = 1.0f / (1.2f * 32768.0f);
        const float ev14PreExposure = 1.0f / (1.2f * 16384.0f);
        const float ev15 = ComputeSunDiskPreExposedLuminance(parameters,
                                                             ev15PreExposure);
        const float ev14 = ComputeSunDiskPreExposedLuminance(parameters,
                                                             ev14PreExposure);
        assert(std::isfinite(ev15));
        assert(std::isfinite(ev14));
        assert(IsSunDiskWithinFp16SafetyRange(parameters, ev15PreExposure));
        assert(!IsSunDiskWithinFp16SafetyRange(parameters, ev14PreExposure));
        assert(ev15 < 65504.0f * 0.9f);
    }
}

int main()
{
    TestNamedResourceContract();
    TestSourceWiringContract();
    TestSunDiskFiniteSafetyContract();
    std::cout << "SkyAtmospherePassContractTest: PASS\n";
    return 0;
}
