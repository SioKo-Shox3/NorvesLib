#include "VulkanRenderPass.h"
#include "VulkanDevice.h"
#include <stdexcept>

namespace NorvesLib::RHI::Vulkan
{

// コンストラクタ
VulkanRenderPass::VulkanRenderPass(std::shared_ptr<VulkanDevice> device, const RenderPassDesc& desc)
    : m_device(device)
    , m_desc(desc)
{
    CreateRenderPass();
}

// デストラクタ
VulkanRenderPass::~VulkanRenderPass()
{
    if (m_renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_device->GetVkDevice(), m_renderPass, nullptr);
    }
}

// レンダーパスの作成
void VulkanRenderPass::CreateRenderPass()
{
    // アタッチメント記述子の作成
    m_attachmentDescs.reserve(m_desc.colorAttachments.size() + (m_desc.depthStencilAttachment.has_value() ? 1 : 0));
    m_colorAttachmentRefs.reserve(m_desc.colorAttachments.size());

    // カラーアタッチメント
    for (size_t i = 0; i < m_desc.colorAttachments.size(); ++i) {
        const auto& attachment = m_desc.colorAttachments[i];
        
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = GetVulkanFormat(attachment.format);
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = GetVulkanLoadOp(attachment.loadOp);
        colorAttachment.storeOp = GetVulkanStoreOp(attachment.storeOp);
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = GetVulkanInitialLayout(attachment.initialState);
        colorAttachment.finalLayout = GetVulkanFinalLayout(attachment.finalState);
        
        m_attachmentDescs.push_back(colorAttachment);
        
        VkAttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = static_cast<uint32_t>(i);
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        m_colorAttachmentRefs.push_back(colorAttachmentRef);
    }
    
    // デプス/ステンシルアタッチメント
    bool hasDepthStencil = m_desc.depthStencilAttachment.has_value();
    if (hasDepthStencil) {
        const auto& depthAttachment = m_desc.depthStencilAttachment.value();
        
        VkAttachmentDescription depthAttachmentDesc{};
        depthAttachmentDesc.format = GetVulkanFormat(depthAttachment.format);
        depthAttachmentDesc.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAttachmentDesc.loadOp = GetVulkanLoadOp(depthAttachment.loadOp);
        depthAttachmentDesc.storeOp = GetVulkanStoreOp(depthAttachment.storeOp);
        depthAttachmentDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachmentDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachmentDesc.initialLayout = GetVulkanInitialLayout(depthAttachment.initialState);
        depthAttachmentDesc.finalLayout = GetVulkanFinalLayout(depthAttachment.finalState);
        
        m_attachmentDescs.push_back(depthAttachmentDesc);
        
        m_depthAttachmentRef.attachment = static_cast<uint32_t>(m_attachmentDescs.size() - 1);
        m_depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }
    
    // サブパス記述子
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    
    // カラーアタッチメント
    if (!m_colorAttachmentRefs.empty()) {
        subpass.colorAttachmentCount = static_cast<uint32_t>(m_colorAttachmentRefs.size());
        subpass.pColorAttachments = m_colorAttachmentRefs.data();
    }
    
    // デプス/ステンシルアタッチメント
    if (hasDepthStencil) {
        subpass.pDepthStencilAttachment = &m_depthAttachmentRef;
    }
    
    // 入力アタッチメント
    if (!m_inputAttachmentRefs.empty()) {
        subpass.inputAttachmentCount = static_cast<uint32_t>(m_inputAttachmentRefs.size());
        subpass.pInputAttachments = m_inputAttachmentRefs.data();
    }
    
    // サブパス依存関係
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    
    // レンダーパスの作成
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(m_attachmentDescs.size());
    renderPassInfo.pAttachments = m_attachmentDescs.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;
    
    if (vkCreateRenderPass(m_device->GetVkDevice(), &renderPassInfo, nullptr, &m_renderPass) != VK_SUCCESS) {
        throw std::runtime_error("Vulkanレンダーパスの作成に失敗しました");
    }
}

// RHIフォーマットからVulkanフォーマットに変換
VkFormat VulkanRenderPass::GetVulkanFormat(Format format) const
{
    return m_device->ToVkFormat(format);
}

// ロード操作の変換
VkAttachmentLoadOp VulkanRenderPass::GetVulkanLoadOp(AttachmentLoadOp op) const
{
    switch (op)
    {
    case AttachmentLoadOp::Load:
        return VK_ATTACHMENT_LOAD_OP_LOAD;
    case AttachmentLoadOp::Clear:
        return VK_ATTACHMENT_LOAD_OP_CLEAR;
    case AttachmentLoadOp::DontCare:
        return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    default:
        return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }
}

// ストア操作の変換
VkAttachmentStoreOp VulkanRenderPass::GetVulkanStoreOp(AttachmentStoreOp op) const
{
    switch (op)
    {
    case AttachmentStoreOp::Store:
        return VK_ATTACHMENT_STORE_OP_STORE;
    case AttachmentStoreOp::DontCare:
        return VK_ATTACHMENT_STORE_OP_DONT_CARE;
    default:
        return VK_ATTACHMENT_STORE_OP_STORE;
    }
}

// 初期レイアウトの変換
VkImageLayout VulkanRenderPass::GetVulkanInitialLayout(ResourceState state) const
{
    switch (state)
    {
    case ResourceState::Undefined:
        return VK_IMAGE_LAYOUT_UNDEFINED;
    case ResourceState::RenderTarget:
        return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    case ResourceState::DepthWrite:
        return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    case ResourceState::ShaderResource:
        return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    case ResourceState::Present:
        return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    default:
        return VK_IMAGE_LAYOUT_UNDEFINED;
    }
}

// 最終レイアウトの変換
VkImageLayout VulkanRenderPass::GetVulkanFinalLayout(ResourceState state) const
{
    switch (state)
    {
    case ResourceState::Undefined:
        return VK_IMAGE_LAYOUT_UNDEFINED;
    case ResourceState::RenderTarget:
        return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    case ResourceState::DepthWrite:
        return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    case ResourceState::ShaderResource:
        return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    case ResourceState::Present:
        return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    default:
        return VK_IMAGE_LAYOUT_UNDEFINED;
    }
}

} // namespace NorvesLib::RHI::Vulkan