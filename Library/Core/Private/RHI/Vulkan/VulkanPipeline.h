#pragma once

#include "RHI/IPipeline.h"

#define VULKAN_HPP_NO_CONSTRUCTORS
#include <vulkan/vulkan.hpp>

#include "Container/Containers.h"

namespace NorvesLib::RHI::Vulkan
{

    using namespace NorvesLib::Core::Container;

    class VulkanDevice;
    class VulkanShader;
    class VulkanRenderPass;
    class VulkanDescriptorSetLayout;

    /**
     * @brief Vulkanパイプラインレイアウトのラッパークラス
     */
    class VulkanPipelineLayout
    {
    public:
        VulkanPipelineLayout(TSharedPtr<VulkanDevice> device,
                             const VariableArray<TSharedPtr<VulkanDescriptorSetLayout>> &layouts);
        ~VulkanPipelineLayout();

        vk::PipelineLayout GetVkPipelineLayout() const
        {
            return m_pipelineLayout;
        }

    private:
        TSharedPtr<VulkanDevice> m_device;
        vk::PipelineLayout m_pipelineLayout;
        VariableArray<TSharedPtr<VulkanDescriptorSetLayout>> m_descriptorSetLayouts;
    };

    /**
     * @brief Vulkanパイプラインの基底クラス
     */
    class VulkanPipeline : public IPipeline
    {
    public:
        VulkanPipeline(TSharedPtr<VulkanDevice> device);
        ~VulkanPipeline() override;

        // IDeviceObjectインターフェース実装
        ResourceType GetResourceType() const override
        {
            return ResourceType::Pipeline;
        }

        // IPipelineインターフェース実装
        PipelineType GetPipelineType() const override
        {
            return m_pipelineType;
        }

        // Vulkan固有のメソッド
        vk::Pipeline GetVkPipeline() const
        {
            return m_pipeline;
        }

        vk::PipelineLayout GetVkPipelineLayout() const
        {
            return m_pipelineLayout->GetVkPipelineLayout();
        }

    protected:
        TSharedPtr<VulkanDevice> m_device;
        vk::Pipeline m_pipeline;
        TSharedPtr<VulkanPipelineLayout> m_pipelineLayout;
        PipelineType m_pipelineType;
    };

    /**
     * @brief Vulkanグラフィックスパイプラインの実装クラス
     */
    class VulkanGraphicsPipeline : public VulkanPipeline
    {
    public:
        VulkanGraphicsPipeline(TSharedPtr<VulkanDevice> device,
                               const GraphicsPipelineDesc &desc);
        ~VulkanGraphicsPipeline() override;

    private:
        GraphicsPipelineDesc m_desc;

        void CreateGraphicsPipeline();

        // ヘルパーメソッド
        vk::PrimitiveTopology ConvertPrimitiveTopology(PrimitiveTopology topology);
        vk::PolygonMode ConvertPolygonMode(PolygonMode mode);
        vk::CullModeFlags ConvertCullMode(CullMode mode);
        vk::FrontFace ConvertFrontFace(FrontFace frontFace);
        vk::CompareOp ConvertCompareOp(CompareOp op);
        vk::StencilOp ConvertStencilOp(StencilOp op);
        vk::BlendFactor ConvertBlendFactor(BlendFactor factor);
        vk::BlendOp ConvertBlendOp(BlendOp op);
        vk::ColorComponentFlags ConvertColorWriteMask(ColorWriteMask mask);
    };

    /**
     * @brief Vulkanコンピュートパイプラインの実装クラス
     */
    class VulkanComputePipeline : public VulkanPipeline
    {
    public:
        VulkanComputePipeline(TSharedPtr<VulkanDevice> device,
                              const ComputePipelineDesc &desc);
        ~VulkanComputePipeline() override;

    private:
        ComputePipelineDesc m_desc;

        void CreateComputePipeline();
    };

} // namespace NorvesLib::RHI::Vulkan
