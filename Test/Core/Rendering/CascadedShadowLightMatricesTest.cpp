#include "Rendering/CascadedShadowLightMatrices.h"

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

    bool VectorIsFinite(const NorvesLib::Math::Vector3& value)
    {
        return std::isfinite(value.x) &&
            std::isfinite(value.y) &&
            std::isfinite(value.z);
    }

    bool MatrixIsFinite(const NorvesLib::Math::Matrix4x4& value)
    {
        for (uint32_t index = 0u; index < 16u; ++index)
        {
            if (!std::isfinite(value.values[index]))
            {
                return false;
            }
        }

        return true;
    }

    bool MatrixNearlyEqual(const NorvesLib::Math::Matrix4x4& lhs,
                           const NorvesLib::Math::Matrix4x4& rhs,
                           float epsilon = 1.0e-4f)
    {
        for (uint32_t index = 0u; index < 16u; ++index)
        {
            if (!NearlyEqual(lhs.values[index], rhs.values[index], epsilon))
            {
                return false;
            }
        }

        return true;
    }

    LightProxy MakeDirectionalLight(uint64_t lightId,
                                    float directionX,
                                    float directionY,
                                    float directionZ)
    {
        LightProxy light;
        light.LightId = lightId;
        light.Type = LightType::Directional;
        light.DirectionX = directionX;
        light.DirectionY = directionY;
        light.DirectionZ = directionZ;
        light.bCastShadows = true;
        light.bVisible = true;
        light.Intensity = 1.0f;
        return light;
    }

    CameraProxy MakePerspectiveCamera()
    {
        CameraProxy camera;
        camera.PositionX = 0.0f;
        camera.PositionY = 2.0f;
        camera.PositionZ = 4.0f;
        camera.ForwardX = 0.0f;
        camera.ForwardY = 0.0f;
        camera.ForwardZ = -1.0f;
        camera.UpX = 0.0f;
        camera.UpY = 1.0f;
        camera.UpZ = 0.0f;
        camera.FieldOfView = 60.0f;
        camera.AspectRatio = 16.0f / 9.0f;
        camera.NearPlane = 0.1f;
        camera.FarPlane = 200.0f;
        camera.Projection = ProjectionType::Perspective;
        return camera;
    }

    bool ResultIsFinite(const CascadedShadowMatrixResult& result)
    {
        if (!VectorIsFinite(result.Direction))
        {
            return false;
        }

        for (uint32_t index = 0u; index <= CSM_CASCADE_COUNT; ++index)
        {
            if (!std::isfinite(result.SplitDistances[index]))
            {
                return false;
            }
        }

        for (const CascadedShadowCascade& cascade : result.Cascades)
        {
            if (!std::isfinite(cascade.NearDistance) ||
                !std::isfinite(cascade.FarDistance) ||
                !std::isfinite(cascade.Radius) ||
                !std::isfinite(cascade.NearDepth) ||
                !std::isfinite(cascade.FarDepth) ||
                !std::isfinite(cascade.TexelSize) ||
                !std::isfinite(cascade.OrthoSize) ||
                !VectorIsFinite(cascade.Center) ||
                !VectorIsFinite(cascade.SnappedCenter) ||
                !VectorIsFinite(cascade.LightPosition) ||
                !VectorIsFinite(cascade.Direction) ||
                !MatrixIsFinite(cascade.View) ||
                !MatrixIsFinite(cascade.Projection))
            {
                return false;
            }
        }

        return true;
    }

    void TestFourCascadeSplitAndMatrixContract()
    {
        CoreContainer::VariableArray<LightProxy> lights;
        lights.push_back(MakeDirectionalLight(17u, 0.35f, -0.8f, 0.25f));

        const CameraProxy camera = MakePerspectiveCamera();
        CascadedShadowMatrixSettings settings = MakeDefaultCascadedShadowMatrixSettings();
        settings.ShadowMapResolution = 1024u;
        settings.DepthPadding = 2.0f;

        const CascadedShadowMatrixResult result =
            BuildCascadedShadowLightMatrices(&lights, camera, settings);

        Expect(result.bEnabled, "valid camera and one directional light enable CSM");
        Expect(result.CascadeCount == CSM_CASCADE_COUNT, "CSM produces four cascades");
        Expect(result.LightId == 17u, "CSM records the selected directional light");
        Expect(ResultIsFinite(result), "valid CSM result is finite");
        Expect(NearlyEqual(result.SplitDistances[0], camera.NearPlane),
               "first split uses the camera near plane");
        Expect(NearlyEqual(result.SplitDistances[CSM_CASCADE_COUNT], camera.FarPlane),
               "last split uses the camera far plane");

        for (uint32_t index = 0u; index < CSM_CASCADE_COUNT; ++index)
        {
            const CascadedShadowCascade& cascade = result.Cascades[index];
            Expect(cascade.bEnabled, "each CSM cascade is enabled");
            Expect(cascade.NearDistance < cascade.FarDistance,
                   "each cascade has an increasing camera range");
            Expect(cascade.NearDepth < cascade.FarDepth,
                   "each cascade has an increasing light depth range");
            Expect(NearlyEqual(cascade.NearDistance, result.SplitDistances[index]),
                   "cascade near distance shares the split boundary");
            Expect(NearlyEqual(cascade.FarDistance, result.SplitDistances[index + 1u]),
                   "cascade far distance shares the split boundary");
            if (index > 0u)
            {
                Expect(cascade.NearDistance == result.Cascades[index - 1u].FarDistance,
                       "adjacent cascade boundaries are exactly continuous");
            }
        }
    }

    void TestCasterDepthRangeAndNonFiniteBoundsAreSafe()
    {
        CoreContainer::VariableArray<LightProxy> lights;
        lights.push_back(MakeDirectionalLight(23u, 0.0f, -1.0f, 0.0f));

        CoreContainer::VariableArray<BoundingSphere> bounds;
        bounds.push_back(BoundingSphere{0.0f, 0.0f, -20.0f, 15.0f});
        bounds.push_back(BoundingSphere{
            std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f, 5.0f});
        bounds.push_back(BoundingSphere{
            0.0f, std::numeric_limits<float>::infinity(), 0.0f, 5.0f});

        const CameraProxy camera = MakePerspectiveCamera();
        const CascadedShadowMatrixSettings settings =
            MakeDefaultCascadedShadowMatrixSettings();
        const CascadedShadowMatrixResult result =
            BuildCascadedShadowLightMatrices(&lights, &camera, settings, &bounds);

        Expect(result.bEnabled, "finite caster plus invalid bounds keeps CSM enabled");
        Expect(ResultIsFinite(result), "non-finite caster bounds never leak into CSM output");
        for (const CascadedShadowCascade& cascade : result.Cascades)
        {
            Expect(cascade.FarDepth > cascade.NearDepth,
                   "caster depth fit keeps a valid projection interval");
        }
    }

    void TestInvalidInputsFallBackToShadowOff()
    {
        CoreContainer::VariableArray<LightProxy> lights;
        lights.push_back(MakeDirectionalLight(31u, 0.0f, -1.0f, 0.0f));

        CascadedShadowMatrixSettings settings = MakeDefaultCascadedShadowMatrixSettings();
        const CameraProxy validCamera = MakePerspectiveCamera();

        CameraProxy reversedCamera = validCamera;
        reversedCamera.NearPlane = 20.0f;
        reversedCamera.FarPlane = 10.0f;
        const CascadedShadowMatrixResult reversedResult =
            BuildCascadedShadowLightMatrices(&lights, reversedCamera, settings);
        Expect(!reversedResult.bEnabled, "reversed camera range disables CSM");
        Expect(ResultIsFinite(reversedResult), "reversed camera range returns finite identity data");

        CoreContainer::VariableArray<LightProxy> invalidDirectionLights;
        invalidDirectionLights.push_back(MakeDirectionalLight(32u, 0.0f, 0.0f, 0.0f));
        const CascadedShadowMatrixResult invalidDirectionResult =
            BuildCascadedShadowLightMatrices(&invalidDirectionLights, validCamera, settings);
        Expect(!invalidDirectionResult.bEnabled, "zero directional light disables CSM");
        Expect(ResultIsFinite(invalidDirectionResult), "invalid light direction returns finite data");

        settings.CascadeCount = CSM_CASCADE_COUNT - 1u;
        const CascadedShadowMatrixResult invalidCountResult =
            BuildCascadedShadowLightMatrices(&lights, validCamera, settings);
        Expect(!invalidCountResult.bEnabled, "non-four cascade count disables CSM");
        Expect(ResultIsFinite(invalidCountResult), "invalid cascade count returns finite data");
    }

    void TestSubtexelCameraMotionKeepsSnappedMatrices()
    {
        CoreContainer::VariableArray<LightProxy> lights;
        lights.push_back(MakeDirectionalLight(41u, 0.0f, -1.0f, 0.0f));

        const CameraProxy firstCamera = MakePerspectiveCamera();
        const CascadedShadowMatrixSettings settings =
            MakeDefaultCascadedShadowMatrixSettings();
        const CascadedShadowMatrixResult first =
            BuildCascadedShadowLightMatrices(&lights, firstCamera, settings);

        CameraProxy movedCamera = firstCamera;
        const float subtexelMotion = first.Cascades[0].TexelSize * 0.1f;
        movedCamera.PositionX += subtexelMotion;
        const CascadedShadowMatrixResult moved =
            BuildCascadedShadowLightMatrices(&lights, movedCamera, settings);

        Expect(first.bEnabled && moved.bEnabled, "subtexel motion keeps CSM enabled");
        for (uint32_t index = 0u; index < CSM_CASCADE_COUNT; ++index)
        {
            Expect(MatrixNearlyEqual(first.Cascades[index].View,
                                     moved.Cascades[index].View,
                                     1.0e-4f),
                   "subtexel camera motion keeps the snapped view matrix stable");
            Expect(MatrixNearlyEqual(first.Cascades[index].Projection,
                                     moved.Cascades[index].Projection,
                                     1.0e-4f),
                   "subtexel camera motion keeps the projection scale stable");
            Expect(NearlyEqual(first.Cascades[index].SnappedCenter.x,
                               moved.Cascades[index].SnappedCenter.x),
                   "subtexel camera motion keeps the snapped center stable on x");
        }
    }
}

int main()
{
    TestFourCascadeSplitAndMatrixContract();
    TestCasterDepthRangeAndNonFiniteBoundsAreSafe();
    TestInvalidInputsFallBackToShadowOff();
    TestSubtexelCameraMotionKeepsSnappedMatrices();

    if (g_FailureCount != 0)
    {
        std::cout << "CascadedShadowLightMatricesTest failures: "
                  << g_FailureCount << "\n";
        return 1;
    }

    std::cout << "CascadedShadowLightMatricesTest passed\n";
    return 0;
}
