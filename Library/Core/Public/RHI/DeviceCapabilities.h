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
     * @brief 論理デバイスで有効になったレイトレーシング機能
     */
    struct RayTracingCapabilities
    {
        /** @brief acceleration structure 機能が有効か */
        bool bAccelerationStructure = false;

        /** @brief ray query 機能が有効か */
        bool bRayQuery = false;

        /** @brief レイトレーシングパイプライン機能が有効か */
        bool bRayTracingPipeline = false;
    };

    /**
     * @brief Vulkanの拡張とfeature照会から得たレイトレーシング対応状況
     */
    struct RayTracingFeatureAvailability
    {
        /** @brief VK_KHR_acceleration_structure が利用可能か */
        bool bAccelerationStructureExtension = false;

        /** @brief RT構築に必要なdeferred host operationsが利用可能か */
        bool bDeferredHostOperationsExtension = false;

        /** @brief Vulkan 1.2のbuffer device address featureが利用可能か */
        bool bBufferDeviceAddress = false;

        /** @brief acceleration structure featureが利用可能か */
        bool bAccelerationStructureFeature = false;

        /** @brief VK_KHR_ray_query が利用可能か */
        bool bRayQueryExtension = false;

        /** @brief ray query featureが利用可能か */
        bool bRayQueryFeature = false;

        /** @brief VK_KHR_ray_tracing_pipeline が利用可能か */
        bool bRayTracingPipelineExtension = false;

        /** @brief ray tracing pipeline featureが利用可能か */
        bool bRayTracingPipelineFeature = false;
    };

    /**
     * @brief 拡張・依存機能・featureの結果から有効化できる機能を解決
     *
     * @param availability 拡張とfeatureの個別照会結果
     * @return 論理デバイスで有効化できる機能の組み合わせ
     */
    constexpr RayTracingCapabilities ResolveRayTracingCapabilities(
        const RayTracingFeatureAvailability& availability)
    {
        RayTracingCapabilities capabilities;
        capabilities.bAccelerationStructure =
            availability.bAccelerationStructureExtension &&
            availability.bDeferredHostOperationsExtension &&
            availability.bBufferDeviceAddress &&
            availability.bAccelerationStructureFeature;
        capabilities.bRayQuery =
            capabilities.bAccelerationStructure &&
            availability.bRayQueryExtension &&
            availability.bRayQueryFeature;
        capabilities.bRayTracingPipeline =
            capabilities.bAccelerationStructure &&
            availability.bRayTracingPipelineExtension &&
            availability.bRayTracingPipelineFeature;
        return capabilities;
    }

    /**
     * @brief GPUデバイスの能力情報
     *
     * 物理デバイスがサポートする拡張と、論理デバイスで実際に有効化した
     * コア機能を集約した構造体。VulkanDevice初期化時に検出され、
     * 実行時に参照されます。
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

        /** @brief 論理デバイスで有効になったレイトレーシング機能 */
        RayTracingCapabilities RayTracing;

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

        /** @brief DrawIndexedIndirectCount（Vulkan 1.2コア機能）が論理デバイスで有効か */
        bool bDrawIndirectCount = false;

        /** @brief DrawIndirectFirstInstance（Vulkanコア機能）が論理デバイスで有効か */
        bool bDrawIndirectFirstInstance = false;

        /** @brief バッファのdevice address機能が論理デバイスで有効か */
        bool bBufferDeviceAddress = false;

        /** @brief 64-bit整数シェーダー演算機能が論理デバイスで有効か */
        bool bShaderInt64 = false;
    };

} // namespace NorvesLib::RHI
