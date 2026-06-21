#include "Rendering/CameraViewConstants.h"
#include "RHI/IDevice.h"
#include "Math/MatrixUtils.h"
#include <algorithm>
#include <cstring>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        constexpr float PI = 3.14159265f;

        float NormalizeAspectRatio(float aspectRatio)
        {
            return aspectRatio > 0.0f ? aspectRatio : 1.0f;
        }
    }

    CameraViewConstants CameraViewConstants::Build(const CameraProxy &camera, float aspectRatio)
    {
        CameraViewConstants constants;
        constants.AspectRatio = NormalizeAspectRatio(aspectRatio);
        constants.FieldOfViewRadians = camera.FieldOfView * (PI / 180.0f);
        constants.CameraPosition[0] = camera.PositionX;
        constants.CameraPosition[1] = camera.PositionY;
        constants.CameraPosition[2] = camera.PositionZ;
        constants.CameraPosition[3] = 1.0f;
        constants.bHasCamera = true;

        constants.ViewMatrix = BuildViewMatrix(camera);
        constants.ProjectionMatrix = BuildProjectionMatrix(camera, constants.AspectRatio);
        constants.ViewProjectionMatrix = constants.ProjectionMatrix * constants.ViewMatrix;
        constants.InverseViewMatrix = Math::MatrixUtils::Inverse(constants.ViewMatrix);
        constants.InverseProjectionMatrix = Math::MatrixUtils::Inverse(constants.ProjectionMatrix);
        constants.InverseViewProjectionMatrix = Math::MatrixUtils::Inverse(constants.ViewProjectionMatrix);
        constants.CullingViewProjectionMatrix = BuildCullingViewProjectionMatrix(camera, constants.AspectRatio);
        return constants;
    }

    CameraViewConstants CameraViewConstants::BuildForDevice(const CameraProxy &camera,
                                                            float aspectRatio,
                                                            const RHI::IDevice *device,
                                                            bool bApplyYFlip)
    {
        CameraViewConstants constants = Build(camera, aspectRatio);
        if (device)
        {
            constants.ProjectionMatrix = device->AdjustProjectionForClipSpace(constants.ProjectionMatrix, bApplyYFlip);
            constants.ViewProjectionMatrix = constants.ProjectionMatrix * constants.ViewMatrix;
            constants.InverseProjectionMatrix = Math::MatrixUtils::Inverse(constants.ProjectionMatrix);
            constants.InverseViewProjectionMatrix = Math::MatrixUtils::Inverse(constants.ViewProjectionMatrix);
        }
        return constants;
    }

    Math::Matrix4x4 CameraViewConstants::BuildViewMatrix(const CameraProxy &camera)
    {
        const Math::Vector3 cameraPosition(
            camera.PositionX,
            camera.PositionY,
            camera.PositionZ);
        const Math::Vector3 forward(camera.ForwardX, camera.ForwardY, camera.ForwardZ);
        const Math::Vector3 up(camera.UpX, camera.UpY, camera.UpZ);
        return Math::MatrixUtils::CreateLookAt(cameraPosition, cameraPosition + forward, up);
    }

    Math::Matrix4x4 CameraViewConstants::BuildProjectionMatrix(const CameraProxy &camera, float aspectRatio)
    {
        aspectRatio = NormalizeAspectRatio(aspectRatio);

        if (camera.Projection == ProjectionType::Orthographic)
        {
            return Math::MatrixUtils::CreateOrthographic(
                camera.OrthoWidth,
                camera.OrthoHeight,
                camera.NearPlane,
                camera.FarPlane);
        }

        return Math::MatrixUtils::CreatePerspectiveFieldOfView(
            camera.FieldOfView * (PI / 180.0f),
            aspectRatio,
            camera.NearPlane,
            camera.FarPlane);
    }

    Math::Matrix4x4 CameraViewConstants::BuildCullingViewProjectionMatrix(const CameraProxy &camera, float aspectRatio)
    {
        const Math::Matrix4x4 viewMatrix = BuildViewMatrix(camera);
        const Math::Matrix4x4 projectionMatrix = BuildProjectionMatrix(camera, aspectRatio);
        const Math::Matrix4x4 clipSpaceCorrection = Math::MatrixUtils::CreateScale(1.0f, 1.0f, -1.0f);
        return projectionMatrix * clipSpaceCorrection * viewMatrix;
    }

    void CameraViewConstants::CopyShaderView(float *out) const
    {
        Math::MatrixUtils::TransposeToShaderData(ViewMatrix, out);
    }

    void CameraViewConstants::CopyShaderProjection(float *out) const
    {
        Math::MatrixUtils::TransposeToShaderData(ProjectionMatrix, out);
    }

    void CameraViewConstants::CopyShaderInverseView(float *out) const
    {
        Math::MatrixUtils::TransposeToShaderData(InverseViewMatrix, out);
    }

    void CameraViewConstants::CopyShaderInverseProjection(float *out) const
    {
        Math::MatrixUtils::TransposeToShaderData(InverseProjectionMatrix, out);
    }

    void CameraViewConstants::CopyShaderInverseViewProjection(float *out) const
    {
        Math::MatrixUtils::TransposeToShaderData(InverseViewProjectionMatrix, out);
    }

    void CameraViewConstants::CopyCameraPosition(float *out, uint32_t count) const
    {
        if (!out || count == 0)
        {
            return;
        }
        const uint32_t copyCount = std::min(count, 4u);
        std::memcpy(out, CameraPosition, sizeof(float) * copyCount);
    }

} // namespace NorvesLib::Core::Rendering
