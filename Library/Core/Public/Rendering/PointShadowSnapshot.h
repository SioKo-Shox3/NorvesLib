#pragma once

#include "Rendering/SceneProxy.h"
#include "Rendering/RenderTypes.h"
#include "Container/Containers.h"
#include "Math/Matrix4x4.h"
#include "Math/Vector3.h"

#include "Math/MathTypes.h"
#include "Math/MatrixUtils.h"
#include "Math/VectorUtils.h"

#include <cmath>
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /** @brief キューブシャドウを描く点光源の最大数 */
    inline constexpr uint32_t PointShadowMaxLights = 4u;

    /** @brief キューブの面の数。面の番号はVulkanのキューブの層の順（+X,-X,+Y,-Y,+Z,-Z） */
    inline constexpr uint32_t PointShadowFaceCount = 6u;

    /** @brief キューブの面を描くときのnear面（m）。光源の範囲がこれより小さい灯は影を描かない。 */
    inline constexpr float PointShadowNearPlane = 0.05f;

    /**
     * @brief 点光源のキューブの6面のビュー行列と共通の射影行列
     *
     * View・Projectionは列ベクトル規約（CSMの行列と同じ）。Projectionはクリップ空間の補正前の
     * 値で、描く側はIDevice::AdjustProjectionForClipSpace(Projection, false)を掛けて使う。
     * Vulkanの補正後、面fの行列で光源からの方向dを写すと、NDCのx・yは
     * Vulkanのキューブの面の選び方（sc/|ma|, tc/|ma|）と一致する。
     */
    struct PointShadowFaceMatrices
    {
        Math::Matrix4x4 Views[PointShadowFaceCount];
        Math::Matrix4x4 Projection = Math::Matrix4x4::Identity;
    };

    /**
     * @brief キューブシャドウを描く1灯のスナップショット
     */
    struct PointShadowLightSnapshot
    {
        uint64_t LightId = 0;
        /** @brief FramePacket::Scene.LightProxies内の番号 */
        uint32_t LightIndex = 0;
        Math::Vector3 Position = Math::Vector3(0.0f, 0.0f, 0.0f);
        /** @brief 光源の範囲（m）。キューブへ書く距離はこれで割って0〜1にする。 */
        float Range = 0.0f;
        float NearPlane = PointShadowNearPlane;
        PointShadowFaceMatrices Faces;
    };

    /**
     * @brief 1フレームでキューブシャドウを描く点光源の選択（GameThreadで作り、RenderThreadは読むだけ）
     *
     * Lights[i]はキューブ配列のi番目のキューブ（層6i〜6i+5）に対応する。
     */
    struct PointShadowSnapshot
    {
        PointShadowLightSnapshot Lights[PointShadowMaxLights];
        uint32_t LightCount = 0;

        void Clear()
        {
            for (PointShadowLightSnapshot& light : Lights)
            {
                light = PointShadowLightSnapshot{};
            }
            LightCount = 0;
        }

        bool IsEmpty() const { return LightCount == 0; }
    };

    namespace PointShadowDetail
    {
        // 面ごとの視線方向と上方向。Vulkanのキューブの面の選び方（sc, tc）で、面の画像の
        // 右がsc、下がtcの向きになるよう、ビュー空間の+Xをsc、+Yをtcに合わせる。
        // （ビュー空間の+Yはクリップ空間の+Yに写り、Vulkanでは画像の下向きになる）
        struct PointShadowFaceBasis
        {
            Math::Vector3 Forward;
            Math::Vector3 Up;
        };

        inline const PointShadowFaceBasis& GetFaceBasis(uint32_t faceIndex)
        {
            static const PointShadowFaceBasis Bases[PointShadowFaceCount] = {
                {Math::Vector3(1.0f, 0.0f, 0.0f), Math::Vector3(0.0f, -1.0f, 0.0f)},  // +X: sc=-z, tc=-y
                {Math::Vector3(-1.0f, 0.0f, 0.0f), Math::Vector3(0.0f, -1.0f, 0.0f)}, // -X: sc=+z, tc=-y
                {Math::Vector3(0.0f, 1.0f, 0.0f), Math::Vector3(0.0f, 0.0f, 1.0f)},   // +Y: sc=+x, tc=+z
                {Math::Vector3(0.0f, -1.0f, 0.0f), Math::Vector3(0.0f, 0.0f, -1.0f)}, // -Y: sc=+x, tc=-z
                {Math::Vector3(0.0f, 0.0f, 1.0f), Math::Vector3(0.0f, -1.0f, 0.0f)},  // +Z: sc=+x, tc=-y
                {Math::Vector3(0.0f, 0.0f, -1.0f), Math::Vector3(0.0f, -1.0f, 0.0f)}, // -Z: sc=-x, tc=-y
            };
            return Bases[faceIndex];
        }

        inline bool IsFinitePosition(float x, float y, float z)
        {
            return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
        }
    } // namespace PointShadowDetail

    /**
     * @brief キューブシャドウの対象になる点光源か（影を落とす・表示中・範囲がnear面より大きい）
     */
    inline bool IsEligiblePointShadowLight(const LightProxy& proxy)
    {
        return proxy.Type == LightType::Point &&
               proxy.bCastShadows &&
               proxy.IsValid() &&
               std::isfinite(proxy.CanonicalIntensity) &&
               std::isfinite(proxy.Range) &&
               proxy.Range > PointShadowNearPlane &&
               PointShadowDetail::IsFinitePosition(proxy.PositionX, proxy.PositionY, proxy.PositionZ);
    }

    /**
     * @brief 光源の位置から6面のビュー行列と90°の射影行列を作ります
     * @param position 光源の位置
     * @param nearPlane near面（m、0より大きい）
     * @param farPlane far面（m、nearより大きい）
     */
    inline PointShadowFaceMatrices BuildPointShadowFaceMatrices(const Math::Vector3& position,
                                                                float nearPlane,
                                                                float farPlane)
    {
        PointShadowFaceMatrices result;
        for (uint32_t faceIndex = 0; faceIndex < PointShadowFaceCount; ++faceIndex)
        {
            const PointShadowDetail::PointShadowFaceBasis& basis =
                PointShadowDetail::GetFaceBasis(faceIndex);
            result.Views[faceIndex] =
                Math::MatrixUtils::CreateLookAt(position, position + basis.Forward, basis.Up);
        }
        // 90°の正方形の視錐台で、6面が隙間なくすべての方向を覆う。
        result.Projection = Math::MatrixUtils::CreatePerspectiveFieldOfView(
            Math::Constants::PI * 0.5f, 1.0f, nearPlane, farPlane);
        return result;
    }

    /**
     * @brief 影を落とす点光源を選び、6面の行列を作ります
     *
     * 対象はIsEligiblePointShadowLightを満たす点光源で、カメラの位置からの距離が近い順に
     * 最大PointShadowMaxLights灯を選ぶ（距離が同じならLightProxiesの並び順）。カメラが
     * 無いときはLightProxiesの並び順で選ぶ。
     */
    inline void BuildPointShadowSnapshot(const Container::VariableArray<LightProxy>& lightProxies,
                                         const CameraProxy* camera,
                                         PointShadowSnapshot& outSnapshot)
    {
        outSnapshot.Clear();

        const bool bHasCamera =
            camera != nullptr &&
            PointShadowDetail::IsFinitePosition(camera->PositionX,
                                                camera->PositionY,
                                                camera->PositionZ);
        const Math::Vector3 cameraPosition =
            bHasCamera ? Math::Vector3(camera->PositionX, camera->PositionY, camera->PositionZ)
                       : Math::Vector3::Zero;

        // 近い順に最大PointShadowMaxLights灯を保つ（距離が同じなら先に現れた灯を優先する）。
        uint32_t selectedIndices[PointShadowMaxLights] = {};
        float selectedDistances[PointShadowMaxLights] = {};
        uint32_t selectedCount = 0;
        for (uint32_t lightIndex = 0; lightIndex < lightProxies.size(); ++lightIndex)
        {
            const LightProxy& proxy = lightProxies[lightIndex];
            if (!IsEligiblePointShadowLight(proxy))
            {
                continue;
            }

            const float distanceSquared =
                bHasCamera
                    ? Math::VectorUtils::DistanceSquared(
                          cameraPosition,
                          Math::Vector3(proxy.PositionX, proxy.PositionY, proxy.PositionZ))
                    : 0.0f;

            uint32_t insertAt = selectedCount;
            while (insertAt > 0 && selectedDistances[insertAt - 1] > distanceSquared)
            {
                --insertAt;
            }
            if (insertAt >= PointShadowMaxLights)
            {
                continue;
            }

            const uint32_t lastIndex =
                selectedCount < PointShadowMaxLights ? selectedCount : PointShadowMaxLights - 1;
            for (uint32_t shift = lastIndex; shift > insertAt; --shift)
            {
                selectedIndices[shift] = selectedIndices[shift - 1];
                selectedDistances[shift] = selectedDistances[shift - 1];
            }
            selectedIndices[insertAt] = lightIndex;
            selectedDistances[insertAt] = distanceSquared;
            if (selectedCount < PointShadowMaxLights)
            {
                ++selectedCount;
            }
        }

        for (uint32_t slot = 0; slot < selectedCount; ++slot)
        {
            const LightProxy& proxy = lightProxies[selectedIndices[slot]];
            PointShadowLightSnapshot& light = outSnapshot.Lights[slot];
            light.LightId = proxy.LightId;
            light.LightIndex = selectedIndices[slot];
            light.Position = Math::Vector3(proxy.PositionX, proxy.PositionY, proxy.PositionZ);
            light.Range = proxy.Range;
            light.NearPlane = PointShadowNearPlane;
            light.Faces = BuildPointShadowFaceMatrices(light.Position, light.NearPlane, light.Range);
        }
        outSnapshot.LightCount = selectedCount;
    }

    /**
     * @brief 物体の境界球が光源の範囲の球と交わるか（キューブへ描くキャスターの選別）
     *
     * 境界球が無効（半径0以下・非有限）なら安全側として交わるとみなす。
     */
    inline bool PointShadowCasterIntersectsLight(const BoundingSphere& casterBounds,
                                                 const PointShadowLightSnapshot& light)
    {
        if (!(casterBounds.Radius > 0.0f) || !std::isfinite(casterBounds.Radius) ||
            !PointShadowDetail::IsFinitePosition(casterBounds.CenterX,
                                                 casterBounds.CenterY,
                                                 casterBounds.CenterZ))
        {
            return true;
        }

        const float reach = casterBounds.Radius + light.Range;
        const float distanceSquared = Math::VectorUtils::DistanceSquared(
            light.Position,
            Math::Vector3(casterBounds.CenterX, casterBounds.CenterY, casterBounds.CenterZ));
        return distanceSquared <= reach * reach;
    }
} // namespace NorvesLib::Core::Rendering
