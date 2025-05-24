#include "VulkanFramebuffer.h"
#include "VulkanDevice.h"
#include "VulkanRenderPass.h"
#include "VulkanTexture.h"
#include "VulkanSwapChain.h"
#include <stdexcept>

namespace NorvesLib::RHI::Vulkan
{

// コンストラクタ
VulkanFramebuffer::VulkanFramebuffer(std::shared_ptr<VulkanDevice> device, const FramebufferDesc& desc)
    : m_device(device)
    , m_desc(desc)
{
    // RenderPassのキャスト
    std::shared_ptr<VulkanRenderPass> renderPass = std::dynamic_pointer_cast<VulkanRenderPass>(desc.renderPass);
    if (!renderPass) {
        throw std::runtime_error("無効なRenderPassが指定されました");
    }
    
    // フレームバッファ作成
    CreateFramebuffer(renderPass);
}

// デストラクタ
VulkanFramebuffer::~VulkanFramebuffer()
{
    if (m_framebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(m_device->GetVkDevice(), m_framebuffer, nullptr);
    }
}

// フレームバッファの作成
void VulkanFramebuffer::CreateFramebuffer(std::shared_ptr<VulkanRenderPass> renderPass)
{
    // アタッチメントビューの準備
    m_attachmentViews.clear();
    m_attachmentViews.reserve(m_desc.colorAttachments.size() + (m_desc.depthStencilAttachment.has_value() ? 1 : 0));
    
    // カラーアタッチメントのイメージビュー取得
    for (const auto& attachment : m_desc.colorAttachments) {
        VkImageView imageView = GetImageViewFromAttachment(attachment);
        if (imageView == VK_NULL_HANDLE) {
            throw std::runtime_error("アタッチメントのイメージビューが無効です");
        }
        m_attachmentViews.push_back(imageView);
    }
    
    // デプスアタッチメントのイメージビュー取得
    if (m_desc.depthStencilAttachment.has_value()) {
        VkImageView depthImageView = GetImageViewFromAttachment(m_desc.depthStencilAttachment.value());
        if (depthImageView == VK_NULL_HANDLE) {
            throw std::runtime_error("デプスアタッチメントのイメージビューが無効です");
        }
        m_attachmentViews.push_back(depthImageView);
    }
    
    // フレームバッファ作成情報
    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = renderPass->GetVkRenderPass();
    framebufferInfo.attachmentCount = static_cast<uint32_t>(m_attachmentViews.size());
    framebufferInfo.pAttachments = m_attachmentViews.data();
    framebufferInfo.width = m_desc.width;
    framebufferInfo.height = m_desc.height;
    framebufferInfo.layers = 1; // マルチレイヤーの場合は変更が必要
    
    // フレームバッファの作成
    if (vkCreateFramebuffer(m_device->GetVkDevice(), &framebufferInfo, nullptr, &m_framebuffer) != VK_SUCCESS) {
        throw std::runtime_error("Vulkanフレームバッファの作成に失敗しました");
    }
}

// アタッチメントからVulkanイメージビューを取得
VkImageView VulkanFramebuffer::GetImageViewFromAttachment(const AttachmentRef& attachment)
{
    if (attachment.texture) {
        // テクスチャからイメージビューを取得
        auto vulkanTexture = std::dynamic_pointer_cast<VulkanTexture>(attachment.texture);
        if (vulkanTexture) {
            return vulkanTexture->GetVkImageView();
        }
    } else if (attachment.swapChain) {
        // スワップチェーンからイメージビューを取得
        auto vulkanSwapChain = std::dynamic_pointer_cast<VulkanSwapChain>(attachment.swapChain);
        if (vulkanSwapChain && attachment.swapChainBufferIndex < vulkanSwapChain->GetImageCount()) {
            return vulkanSwapChain->GetImageView(attachment.swapChainBufferIndex);
        }
    }
    
    return VK_NULL_HANDLE;
}

} // namespace NorvesLib::RHI::Vulkan