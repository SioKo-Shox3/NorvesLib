#include "Rendering/SkyAtmosphere.h"
#include "Rendering/SkySunLight.h"

#include "Math/VectorUtils.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <type_traits>

using namespace NorvesLib::Core::Rendering;

static_assert(std::is_trivially_copyable_v<SkyAtmosphereParameters>);
static_assert(std::is_standard_layout_v<SkyAtmosphereParameters>);

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
        Expect(sanitized.PlanetRadiusMeters == 6360000.0f &&
                   sanitized.AtmosphereHeightMeters == 80000.0f &&
                   sanitized.RayleighScaleHeightMeters == 8000.0f &&
                   sanitized.MieScaleHeightMeters == 1200.0f,
               "atmosphere dimensions and density heights use finite defaults");
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
        Expect(RelativeNearlyEqual(zenith.Radiance.x, 463.6f, 0.01f),
               "zenith red reference remains within one percent");
        Expect(RelativeNearlyEqual(zenith.Radiance.y, 944.8f, 0.01f),
               "zenith green reference remains within one percent");
        Expect(RelativeNearlyEqual(zenith.Radiance.z, 1808.2f, 0.01f),
               "zenith blue reference remains within one percent");
        Expect(RelativeNearlyEqual(nearSun.Radiance.x, 3283.9f, 0.01f),
               "sun-near red reference remains within one percent");
        Expect(RelativeNearlyEqual(nearSun.Radiance.y, 3989.0f, 0.01f),
               "sun-near green reference remains within one percent");
        Expect(RelativeNearlyEqual(nearSun.Radiance.z, 5179.0f, 0.01f),
               "sun-near blue reference remains within one percent");
    }

    void TestSunDiskPreExposureContract()
    {
        SkyAtmosphereParameters parameters = MakeDefaultSkyAtmosphereParameters();
        parameters.bEnabled = true;
        const float ev15PreExposure = 1.0f / 32768.0f;
        const float ev14PreExposure = 1.0f / 16384.0f;

        Expect(NearlyEqual(ComputeSunDiskIrradiance(parameters), 109920.0f, 0.5f),
               "solar disk radiance is integrated over its documented solid angle");
        // R1 exposure uses 1 / (1.2 * 2^EV100); keep the EV labels aligned with
        // the camera exposure contract while the disk value remains in float.
        const float r1Ev15PreExposure = 1.0f / (1.2f * 32768.0f);
        const float r1Ev14PreExposure = 1.0f / (1.2f * 16384.0f);
        Expect(NearlyEqual(ComputeSunDiskPreExposedLuminance(
                               parameters, r1Ev15PreExposure),
                           40690.1f,
                           0.5f),
               "EV15 sun disk luminance is represented without FP16 saturation");
        Expect(!IsSunDiskWithinFp16SafetyRange(parameters, r1Ev14PreExposure),
               "EV14 identifies the nominal solar disk as outside the FP16 safety range");
        Expect(IsSunDiskWithinFp16SafetyRange(parameters, r1Ev15PreExposure),
               "EV15 is inside the FP16 safety range");
        Expect(NearlyEqual(ComputeSunDiskPreExposedLuminance(
                               parameters, ev15PreExposure),
                           48828.125f,
                           0.5f),
               "unscaled EV15 pre-exposure remains a valid direct disk value");
        Expect(ComputeSunDiskPreExposedLuminance(parameters, 0.0f) == 0.0f,
               "non-positive pre-exposure disables the disk value");
    }

    bool SameVector(const NorvesLib::Math::Vector3& lhs, const NorvesLib::Math::Vector3& rhs)
    {
        return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
    }

    void TestSunGroundIlluminanceContract()
    {
        SkyAtmosphereParameters parameters = MakeDefaultSkyAtmosphereParameters();
        parameters.bEnabled = true;

        // 天頂の太陽: 地表の透過率は各散乱係数×scale heightを光学的厚さとする指数減衰。
        parameters.SunAltitudeDegrees = 90.0f;
        const NorvesLib::Math::Vector3 zenith = ComputeSunGroundTransmittance(parameters);
        const float mie = std::exp(-3.996e-6f * 1200.0f);
        Expect(NearlyEqual(zenith.x, std::exp(-5.802e-6f * 8000.0f) * mie, 1.0e-6f) &&
                   NearlyEqual(zenith.y, std::exp(-13.558e-6f * 8000.0f) * mie, 1.0e-6f) &&
                   NearlyEqual(zenith.z, std::exp(-33.100e-6f * 8000.0f) * mie, 1.0e-6f),
               "zenith sun transmittance matches the analytic optical depth");

        // 低い太陽は長い光路で赤く暗くなる。
        parameters.SunAltitudeDegrees = 8.0f;
        const NorvesLib::Math::Vector3 low = ComputeSunGroundTransmittance(parameters);
        Expect(low.x > low.y && low.y > low.z && low.x < zenith.x && low.z > 0.0f,
               "low sun transmittance is reddened and dimmer than the zenith sun");

        // 空の参照評価が散乱源に使う太陽の透過率と同じ値。
        const SkyRadianceSample sample = EvaluateHillaireSkyReference(
            parameters, NorvesLib::Math::Vector3(0.0f, 1.0f, 0.0f));
        Expect(NearlyEqual((low.x + low.y + low.z) / 3.0f, sample.MeanSunTransmittance, 1.0e-6f),
               "ground sun transmittance equals the sky reference scattering source");

        // 透過率LUTと同じ関数の地表・太陽の余弦の値。
        const float sunCosine = MakeSunDirectionFromAltitudeAzimuth(
            parameters.SunAltitudeDegrees, parameters.SunAzimuthDegrees).y;
        Expect(SameVector(ComputeAtmosphereTransmittance(parameters, 0.0f, sunCosine), low),
               "ground sun transmittance is the LUT formula at the ground altitude");
        const NorvesLib::Math::Vector3 ground =
            ComputeAtmosphereTransmittance(parameters, 0.0f, 0.2f);
        const NorvesLib::Math::Vector3 top =
            ComputeAtmosphereTransmittance(parameters, 1.0f, 0.2f);
        Expect(top.x > ground.x && top.y > ground.y && top.z > ground.z,
               "thinner air at altitude transmits more light");
        ExpectFiniteVector(ComputeAtmosphereTransmittance(
                               parameters,
                               std::numeric_limits<float>::quiet_NaN(),
                               std::numeric_limits<float>::infinity()),
                           "non-finite altitude and cosine stay finite");

        // 地表照度は太陽円盤の照度×透過率。
        const NorvesLib::Math::Vector3 illuminance = ComputeSunGroundIlluminance(parameters);
        const float disk = ComputeSunDiskIrradiance(parameters);
        Expect(RelativeNearlyEqual(illuminance.x, disk * low.x, 1.0e-6f) &&
                   RelativeNearlyEqual(illuminance.y, disk * low.y, 1.0e-6f) &&
                   RelativeNearlyEqual(illuminance.z, disk * low.z, 1.0e-6f),
               "ground illuminance is the disk irradiance times the ground transmittance");

        parameters.bEnabled = false;
        Expect(SameVector(ComputeSunGroundTransmittance(parameters),
                          NorvesLib::Math::Vector3::Zero) &&
                   SameVector(ComputeSunGroundIlluminance(parameters),
                              NorvesLib::Math::Vector3::Zero),
               "a disabled sky has no ground sun");
    }

    void TestSkyViewRadianceContract()
    {
        SkyAtmosphereParameters parameters = MakeDefaultSkyAtmosphereParameters();
        parameters.bEnabled = true;
        parameters.SunAltitudeDegrees = 20.0f;
        // 地表から見た空は、散乱光に地表からその方向への透過率を掛けた値。
        const NorvesLib::Math::Vector3 directions[] = {
            NorvesLib::Math::Vector3(0.0f, 1.0f, 0.0f),
            NorvesLib::Math::VectorUtils::Normalize(NorvesLib::Math::Vector3(1.0f, 0.1f, 0.0f)),
            NorvesLib::Math::VectorUtils::Normalize(NorvesLib::Math::Vector3(0.3f, 0.5f, -0.8f))};
        for (const NorvesLib::Math::Vector3& direction : directions)
        {
            const SkyRadianceSample scattered = EvaluateHillaireSkyReference(parameters, direction);
            const SkyRadianceSample viewed = EvaluateSkyViewRadiance(parameters, direction);
            const NorvesLib::Math::Vector3 transmittance =
                ComputeAtmosphereTransmittance(parameters, 0.0f, direction.y);
            Expect(viewed.bValid == scattered.bValid &&
                       viewed.MeanSunTransmittance == scattered.MeanSunTransmittance,
                   "viewed sky keeps the reference validity and sun transmittance");
            Expect(RelativeNearlyEqual(viewed.Radiance.x, scattered.Radiance.x * transmittance.x, 1.0e-6f) &&
                       RelativeNearlyEqual(viewed.Radiance.y, scattered.Radiance.y * transmittance.y, 1.0e-6f) &&
                       RelativeNearlyEqual(viewed.Radiance.z, scattered.Radiance.z * transmittance.z, 1.0e-6f),
                   "viewed sky is the scattered radiance times the ground view transmittance");
        }
        // 地平線近くは長い光路で減衰し、散乱光より明確に暗い（青が最も減る）。
        const SkyRadianceSample horizonScattered = EvaluateHillaireSkyReference(parameters, directions[1]);
        const SkyRadianceSample horizonViewed = EvaluateSkyViewRadiance(parameters, directions[1]);
        Expect(horizonViewed.Radiance.z < 0.5f * horizonScattered.Radiance.z,
               "the horizon sky is attenuated along the long view path");
        const SkyRadianceSample below = EvaluateSkyViewRadiance(
            parameters, NorvesLib::Math::Vector3(0.0f, -1.0f, 0.0f));
        Expect(below.Radiance.x == 0.0f && below.Radiance.y == 0.0f && below.Radiance.z == 0.0f,
               "the sky below the horizon stays black");
    }

    void TestSkySunLightContract()
    {
        SkyAtmosphereParameters parameters = MakeDefaultSkyAtmosphereParameters();
        parameters.bEnabled = true;
        parameters.SunAltitudeDegrees = 30.0f;
        parameters.SunAzimuthDegrees = 45.0f;

        LightProxy light;
        Expect(MakeSkySunLightProxy(parameters, light), "an enabled sky builds a sun light");
        Expect(IsSkySunLight(light) && light.LightId == SkySunLightId &&
                   light.Type == LightType::Directional && light.bCastShadows &&
                   light.IsValid(),
               "the sky sun is a valid shadow-casting directional light with the reserved id");
        const NorvesLib::Math::Vector3 sunDirection =
            MakeSunDirectionFromAltitudeAzimuth(30.0f, 45.0f);
        Expect(NearlyEqual(light.DirectionX, -sunDirection.x, 1.0e-6f) &&
                   NearlyEqual(light.DirectionY, -sunDirection.y, 1.0e-6f) &&
                   NearlyEqual(light.DirectionZ, -sunDirection.z, 1.0e-6f),
               "the sky sun light travels away from the sun");
        const NorvesLib::Math::Vector3 illuminance = ComputeSunGroundIlluminance(parameters);
        Expect(RelativeNearlyEqual(light.ColorR * light.CanonicalIntensity, illuminance.x, 1.0e-5f) &&
                   RelativeNearlyEqual(light.ColorG * light.CanonicalIntensity, illuminance.y, 1.0e-5f) &&
                   RelativeNearlyEqual(light.ColorB * light.CanonicalIntensity, illuminance.z, 1.0e-5f),
               "the sky sun light carries the ground illuminance");
        Expect(NearlyEqual(0.2126f * light.ColorR + 0.7152f * light.ColorG + 0.0722f * light.ColorB,
                           1.0f, 1.0e-5f),
               "the sky sun light color has unit luminance");

        // 差し替えはシーンの灯を残し、空の太陽を1つだけ持つ。
        NorvesLib::Core::Container::VariableArray<LightProxy> lights;
        LightProxy sceneLight;
        sceneLight.LightId = 7u;
        lights.push_back(sceneLight);
        ReplaceSkySunLight(parameters, lights);
        ReplaceSkySunLight(parameters, lights);
        Expect(lights.size() == 2u && lights[0].LightId == 7u && IsSkySunLight(lights[1]),
               "replacing the sky sun keeps scene lights and never duplicates the sun");
        parameters.bEnabled = false;
        Expect(!MakeSkySunLightProxy(parameters, light), "a disabled sky builds no sun light");
        ReplaceSkySunLight(parameters, lights);
        Expect(lights.size() == 1u && lights[0].LightId == 7u,
               "disabling the sky removes the sky sun light");
    }
} // namespace

int main()
{
    TestSunDirectionCoordinateContract();
    TestSanitizationKeepsSnapshotFinite();
    TestReferenceSamples();
    TestSunDiskPreExposureContract();
    TestSunGroundIlluminanceContract();
    TestSkyViewRadianceContract();
    TestSkySunLightContract();

    if (g_FailureCount != 0)
    {
        std::cout << "SkyAtmosphereModelTest failures: " << g_FailureCount << "\n";
        return 1;
    }

    std::cout << "SkyAtmosphereModelTest: PASS\n";
    return 0;
}
