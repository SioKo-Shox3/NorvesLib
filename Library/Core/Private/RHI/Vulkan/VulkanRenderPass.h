#pragma once

#include "RHI/IRenderPass.h"
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

    /**
     * @brief Vulkanレンダーパスの実装クラス (vulkan.hpp使用)
     */
    class VulkanRenderPass : public IRenderPass
    {
    public:
        /**
         * @brief VulkanRenderPassのコンストラクタ
         * @param device Vulkanデバイス
         * @param desc レンダーパス記述子
         */
        VulkanRenderPass(TSharedPtr<VulkanDevice> device, const RenderPassDesc &desc);

        /**
         * @brief デストラクタ
         */
        ~VulkanRenderPass() override;

        // IRenderPassインターフェース実装
        uint32_t GetColorAttachmentCount() const override;
        bool HasDepthStencilAttachment() const override;
        Format GetColorAttachmentFormat(uint32_t index) const override;
        Format GetDepthStencilFormat() const override;

        // VulkanRenderPass固有
        const RenderPassDesc &GetDesc() const { return m_desc; }

        // Vulkan固有のメソッド (vulkan.hpp型)
        vk::RenderPass GetVkRenderPass() const { return m_renderPass; }

    private:
        TSharedPtr<VulkanDevice> m_device;
        RenderPassDesc m_desc;
        vk::RenderPass m_renderPass;

        // アタッチメント情報 (vulkan.hpp型)
        VariableArray<vk::AttachmentDescription> m_attachmentDescs;
        VariableArray<vk::AttachmentReference> m_colorAttachmentRefs;
        VariableArray<vk::AttachmentReference> m_inputAttachmentRefs;
        vk::AttachmentReference m_depthAttachmentRef;

        // ヘルパーメソッド
        void CreateRenderPass();
        vk::Format GetVulkanFormat(Format format) const;
        vk::AttachmentLoadOp GetVulkanLoadOp(AttachmentLoadOp op) const;
        vk::AttachmentStoreOp GetVulkanStoreOp(AttachmentStoreOp op) const;
        vk::ImageLayout GetVulkanInitialLayout(ResourceState state) const;
        vk::ImageLayout GetVulkanFinalLayout(ResourceState state) const;
    };

} // namespace NorvesLib::RHI::Vulkan
