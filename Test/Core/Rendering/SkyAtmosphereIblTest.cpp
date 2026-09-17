#include "Rendering/SceneProxy.h"
#include "Rendering/SkyAtmosphere.h"

#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

using namespace NorvesLib::Core::Rendering;

namespace
{
    int g_FailureCount = 0;

    void Expect(bool condition, const char* message)
    {
        if (!condition)
        {
            ++g_FailureCount;
            std::cout << "FAILED: " << message << "\n";
        }
    }

    bool IsFiniteVector(const NorvesLib::Math::Vector3& value)
    {
        return std::isfinite(value.x) &&
               std::isfinite(value.y) &&
               std::isfinite(value.z);
    }

    std::string ReadSource(const std::string& relativePath)
    {
#ifndef NORVES_SOURCE_DIR
#error NORVES_SOURCE_DIR must be defined for SkyAtmosphereIblTest.
#endif
        std::ifstream input(std::string(NORVES_SOURCE_DIR) + "/" + relativePath,
                            std::ios::binary);
        if (!input)
        {
            return {};
        }
        return std::string(std::istreambuf_iterator<char>(input),
                           std::istreambuf_iterator<char>());
    }

    void TestSkyEvRangeAndSunAltitudeMutation()
    {
        struct EvCase
        {
            float altitude;
            float azimuth;
            float preExposure;
        };

        const EvCase cases[] = {
            {8.0f, -35.0f, 1.0f / (1.2f * 16384.0f)},
            {65.0f, 20.0f, 1.0f / (1.2f * 32768.0f)},
            {22.0f, 145.0f, 1.0f / (1.2f * 32768.0f)}};

        SkyAtmosphereParameters previousParameters = MakeDefaultSkyAtmosphereParameters();
        previousParameters.bEnabled = true;
        previousParameters.SunAltitudeDegrees = cases[0].altitude;
        previousParameters.SunAzimuthDegrees = cases[0].azimuth;
        const SkyRadianceSample previousZenith = EvaluateHillaireSkyReference(
            previousParameters, NorvesLib::Math::Vector3::UnitY);

        for (const EvCase& evCase : cases)
        {
            SkyAtmosphereParameters parameters = MakeDefaultSkyAtmosphereParameters();
            parameters.bEnabled = true;
            parameters.SunAltitudeDegrees = evCase.altitude;
            parameters.SunAzimuthDegrees = evCase.azimuth;

            const SkyRadianceSample zenith = EvaluateHillaireSkyReference(
                parameters, NorvesLib::Math::Vector3::UnitY);
            const NorvesLib::Math::Vector3 sunDirection =
                MakeSunDirectionFromAltitudeAzimuth(evCase.altitude, evCase.azimuth);
            const SkyRadianceSample sunView = EvaluateHillaireSkyReference(
                parameters, sunDirection);
            const float directSun = ComputeSunDiskPreExposedLuminance(
                parameters, evCase.preExposure);

            Expect(zenith.bValid && sunView.bValid,
                   "morning/day/evening sky radiance samples remain valid");
            Expect(IsFiniteVector(zenith.Radiance) && IsFiniteVector(sunView.Radiance),
                   "sky radiance samples remain finite across the EV range");
            Expect(std::isfinite(zenith.MeanSunTransmittance) &&
                       std::isfinite(sunView.MeanSunTransmittance),
                   "sky transmittance remains finite across the EV range");
            Expect(std::isfinite(directSun) && directSun >= 0.0f,
                   "pre-exposed direct sun remains finite across the EV range");
        }

        SkyAtmosphereParameters noonParameters = MakeDefaultSkyAtmosphereParameters();
        noonParameters.bEnabled = true;
        noonParameters.SunAltitudeDegrees = cases[1].altitude;
        noonParameters.SunAzimuthDegrees = cases[1].azimuth;
        const SkyRadianceSample noonZenith = EvaluateHillaireSkyReference(
            noonParameters, NorvesLib::Math::Vector3::UnitY);
        Expect(std::abs(noonZenith.Radiance.y - previousZenith.Radiance.y) > 1.0e-3f,
               "changing sun altitude changes the equirectangular sky source");

        SceneProxy scene;
        scene.SkyAtmosphere.bEnabled = true;
        scene.Clear();
        Expect(!scene.SkyAtmosphere.bEnabled,
               "clearing a scene removes the previous sky snapshot");
    }

    void TestDynamicIblSourceContract()
    {
        const std::string lightingSource =
            ReadSource("Library/Core/Private/Rendering/LightingPass.cpp");
        const std::string skyPassSource =
            ReadSource("Library/Core/Private/Rendering/SkyAtmospherePass.cpp");

        Expect(lightingSource.find("BuildSkyAtmosphereRadianceSource") != std::string::npos,
               "LightingPass builds the sky equirectangular source");
        Expect(lightingSource.find("CreateIblResources(m_Device") != std::string::npos,
               "sky IBL uses the existing radiometric IBL generator");
        Expect(lightingSource.find("m_SkyAtmosphereDiffuseIrradianceTexture") !=
                   std::string::npos,
               "dynamic diffuse irradiance is retained for the frame");
        Expect(lightingSource.find("m_SkyAtmospherePrefilteredSpecularTexture") !=
                   std::string::npos,
               "dynamic prefiltered specular is retained for the frame");
        Expect(lightingSource.find("AreSkyAtmosphereParametersEqual") !=
                   std::string::npos,
               "dynamic IBL cache is keyed by the sky snapshot");
        Expect(lightingSource.find("bSkyAtmosphereRequested ? m_DefaultBlackTexture") !=
                   std::string::npos,
               "failed sky generation does not fall back to static HDR implicitly");
        Expect(skyPassSource.find("m_RadianceTexture->Update") != std::string::npos,
               "SkyAtmospherePass publishes the equirectangular radiance texture");
        Expect(skyPassSource.find("FloatToHalfRne") != std::string::npos,
               "sky radiance storage is clamped to finite FP16 values");
    }
} // namespace

int main()
{
    TestSkyEvRangeAndSunAltitudeMutation();
    TestDynamicIblSourceContract();

    if (g_FailureCount != 0)
    {
        std::cout << "SkyAtmosphereIblTest failures: " << g_FailureCount << "\n";
        return 1;
    }

    std::cout << "SkyAtmosphereIblTest: PASS\n";
    return 0;
}
