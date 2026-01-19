#pragma once

#include "RHI/ITexture.h"
#define VULKAN_HPP_NO_CONSTRUCTORS
#include <vulkan/vulkan.hpp>
#include "Container/Containers.h"

namespace NorvesLib::RHI::Vulkan
{

    class VulkanDevice;

    /**
     * @brief テクスチャの Vulkan 実装 (vulkan.hpp使用)
     */
    class VulkanTexture : public ITexture
    {
    public:
        /**
         * @brief コンストラクタ
         * @param device Vulkanデバイス
         * @param desc テクスチャ記述子
         */
        VulkanTexture(TSharedPtr<VulkanDevice> device, const TextureDesc &desc);

        /**
         * @brief VulkanTextureのコンストラクタ (既存のイメージから)
         * @param device Vulkanデバイス
         * @param desc テクスチャ記述子
         * @param image 既存のvk::Image (所有権は移行しない)
         */
        VulkanTexture(TSharedPtr<VulkanDevice> device, const TextureDesc &desc, vk::Image image);

        /**
         * @brief デストラクタ
         */
        virtual ~VulkanTexture();

        // ITextureインターフェース実装
        virtual uint32_t GetWidth() const override { return m_desc.width; }
        virtual uint32_t GetHeight() const override { return m_desc.height; }
        virtual uint32_t GetDepth() const override { return m_desc.depth; }
        virtual uint32_t GetMipLevels() const override { return m_desc.mipLevels; }
        virtual uint32_t GetArraySize() const override { return m_desc.arraySize; }
        virtual Format GetFormat() const override { return m_desc.format; }
        virtual ResourceUsage GetUsage() const override { return m_desc.usage; }
        virtual bool IsCubemap() const override { return m_desc.isCubemap; }
        virtual void Update(const void *data, uint32_t rowPitch, uint32_t slicePitch,
                            uint32_t mipLevel = 0, uint32_t arrayIndex = 0) override;

        // Vulkan固有のメソッド (vulkan.hpp型)
        vk::Image GetVkImage() const { return m_image; }
        vk::ImageView GetVkImageView() const { return m_imageView; }
        vk::ImageLayout GetVkImageLayout() const { return m_currentLayout; }
        void SetVkImageLayout(vk::ImageLayout layout) { m_currentLayout = layout; }

        /**
         * @brief イメージレイアウトの遷移
         * @param cmdBuffer コマンドバッファ
         * @param newLayout 新しいレイアウト
         * @param subresourceRange サブリソース範囲
         */
        void TransitionLayout(
            vk::CommandBuffer cmdBuffer,
            vk::ImageLayout newLayout,
            vk::ImageSubresourceRange subresourceRange);

        /**
         * @brief イメージレイアウトの遷移 (全サブリソース)
         * @param cmdBuffer コマンドバッファ
         * @param newLayout 新しいレイアウト
         */
        void TransitionLayout(
            vk::CommandBuffer cmdBuffer,
            vk::ImageLayout newLayout);

    private:
        void CreateTexture();
        void CreateImageView();

    private:
        TSharedPtr<VulkanDevice> m_device;
        TextureDesc m_desc;
        vk::Image m_image;
        vk::DeviceMemory m_memory;
        vk::ImageView m_imageView;
        vk::ImageLayout m_currentLayout = vk::ImageLayout::eUndefined;
        bool m_bOwnsImage = true;
    };

} // namespace NorvesLib::RHI::Vulkan
