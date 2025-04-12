#include "VulkanRenderPass.h"
#include "VulkanDevice.h"
#include <stdexcept>

namespace NorvesLib::RHI::Vulkan
{

// Vulkanフォーマットの変換関数（仮実装）
VkFormat ToVkFormat(Format format)
{
    switch (format)
    {
    case Format::R8G8B8A8_UNORM:     return VK_FORMAT_R8G8B8A8_UNORM;
    case Format::B8G8R8A8_UNORM:     return VK_FORMAT_B8G8R8A8_UNORM;
    case Format::R32G32B32A32_FLOAT: return VK_FORMAT_R32G32B32A32_SFLOAT;
    case Format::R32G32B32_FLOAT:    return VK_FORMAT_R32G32B32_SFLOAT;
    case Format::R32G32_FLOAT:       return VK_FORMAT_R32G32_SFLOAT;
    case Format::R32_FLOAT:          return VK_FORMAT_R32_SFLOAT;
    case Format::D24_UNORM_S8_UINT:  return VK_FORMAT_D24_UNORM_S8_UINT;
    case Format::D32_FLOAT:          return VK_FORMAT_D32_SFLOAT;
    default:                          return VK_FORMAT_UNDEFINED;
    }
}

// コンストラクタ
VulkanRenderPass::VulkanRenderPass(std::shared_ptr<VulkanDevice> device, 
                                 const std::vector<Format>& colorFormats,
                                 Format depthStencilFormat)
    : m_device(device)
    , m_colorFormats(colorFormats)
    , m_depthStencilFormat(depthStencilFormat)
{
    CreateRenderPass();
}

// デストラクタ
VulkanRenderPass::~VulkanRenderPass()
{
    if (m_renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_device->GetVkDevice(), m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }
}

// カラーアタッチメント数を取得
uint32_t VulkanRenderPass::GetColorAttachmentCount() const
{
    return static_cast<uint32_t>(m_colorFormats.size());
}

// デプスステンシルアタッチメントを持つかどうか
bool VulkanRenderPass::HasDepthStencilAttachment() const
{
    return m_depthStencilFormat != Format::Unknown;
}

// カラーアタッチメントのフォーマットを取得
Format VulkanRenderPass::GetColorAttachmentFormat(uint32_t index) const
{
    if (index < m_colorFormats.size()) {
        return m_colorFormats[index];
    }
    return Format::Unknown;
}

// デプスステンシルアタッチメントのフォーマットを取得
Format VulkanRenderPass::GetDepthStencilFormat() const
{
    return m_depthStencilFormat;
}

// レンダーパスの作成
void VulkanRenderPass::CreateRenderPass()
{
    // アタッチメント記述子の配列
    std::vector<VkAttachmentDescription> attachmentDescriptions;
    
    // カラーアタッチメントのリファレンス配列
    std::vector<VkAttachmentReference> colorAttachmentRefs;
    
    // カラーアタッチメントの設定
    for (size_t i = 0; i < m_colorFormats.size(); i++) {
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = ToVkFormat(m_colorFormats[i]);
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;  // マルチサンプリングなし
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;  // クリアで開始
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;  // 結果を保存
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;  // プレゼント用レイアウト
        
        attachmentDescriptions.push_back(colorAttachment);
        
        // カラーアタッチメントのリファレンス
        VkAttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = static_cast<uint32_t>(i);
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        
        colorAttachmentRefs.push_back(colorAttachmentRef);
    }
    
    // デプスステンシルアタッチメントのリファレンス（オプション）
    VkAttachmentReference depthAttachmentRef{};
    if (HasDepthStencilAttachment()) {
        VkAttachmentDescription depthAttachment{};
        depthAttachment.format = ToVkFormat(m_depthStencilFormat);
        depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;  // クリアで開始
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;  // ステンシルもクリア
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        
        attachmentDescriptions.push_back(depthAttachment);
        
        depthAttachmentRef.attachment = static_cast<uint32_t>(attachmentDescriptions.size() - 1);
        depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }
    
    // サブパスの設定
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = static_cast<uint32_t>(colorAttachmentRefs.size());
    subpass.pColorAttachments = colorAttachmentRefs.data();
    
    if (HasDepthStencilAttachment()) {
        subpass.pDepthStencilAttachment = &depthAttachmentRef;
    }
    
    // サブパスの依存関係
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    
    // レンダーパスの作成
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachmentDescriptions.size());
    renderPassInfo.pAttachments = attachmentDescriptions.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;
    
    if (vkCreateRenderPass(m_device->GetVkDevice(), &renderPassInfo, nullptr, &m_renderPass) != VK_SUCCESS) {
        throw std::runtime_error("レンダーパスの作成に失敗しました");
    }
}

} // namespace NorvesLib::RHI::Vulkan