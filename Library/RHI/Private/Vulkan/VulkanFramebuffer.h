#pragma once

#include "RHI/Public/IFramebuffer.h"
#include <vulkan/vulkan.h>
#include <memory>
#include <vector>

namespace NorvesLib::RHI::Vulkan
{

class VulkanDevice;
class VulkanRenderPass;

/**
 * @brief Vulkanフレームバッファの実装クラス
 */
class VulkanFramebuffer : public IFramebuffer
{
public:
    /**
     * @brief VulkanFramebufferのコンストラクタ
     * @param device Vulkanデバイス
     * @param desc フレームバッファ記述子
     */
    VulkanFramebuffer(std::shared_ptr<VulkanDevice> device, const FramebufferDesc& desc);
    
    /**
     * @brief デストラクタ
     */
    ~VulkanFramebuffer() override;

    // IDeviceObjectインターフェース実装
    ResourceType GetResourceType() const override { return ResourceType::Framebuffer; }

    // IFramebufferインターフェース実装
    const FramebufferDesc& GetDesc() const override { return m_desc; }
    uint32_t GetWidth() const override { return m_desc.width; }
    uint32_t GetHeight() const override { return m_desc.height; }

    // Vulkan固有のメソッド
    VkFramebuffer GetVkFramebuffer() const { return m_framebuffer; }

private:
    std::shared_ptr<VulkanDevice> m_device;
    FramebufferDesc m_desc;
    VkFramebuffer m_framebuffer = VK_NULL_HANDLE;
    
    // アタッチメントのVulkanイメージビュー
    std::vector<VkImageView> m_attachmentViews;
    
    // ヘルパーメソッド
    void CreateFramebuffer(std::shared_ptr<VulkanRenderPass> renderPass);
    VkImageView GetImageViewFromAttachment(const AttachmentRef& attachment);
};

} // namespace NorvesLib::RHI::Vulkan