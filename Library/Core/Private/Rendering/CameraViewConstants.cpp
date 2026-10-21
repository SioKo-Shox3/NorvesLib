#include "Rendering/CameraViewConstants.h"
#include "Rendering/TemporalAA.h"
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
        constants.CameraRelativeInverseViewProjectionMatrix =
            Math::MatrixUtils::Inverse(constants.ProjectionMatrix * BuildViewRotationMatrix(camera));
        if (camera.Projection == ProjectionType::Perspective && camera.NearPlane > 0.0f &&
            camera.FarPlane > camera.NearPlane)
        {
            constants.DepthNearPlane = camera.NearPlane;
            constants.DepthFarPlane = camera.FarPlane;
        }
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
            constants.CameraRelativeInverseViewProjectionMatrix =
                Math::MatrixUtils::Inverse(constants.ProjectionMatrix * BuildViewRotationMatrix(camera));
        }
        return constants;
    }

    Math::Matrix4x4 CameraViewConstants::BuildViewMatrix(const CameraProxy &camera)
    {
        // 向きは原点から前の向きへの視線で作り、位置は後から平行移動で掛ける。注視点を「位置 + 前」で作ると、
        // 座標が大きい所（荒野の 2 km など）で float の丸め（2 km で約 2.4e-4）が前の向きをずらし、
        // 望遠の視点で画が 1 画素近く記録のカメラからずれる（列ベクトル規約: ビュー = 回転 × 平行移動(−位置)）。
        return BuildViewRotationMatrix(camera) *
               Math::MatrixUtils::CreateTranslation(-camera.PositionX, -camera.PositionY, -camera.PositionZ);
    }

    Math::Matrix4x4 CameraViewConstants::BuildViewRotationMatrix(const CameraProxy &camera)
    {
        const Math::Vector3 forward(camera.ForwardX, camera.ForwardY, camera.ForwardZ);
        const Math::Vector3 up(camera.UpX, camera.UpY, camera.UpZ);
        const Math::Vector3 origin(0.0f, 0.0f, 0.0f);
        return Math::MatrixUtils::CreateLookAt(origin, forward, up);
    }

    Math::Matrix4x4 CameraViewConstants::BuildProjectionMatrix(const CameraProxy &camera, float aspectRatio)
    {
        aspectRatio = NormalizeAspectRatio(aspectRatio);

        if (camera.Projection == ProjectionType::Orthographic)
        {
            return ApplyTemporalAAProjectionJitter(
                Math::MatrixUtils::CreateOrthographic(
                    camera.OrthoWidth,
                    camera.OrthoHeight,
                    camera.NearPlane,
                    camera.FarPlane),
                camera.ProjectionJitterNdcX,
                camera.ProjectionJitterNdcY);
        }

        return ApplyTemporalAAProjectionJitter(
            Math::MatrixUtils::CreatePerspectiveFieldOfView(
                camera.FieldOfView * (PI / 180.0f),
                aspectRatio,
                camera.NearPlane,
                camera.FarPlane),
            camera.ProjectionJitterNdcX,
            camera.ProjectionJitterNdcY);
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

    void CameraViewConstants::CopyShaderViewProjection(float *out) const
    {
        Math::MatrixUtils::TransposeToShaderData(ViewProjectionMatrix, out);
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

    void CameraViewConstants::CopyShaderCameraRelativeInverseViewProjection(float *out) const
    {
        Math::MatrixUtils::TransposeToShaderData(CameraRelativeInverseViewProjectionMatrix, out);
    }

    void CameraViewConstants::CopyDepthLinearization(float *out) const
    {
        const bool bValid = DepthNearPlane > 0.0f && DepthFarPlane > DepthNearPlane;
        out[0] = bValid ? DepthNearPlane : 0.0f;
        out[1] = bValid ? DepthNearPlane / DepthFarPlane : 0.0f;
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
