#include "Rendering/SkyAtmosphere.h"

#include "Math/VectorUtils.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

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

    bool NearlyEqual(float lhs, float rhs, float epsilon)
    {
        return std::abs(lhs - rhs) <= epsilon;
    }

    bool RelativeNearlyEqual(float lhs, float rhs, float relativeError)
    {
        const float scale = std::max(1.0f, std::abs(rhs));
        return std::abs(lhs - rhs) <= scale * relativeError;
    }

    void ExpectFiniteVector(const NorvesLib::Math::Vector3& value, const char* message)
    {
        Expect(std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z),
               message);
    }

    void TestSunDirectionCoordinateContract()
    {
        const NorvesLib::Math::Vector3 horizon =
            MakeSunDirectionFromAltitudeAzimuth(0.0f, 0.0f);
        Expect(NearlyEqual(horizon.x, 1.0f, 1.0e-5f) &&
                   NearlyEqual(horizon.y, 0.0f, 1.0e-5f) &&
                   NearlyEqual(horizon.z, 0.0f, 1.0e-5f),
               "zero altitude and azimuth point along +X");

        const NorvesLib::Math::Vector3 zenith =
            MakeSunDirectionFromAltitudeAzimuth(90.0f, 135.0f);
        Expect(NearlyEqual(zenith.x, 0.0f, 1.0e-5f) &&
                   NearlyEqual(zenith.y, 1.0f, 1.0e-5f) &&
                   NearlyEqual(zenith.z, 0.0f, 1.0e-5f),
               "ninety degree altitude points along +Y");

        const NorvesLib::Math::Vector3 diagonal =
            MakeSunDirectionFromAltitudeAzimuth(45.0f, 90.0f);
        const float diagonalComponent = std::sqrt(0.5f);
        Expect(NearlyEqual(diagonal.x, 0.0f, 1.0e-5f) &&
                   NearlyEqual(diagonal.y, diagonalComponent, 1.0e-5f) &&
                   NearlyEqual(diagonal.z, diagonalComponent, 1.0e-5f),
               "altitude and azimuth use the horizontal XZ plane");
        Expect(NearlyEqual(NorvesLib::Math::VectorUtils::Length(diagonal), 1.0f, 1.0e-5f),
               "sun direction is normalized");
    }

    void TestSanitizationKeepsSnapshotFinite()
    {
        SkyAtmosphereParameters invalid = MakeDefaultSkyAtmosphereParameters();
        invalid.bEnabled = true;
        invalid.SunAltitudeDegrees = std::numeric_limits<float>::quiet_NaN();
        invalid.SunAzimuthDegrees = std::numeric_limits<float>::infinity();
        invalid.SunLuminanceNits = -1.0f;
        invalid.PlanetRadiusMeters = std::numeric_limits<float>::infinity();
        invalid.AtmosphereHeightMeters = -1.0f;
        invalid.RayleighScaleHeightMeters = std::numeric_limits<float>::quiet_NaN();
        invalid.MieAnisotropy = 3.0f;
        invalid.GroundAlbedo = NorvesLib::Math::Vector3(
            -1.0f, 2.0f, std::numeric_limits<float>::quiet_NaN());

        const SkyAtmosphereParameters sanitized =
            SanitizeSkyAtmosphereParameters(invalid);
        Expect(sanitized.bEnabled, "sanitization preserves enabled state");
        Expect(sanitized.SunAltitudeDegrees == 45.0f,
               "non-finite altitude uses the default");
        Expect(sanitized.SunAzimuthDegrees == 0.0f,
               "non-finite azimuth uses the default");
        Expect(sanitized.SunLuminanceNits == 1.6e9f,
               "non-positive luminance uses the default");
        Expect(sanitized.MieAnisotropy == 0.9f,
               "anisotropy is clamped to the phase-function domain");
        Expect(sanitized.GroundAlbedo.x == 0.0f &&
                   sanitized.GroundAlbedo.y == 1.0f &&
                   sanitized.GroundAlbedo.z == 0.1f,
               "ground albedo is finite and clamped");
        ExpectFiniteVector(MakeSunDirectionFromAltitudeAzimuth(
                               invalid.SunAltitudeDegrees,
                               invalid.SunAzimuthDegrees),
                           "sanitized sun direction is finite");
    }

    void TestReferenceSamples()
    {
        SkyAtmosphereParameters parameters = MakeDefaultSkyAtmosphereParameters();
        parameters.bEnabled = true;
        parameters.SunAltitudeDegrees = 45.0f;

        const SkyRadianceSample zenith = EvaluateHillaireSkyReference(
            parameters, NorvesLib::Math::Vector3::UnitY);
        const NorvesLib::Math::Vector3 sunDirection =
            MakeSunDirectionFromAltitudeAzimuth(45.0f, 0.0f);
        const SkyRadianceSample nearSun = EvaluateHillaireSkyReference(
            parameters, sunDirection);

        Expect(zenith.bValid && nearSun.bValid,
               "enabled reference samples are valid");
        ExpectFiniteVector(zenith.Radiance, "zenith radiance is finite");
        ExpectFiniteVector(nearSun.Radiance, "sun-near radiance is finite");
        Expect(zenith.MeanSunTransmittance > 0.0f &&
                   zenith.MeanSunTransmittance < 1.0f,
               "zenith sample exposes a physical transmittance range");
        Expect(nearSun.Radiance.x > zenith.Radiance.x &&
                   nearSun.Radiance.y > zenith.Radiance.y &&
                   nearSun.Radiance.z > zenith.Radiance.z,
               "sun-near radiance exceeds the zenith reference");

        // These values are the frozen R2-P1 CPU reference from the documented
        // default atmosphere parameters.
        Expect(RelativeNearlyEqual(zenith.Radiance.x, 6747700.0f, 0.01f),
               "zenith red reference remains within one percent");
        Expect(RelativeNearlyEqual(zenith.Radiance.y, 13753100.0f, 0.01f),
               "zenith green reference remains within one percent");
        Expect(RelativeNearlyEqual(zenith.Radiance.z, 26319800.0f, 0.01f),
               "zenith blue reference remains within one percent");
        Expect(RelativeNearlyEqual(nearSun.Radiance.x, 47801000.0f, 0.01f),
               "sun-near red reference remains within one percent");
        Expect(RelativeNearlyEqual(nearSun.Radiance.y, 58063800.0f, 0.01f),
               "sun-near green reference remains within one percent");
        Expect(RelativeNearlyEqual(nearSun.Radiance.z, 75386300.0f, 0.01f),
               "sun-near blue reference remains within one percent");
    }

    void TestSunDiskPreExposureContract()
    {
        SkyAtmosphereParameters parameters = MakeDefaultSkyAtmosphereParameters();
        parameters.bEnabled = true;
        const float ev15PreExposure = 1.0f / 32768.0f;
        const float ev14PreExposure = 1.0f / 16384.0f;

        Expect(NearlyEqual(ComputeSunDiskPreExposedLuminance(
                               parameters, ev15PreExposure),
                           48828.125f,
                           0.5f),
               "EV15 sun disk luminance is represented without FP16 saturation");
        Expect(!IsSunDiskWithinFp16SafetyRange(parameters, ev14PreExposure),
               "EV14 identifies the nominal solar disk as outside the FP16 safety range");
        Expect(IsSunDiskWithinFp16SafetyRange(parameters, ev15PreExposure),
               "EV15 is inside the FP16 safety range");
        Expect(ComputeSunDiskPreExposedLuminance(parameters, 0.0f) == 0.0f,
               "non-positive pre-exposure disables the disk value");
    }
} // namespace

int main()
{
    TestSunDirectionCoordinateContract();
    TestSanitizationKeepsSnapshotFinite();
    TestReferenceSamples();
    TestSunDiskPreExposureContract();

    if (g_FailureCount != 0)
    {
        std::cout << "SkyAtmosphereModelTest failures: " << g_FailureCount << "\n";
        return 1;
    }

    std::cout << "SkyAtmosphereModelTest: PASS\n";
    return 0;
}
