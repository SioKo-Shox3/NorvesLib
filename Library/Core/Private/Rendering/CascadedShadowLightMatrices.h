#pragma once

#include "Rendering/DirectionalShadowLightMatrices.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    inline constexpr uint32_t CSM_CASCADE_COUNT = 4u;
    inline constexpr uint32_t CascadedShadowCascadeCount = CSM_CASCADE_COUNT;

    /**
     * @brief CPU側でCSMの分割とライト行列を構築する設定。
     *
     * カメラのnear/farはCameraProxyから取得し、Directionalは方向ライトの
     * 最小ライト距離だけを共有する。CSMは常に4カスケードで固定する。
     */
    struct CascadedShadowMatrixSettings
    {
        uint32_t CascadeCount = CSM_CASCADE_COUNT;
        float SplitLambda = 0.5f;
        uint32_t ShadowMapResolution = 2048u;
        float DepthPadding = 1.0f;
        DirectionalShadowMatrixSettings Directional;
    };

    /**
     * @brief CSMの1カスケード分のCPU契約。
     */
    struct CascadedShadowCascade
    {
        bool bEnabled = false;
        float NearDistance = 0.0f;
        float FarDistance = 0.0f;
        float Radius = 0.0f;
        float NearDepth = 0.0f;
        float FarDepth = 0.0f;
        float TexelSize = 0.0f;
        float OrthoSize = 0.0f;
        Math::Vector3 Center = Math::Vector3::Zero;
        Math::Vector3 SnappedCenter = Math::Vector3::Zero;
        Math::Vector3 LightPosition = Math::Vector3::Zero;
        Math::Vector3 Direction = Math::Vector3(0.0f, -1.0f, 0.0f);
        Math::Matrix4x4 View = Math::Matrix4x4::Identity;
        Math::Matrix4x4 Projection = Math::Matrix4x4::Identity;
    };

    /**
     * @brief 4カスケードCSMのCPU構築結果。
     *
     * SplitDistances[i]とSplitDistances[i + 1]は各カスケードの
     * NearDistance/FarDistanceと同じ値を共有し、境界の不連続を作らない。
     */
    struct CascadedShadowMatrixResult
    {
        bool bEnabled = false;
        bool bHasMultipleDirectionalLights = false;
        uint32_t CascadeCount = 0;
        uint64_t LightId = 0;
        Math::Vector3 Direction = Math::Vector3(0.0f, -1.0f, 0.0f);
        float SplitDistances[CSM_CASCADE_COUNT + 1] = {};
        CascadedShadowCascade Cascades[CSM_CASCADE_COUNT];
    };

    CascadedShadowMatrixSettings MakeDefaultCascadedShadowMatrixSettings();

    CascadedShadowMatrixResult BuildCascadedShadowLightMatrices(
        const Container::VariableArray<LightProxy>* lightProxies,
        const CameraProxy& camera,
        const CascadedShadowMatrixSettings& settings,
        const Container::VariableArray<BoundingSphere>* casterBounds = nullptr);

    CascadedShadowMatrixResult BuildCascadedShadowLightMatrices(
        const Container::VariableArray<LightProxy>* lightProxies,
        const CameraProxy* camera,
        const CascadedShadowMatrixSettings& settings,
        const Container::VariableArray<BoundingSphere>* casterBounds = nullptr);
} // namespace NorvesLib::Core::Rendering
