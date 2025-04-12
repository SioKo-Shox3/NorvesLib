#pragma once

#include "IPipeline.h"
#include <vulkan/vulkan.h>
#include <memory>
#include <vector>
#include <string>

namespace NorvesLib::RHI::Vulkan
{

class VulkanDevice;
class VulkanShader;
class VulkanRenderPass;
class VulkanDescriptorSetLayout;

/**
 * @brief パイプラインのVulkan実装
 */
class VulkanPipeline : public IPipeline
{
public:
    /**
     * @brief グラフィックスパイプライン作成コンストラクタ
     * @param device Vulkanデバイス
     * @param desc パイプライン記述子
     * @param renderPass レンダーパス
     * @param shaders 各ステージのシェーダー配列
     * @param descriptorSetLayouts ディスクリプタセットレイアウト配列
     */
    VulkanPipeline(
        std::shared_ptr<VulkanDevice> device,
        const GraphicsPipelineDesc& desc,
        std::shared_ptr<VulkanRenderPass> renderPass,
        const std::vector<std::shared_ptr<VulkanShader>>& shaders,
        const std::vector<std::shared_ptr<VulkanDescriptorSetLayout>>& descriptorSetLayouts
    );

    /**
     * @brief コンピュートパイプライン作成コンストラクタ
     * @param device Vulkanデバイス
     * @param desc パイプライン記述子
     * @param computeShader コンピュートシェーダー
     * @param descriptorSetLayouts ディスクリプタセットレイアウト配列
     */
    VulkanPipeline(
        std::shared_ptr<VulkanDevice> device,
        const ComputePipelineDesc& desc,
        std::shared_ptr<VulkanShader> computeShader,
        const std::vector<std::shared_ptr<VulkanDescriptorSetLayout>>& descriptorSetLayouts
    );

    /**
     * @brief デストラクタ
     */
    virtual ~VulkanPipeline();

    /**
     * @brief パイプラインがコンピュートパイプラインかどうか
     * @return コンピュートパイプラインの場合true、グラフィックパイプラインの場合false
     */
    virtual bool IsComputePipeline() const override { return m_isCompute; }

    /**
     * @brief バインドポイント数の取得
     * @return バインドポイント（ディスクリプタセット）の数
     */
    virtual uint32_t GetBindPointCount() const override { return static_cast<uint32_t>(m_descriptorSetLayouts.size()); }

    /**
     * @brief Vulkanパイプラインオブジェクトの取得
     * @return パイプラインオブジェクト
     */
    VkPipeline GetVkPipeline() const { return m_pipeline; }

    /**
     * @brief Vulkanパイプラインレイアウトオブジェクトの取得
     * @return パイプラインレイアウトオブジェクト
     */
    VkPipelineLayout GetVkPipelineLayout() const { return m_pipelineLayout; }

    /**
     * @brief コンピュートパイプラインかどうかの取得（別名関数）
     * @return コンピュートパイプラインならtrue
     */
    bool IsCompute() const { return m_isCompute; }

    /**
     * @brief パイプラインレイアウトの取得
     * @return パイプラインレイアウト
     */
    VkPipelineLayout GetPipelineLayout() const { return m_pipelineLayout; }

private:
    /**
     * @brief パイプラインレイアウトの作成
     * @param descriptorSetLayouts ディスクリプタセットレイアウト配列
     */
    void CreatePipelineLayout(const std::vector<std::shared_ptr<VulkanDescriptorSetLayout>>& descriptorSetLayouts);

    /**
     * @brief グラフィックパイプラインの作成
     * @param desc パイプライン記述子
     * @param renderPass レンダーパス
     * @param shaders 各ステージのシェーダー配列
     */
    void CreateGraphicsPipeline(
        const GraphicsPipelineDesc& desc,
        std::shared_ptr<VulkanRenderPass> renderPass,
        const std::vector<std::shared_ptr<VulkanShader>>& shaders
    );

    /**
     * @brief コンピュートパイプラインの作成
     * @param desc パイプライン記述子
     * @param computeShader コンピュートシェーダー
     */
    void CreateComputePipeline(
        const ComputePipelineDesc& desc,
        std::shared_ptr<VulkanShader> computeShader
    );

private:
    std::shared_ptr<VulkanDevice> m_device;                                     ///< Vulkanデバイス
    VkPipeline m_pipeline = VK_NULL_HANDLE;                                      ///< Vulkanパイプライン
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;                         ///< Vulkanパイプラインレイアウト
    std::vector<std::shared_ptr<VulkanDescriptorSetLayout>> m_descriptorSetLayouts; ///< ディスクリプタセットレイアウト配列
    bool m_isCompute = false;                                                   ///< コンピュートパイプラインかどうか
};

} // namespace NorvesLib::RHI::Vulkan