#include "Rendering/CameraViewConstants.h"
#include "Math/MatrixUtils.h"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace NorvesLib::Core::Rendering;

namespace
{
    CameraProxy MakeCamera()
    {
        CameraProxy camera;
        camera.PositionX = 0.0f;
        camera.PositionY = 0.0f;
        camera.PositionZ = 5.0f;
        camera.ForwardX = 0.0f;
        camera.ForwardY = 0.0f;
        camera.ForwardZ = -1.0f;
        camera.UpX = 0.0f;
        camera.UpY = 1.0f;
        camera.UpZ = 0.0f;
        camera.FieldOfView = 60.0f;
        camera.NearPlane = 0.1f;
        camera.FarPlane = 1000.0f;
        camera.Viewport.Width = 1280.0f;
        camera.Viewport.Height = 720.0f;
        return camera;
    }

    bool IsNearlyEqual(float lhs, float rhs, float tolerance = 0.0001f)
    {
        return std::abs(lhs - rhs) <= tolerance;
    }

    void AssertFloatArrayNear(const float* actual, const float* expected, uint32_t count)
    {
        for (uint32_t i = 0; i < count; ++i)
        {
            assert(IsNearlyEqual(actual[i], expected[i]));
        }
    }

    float PlaneNormalLength(const NorvesLib::Math::Vector4 &plane)
    {
        return std::sqrt(plane.x * plane.x + plane.y * plane.y + plane.z * plane.z);
    }
}

int main()
{
    std::cout << "CameraViewConstantsTest start\n";

    {
        CameraProxy camera = MakeCamera();
        const CameraViewConstants constants = CameraViewConstants::Build(camera, 16.0f / 9.0f);

        assert(constants.bHasCamera);
        assert(IsNearlyEqual(constants.AspectRatio, 16.0f / 9.0f));
        assert(IsNearlyEqual(constants.CameraPosition[0], camera.PositionX));
        assert(IsNearlyEqual(constants.CameraPosition[1], camera.PositionY));
        assert(IsNearlyEqual(constants.CameraPosition[2], camera.PositionZ));
        assert(IsNearlyEqual(constants.CameraPosition[3], 1.0f));

        float shaderView[16] = {};
        float shaderProjection[16] = {};
        float shaderInverseViewProjection[16] = {};
        constants.CopyShaderView(shaderView);
        constants.CopyShaderProjection(shaderProjection);
        constants.CopyShaderInverseViewProjection(shaderInverseViewProjection);

        const float expectedShaderView[16] =
        {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, -5.0f, 1.0f
        };
        const float expectedShaderProjection[16] =
        {
            0.97427857f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.7320508f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0001f, 1.0f,
            0.0f, 0.0f, -0.10001f, 0.0f
        };

        AssertFloatArrayNear(shaderView, expectedShaderView, 16);
        AssertFloatArrayNear(shaderProjection, expectedShaderProjection, 16);
        assert(shaderInverseViewProjection[0] != 0.0f);
    }

    {
        CameraProxy camera = MakeCamera();
        const CameraViewConstants constants = CameraViewConstants::Build(camera, 16.0f / 9.0f);

        const auto visibleSphere = NorvesLib::Math::MatrixUtils::TransformSphereToClipSpace(
            constants.CullingViewProjectionMatrix,
            NorvesLib::Math::Vector3(0.0f, 0.0f, 0.0f),
            1.0f);
        assert(NorvesLib::Math::MatrixUtils::IntersectsClipSpace(
            visibleSphere,
            NorvesLib::Math::ClipSpaceDepthRange::ZeroToOne));

        const auto behindCameraSphere = NorvesLib::Math::MatrixUtils::TransformSphereToClipSpace(
            constants.CullingViewProjectionMatrix,
            NorvesLib::Math::Vector3(0.0f, 0.0f, 8.0f),
            0.5f);
        assert(!NorvesLib::Math::MatrixUtils::IntersectsClipSpace(
            behindCameraSphere,
            NorvesLib::Math::ClipSpaceDepthRange::ZeroToOne));
    }

    {
        CameraProxy camera = MakeCamera();
        const CameraViewConstants constants = CameraViewConstants::Build(camera, 16.0f / 9.0f);
        const auto frustumPlanes = NorvesLib::Math::MatrixUtils::ExtractClipSpaceFrustumPlanes(
            constants.CullingViewProjectionMatrix,
            NorvesLib::Math::ClipSpaceDepthRange::ZeroToOne);

        float shaderPlanes[6][4] = {};
        frustumPlanes.CopyToShaderData(shaderPlanes);

        for (int i = 0; i < 6; ++i)
        {
            assert(IsNearlyEqual(PlaneNormalLength(frustumPlanes.Planes[i]), 1.0f));
            assert(IsNearlyEqual(shaderPlanes[i][0], frustumPlanes.Planes[i].x));
            assert(IsNearlyEqual(shaderPlanes[i][1], frustumPlanes.Planes[i].y));
            assert(IsNearlyEqual(shaderPlanes[i][2], frustumPlanes.Planes[i].z));
            assert(IsNearlyEqual(shaderPlanes[i][3], frustumPlanes.Planes[i].w));
        }
    }

    {
        CameraProxy camera = MakeCamera();
        const CameraViewConstants constants = CameraViewConstants::Build(camera, 0.0f);
        assert(IsNearlyEqual(constants.AspectRatio, 1.0f));
    }

    {
        CameraProxy camera = MakeCamera();
        camera.Projection = ProjectionType::Orthographic;
        camera.OrthoWidth = 80.0f;
        camera.OrthoHeight = 25.0f;
        camera.NearPlane = 0.5f;
        camera.FarPlane = 9.5f;

        const CameraViewConstants constants = CameraViewConstants::Build(camera, 4.0f);
        const NorvesLib::Math::Matrix4x4 expected =
            NorvesLib::Math::MatrixUtils::CreateOrthographic(80.0f, 25.0f, 0.5f, 9.5f);
        assert(NorvesLib::Math::MatrixUtils::ApproxEqual(constants.ProjectionMatrix, expected, 0.0001f));
        const float projectionScaleY = constants.ProjectionMatrix.GetRow(1).y;
        assert(IsNearlyEqual(projectionScaleY, 2.0f / 25.0f));
        assert(!IsNearlyEqual(projectionScaleY, 2.0f / 20.0f));
    }

    std::cout << "CameraViewConstantsTest passed\n";
    return 0;
}
