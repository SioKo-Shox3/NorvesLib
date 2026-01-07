#pragma once

#include "RHI/IFramebuffer.h"

#define VULKAN_HPP_NO_CONSTRUCTORS
#include <vulkan/vulkan.hpp>

#include "Container/Containers.h"

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
        VulkanFramebuffer(TSharedPtr<VulkanDevice> device, const FramebufferDesc &desc);

        /**
         * @brief デストラクタ
         */
        ~VulkanFramebuffer() override;

        // コピー・ムーブ禁止
        VulkanFramebuffer(const VulkanFramebuffer &) = delete;
        VulkanFramebuffer &operator=(const VulkanFramebuffer &) = delete;
        VulkanFramebuffer(VulkanFramebuffer &&) = delete;
        VulkanFramebuffer &operator=(VulkanFramebuffer &&) = delete;

        // IDeviceObjectインターフェース実装
        ResourceType GetResourceType() const override { return ResourceType::Framebuffer; }

        // IFramebufferインターフェース実装
        const FramebufferDesc &GetDesc() const override { return m_desc; }
        uint32_t GetWidth() const override { return m_desc.width; }
        uint32_t GetHeight() const override { return m_desc.height; }

        // Vulkan固有のメソッド
        vk::Framebuffer GetVkFramebuffer() const { return m_framebuffer; }

    private:
        TSharedPtr<VulkanDevice> m_device;
        FramebufferDesc m_desc;
        vk::Framebuffer m_framebuffer;

        // アタッチメントのVulkanイメージビュー
        VariableArray<vk::ImageView> m_attachmentViews;

        // ヘルパーメソッド
        void CreateFramebuffer(TSharedPtr<VulkanRenderPass> renderPass);
        vk::ImageView GetImageViewFromAttachment(const AttachmentRef &attachment);
    };

} // namespace NorvesLib::RHI::Vulkan
