#include "VulkanFramebuffer.h"
#include "VulkanDevice.h"
#include "VulkanRenderPass.h"
#include "VulkanTexture.h"
#include "VulkanSwapChain.h"

#include <stdexcept>

namespace NorvesLib::RHI::Vulkan
{

    // コンストラクタ
    VulkanFramebuffer::VulkanFramebuffer(TSharedPtr<VulkanDevice> device, const FramebufferDesc &desc)
        : m_device(device), m_desc(desc), m_framebuffer()
    {
        // RenderPassのキャスト
        TSharedPtr<VulkanRenderPass> renderPass = DynamicPointerCast<VulkanRenderPass>(desc.renderPass);
        if (!renderPass)
        {
            throw std::runtime_error("無効なRenderPassが指定されました");
        }

        // フレームバッファ作成
        CreateFramebuffer(renderPass);
    }

    // デストラクタ
    VulkanFramebuffer::~VulkanFramebuffer()
    {
        if (m_framebuffer)
        {
            m_device->GetVkDevice().destroyFramebuffer(m_framebuffer);
            m_framebuffer = nullptr;
        }
    }

    // フレームバッファの作成
    void VulkanFramebuffer::CreateFramebuffer(TSharedPtr<VulkanRenderPass> renderPass)
    {
        // アタッチメントビューの準備
        m_attachmentViews.clear();
        m_attachmentViews.reserve(m_desc.colorTargets.size() + (m_desc.depthStencilTarget ? 1 : 0));

        // カラーアタッチメントのイメージビュー取得
        for (const auto &target : m_desc.colorTargets)
        {
            vk::ImageView imageView = GetImageViewFromTexture(target);
            if (!imageView)
            {
                throw std::runtime_error("アタッチメントのイメージビューが無効です");
            }
            m_attachmentViews.push_back(imageView);
        }

        // デプスアタッチメントのイメージビュー取得
        if (m_desc.depthStencilTarget)
        {
            vk::ImageView depthImageView = GetImageViewFromTexture(m_desc.depthStencilTarget);
            if (!depthImageView)
            {
                throw std::runtime_error("デプスアタッチメントのイメージビューが無効です");
            }
            m_attachmentViews.push_back(depthImageView);
        }

        // フレームバッファ作成情報
        vk::FramebufferCreateInfo framebufferInfo{};
        framebufferInfo.renderPass = renderPass->GetVkRenderPass();
        framebufferInfo.attachmentCount = static_cast<uint32_t>(m_attachmentViews.size());
        framebufferInfo.pAttachments = m_attachmentViews.data();
        framebufferInfo.width = m_desc.width;
        framebufferInfo.height = m_desc.height;
        framebufferInfo.layers = 1; // マルチレイヤーの場合は変更が必要

        // フレームバッファの作成
        vk::Result result;
        std::tie(result, m_framebuffer) = m_device->GetVkDevice().createFramebuffer(framebufferInfo);

        if (result != vk::Result::eSuccess)
        {
            throw std::runtime_error("Vulkanフレームバッファの作成に失敗しました");
        }
    }

    // テクスチャからVulkanイメージビューを取得
    vk::ImageView VulkanFramebuffer::GetImageViewFromTexture(const TexturePtr &texture)
    {
        if (texture)
        {
            // テクスチャからイメージビューを取得
            auto vulkanTexture = DynamicPointerCast<VulkanTexture>(texture);
            if (vulkanTexture)
            {
                return vulkanTexture->GetVkImageView();
            }
        }

        return vk::ImageView{};
    }

    // カラーアタッチメントの取得
    TexturePtr VulkanFramebuffer::GetColorAttachment(uint32_t index) const
    {
        if (index < m_desc.colorTargets.size())
        {
            return m_desc.colorTargets[index];
        }
        return nullptr;
    }

} // namespace NorvesLib::RHI::Vulkan
