#pragma once

#include "RHI/Public/ITexture.h"
#include <vulkan/vulkan.h>
#include <memory>

namespace NorvesLib::RHI::Vulkan
{

class VulkanDevice;

/**
 * @brief テクスチャの Vulkan 実装
 */
class VulkanTexture : public ITexture
{
public:
    /**
     * @brief コンストラクタ
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
    virtual ~VulkanTexture();

    /**
     * @brief テクスチャの幅を取得
     * @return テクスチャの幅（ピクセル）
     */
    virtual uint32_t GetWidth() const override { return m_desc.width; }

    /**
     * @brief テクスチャの高さを取得
     * @return テクスチャの高さ（ピクセル）
     */
    virtual uint32_t GetHeight() const override { return m_desc.height; }

    /**
     * @brief テクスチャの深さを取得
     * @return テクスチャの深さ（3Dテクスチャの場合）
     */
    virtual uint32_t GetDepth() const override { return m_desc.depth; }

    /**
     * @brief テクスチャのミップレベル数を取得
     * @return ミップレベル数
     */
    virtual uint32_t GetMipLevels() const override { return m_desc.mipLevels; }

    /**
     * @brief テクスチャの配列サイズを取得
     * @return 配列サイズ
     */
    virtual uint32_t GetArraySize() const override { return m_desc.arraySize; }

    /**
     * @brief テクスチャのフォーマットを取得
     * @return テクスチャのフォーマット
     */
    virtual Format GetFormat() const override { return m_desc.format; }

    /**
     * @brief テクスチャの使用用途を取得
     * @return テクスチャの使用用途
     */
    virtual ResourceUsage GetUsage() const override { return m_desc.usage; }

    /**
     * @brief キューブマップかどうかを取得
     * @return キューブマップの場合true
     */
    virtual bool IsCubemap() const override { return m_desc.isCubemap; }

    /**
     * @brief テクスチャデータを更新
     * @param data 更新するデータへのポインタ
     * @param rowPitch 1行あたりのバイト数
     * @param slicePitch 1スライスあたりのバイト数
     * @param mipLevel 更新するミップレベル
     * @param arrayIndex 更新する配列インデックス
     */
    virtual void Update(const void* data, uint32_t rowPitch, uint32_t slicePitch, uint32_t mipLevel = 0, uint32_t arrayIndex = 0) override;

    /**
     * @brief Vulkanイメージハンドルを取得
     * @return Vulkanイメージハンドル
     */
    VkImage GetVkImage() const { return m_image; }

    /**
     * @brief Vulkanイメージビューハンドルを取得
     * @return Vulkanイメージビューハンドル
     */
    VkImageView GetVkImageView() const { return m_imageView; }

    /**
     * @brief ストレージテクスチャかどうかを判定
     * @return ストレージテクスチャの場合はtrue
     */
    bool IsStorage() const {
        return (m_desc.usage & ResourceUsage::UnorderedAccess) != ResourceUsage::None;
    }

    /**
     * @brief イメージレイアウトを取得
     * @return 現在のイメージレイアウト
     */
    VkImageLayout GetVkImageLayout() const { return m_currentLayout; }

    /**
     * @brief イメージレイアウトを設定
     * @param layout 新しいイメージレイアウト
     */
    void SetVkImageLayout(VkImageLayout layout) { m_currentLayout = layout; }

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
    /**
     * @brief テクスチャリソースの作成
     */
    void CreateTexture();

    /**
     * @brief イメージビューの作成
     */
    void CreateImageView();

private:
    std::shared_ptr<VulkanDevice> m_device;           ///< Vulkanデバイス
    TextureDesc m_desc;                             ///< テクスチャ記述子
    VkImage m_image = VK_NULL_HANDLE;                ///< Vulkanイメージハンドル
    VkDeviceMemory m_memory = VK_NULL_HANDLE;        ///< デバイスメモリ
    VkImageView m_imageView = VK_NULL_HANDLE;        ///< Vulkanイメージビューハンドル
    VkImageLayout m_currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;  ///< 現在のイメージレイアウト
    bool m_ownsImage = true; // 自身でイメージを所有するかどうか
};

} // namespace NorvesLib::RHI::Vulkan