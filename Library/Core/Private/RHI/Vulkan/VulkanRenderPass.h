#pragma once

#include "RHI/IRenderPass.h"
#define VULKAN_HPP_NO_CONSTRUCTORS
#include <vulkan/vulkan.hpp>
#include "Container/Containers.h"

namespace NorvesLib::RHI::Vulkan
{

    class VulkanDevice;

    /**
     * @brief Vulkanレンダーパスの実装クラス (vulkan.hpp使用)
     */
    class VulkanRenderPass : public IRenderPass
    {
    public:
        /**
         * @brief VulkanRenderPassのコンストラクタ
         * @param device Vulkanデバイス
         * @param desc レンダーパス記述子
         */
        VulkanRenderPass(NorvesLib::Core::Container::TSharedPtr<VulkanDevice> device, const RenderPassDesc &desc);

        /**
         * @brief デストラクタ
         */
        ~VulkanRenderPass() override;

        // IDeviceObjectインターフェース実装
        ResourceType GetResourceType() const override { return ResourceType::RenderPass; }

        // IRenderPassインターフェース実装
        const RenderPassDesc &GetDesc() const override { return m_desc; }
        uint32_t GetColorAttachmentCount() const override;
        bool HasDepthStencilAttachment() const override;
        Format GetColorAttachmentFormat(uint32_t index) const override;
        Format GetDepthStencilFormat() const override;

        // Vulkan固有のメソッド (vulkan.hpp型)
        vk::RenderPass GetVkRenderPass() const { return m_renderPass; }

    private:
        NorvesLib::Core::Container::TSharedPtr<VulkanDevice> m_device;
        RenderPassDesc m_desc;
        vk::RenderPass m_renderPass;

        // アタッチメント情報 (vulkan.hpp型)
        NorvesLib::Core::Container::VariableArray<vk::AttachmentDescription> m_attachmentDescs;
        NorvesLib::Core::Container::VariableArray<vk::AttachmentReference> m_colorAttachmentRefs;
        NorvesLib::Core::Container::VariableArray<vk::AttachmentReference> m_inputAttachmentRefs;
        vk::AttachmentReference m_depthAttachmentRef;

        // ヘルパーメソッド
        void CreateRenderPass();
        vk::Format GetVulkanFormat(Format format) const;
        vk::AttachmentLoadOp GetVulkanLoadOp(AttachmentLoadOp op) const;
        vk::AttachmentStoreOp GetVulkanStoreOp(AttachmentStoreOp op) const;
        vk::ImageLayout GetVulkanInitialLayout(ResourceState state) const;
        vk::ImageLayout GetVulkanFinalLayout(ResourceState state) const;
    };

} // namespace NorvesLib::RHI::Vulkan
