#pragma once

#include <cstdint>

#include "RHITypes.h"

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
     * @brief DeviceLocal ヒープの予算と使用量（全 DeviceLocal ヒープの合計）
     *
     * VK_EXT_memory_budget のような取得手段が無いバックエンドでは bValid が false のまま返る。
     * その場合 BudgetBytes と UsageBytes は意味を持たない。取得手段がある場合は bValid が true で、
     * BudgetBytes が 0 のときは異常値として呼び出し側が扱う。
     */
    struct VideoMemoryBudget
    {
        /** @brief OS とドライバがこのプロセスへ見積もるビデオメモリの予算（バイト） */
        uint64_t BudgetBytes = 0;

        /** @brief このプロセスが現在使っているビデオメモリ（バイト） */
        uint64_t UsageBytes = 0;

        /** @brief 取得手段があり、予算と使用量を取得したか（無いバックエンドでは false） */
        bool bValid = false;

        /**
         * @brief 全 DeviceLocal ヒープの大きさの合計（バイト）。bValid に依らず、取れるバックエンドでは埋める
         *
         * 予算・使用量が取れないときに、予算の上限の代わりに呼び出し側が使う。0 は取れていない。
         */
        uint64_t DeviceLocalHeapBytes = 0;
    };

    /**
     * @brief 形式ごとの sparse（部分常駐）2D テクスチャの標準ブロック形状
     *
     * vkGetPhysicalDeviceSparseImageFormatProperties の imageGranularity と標準形状の印。
     * 標準形状なら 64 KiB のタイルが 1 つの結び付け単位になる。
     */
    struct SparseFormatProperties
    {
        /** @brief 照会した形式 */
        Format TextureFormat = Format::UNKNOWN;

        /** @brief 2D・最適タイリング・サンプル数1で sparse のテクスチャを作れるか */
        bool bSupported = false;

        /** @brief 結び付け単位（タイル）の幅（texel） */
        uint32_t GranularityWidth = 0;

        /** @brief 結び付け単位（タイル）の高さ（texel） */
        uint32_t GranularityHeight = 0;

        /** @brief 標準のブロック形状（64 KiB のタイル）か。非標準なら false */
        bool bStandardBlockShape = false;

        /** @brief ミップの末尾（mip tail）が全配列レイヤーで1つにまとまるか（VK_SPARSE_IMAGE_FORMAT_SINGLE_MIPTAIL_BIT） */
        bool bSingleMipTail = false;
    };

    /**
     * @brief 論理デバイスで有効になった sparse（部分常駐）テクスチャの機能
     *
     * いずれも論理デバイスで有効にできたものだけ true。結び付けに使うキューが無ければ
     * bSparseBinding も false になる（その場合 VT は使わず BC の全常駐で描く）。
     */
    struct SparseCapabilities
    {
        /** @brief 照会する形式の最大数 */
        static constexpr uint32_t MaxFormats = 12;

        /** @brief sparseBinding が有効で、結び付け用のキューも見つかったか */
        bool bSparseBinding = false;

        /** @brief 2D の部分常駐（sparseResidencyImage2D）が有効か */
        bool bResidencyImage2D = false;

        /** @brief 別名付けの部分常駐（sparseResidencyAliased）が有効か */
        bool bResidencyAliased = false;

        /** @brief シェーダーで常駐の照会ができる（shaderResourceResidency）か */
        bool bShaderResourceResidency = false;

        /** @brief シェーダーで最小LODを指定できる（shaderResourceMinLod）か */
        bool bShaderResourceMinLod = false;

        /** @brief 照会済みの形式の数 */
        uint32_t FormatCount = 0;

        /** @brief 形式ごとの標準ブロック形状 */
        SparseFormatProperties Formats[MaxFormats] = {};

        /**
         * @brief 形式の標準ブロック形状を引く
         * @return 照会していない形式は nullptr
         */
        const SparseFormatProperties* FindFormat(Format format) const
        {
            for (uint32_t i = 0; i < FormatCount; ++i)
            {
                if (Formats[i].TextureFormat == format)
                {
                    return &Formats[i];
                }
            }
            return nullptr;
        }
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

        /** @brief 配列sampled imageを呼び出しごとに異なる添字で参照できるか（Vulkan 1.2 descriptor indexing） */
        bool bSampledImageArrayNonUniformIndexing = false;

        /** @brief BC1/BC4/BC5/BC7 のブロック圧縮テクスチャ（textureCompressionBC）が論理デバイスで有効か */
        bool bTextureCompressionBC = false;

        /** @brief フラグメントシェーダーから storage buffer へ書く・アトミック操作をする（fragmentStoresAndAtomics）が論理デバイスで有効か */
        bool bFragmentStoresAndAtomics = false;

        /**
         * @brief geometryShader が論理デバイスで有効か
         *
         * ジオメトリシェーダーは使わない。フラグメントシェーダーが gl_PrimitiveID を読むには SPIR-V の Geometry
         * 機能が要り、Vulkan ではこの機能の有効化が前提になる（ビジビリティバッファが三角形の番号を書くのに使う）。
         * false のデバイスでは gl_PrimitiveID を使う経路を使わず、従来の GBuffer の経路で描く。
         */
        bool bGeometryShader = false;

        /**
         * @brief shaderStorageImageExtendedFormats が論理デバイスで有効か
         *
         * RG16F などの形式を、formatless でなく形式を明示した storage image として読み書きできる。
         */
        bool bShaderStorageImageExtendedFormats = false;

        /** @brief sparse（部分常駐）テクスチャの機能と形式ごとの標準ブロック形状 */
        SparseCapabilities Sparse;

        /**
         * @brief 材質のシェーダーが VT のタイルの要求（フィードバック）を書けるか
         *
         * VT は sparse の結び付けと 2D の部分常駐（要求のバッファのリングを作る条件）、常駐の照会
         * （shaderResourceResidency）を前提にし、要求の書き込みはフラグメントシェーダーの storage buffer への
         * 書き込みとアトミック操作が要る。
         * false のデバイスでは材質のシェーダーにフィードバックのコードも binding も入らない（材質は従来どおり描ける）。
         */
        bool SupportsVirtualTextureFeedback() const
        {
            return bFragmentStoresAndAtomics && Sparse.bSparseBinding && Sparse.bResidencyImage2D &&
                   Sparse.bShaderResourceResidency;
        }
    };

} // namespace NorvesLib::RHI
