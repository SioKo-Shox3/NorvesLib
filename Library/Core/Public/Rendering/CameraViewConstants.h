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
        // カメラの位置を原点にした逆ビュー射影行列（ビューの平行移動を除く）。深度から位置を戻す側は、これで画素の向きだけを求め、
        // 前方への距離は深度から DepthNearPlane・DepthFarPlane で求める。絶対座標の逆ビュー射影行列で戻すと、数 km 先では
        // 1 - 深度が小さく float の桁落ちで位置が m 単位でずれ、そのずれが TAA のジッタのたびに変わる（太陽の影の判定が揺れる）
        Math::Matrix4x4 CameraRelativeInverseViewProjectionMatrix = Math::Matrix4x4::Identity;
        // 透視投影の近方面・遠方面（m）。正射影・不正な値では 0（深度から前方への距離を求めない）
        float DepthNearPlane = 0.0f;
        float DepthFarPlane = 0.0f;

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
        // ビュー行列の回転だけ（カメラの位置を原点にしたビュー行列）
        static Math::Matrix4x4 BuildViewRotationMatrix(const CameraProxy &camera);
        static Math::Matrix4x4 BuildProjectionMatrix(const CameraProxy &camera, float aspectRatio);
        static Math::Matrix4x4 BuildCullingViewProjectionMatrix(const CameraProxy &camera, float aspectRatio);

        void CopyShaderView(float *out) const;
        void CopyShaderProjection(float *out) const;
        void CopyShaderViewProjection(float *out) const;
        void CopyShaderInverseView(float *out) const;
        void CopyShaderInverseProjection(float *out) const;
        void CopyShaderInverseViewProjection(float *out) const;
        void CopyShaderCameraRelativeInverseViewProjection(float *out) const;
        // 深度から前方への距離を求める定数: out[0] = 近方面（m）、out[1] = 近方面 ÷ 遠方面。前方への距離は
        // out[0] / ((1 - 深度) + 深度 * out[1])。正射影では 0, 0
        void CopyDepthLinearization(float *out) const;
        void CopyCameraPosition(float *out, uint32_t count = 4) const;
    };

} // namespace NorvesLib::Core::Rendering
