#pragma once

#include "SceneProxy.h"
#include "Math/Matrix4x4.h"
#include <cstdint>

namespace NorvesLib::RHI
{
    class IDevice;
}

namespace NorvesLib::Core::Rendering
{
    /**
     * @brief Render camera constants derived from CameraProxy.
     *
     * This keeps view/projection construction in one place so CPU culling,
     * GPU shader inputs, and inverse matrices share the same camera convention.
     */
    struct CameraViewConstants
    {
        Math::Matrix4x4 ViewMatrix = Math::Matrix4x4::Identity;
        Math::Matrix4x4 ProjectionMatrix = Math::Matrix4x4::Identity;
        Math::Matrix4x4 ViewProjectionMatrix = Math::Matrix4x4::Identity;
        Math::Matrix4x4 InverseViewMatrix = Math::Matrix4x4::Identity;
        Math::Matrix4x4 InverseProjectionMatrix = Math::Matrix4x4::Identity;
        Math::Matrix4x4 InverseViewProjectionMatrix = Math::Matrix4x4::Identity;
        Math::Matrix4x4 CullingViewProjectionMatrix = Math::Matrix4x4::Identity;

        float CameraPosition[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        float AspectRatio = 1.0f;
        float FieldOfViewRadians = 0.0f;
        bool bHasCamera = false;

        static CameraViewConstants Build(const CameraProxy &camera, float aspectRatio);
        static CameraViewConstants BuildForDevice(const CameraProxy &camera,
                                                  float aspectRatio,
                                                  const RHI::IDevice *device,
                                                  bool bApplyYFlip = true);

        static Math::Matrix4x4 BuildViewMatrix(const CameraProxy &camera);
        static Math::Matrix4x4 BuildProjectionMatrix(const CameraProxy &camera, float aspectRatio);
        static Math::Matrix4x4 BuildCullingViewProjectionMatrix(const CameraProxy &camera, float aspectRatio);

        void CopyShaderView(float *out) const;
        void CopyShaderProjection(float *out) const;
        void CopyShaderInverseView(float *out) const;
        void CopyShaderInverseProjection(float *out) const;
        void CopyShaderInverseViewProjection(float *out) const;
        void CopyCameraPosition(float *out, uint32_t count = 4) const;
    };

} // namespace NorvesLib::Core::Rendering
