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
        m_attachmentDescs.reserve(m_desc.colorAttachments.size() + (m_desc.hasDepthStencil ? 1 : 0));
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
        bool bHasDepthStencil = m_desc.hasDepthStencil;
        if (bHasDepthStencil)
        {
            const auto &depthAttachment = m_desc.depthStencilAttachment;
            const bool bDepthAttachmentReadOnly =
                depthAttachment.initialState == ResourceState::DepthRead &&
                depthAttachment.finalState == ResourceState::DepthRead;

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
            if (bDepthAttachmentReadOnly)
            {
                m_depthAttachmentRef.layout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
            }
            else
            {
                m_depthAttachmentRef.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
            }
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
        // ========================================
        // 入力依存（EXTERNAL → サブパス0）:
        //   前のレンダーパスのカラー/デプス書き込みを待ち、
        //   このパスのアタッチメント書き込みとフラグメント読み取りを可能にする
        // ========================================
        VariableArray<vk::SubpassDependency> dependencies;

        vk::SubpassDependency incomingDep{};
        incomingDep.srcSubpass = VK_SUBPASS_EXTERNAL;
        incomingDep.dstSubpass = 0;
        incomingDep.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eLateFragmentTests;
        incomingDep.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eFragmentShader;
        incomingDep.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        incomingDep.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite | vk::AccessFlagBits::eShaderRead;
        dependencies.push_back(incomingDep);

        // ========================================
        // 出力依存（サブパス0 → EXTERNAL）:
        //   このパスのカラー/デプス書き込みが完了してから、
        //   後続パスのフラグメントシェーダ読み取りを許可する
        // ========================================
        vk::SubpassDependency outgoingDep{};
        outgoingDep.srcSubpass = 0;
        outgoingDep.dstSubpass = VK_SUBPASS_EXTERNAL;
        outgoingDep.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eLateFragmentTests;
        outgoingDep.dstStageMask = vk::PipelineStageFlagBits::eFragmentShader | vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests;
        outgoingDep.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        outgoingDep.dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        outgoingDep.dependencyFlags = vk::DependencyFlagBits::eByRegion;
        dependencies.push_back(outgoingDep);

        // レンダーパスの作成
        vk::RenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = vk::StructureType::eRenderPassCreateInfo;
        renderPassInfo.attachmentCount = static_cast<uint32_t>(m_attachmentDescs.size());
        renderPassInfo.pAttachments = m_attachmentDescs.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
        renderPassInfo.pDependencies = dependencies.data();

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
        return m_desc.hasDepthStencil;
    }

    // カラーアタッチメントフォーマットを取得
    Format VulkanRenderPass::GetColorAttachmentFormat(uint32_t index) const
    {
        if (index < m_desc.colorAttachments.size())
        {
            return m_desc.colorAttachments[index].format;
        }
        return Format::UNKNOWN;
    }

    // デプス/ステンシルフォーマットを取得
    Format VulkanRenderPass::GetDepthStencilFormat() const
    {
        if (m_desc.hasDepthStencil)
        {
            return m_desc.depthStencilAttachment.format;
        }
        return Format::UNKNOWN;
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
        case ResourceState::DepthRead:
            return vk::ImageLayout::eDepthStencilReadOnlyOptimal;
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
        case ResourceState::DepthRead:
            return vk::ImageLayout::eDepthStencilReadOnlyOptimal;
        case ResourceState::ShaderResource:
            return vk::ImageLayout::eShaderReadOnlyOptimal;
        case ResourceState::Present:
            return vk::ImageLayout::ePresentSrcKHR;
        default:
            return vk::ImageLayout::eUndefined;
        }
    }

} // namespace NorvesLib::RHI::Vulkan
