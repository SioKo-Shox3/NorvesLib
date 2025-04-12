#pragma once

#include "RHI/Public/IPipeline.h"
#include <vulkan/vulkan.h>
#include <memory>
#include "Core/Public/Container/Containers.h"

namespace NorvesLib::RHI::Vulkan
{

class VulkanDevice;
class VulkanShader;
class VulkanRenderPass;
class VulkanDescriptorSetLayout;

/**
 * @brief VulkanパイプラインレイアウトのラッパークラS
 */
class VulkanPipelineLayout
{
public:
    VulkanPipelineLayout(std::shared_ptr<VulkanDevice> device, 
                          const NorvesLib::Core::Container::VariableArray<std::shared_ptr<VulkanDescriptorSetLayout>>& layouts);
    ~VulkanPipelineLayout();

    VkPipelineLayout GetVkPipelineLayout() const { return m_pipelineLayout; }

private:
    std::shared_ptr<VulkanDevice> m_device;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    NorvesLib::Core::Container::VariableArray<std::shared_ptr<VulkanDescriptorSetLayout>> m_descriptorSetLayouts;
};

/**
 * @brief Vulkanパイプラインの基底クラス
 */
class VulkanPipeline : public IPipeline
{
public:
    VulkanPipeline(std::shared_ptr<VulkanDevice> device);
    ~VulkanPipeline() override;

    // IDeviceObjectインターフェース実装
    ResourceType GetResourceType() const override { return ResourceType::Pipeline; }

    // IPipelineインターフェース実装
    PipelineType GetPipelineType() const override { return m_pipelineType; }

    // Vulkan固有のメソッド
    VkPipeline GetVkPipeline() const { return m_pipeline; }
    VkPipelineLayout GetVkPipelineLayout() const { return m_pipelineLayout->GetVkPipelineLayout(); }

protected:
    std::shared_ptr<VulkanDevice> m_device;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    std::shared_ptr<VulkanPipelineLayout> m_pipelineLayout;
    PipelineType m_pipelineType;
};

/**
 * @brief Vulkanグラフィックスパイプラインの実装クラス
 */
class VulkanGraphicsPipeline : public VulkanPipeline
{
public:
    VulkanGraphicsPipeline(std::shared_ptr<VulkanDevice> device, 
                           const GraphicsPipelineDesc& desc);
    ~VulkanGraphicsPipeline() override;

private:
    GraphicsPipelineDesc m_desc;
    
    void CreateGraphicsPipeline();
    
    // ヘルパーメソッド
    VkPrimitiveTopology ConvertPrimitiveTopology(PrimitiveTopology topology);
    VkPolygonMode ConvertPolygonMode(PolygonMode mode);
    VkCullModeFlags ConvertCullMode(CullMode mode);
    VkFrontFace ConvertFrontFace(FrontFace frontFace);
    VkCompareOp ConvertCompareOp(CompareOp op);
    VkStencilOp ConvertStencilOp(StencilOp op);
    VkBlendFactor ConvertBlendFactor(BlendFactor factor);
    VkBlendOp ConvertBlendOp(BlendOp op);
    VkColorComponentFlags ConvertColorWriteMask(ColorWriteMask mask);
};

/**
 * @brief Vulkanコンピュートパイプラインの実装クラス
 */
class VulkanComputePipeline : public VulkanPipeline
{
public:
    VulkanComputePipeline(std::shared_ptr<VulkanDevice> device, 
                          const ComputePipelineDesc& desc);
    ~VulkanComputePipeline() override;

private:
    ComputePipelineDesc m_desc;
    
    void CreateComputePipeline();
};

} // namespace NorvesLib::RHI::Vulkan