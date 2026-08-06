#include "Rendering/DirectionalShadowLightMatrices.h"
#include "Rendering/ShadowMapPass.h"
#include "Math/MatrixUtils.h"
#include "Math/VectorUtils.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <utility>

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

    void ExpectNearlyEqual(float lhs, float rhs, const char* message)
    {
        Expect(NearlyEqual(lhs, rhs), message);
    }

    void ExpectVectorNearlyEqual(const NorvesLib::Math::Vector3& lhs,
                                 const NorvesLib::Math::Vector3& rhs,
                                 const char* message)
    {
        Expect(NearlyEqual(lhs.x, rhs.x) &&
                   NearlyEqual(lhs.y, rhs.y) &&
                   NearlyEqual(lhs.z, rhs.z),
               message);
    }

    void ExpectSettingsNearlyEqual(const DirectionalShadowMatrixSettings& lhs,
                                   const DirectionalShadowMatrixSettings& rhs,
                                   const char* message)
    {
        Expect(NearlyEqual(lhs.OrthoSize, rhs.OrthoSize) &&
                   NearlyEqual(lhs.NearPlane, rhs.NearPlane) &&
                   NearlyEqual(lhs.FarPlane, rhs.FarPlane) &&
                   NearlyEqual(lhs.LightDistance, rhs.LightDistance) &&
                   NearlyEqual(lhs.Target.x, rhs.Target.x) &&
                   NearlyEqual(lhs.Target.y, rhs.Target.y) &&
                   NearlyEqual(lhs.Target.z, rhs.Target.z),
               message);
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

    BoundingSphere MakeBounds(float centerX, float centerY, float centerZ, float radius)
    {
        BoundingSphere bounds;
        bounds.CenterX = centerX;
        bounds.CenterY = centerY;
        bounds.CenterZ = centerZ;
        bounds.Radius = radius;
        return bounds;
    }

    MeshProxy MakeMeshCaster(uint64_t handleId,
                             float centerX,
                             float centerY,
                             float centerZ,
                             float radius,
                             bool bCastShadow = true,
                             bool bVisible = true)
    {
        MeshProxy proxy;
        proxy.MeshHandle = MeshDataHandle{handleId};
        proxy.bVisible = bVisible;
        proxy.bCastShadow = bCastShadow;
        proxy.WorldBounds = MakeBounds(centerX, centerY, centerZ, radius);
        return proxy;
    }

    MegaGeometryProxy MakeMegaCaster(uint64_t handleId,
                                     float centerX,
                                     float centerY,
                                     float centerZ,
                                     float radius,
                                     bool bCastShadow = true,
                                     bool bVisible = true)
    {
        MegaGeometryProxy proxy;
        proxy.MegaMeshHandle = MegaGeometry::MegaMeshHandle{handleId};
        proxy.bVisible = bVisible;
        proxy.bCastShadow = bCastShadow;
        proxy.WorldBounds = MakeBounds(centerX, centerY, centerZ, radius);
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

    void TestMeshCasterTargetFollowsBoundsCenter()
    {
        const DirectionalShadowMatrixSettings baseSettings = MakeDefaultDirectionalShadowMatrixSettings();
        CoreContainer::VariableArray<MeshProxy> meshes;
        meshes.push_back(MakeMeshCaster(101, 10.0f, 20.0f, -5.0f, 2.0f));

        const DirectionalShadowMatrixSettings fitted =
            FitDirectionalShadowMatrixSettingsToCasters(baseSettings, &meshes, nullptr);

        ExpectVectorNearlyEqual(fitted.Target,
                                NorvesLib::Math::Vector3(10.0f, 20.0f, -5.0f),
                                "mesh caster target follows bounds center");
        ExpectNearlyEqual(fitted.OrthoSize, baseSettings.OrthoSize,
                          "small mesh caster keeps base ortho size");
        ExpectNearlyEqual(fitted.LightDistance, baseSettings.LightDistance,
                          "small mesh caster keeps base light distance");
        Expect(IsEligibleDirectionalShadowMeshCaster(meshes[0]),
               "valid mesh caster is eligible");
    }

    void TestLargeCasterExpandsRange()
    {
        const DirectionalShadowMatrixSettings baseSettings = MakeDefaultDirectionalShadowMatrixSettings();
        CoreContainer::VariableArray<MeshProxy> meshes;
        meshes.push_back(MakeMeshCaster(102, 0.0f, 0.0f, 0.0f, 80.0f));

        const DirectionalShadowMatrixSettings fitted =
            FitDirectionalShadowMatrixSettingsToCasters(baseSettings, &meshes, nullptr);

        ExpectVectorNearlyEqual(fitted.Target,
                                NorvesLib::Math::Vector3(0.0f, 0.0f, 0.0f),
                                "large caster target remains at center");
        ExpectNearlyEqual(fitted.OrthoSize, 80.0f,
                          "large caster expands ortho size");
        ExpectNearlyEqual(fitted.LightDistance, 80.1f,
                          "large caster expands light distance with near plane");
        ExpectNearlyEqual(fitted.FarPlane, 160.1f,
                          "large caster expands far plane beyond base");
    }

    void TestInvalidCastersIgnored()
    {
        const DirectionalShadowMatrixSettings baseSettings = MakeDefaultDirectionalShadowMatrixSettings();
        CoreContainer::VariableArray<MeshProxy> meshes;
        meshes.push_back(MakeMeshCaster(0, 1000.0f, 0.0f, 0.0f, 500.0f));
        meshes.push_back(MakeMeshCaster(103, -1000.0f, 0.0f, 0.0f, 500.0f, false));
        meshes.push_back(MakeMeshCaster(104, 1000.0f, 0.0f, 0.0f, 500.0f, true, false));
        meshes.push_back(MakeMeshCaster(105, 1000.0f, 0.0f, 0.0f, 0.0f));
        meshes.push_back(MakeMeshCaster(106, 4.0f, 0.0f, 0.0f, 30.0f));

        const DirectionalShadowMatrixSettings fitted =
            FitDirectionalShadowMatrixSettingsToCasters(baseSettings, &meshes, nullptr);

        Expect(!IsEligibleDirectionalShadowMeshCaster(meshes[0]),
               "mesh caster with invalid handle is ignored");
        Expect(!IsEligibleDirectionalShadowMeshCaster(meshes[1]),
               "non-shadow mesh caster is ignored");
        Expect(!IsEligibleDirectionalShadowMeshCaster(meshes[2]),
               "invisible mesh caster is ignored");
        Expect(!IsEligibleDirectionalShadowMeshCaster(meshes[3]),
               "invalid bounds mesh caster is ignored");
        ExpectVectorNearlyEqual(fitted.Target,
                                NorvesLib::Math::Vector3(4.0f, 0.0f, 0.0f),
                                "invalid mesh casters do not affect target");
        ExpectNearlyEqual(fitted.OrthoSize, 30.0f,
                          "valid mesh caster drives ortho after invalid casters");
        ExpectNearlyEqual(fitted.LightDistance, 30.1f,
                          "valid mesh caster drives light distance after invalid casters");
        ExpectNearlyEqual(fitted.FarPlane, 60.1f,
                          "valid mesh caster drives far plane after invalid casters");
    }

    void TestMegaGeometryCasterIncluded()
    {
        const DirectionalShadowMatrixSettings baseSettings = MakeDefaultDirectionalShadowMatrixSettings();
        CoreContainer::VariableArray<MegaGeometryProxy> megas;
        megas.push_back(MakeMegaCaster(201, 0.0f, -10.0f, 0.0f, 25.0f));

        const DirectionalShadowMatrixSettings fitted =
            FitDirectionalShadowMatrixSettingsToCasters(baseSettings, nullptr, &megas);

        Expect(IsEligibleDirectionalShadowMegaGeometryCaster(megas[0]),
               "valid mega geometry caster is eligible");
        ExpectVectorNearlyEqual(fitted.Target,
                                NorvesLib::Math::Vector3(0.0f, -10.0f, 0.0f),
                                "mega geometry caster contributes target");
        ExpectNearlyEqual(fitted.OrthoSize, 25.0f,
                          "mega geometry caster expands ortho size");
        ExpectNearlyEqual(fitted.LightDistance, 25.1f,
                          "mega geometry caster expands light distance");
        ExpectNearlyEqual(fitted.FarPlane, 50.1f,
                          "mega geometry caster expands far plane when needed");
    }

    void TestNullAndEmptyCasterFallbackEqualsBase()
    {
        DirectionalShadowMatrixSettings baseSettings;
        baseSettings.OrthoSize = 33.0f;
        baseSettings.NearPlane = 0.5f;
        baseSettings.FarPlane = 77.0f;
        baseSettings.LightDistance = 44.0f;
        baseSettings.Target = NorvesLib::Math::Vector3(3.0f, 4.0f, 5.0f);

        CoreContainer::VariableArray<MeshProxy> emptyMeshes;
        CoreContainer::VariableArray<MegaGeometryProxy> emptyMegas;

        ExpectSettingsNearlyEqual(FitDirectionalShadowMatrixSettingsToCasters(baseSettings, nullptr, nullptr),
                                  baseSettings,
                                  "null caster arrays keep base settings");
        ExpectSettingsNearlyEqual(FitDirectionalShadowMatrixSettingsToCasters(baseSettings, &emptyMeshes, &emptyMegas),
                                  baseSettings,
                                  "empty caster arrays keep base settings");
    }

    void TestFittedSettingsBuildMatricesWithExpectedLightPosition()
    {
        const DirectionalShadowMatrixSettings baseSettings = MakeDefaultDirectionalShadowMatrixSettings();
        CoreContainer::VariableArray<LightProxy> lights;
        lights.push_back(MakeDirectional(301, 1.0f, 0.0f, 0.0f, true));

        CoreContainer::VariableArray<MeshProxy> meshes;
        meshes.push_back(MakeMeshCaster(107, 10.0f, 0.0f, 0.0f, 30.0f));

        const DirectionalShadowMatrixResult result =
            BuildFittedDirectionalShadowLightMatrices(&lights, &meshes, nullptr, baseSettings);

        Expect(result.bEnabled, "fitted build enables eligible directional shadow light");
        ExpectVectorNearlyEqual(result.LightPosition,
                                NorvesLib::Math::Vector3(-20.1f, 0.0f, 0.0f),
                                "fitted build uses fitted target and light distance");
    }

    void TestMultiCasterFitUsesAllValidCasters()
    {
        const DirectionalShadowMatrixSettings baseSettings = MakeDefaultDirectionalShadowMatrixSettings();
        CoreContainer::VariableArray<MeshProxy> meshes;
        meshes.push_back(MakeMeshCaster(108, -10.0f, 0.0f, 0.0f, 2.0f));

        CoreContainer::VariableArray<MegaGeometryProxy> megas;
        megas.push_back(MakeMegaCaster(202, 30.0f, 0.0f, 0.0f, 6.0f));

        const DirectionalShadowMatrixSettings fitted =
            FitDirectionalShadowMatrixSettingsToCasters(baseSettings, &meshes, &megas);

        ExpectVectorNearlyEqual(fitted.Target,
                                NorvesLib::Math::Vector3(12.0f, 0.0f, 0.0f),
                                "multi-caster AABB center is used as target");
        ExpectNearlyEqual(fitted.OrthoSize, 24.0f,
                          "multi-caster fit radius drives ortho size");
        ExpectNearlyEqual(fitted.LightDistance, 24.1f,
                          "multi-caster fit radius drives light distance");
        ExpectNearlyEqual(fitted.FarPlane, 50.0f,
                          "multi-caster fit keeps base far plane when sufficient");
    }

    void TestNonFiniteCasterBoundsIgnored()
    {
        const float quietNaN = std::numeric_limits<float>::quiet_NaN();
        const float infinity = std::numeric_limits<float>::infinity();
        const DirectionalShadowMatrixSettings baseSettings = MakeDefaultDirectionalShadowMatrixSettings();

        CoreContainer::VariableArray<MeshProxy> nonFiniteMeshes;
        nonFiniteMeshes.push_back(MakeMeshCaster(109, quietNaN, 0.0f, 0.0f, 10.0f));
        nonFiniteMeshes.push_back(MakeMeshCaster(110, infinity, 0.0f, 0.0f, 10.0f));
        nonFiniteMeshes.push_back(MakeMeshCaster(111, 0.0f, 0.0f, 0.0f, quietNaN));
        nonFiniteMeshes.push_back(MakeMeshCaster(112, 0.0f, 0.0f, 0.0f, infinity));

        ExpectSettingsNearlyEqual(FitDirectionalShadowMatrixSettingsToCasters(baseSettings, &nonFiniteMeshes, nullptr),
                                  baseSettings,
                                  "only non-finite casters fall back to base settings");
        for (const MeshProxy& proxy : nonFiniteMeshes)
        {
            Expect(!IsEligibleDirectionalShadowMeshCaster(proxy),
                   "non-finite mesh caster is ineligible");
        }

        nonFiniteMeshes.push_back(MakeMeshCaster(113, 0.0f, 5.0f, 0.0f, 40.0f));
        const DirectionalShadowMatrixSettings fitted =
            FitDirectionalShadowMatrixSettingsToCasters(baseSettings, &nonFiniteMeshes, nullptr);

        ExpectVectorNearlyEqual(fitted.Target,
                                NorvesLib::Math::Vector3(0.0f, 5.0f, 0.0f),
                                "valid caster controls target after non-finite casters");
        ExpectNearlyEqual(fitted.OrthoSize, 40.0f,
                          "valid caster controls ortho after non-finite casters");
        ExpectNearlyEqual(fitted.LightDistance, 40.1f,
                          "valid caster controls light distance after non-finite casters");
        ExpectNearlyEqual(fitted.FarPlane, 80.1f,
                          "valid caster controls far plane after non-finite casters");
    }

    void TestShadowMapSettingsConverter()
    {
        ShadowMapPassSettings passSettings;
        passSettings.OrthoSize = 37.0f;
        passSettings.NearPlane = 0.25f;
        passSettings.FarPlane = 123.0f;

        const DirectionalShadowMatrixSettings defaults = MakeDefaultDirectionalShadowMatrixSettings();
        const DirectionalShadowMatrixSettings converted = MakeDirectionalShadowMatrixSettings(passSettings);

        ExpectNearlyEqual(converted.OrthoSize, passSettings.OrthoSize,
                          "converter maps ShadowMapPassSettings OrthoSize");
        ExpectNearlyEqual(converted.NearPlane, passSettings.NearPlane,
                          "converter maps ShadowMapPassSettings NearPlane");
        ExpectNearlyEqual(converted.FarPlane, passSettings.FarPlane,
                          "converter maps ShadowMapPassSettings FarPlane");
        ExpectNearlyEqual(converted.LightDistance, defaults.LightDistance,
                          "converter preserves default LightDistance");
        ExpectVectorNearlyEqual(converted.Target, defaults.Target,
                                "converter preserves default Target");
    }

    void TestSkinnedCasterBoundsUseRowVectorPointAndExtentTransforms()
    {
        const SkinnedMeshHandle handle{1, 1};
        CoreContainer::VariableArray<SkinnedMeshVertex> vertices(1);
        CoreContainer::VariableArray<uint32_t> indices = {0, 0, 0};
        auto assetLease = CoreContainer::MakeShared<SkinnedMeshAssetLease>(
            handle, std::move(vertices), std::move(indices));

        SkinnedMeshProxy proxy;
        proxy.MeshHandle = handle;
        proxy.AssetLease = assetLease;
        proxy.ComponentId = 1;
        proxy.BonePalette.push_back(NorvesLib::Math::Matrix4x4::Identity);
        proxy.bHasAnimatedBounds = true;
        proxy.AnimatedBounds.Min = NorvesLib::Math::Vector3(0.5f, 1.0f, 1.0f);
        proxy.AnimatedBounds.Max = NorvesLib::Math::Vector3(1.5f, 3.0f, 5.0f);
        proxy.WorldTransform = NorvesLib::Math::Matrix4x4(
            2.0f, 3.0f, 5.0f, 0.0f,
            7.0f, 11.0f, 13.0f, 0.0f,
            17.0f, 19.0f, 23.0f, 0.0f,
            29.0f, 31.0f, 37.0f, 1.0f);

        CoreContainer::VariableArray<SkinnedMeshProxy> skinnedCasters = {proxy};
        DirectionalShadowMatrixSettings baseSettings;
        baseSettings.OrthoSize = 0.0f;
        baseSettings.LightDistance = 0.0f;
        baseSettings.FarPlane = 0.0f;
        const DirectionalShadowMatrixSettings fitted =
            FitDirectionalShadowMatrixSettingsToCasters(
                baseSettings, nullptr, &skinnedCasters, nullptr);

        ExpectVectorNearlyEqual(
            fitted.Target,
            NorvesLib::Math::Vector3(96.0f, 113.0f, 137.0f),
            "skinned caster center uses row-vector point transform including translation");
        const float expectedRadius = std::sqrt(42.0f * 42.0f + 50.5f * 50.5f + 61.5f * 61.5f);
        ExpectNearlyEqual(fitted.OrthoSize, expectedRadius,
                          "skinned caster radius uses absolute row-vector upper3x3 extents");
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
    TestMeshCasterTargetFollowsBoundsCenter();
    TestLargeCasterExpandsRange();
    TestInvalidCastersIgnored();
    TestMegaGeometryCasterIncluded();
    TestNullAndEmptyCasterFallbackEqualsBase();
    TestFittedSettingsBuildMatricesWithExpectedLightPosition();
    TestMultiCasterFitUsesAllValidCasters();
    TestNonFiniteCasterBoundsIgnored();
    TestShadowMapSettingsConverter();
    TestSkinnedCasterBoundsUseRowVectorPointAndExtentTransforms();
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
