#include "Rendering/DirectionalShadowLightMatrices.h"
#include "Rendering/ShadowMapPass.h"
#include "Math/MatrixUtils.h"
#include "Math/VectorUtils.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>

using namespace NorvesLib::Core::Rendering;

namespace
{
    namespace CoreContainer = NorvesLib::Core::Container;

    int g_FailureCount = 0;

    void Expect(bool bCondition, const char* message)
    {
        if (!bCondition)
        {
            ++g_FailureCount;
            std::cout << "FAILED: " << message << "\n";
        }
    }

    bool NearlyEqual(float lhs, float rhs, float epsilon = 1.0e-4f)
    {
        return std::abs(lhs - rhs) <= epsilon;
    }

    bool MatrixNearlyEqual(const NorvesLib::Math::Matrix4x4& lhs,
                           const NorvesLib::Math::Matrix4x4& rhs,
                           float epsilon = 1.0e-4f)
    {
        for (int index = 0; index < 16; ++index)
        {
            if (!NearlyEqual(lhs.values[index], rhs.values[index], epsilon))
            {
                return false;
            }
        }
        return true;
    }

    bool MatrixIsIdentity(const NorvesLib::Math::Matrix4x4& matrix)
    {
        return MatrixNearlyEqual(matrix, NorvesLib::Math::Matrix4x4::Identity);
    }

    bool MatrixIsFinite(const NorvesLib::Math::Matrix4x4& matrix)
    {
        for (int index = 0; index < 16; ++index)
        {
            if (!std::isfinite(matrix.values[index]))
            {
                return false;
            }
        }
        return true;
    }

    bool ShaderDataNearlyEqual(const float* lhs, const float* rhs)
    {
        for (int index = 0; index < 16; ++index)
        {
            if (!NearlyEqual(lhs[index], rhs[index]))
            {
                return false;
            }
        }
        return true;
    }

    LightProxy MakeDirectional(uint64_t lightId,
                               float directionX,
                               float directionY,
                               float directionZ,
                               bool bCastShadows)
    {
        LightProxy proxy;
        proxy.LightId = lightId;
        proxy.Type = LightType::Directional;
        proxy.DirectionX = directionX;
        proxy.DirectionY = directionY;
        proxy.DirectionZ = directionZ;
        proxy.bCastShadows = bCastShadows;
        proxy.bVisible = true;
        proxy.Intensity = 1.0f;
        return proxy;
    }

    LightProxy MakePoint(uint64_t lightId)
    {
        LightProxy proxy = MakeDirectional(lightId, 0.0f, -1.0f, 0.0f, true);
        proxy.Type = LightType::Point;
        return proxy;
    }

    LightProxy MakeSpot(uint64_t lightId)
    {
        LightProxy proxy = MakeDirectional(lightId, 0.0f, -1.0f, 0.0f, true);
        proxy.Type = LightType::Spot;
        return proxy;
    }

    void AssertDisabledIdentityResult(const DirectionalShadowMatrixResult& result, const char* context)
    {
        Expect(!result.bEnabled, context);
        Expect(MatrixIsIdentity(result.View), "disabled result uses identity view");
        Expect(MatrixIsIdentity(result.Projection), "disabled result uses identity projection");
    }

    void TestNullAndEmptyInputs()
    {
        const DirectionalShadowMatrixSettings settings = MakeDefaultDirectionalShadowMatrixSettings();
        AssertDisabledIdentityResult(BuildDirectionalShadowLightMatrices(nullptr, settings),
                                     "null input disables shadows");
        Expect(CountShaderVisibleDirectionalLights(nullptr) == 0,
               "null input has zero shader-visible directional lights");
        Expect(SelectDirectionalShadowLight(nullptr) == nullptr,
               "null input selects no light");

        CoreContainer::VariableArray<LightProxy> emptyLights;
        AssertDisabledIdentityResult(BuildDirectionalShadowLightMatrices(&emptyLights, settings),
                                     "empty input disables shadows");
        Expect(CountShaderVisibleDirectionalLights(&emptyLights) == 0,
               "empty input has zero shader-visible directional lights");
        Expect(SelectDirectionalShadowLight(&emptyLights) == nullptr,
               "empty input selects no light");
    }

    void TestNoEligibleInputDisables()
    {
        CoreContainer::VariableArray<LightProxy> lights;
        LightProxy invalid = MakeDirectional(1, 0.0f, -1.0f, 0.0f, true);
        invalid.bVisible = false;
        lights.push_back(invalid);
        lights.push_back(MakePoint(2));
        lights.push_back(MakeSpot(3));

        const DirectionalShadowMatrixSettings settings = MakeDefaultDirectionalShadowMatrixSettings();
        AssertDisabledIdentityResult(BuildDirectionalShadowLightMatrices(&lights, settings),
                                     "no eligible light disables shadows");
        Expect(SelectDirectionalShadowLight(&lights) == nullptr,
               "no eligible light selects no light");
    }

    void TestEligibleLightSelectionSkipsInvalidInputs()
    {
        CoreContainer::VariableArray<LightProxy> lights;
        LightProxy invalid = MakeDirectional(1, 0.0f, -1.0f, 0.0f, true);
        invalid.bVisible = false;
        lights.push_back(invalid);
        lights.push_back(MakePoint(2));
        lights.push_back(MakeSpot(3));
        lights.push_back(MakeDirectional(4, 0.0f, -1.0f, 0.0f, false));
        lights.push_back(MakeDirectional(5, 0.0f, 0.0f, 0.0f, true));
        lights.push_back(MakeDirectional(6,
                                         std::numeric_limits<float>::infinity(),
                                         -1.0f,
                                         0.0f,
                                         true));
        lights.push_back(MakeDirectional(77, 1.0f, -2.0f, 3.0f, true));

        Expect(!IsEligibleDirectionalShadowLight(invalid),
               "invalid directional light is not eligible");
        Expect(!IsEligibleDirectionalShadowLight(lights[1]),
               "point light is not eligible");
        Expect(!IsEligibleDirectionalShadowLight(lights[2]),
               "spot light is not eligible");
        Expect(!IsEligibleDirectionalShadowLight(lights[3]),
               "non-shadow directional light is not eligible");
        Expect(!IsEligibleDirectionalShadowLight(lights[4]),
               "zero-direction directional light is not eligible");
        Expect(!IsEligibleDirectionalShadowLight(lights[5]),
               "non-finite directional light is not eligible");
        Expect(IsEligibleDirectionalShadowLight(lights[6]),
               "casting finite non-zero directional light is eligible");
        Expect(SelectDirectionalShadowLight(&lights) == &lights[6],
               "eligible directional light is selected after skipped inputs");
    }

    void TestMultipleDirectionalLightsDisableShadows()
    {
        const DirectionalShadowMatrixSettings settings = MakeDefaultDirectionalShadowMatrixSettings();

        CoreContainer::VariableArray<LightProxy> twoCasting;
        twoCasting.push_back(MakeDirectional(10, 1.0f, -1.0f, 0.0f, true));
        twoCasting.push_back(MakeDirectional(11, -1.0f, -1.0f, 0.0f, true));
        DirectionalShadowMatrixResult twoCastingResult =
            BuildDirectionalShadowLightMatrices(&twoCasting, settings);
        Expect(CountShaderVisibleDirectionalLights(&twoCasting) == 2,
               "two casting directional lights are both shader-visible");
        Expect(twoCastingResult.bHasMultipleDirectionalLights,
               "two casting directional lights are marked multiple");
        Expect(!twoCastingResult.bEnabled,
               "two casting directional lights disable the single global shadow");

        CoreContainer::VariableArray<LightProxy> castingAndNonCasting;
        castingAndNonCasting.push_back(MakeDirectional(12, 1.0f, -1.0f, 0.0f, true));
        castingAndNonCasting.push_back(MakeDirectional(13, -1.0f, -1.0f, 0.0f, false));
        DirectionalShadowMatrixResult mixedResult =
            BuildDirectionalShadowLightMatrices(&castingAndNonCasting, settings);
        Expect(CountShaderVisibleDirectionalLights(&castingAndNonCasting) == 2,
               "casting plus non-casting directional lights are both shader-visible");
        Expect(mixedResult.bHasMultipleDirectionalLights,
               "casting plus non-casting directional lights are marked multiple");
        Expect(!mixedResult.bEnabled,
               "casting plus non-casting directional lights disable the single global shadow");
    }

    void TestMatrixConstruction()
    {
        DirectionalShadowMatrixSettings settings;
        settings.OrthoSize = 32.0f;
        settings.NearPlane = 0.25f;
        settings.FarPlane = 75.0f;
        settings.LightDistance = 40.0f;
        settings.Target = NorvesLib::Math::Vector3(2.0f, 3.0f, -4.0f);

        CoreContainer::VariableArray<LightProxy> lights;
        lights.push_back(MakeDirectional(21, 1.0f, 0.0f, 0.0f, true));

        const DirectionalShadowMatrixResult result =
            BuildDirectionalShadowLightMatrices(&lights, settings);

        const NorvesLib::Math::Vector3 expectedDirection(1.0f, 0.0f, 0.0f);
        const NorvesLib::Math::Vector3 expectedPosition =
            settings.Target - expectedDirection * settings.LightDistance;
        const NorvesLib::Math::Matrix4x4 expectedView =
            NorvesLib::Math::MatrixUtils::CreateLookAt(expectedPosition,
                                                       settings.Target,
                                                       NorvesLib::Math::Vector3(0.0f, 1.0f, 0.0f));
        const NorvesLib::Math::Matrix4x4 expectedProjection =
            NorvesLib::Math::MatrixUtils::CreateOrthographic(settings.OrthoSize * 2.0f,
                                                             settings.OrthoSize * 2.0f,
                                                             settings.NearPlane,
                                                             settings.FarPlane);

        Expect(result.bEnabled, "single eligible directional light enables shadows");
        Expect(result.LightId == 21, "result records selected LightId");
        Expect(NearlyEqual(result.Direction.x, expectedDirection.x) &&
                   NearlyEqual(result.Direction.y, expectedDirection.y) &&
                   NearlyEqual(result.Direction.z, expectedDirection.z),
               "result direction is normalized");
        Expect(NearlyEqual(result.LightPosition.x, expectedPosition.x) &&
                   NearlyEqual(result.LightPosition.y, expectedPosition.y) &&
                   NearlyEqual(result.LightPosition.z, expectedPosition.z),
               "light position uses target minus direction times distance");
        Expect(MatrixNearlyEqual(result.View, expectedView),
               "view matrix follows the pinned sign convention");
        Expect(MatrixNearlyEqual(result.Projection, expectedProjection),
               "projection matrix uses explicit settings");
        Expect(MatrixIsFinite(result.View) && MatrixIsFinite(result.Projection),
               "valid matrix result is finite");
    }

    void TestDifferentDirectionsProduceDifferentViews()
    {
        const DirectionalShadowMatrixSettings settings = MakeDefaultDirectionalShadowMatrixSettings();

        CoreContainer::VariableArray<LightProxy> firstLights;
        firstLights.push_back(MakeDirectional(31, 1.0f, -1.0f, 0.0f, true));
        CoreContainer::VariableArray<LightProxy> secondLights;
        secondLights.push_back(MakeDirectional(32, -1.0f, -1.0f, 0.0f, true));

        const DirectionalShadowMatrixResult first =
            BuildDirectionalShadowLightMatrices(&firstLights, settings);
        const DirectionalShadowMatrixResult second =
            BuildDirectionalShadowLightMatrices(&secondLights, settings);

        Expect(first.bEnabled && second.bEnabled,
               "single eligible directional lights enable both results");
        Expect(!MatrixNearlyEqual(first.View, second.View),
               "different light directions produce different view matrices");
    }

    void TestFallbackUpProducesFiniteMatrices()
    {
        const DirectionalShadowMatrixSettings settings = MakeDefaultDirectionalShadowMatrixSettings();
        CoreContainer::VariableArray<LightProxy> lights;
        lights.push_back(MakeDirectional(41, 0.0f, 1.0f, 0.0f, true));

        const DirectionalShadowMatrixResult result =
            BuildDirectionalShadowLightMatrices(&lights, settings);
        Expect(result.bEnabled, "direction parallel to default up still enables shadows");
        Expect(MatrixIsFinite(result.View) && MatrixIsFinite(result.Projection),
               "fallback up direction produces finite matrices");
    }

    void TestDefaultSettingsMatchShadowMapPass()
    {
        const DirectionalShadowMatrixSettings helperSettings =
            MakeDefaultDirectionalShadowMatrixSettings();
        const ShadowMapPassSettings passSettings;

        Expect(NearlyEqual(helperSettings.OrthoSize, passSettings.OrthoSize),
               "helper default OrthoSize matches ShadowMapPassSettings");
        Expect(NearlyEqual(helperSettings.NearPlane, passSettings.NearPlane),
               "helper default NearPlane matches ShadowMapPassSettings");
        Expect(NearlyEqual(helperSettings.FarPlane, passSettings.FarPlane),
               "helper default FarPlane matches ShadowMapPassSettings");
    }

    void TestShaderMatrixCopies()
    {
        const NorvesLib::Math::Matrix4x4 matrix(
            1.0f, 2.0f, 3.0f, 4.0f,
            5.0f, 6.0f, 7.0f, 8.0f,
            9.0f, 10.0f, 11.0f, 12.0f,
            13.0f, 14.0f, 15.0f, 16.0f);

        float actual[16] = {};
        float expected[16] = {};
        CopyShadowMatrixToShaderData(matrix, actual);
        NorvesLib::Math::MatrixUtils::TransposeToShaderData(matrix, expected);
        Expect(ShaderDataNearlyEqual(actual, expected),
               "CopyShadowMatrixToShaderData uses transpose shader layout");

        float identityView[16] = {};
        float identityProjection[16] = {};
        float expectedIdentity[16] = {};
        CopyIdentityShadowMatricesToShaderData(identityView, identityProjection);
        NorvesLib::Math::MatrixUtils::TransposeToShaderData(NorvesLib::Math::Matrix4x4::Identity,
                                                            expectedIdentity);
        Expect(ShaderDataNearlyEqual(identityView, expectedIdentity),
               "CopyIdentityShadowMatricesToShaderData writes identity view");
        Expect(ShaderDataNearlyEqual(identityProjection, expectedIdentity),
               "CopyIdentityShadowMatricesToShaderData writes identity projection");
    }
} // namespace

int main()
{
    std::cout << "DirectionalShadowLightMatricesTest start\n";

    TestNullAndEmptyInputs();
    TestNoEligibleInputDisables();
    TestEligibleLightSelectionSkipsInvalidInputs();
    TestMultipleDirectionalLightsDisableShadows();
    TestMatrixConstruction();
    TestDifferentDirectionsProduceDifferentViews();
    TestFallbackUpProducesFiniteMatrices();
    TestDefaultSettingsMatchShadowMapPass();
    TestShaderMatrixCopies();

    if (g_FailureCount != 0)
    {
        std::cout << "DirectionalShadowLightMatricesTest failed with "
                  << g_FailureCount << " failure(s)\n";
        return 1;
    }

    std::cout << "DirectionalShadowLightMatricesTest passed\n";
    return 0;
}
