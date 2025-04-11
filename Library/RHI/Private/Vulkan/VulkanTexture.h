#pragma once

#include "RHI/Public/ITexture.h"
#include <vulkan/vulkan.h>
#include <memory>

namespace NorvesLib::RHI::Vulkan
{

class VulkanDevice;

/**
 * @brief Vulkanテクスチャ実装クラス
 */
class VulkanTexture : public ITexture
{
public:
    /**
     * @brief VulkanTextureのコンストラクタ
     * @param device Vulkanデバイス
     * @param desc テクスチャ記述子
     */
    VulkanTexture(std::shared_ptr<VulkanDevice> device, const TextureDesc& desc);
    
    /**
     * @brief VulkanTextureのコンストラクタ (既存のイメージから)
     * @param device Vulkanデバイス
     * @param desc テクスチャ記述子
     * @param image 既存のVkImage (所有権は移行しない)
     */
    VulkanTexture(std::shared_ptr<VulkanDevice> device, const TextureDesc& desc, VkImage image);
    
    /**
     * @brief デストラクタ
     */
    ~VulkanTexture() override;

    // ITextureインターフェース実装
    uint32_t GetWidth() const override { return m_desc.width; }
    uint32_t GetHeight() const override { return m_desc.height; }
    uint32_t GetDepth() const override { return m_desc.depth; }
    uint32_t GetMipLevels() const override { return m_desc.mipLevels; }
    uint32_t GetArraySize() const override { return m_desc.arraySize; }
    Format GetFormat() const override { return m_desc.format; }
    ResourceUsage GetUsage() const override { return m_desc.usage; }
    bool IsCubemap() const override { return m_desc.isCubemap; }
    void Update(const void* data, uint32_t rowPitch, uint32_t slicePitch, uint32_t mipLevel = 0, uint32_t arrayIndex = 0) override;

    // Vulkan固有のメソッド
    VkImage GetVkImage() const { return m_image; }
    VkImageView GetVkImageView() const { return m_imageView; }
    VkImageLayout GetCurrentLayout() const { return m_currentLayout; }
    
    /**
     * @brief イメージレイアウトの遷移
     * @param cmdBuffer コマンドバッファ
     * @param newLayout 新しいレイアウト
     * @param subresourceRange サブリソース範囲
     */
    void TransitionLayout(
        VkCommandBuffer cmdBuffer, 
        VkImageLayout newLayout, 
        VkImageSubresourceRange subresourceRange);
    
    /**
     * @brief イメージレイアウトの遷移 (全サブリソース)
     * @param cmdBuffer コマンドバッファ
     * @param newLayout 新しいレイアウト
     */
    void TransitionLayout(
        VkCommandBuffer cmdBuffer, 
        VkImageLayout newLayout);

private:
    std::shared_ptr<VulkanDevice> m_device;
    TextureDesc m_desc;
    
    VkImage m_image = VK_NULL_HANDLE;
    VkDeviceMemory m_deviceMemory = VK_NULL_HANDLE;
    VkImageView m_imageView = VK_NULL_HANDLE;
    
    VkImageLayout m_currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    bool m_ownsImage = true; // 自身でイメージを所有するかどうか
    
    // イメージとイメージビューの作成
    void CreateImage();
    void CreateImageView();
    
    // VkImageUsageFlagsに変換
    VkImageUsageFlags GetVkImageUsage() const;
    
    // VkImageAspectFlagsを取得
    VkImageAspectFlags GetImageAspect() const;
};

} // namespace NorvesLib::RHI::Vulkan