#pragma once

#include <cstdint>

namespace NorvesLib::RHI
{

    /**
     * @brief NVIDIA Cooperative Vector (Neural Shaders) 機能情報
     *
     * VK_NV_cooperative_vector 拡張のサポート状況と
     * 対応するプロパティを保持します。
     */
    struct CooperativeVectorCapabilities
    {
        /** @brief VK_NV_cooperative_vector 拡張が利用可能か */
        bool bSupported = false;

        /** @brief Cooperative Vector のシェーダー変換が利用可能か */
        bool bCooperativeVectorShaderConvert = false;

        /** @brief Cooperative Vector のトレーニングが利用可能か */
        bool bCooperativeVectorTraining = false;

        /** @brief サポートされるコンポーネント型の最大数 */
        uint32_t MaxCooperativeVectorComponents = 0;
    };

    /**
     * @brief NVIDIA Cluster Acceleration Structure (Mega Geometry) 機能情報
     *
     * VK_NV_cluster_acceleration_structure 拡張のサポート状況を保持します。
     */
    struct ClusterAccelerationStructureCapabilities
    {
        /** @brief VK_NV_cluster_acceleration_structure 拡張が利用可能か */
        bool bSupported = false;

        /** @brief VK_KHR_acceleration_structure の前提拡張が利用可能か */
        bool bAccelerationStructureSupported = false;

        /** @brief VK_KHR_ray_tracing_pipeline が利用可能か */
        bool bRayTracingPipelineSupported = false;
    };

    /**
     * @brief GPUデバイスの能力情報
     *
     * 物理デバイスがサポートする機能・拡張を集約した構造体。
     * VulkanDevice初期化時に検出され、実行時に参照されます。
     *
     * 使用例:
     * ```cpp
     * const auto& caps = device->GetCapabilities();
     * if (caps.NeuralShaders.bSupported)
     * {
     *     // Cooperative Vector パスを有効化
     * }
     * ```
     */
    struct DeviceCapabilities
    {
        // ========================================
        // NVIDIA Neural Shaders (Cooperative Vector)
        // ========================================

        /** @brief Cooperative Vector 機能 */
        CooperativeVectorCapabilities NeuralShaders;

        // ========================================
        // NVIDIA Mega Geometry (Cluster Acceleration Structure)
        // ========================================

        /** @brief Cluster Acceleration Structure 機能 */
        ClusterAccelerationStructureCapabilities MegaGeometry;

        // ========================================
        // 基本機能フラグ
        // ========================================

        /** @brief GPU名 */
        char DeviceName[256] = {};

        /** @brief NVIDIA GPUかどうか */
        bool bIsNvidia = false;

        /** @brief ディスクリートGPUかどうか */
        bool bIsDiscreteGPU = false;

        /** @brief コンピュートシェーダーのサポート */
        bool bComputeShader = true;

        /** @brief DrawIndexedIndirectCount（Vulkan 1.2コア機能）のサポート */
        bool bDrawIndirectCount = false;
    };

} // namespace NorvesLib::RHI
