#include "VulkanFramebuffer.h"
#include "VulkanDevice.h"
#include "VulkanRenderPass.h"
#include "VulkanTexture.h"
#include <stdexcept>

namespace NorvesLib::RHI::Vulkan
{

// コンストラクタ
VulkanFramebuffer::VulkanFramebuffer(std::shared_ptr<VulkanDevice> device, 
                                     std::shared_ptr<VulkanRenderPass> renderPass,
                                     const std::vector<std::shared_ptr<VulkanTexture>>& colorAttachments,
                                     std::shared_ptr<VulkanTexture> depthStencilAttachment,
                                     uint32_t width,
                                     uint32_t height)
    : m_device(device)
    , m_renderPass(renderPass)
    , m_colorAttachments(colorAttachments)
    , m_depthStencilAttachment(depthStencilAttachment)
{
    // 幅と高さが指定されていない場合、最初のアタッチメントから取得
    if (width == 0 || height == 0) {
        if (!m_colorAttachments.empty() && m_colorAttachments[0]) {
            m_width = m_colorAttachments[0]->GetWidth();
            m_height = m_colorAttachments[0]->GetHeight();
        } else if (m_depthStencilAttachment) {
            m_width = m_depthStencilAttachment->GetWidth();
            m_height = m_depthStencilAttachment->GetHeight();
        } else {
            throw std::runtime_error("フレームバッファの幅と高さを決定できません");
        }
    } else {
        m_width = width;
        m_height = height;
    }
    
    // フレームバッファの作成
    CreateFramebuffer();
}

// デストラクタ
VulkanFramebuffer::~VulkanFramebuffer()
{
    if (m_framebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(m_device->GetVkDevice(), m_framebuffer, nullptr);
        m_framebuffer = VK_NULL_HANDLE;
    }
}

// フレームバッファの幅を取得
uint32_t VulkanFramebuffer::GetWidth() const
{
    return m_width;
}

// フレームバッファの高さを取得
uint32_t VulkanFramebuffer::GetHeight() const
{
    return m_height;
}

// 関連付けられたレンダーパスを取得
RenderPassPtr VulkanFramebuffer::GetRenderPass() const
{
    return m_renderPass;
}

// カラーアタッチメントを取得
TexturePtr VulkanFramebuffer::GetColorAttachment(uint32_t index) const
{
    if (index < m_colorAttachments.size()) {
        return m_colorAttachments[index];
    }
    return nullptr;
}

// デプスステンシルアタッチメントを取得
TexturePtr VulkanFramebuffer::GetDepthStencilAttachment() const
{
    return m_depthStencilAttachment;
}

// カラーアタッチメント数を取得
uint32_t VulkanFramebuffer::GetColorAttachmentCount() const
{
    return static_cast<uint32_t>(m_colorAttachments.size());
}

// デプスステンシルアタッチメントを持つかどうか
bool VulkanFramebuffer::HasDepthStencilAttachment() const
{
    return m_depthStencilAttachment != nullptr;
}

// フレームバッファの作成
void VulkanFramebuffer::CreateFramebuffer()
{
    // アタッチメントビューのリストを作成
    std::vector<VkImageView> attachments;
    
    // カラーアタッチメントのビューを追加
    for (const auto& texture : m_colorAttachments) {
        if (!texture) {
            throw std::runtime_error("無効なカラーアタッチメントがフレームバッファに渡されました");
        }
        attachments.push_back(texture->GetVkImageView());
    }
    
    // デプスステンシルアタッチメントのビューを追加
    if (m_depthStencilAttachment) {
        attachments.push_back(m_depthStencilAttachment->GetVkImageView());
    }
    
    // フレームバッファの作成情報を設定
    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = m_renderPass->GetVkRenderPass();
    framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    framebufferInfo.pAttachments = attachments.data();
    framebufferInfo.width = m_width;
    framebufferInfo.height = m_height;
    framebufferInfo.layers = 1;
    
    // フレームバッファを作成
    if (vkCreateFramebuffer(m_device->GetVkDevice(), &framebufferInfo, nullptr, &m_framebuffer) != VK_SUCCESS) {
        throw std::runtime_error("フレームバッファの作成に失敗しました");
    }
}

} // namespace NorvesLib::RHI::Vulkan