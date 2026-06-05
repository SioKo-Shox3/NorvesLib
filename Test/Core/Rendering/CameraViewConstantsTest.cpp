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

        assert(IsNearlyEqual(shaderView[0], constants.ViewMatrix.m00));
        assert(IsNearlyEqual(shaderProjection[5], constants.ProjectionMatrix.m11));
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
        const CameraViewConstants constants = CameraViewConstants::Build(camera, 0.0f);
        assert(IsNearlyEqual(constants.AspectRatio, 1.0f));
    }

    std::cout << "CameraViewConstantsTest passed\n";
    return 0;
}
