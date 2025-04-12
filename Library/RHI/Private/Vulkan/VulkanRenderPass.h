#pragma once

#include "RHI/Public/IRenderPass.h"
#include <vulkan/vulkan.h>
#include <memory>
#include <vector>

namespace NorvesLib::RHI::Vulkan
{

class VulkanDevice;

/**
 * @brief Vulkanレンダーパスの実装クラス
 */
class VulkanRenderPass : public IRenderPass
{
public:
    /**
     * @brief VulkanRenderPassのコンストラクタ
     * @param device Vulkanデバイス
     * @param desc レンダーパス記述子
     */
    VulkanRenderPass(std::shared_ptr<VulkanDevice> device, const RenderPassDesc& desc);
    
    /**
     * @brief デストラクタ
     */
    ~VulkanRenderPass() override;

    // IDeviceObjectインターフェース実装
    ResourceType GetResourceType() const override { return ResourceType::RenderPass; }

    // IRenderPassインターフェース実装
    const RenderPassDesc& GetDesc() const override { return m_desc; }

    // Vulkan固有のメソッド
    VkRenderPass GetVkRenderPass() const { return m_renderPass; }

private:
    std::shared_ptr<VulkanDevice> m_device;
    RenderPassDesc m_desc;
    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    
    // アタッチメント情報
    std::vector<VkAttachmentDescription> m_attachmentDescs;
    std::vector<VkAttachmentReference> m_colorAttachmentRefs;
    std::vector<VkAttachmentReference> m_inputAttachmentRefs;
    VkAttachmentReference m_depthAttachmentRef;
    
    // ヘルパーメソッド
    void CreateRenderPass();
    VkFormat GetVulkanFormat(Format format) const;
    VkAttachmentLoadOp GetVulkanLoadOp(AttachmentLoadOp op) const;
    VkAttachmentStoreOp GetVulkanStoreOp(AttachmentStoreOp op) const;
    VkImageLayout GetVulkanInitialLayout(ResourceState state) const;
    VkImageLayout GetVulkanFinalLayout(ResourceState state) const;
};

} // namespace NorvesLib::RHI::Vulkan