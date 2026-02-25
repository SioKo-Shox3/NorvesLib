#pragma once

#include "RHI/IFramebuffer.h"
#include "RHI/IDevice.h"

#define VULKAN_HPP_NO_CONSTRUCTORS
#include <vulkan/vulkan.hpp>

#include "Container/Containers.h"

namespace NorvesLib::RHI::Vulkan
{
    // 明示的なusing宣言（グローバル名前空間から参照）
    using ::NorvesLib::Core::Container::MakeShared;
    using ::NorvesLib::Core::Container::TSharedPtr;
    using ::NorvesLib::Core::Container::TWeakPtr;
    using ::NorvesLib::Core::Container::VariableArray;

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

        // IFramebufferインターフェース実装
        uint32_t GetWidth() const override { return m_desc.width; }
        uint32_t GetHeight() const override { return m_desc.height; }
        RenderPassPtr GetRenderPass() const override { return m_desc.renderPass; }
        TexturePtr GetColorAttachment(uint32_t index) const override;
        TexturePtr GetDepthStencilAttachment() const override { return m_desc.depthStencilTarget; }
        uint32_t GetColorAttachmentCount() const override { return static_cast<uint32_t>(m_desc.colorTargets.size()); }
        bool HasDepthStencilAttachment() const override { return m_desc.depthStencilTarget != nullptr; }

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
        vk::ImageView GetImageViewFromTexture(const TexturePtr &texture);
    };

} // namespace NorvesLib::RHI::Vulkan
