#include "VulkanRenderPass.h"
#include "VulkanDevice.h"
#include <stdexcept>

namespace NorvesLib::RHI::Vulkan
{

    using namespace NorvesLib::Core::Container;

    // コンストラクタ
    VulkanRenderPass::VulkanRenderPass(TSharedPtr<VulkanDevice> device, const RenderPassDesc &desc)
        : m_device(device), m_desc(desc)
    {
        CreateRenderPass();
    }

    // デストラクタ
    VulkanRenderPass::~VulkanRenderPass()
    {
        if (m_renderPass)
        {
            m_device->GetVkDevice().destroyRenderPass(m_renderPass);
        }
    }

    // レンダーパスの作成
    void VulkanRenderPass::CreateRenderPass()
    {
        // アタッチメント記述子の作成
        m_attachmentDescs.reserve(m_desc.colorAttachments.size() + (m_desc.depthStencilAttachment.has_value() ? 1 : 0));
        m_colorAttachmentRefs.reserve(m_desc.colorAttachments.size());

        // カラーアタッチメント
        for (size_t i = 0; i < m_desc.colorAttachments.size(); ++i)
        {
            const auto &attachment = m_desc.colorAttachments[i];

            vk::AttachmentDescription colorAttachment{};
            colorAttachment.format = GetVulkanFormat(attachment.format);
            colorAttachment.samples = vk::SampleCountFlagBits::e1;
            colorAttachment.loadOp = GetVulkanLoadOp(attachment.loadOp);
            colorAttachment.storeOp = GetVulkanStoreOp(attachment.storeOp);
            colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
            colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
            colorAttachment.initialLayout = GetVulkanInitialLayout(attachment.initialState);
            colorAttachment.finalLayout = GetVulkanFinalLayout(attachment.finalState);

            m_attachmentDescs.push_back(colorAttachment);

            vk::AttachmentReference colorAttachmentRef{};
            colorAttachmentRef.attachment = static_cast<uint32_t>(i);
            colorAttachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;
            m_colorAttachmentRefs.push_back(colorAttachmentRef);
        }

        // デプス/ステンシルアタッチメント
        bool bHasDepthStencil = m_desc.depthStencilAttachment.has_value();
        if (bHasDepthStencil)
        {
            const auto &depthAttachment = m_desc.depthStencilAttachment.value();

            vk::AttachmentDescription depthAttachmentDesc{};
            depthAttachmentDesc.format = GetVulkanFormat(depthAttachment.format);
            depthAttachmentDesc.samples = vk::SampleCountFlagBits::e1;
            depthAttachmentDesc.loadOp = GetVulkanLoadOp(depthAttachment.loadOp);
            depthAttachmentDesc.storeOp = GetVulkanStoreOp(depthAttachment.storeOp);
            depthAttachmentDesc.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
            depthAttachmentDesc.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
            depthAttachmentDesc.initialLayout = GetVulkanInitialLayout(depthAttachment.initialState);
            depthAttachmentDesc.finalLayout = GetVulkanFinalLayout(depthAttachment.finalState);

            m_attachmentDescs.push_back(depthAttachmentDesc);

            m_depthAttachmentRef.attachment = static_cast<uint32_t>(m_attachmentDescs.size() - 1);
            m_depthAttachmentRef.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        }

        // サブパス記述子
        vk::SubpassDescription subpass{};
        subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;

        // カラーアタッチメント
        if (!m_colorAttachmentRefs.empty())
        {
            subpass.colorAttachmentCount = static_cast<uint32_t>(m_colorAttachmentRefs.size());
            subpass.pColorAttachments = m_colorAttachmentRefs.data();
        }

        // デプス/ステンシルアタッチメント
        if (bHasDepthStencil)
        {
            subpass.pDepthStencilAttachment = &m_depthAttachmentRef;
        }

        // 入力アタッチメント
        if (!m_inputAttachmentRefs.empty())
        {
            subpass.inputAttachmentCount = static_cast<uint32_t>(m_inputAttachmentRefs.size());
            subpass.pInputAttachments = m_inputAttachmentRefs.data();
        }

        // サブパス依存関係
        vk::SubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dependency.srcAccessMask = vk::AccessFlagBits::eNone;
        dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

        // レンダーパスの作成
        vk::RenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = vk::StructureType::eRenderPassCreateInfo;
        renderPassInfo.attachmentCount = static_cast<uint32_t>(m_attachmentDescs.size());
        renderPassInfo.pAttachments = m_attachmentDescs.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;

        vk::Result result = m_device->GetVkDevice().createRenderPass(&renderPassInfo, nullptr, &m_renderPass);
        if (result != vk::Result::eSuccess)
        {
            throw std::runtime_error("Vulkanレンダーパスの作成に失敗しました");
        }
    }

    // カラーアタッチメント数を取得
    uint32_t VulkanRenderPass::GetColorAttachmentCount() const
    {
        return static_cast<uint32_t>(m_desc.colorAttachments.size());
    }

    // デプス/ステンシルアタッチメントの有無を取得
    bool VulkanRenderPass::HasDepthStencilAttachment() const
    {
        return m_desc.depthStencilAttachment.has_value();
    }

    // カラーアタッチメントフォーマットを取得
    Format VulkanRenderPass::GetColorAttachmentFormat(uint32_t index) const
    {
        if (index < m_desc.colorAttachments.size())
        {
            return m_desc.colorAttachments[index].format;
        }
        return Format::Unknown;
    }

    // デプス/ステンシルフォーマットを取得
    Format VulkanRenderPass::GetDepthStencilFormat() const
    {
        if (m_desc.depthStencilAttachment.has_value())
        {
            return m_desc.depthStencilAttachment.value().format;
        }
        return Format::Unknown;
    }

    // RHIフォーマットからVulkanフォーマットに変換
    vk::Format VulkanRenderPass::GetVulkanFormat(Format format) const
    {
        return m_device->ToVkFormat(format);
    }

    // ロード操作の変換
    vk::AttachmentLoadOp VulkanRenderPass::GetVulkanLoadOp(AttachmentLoadOp op) const
    {
        switch (op)
        {
        case AttachmentLoadOp::Load:
            return vk::AttachmentLoadOp::eLoad;
        case AttachmentLoadOp::Clear:
            return vk::AttachmentLoadOp::eClear;
        case AttachmentLoadOp::DontCare:
            return vk::AttachmentLoadOp::eDontCare;
        default:
            return vk::AttachmentLoadOp::eDontCare;
        }
    }

    // ストア操作の変換
    vk::AttachmentStoreOp VulkanRenderPass::GetVulkanStoreOp(AttachmentStoreOp op) const
    {
        switch (op)
        {
        case AttachmentStoreOp::Store:
            return vk::AttachmentStoreOp::eStore;
        case AttachmentStoreOp::DontCare:
            return vk::AttachmentStoreOp::eDontCare;
        default:
            return vk::AttachmentStoreOp::eStore;
        }
    }

    // 初期レイアウトの変換
    vk::ImageLayout VulkanRenderPass::GetVulkanInitialLayout(ResourceState state) const
    {
        switch (state)
        {
        case ResourceState::Undefined:
            return vk::ImageLayout::eUndefined;
        case ResourceState::RenderTarget:
            return vk::ImageLayout::eColorAttachmentOptimal;
        case ResourceState::DepthWrite:
            return vk::ImageLayout::eDepthStencilAttachmentOptimal;
        case ResourceState::ShaderResource:
            return vk::ImageLayout::eShaderReadOnlyOptimal;
        case ResourceState::Present:
            return vk::ImageLayout::ePresentSrcKHR;
        default:
            return vk::ImageLayout::eUndefined;
        }
    }

    // 最終レイアウトの変換
    vk::ImageLayout VulkanRenderPass::GetVulkanFinalLayout(ResourceState state) const
    {
        switch (state)
        {
        case ResourceState::Undefined:
            return vk::ImageLayout::eUndefined;
        case ResourceState::RenderTarget:
            return vk::ImageLayout::eColorAttachmentOptimal;
        case ResourceState::DepthWrite:
            return vk::ImageLayout::eDepthStencilAttachmentOptimal;
        case ResourceState::ShaderResource:
            return vk::ImageLayout::eShaderReadOnlyOptimal;
        case ResourceState::Present:
            return vk::ImageLayout::ePresentSrcKHR;
        default:
            return vk::ImageLayout::eUndefined;
        }
    }

} // namespace NorvesLib::RHI::Vulkan
