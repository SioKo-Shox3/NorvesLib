#include "VulkanTexture.h"
#include "VulkanDevice.h"
#include "VulkanBuffer.h"
#include <stdexcept>

namespace NorvesLib::RHI::Vulkan
{

// 新しいテクスチャを作成するコンストラクタ
VulkanTexture::VulkanTexture(std::shared_ptr<VulkanDevice> device, const TextureDesc& desc)
    : m_device(device)
    , m_desc(desc)
    , m_ownsImage(true)
{
    // イメージとイメージビューの作成
    CreateImage();
    CreateImageView();
}

// 既存のイメージから作成するコンストラクタ
VulkanTexture::VulkanTexture(std::shared_ptr<VulkanDevice> device, const TextureDesc& desc, VkImage image)
    : m_device(device)
    , m_desc(desc)
    , m_image(image)
    , m_ownsImage(false)
{
    // 既存のイメージに対してイメージビューのみを作成
    CreateImageView();
}

// デストラクタ
VulkanTexture::~VulkanTexture()
{
    // イメージビューを破棄
    if (m_imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device->GetVkDevice(), m_imageView, nullptr);
    }
    
    // 自身が所有するイメージのみ破棄
    if (m_ownsImage) {
        if (m_image != VK_NULL_HANDLE) {
            vkDestroyImage(m_device->GetVkDevice(), m_image, nullptr);
        }
        
        if (m_deviceMemory != VK_NULL_HANDLE) {
            vkFreeMemory(m_device->GetVkDevice(), m_deviceMemory, nullptr);
        }
    }
}

// テクスチャデータの更新
void VulkanTexture::Update(const void* data, uint32_t rowPitch, uint32_t slicePitch, uint32_t mipLevel, uint32_t arrayIndex)
{
    if (data == nullptr) {
        throw std::runtime_error("テクスチャ更新データがnullです");
    }
    
    // 単一のミップレベルと配列要素の更新のみサポート
    if (mipLevel >= m_desc.mipLevels || arrayIndex >= m_desc.arraySize) {
        throw std::runtime_error("無効なミップレベルまたは配列インデックスです");
    }
    
    // ステージングバッファを使用してテクスチャデータを転送
    // 計算はミップレベルに基づいて調整
    uint32_t mipWidth = std::max(1u, m_desc.width >> mipLevel);
    uint32_t mipHeight = std::max(1u, m_desc.height >> mipLevel);
    uint32_t mipDepth = std::max(1u, m_desc.depth >> mipLevel);
    
    // データサイズ計算
    uint64_t dataSize = static_cast<uint64_t>(slicePitch) * mipDepth;
    
    // ステージングバッファの作成
    BufferDesc stagingDesc;
    stagingDesc.size = dataSize;
    stagingDesc.usage = ResourceUsage::TransferSrc;
    stagingDesc.hostVisible = true;
    
    auto stagingBuffer = std::make_shared<VulkanBuffer>(m_device, stagingDesc);
    
    // データをステージングバッファにコピー
    void* mappedData = stagingBuffer->Map();
    memcpy(mappedData, data, dataSize);
    stagingBuffer->Unmap();
    
    // コマンドバッファの作成（一時的なもの）
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_device->GetCommandPool();
    allocInfo.commandBufferCount = 1;
    
    VkCommandBuffer cmdBuffer;
    vkAllocateCommandBuffers(m_device->GetVkDevice(), &allocInfo, &cmdBuffer);
    
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    
    vkBeginCommandBuffer(cmdBuffer, &beginInfo);
    
    // イメージレイアウトの遷移: UNDEFINED -> TRANSFER_DST_OPTIMAL
    VkImageSubresourceRange subresourceRange{};
    subresourceRange.aspectMask = GetImageAspect();
    subresourceRange.baseMipLevel = mipLevel;
    subresourceRange.levelCount = 1;
    subresourceRange.baseArrayLayer = arrayIndex;
    subresourceRange.layerCount = 1;
    
    TransitionLayout(cmdBuffer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, subresourceRange);
    
    // バッファからイメージへのコピーを実行
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;  // 0は密パッキングを意味する
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = GetImageAspect();
    region.imageSubresource.mipLevel = mipLevel;
    region.imageSubresource.baseArrayLayer = arrayIndex;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {mipWidth, mipHeight, mipDepth};
    
    vkCmdCopyBufferToImage(
        cmdBuffer,
        stagingBuffer->GetVkBuffer(),
        m_image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region);
    
    // レイアウトを汎用的な読み取りに適した形式に遷移
    TransitionLayout(cmdBuffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, subresourceRange);
    
    vkEndCommandBuffer(cmdBuffer);
    
    // コマンドの提出と実行
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;
    
    VkFence fence;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    
    vkCreateFence(m_device->GetVkDevice(), &fenceInfo, nullptr, &fence);
    vkQueueSubmit(m_device->GetGraphicsQueue(), 1, &submitInfo, fence);
    vkWaitForFences(m_device->GetVkDevice(), 1, &fence, VK_TRUE, UINT64_MAX);
    
    // リソース解放
    vkDestroyFence(m_device->GetVkDevice(), fence, nullptr);
    vkFreeCommandBuffers(m_device->GetVkDevice(), m_device->GetCommandPool(), 1, &cmdBuffer);
}

// イメージの作成
void VulkanTexture::CreateImage()
{
    // イメージタイプの決定
    VkImageType imageType;
    if (m_desc.depth > 1) {
        imageType = VK_IMAGE_TYPE_3D;
    } else if (m_desc.height > 1) {
        imageType = VK_IMAGE_TYPE_2D;
    } else {
        imageType = VK_IMAGE_TYPE_1D;
    }
    
    // イメージ作成情報の設定
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = imageType;
    imageInfo.extent.width = m_desc.width;
    imageInfo.extent.height = m_desc.height;
    imageInfo.extent.depth = m_desc.depth;
    imageInfo.mipLevels = m_desc.mipLevels;
    imageInfo.arrayLayers = m_desc.arraySize;
    imageInfo.format = m_device->ToVkFormat(m_desc.format);
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = GetVkImageUsage();
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.flags = m_desc.isCubemap ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
    
    // イメージ作成
    if (vkCreateImage(m_device->GetVkDevice(), &imageInfo, nullptr, &m_image) != VK_SUCCESS) {
        throw std::runtime_error("テクスチャイメージの作成に失敗しました");
    }
    
    // メモリ要件の取得
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_device->GetVkDevice(), m_image, &memRequirements);
    
    // メモリ割り当て情報の設定
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = m_device->FindMemoryType(
        memRequirements.memoryTypeBits, 
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    
    // メモリ割り当て
    if (vkAllocateMemory(m_device->GetVkDevice(), &allocInfo, nullptr, &m_deviceMemory) != VK_SUCCESS) {
        throw std::runtime_error("テクスチャメモリの割り当てに失敗しました");
    }
    
    // イメージとメモリをバインド
    vkBindImageMemory(m_device->GetVkDevice(), m_image, m_deviceMemory, 0);
    
    // 現在のレイアウトを設定
    m_currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
}

// イメージビューの作成
void VulkanTexture::CreateImageView()
{
    // ビュータイプの決定
    VkImageViewType viewType;
    if (m_desc.isCubemap) {
        viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    } else if (m_desc.depth > 1) {
        viewType = VK_IMAGE_VIEW_TYPE_3D;
    } else if (m_desc.height > 1) {
        if (m_desc.arraySize > 1) {
            viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        } else {
            viewType = VK_IMAGE_VIEW_TYPE_2D;
        }
    } else {
        if (m_desc.arraySize > 1) {
            viewType = VK_IMAGE_VIEW_TYPE_1D_ARRAY;
        } else {
            viewType = VK_IMAGE_VIEW_TYPE_1D;
        }
    }
    
    // イメージビュー作成情報の設定
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_image;
    viewInfo.viewType = viewType;
    viewInfo.format = m_device->ToVkFormat(m_desc.format);
    
    // コンポーネントマッピング
    viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    
    // サブリソース範囲
    viewInfo.subresourceRange.aspectMask = GetImageAspect();
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = m_desc.mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = m_desc.arraySize;
    
    // イメージビューの作成
    if (vkCreateImageView(m_device->GetVkDevice(), &viewInfo, nullptr, &m_imageView) != VK_SUCCESS) {
        throw std::runtime_error("テクスチャイメージビューの作成に失敗しました");
    }
}

// VkImageUsageFlagsに変換
VkImageUsageFlags VulkanTexture::GetVkImageUsage() const
{
    VkImageUsageFlags usage = 0;
    
    // 使用法フラグの変換
    if ((m_desc.usage & ResourceUsage::ShaderRead) == ResourceUsage::ShaderRead) {
        usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    }
    
    if ((m_desc.usage & ResourceUsage::ShaderWrite) == ResourceUsage::ShaderWrite) {
        usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    }
    
    if ((m_desc.usage & ResourceUsage::RenderTarget) == ResourceUsage::RenderTarget) {
        usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }
    
    if ((m_desc.usage & ResourceUsage::DepthStencil) == ResourceUsage::DepthStencil) {
        usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    }
    
    if ((m_desc.usage & ResourceUsage::TransferSrc) == ResourceUsage::TransferSrc) {
        usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }
    
    if ((m_desc.usage & ResourceUsage::TransferDst) == ResourceUsage::TransferDst) {
        usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }
    
    // デフォルトでトランスファー先として使用可能にする（テクスチャへのデータ転送のため）
    usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    
    return usage;
}

// VkImageAspectFlagsを取得
VkImageAspectFlags VulkanTexture::GetImageAspect() const
{
    // フォーマットに基づいてアスペクトフラグを決定
    if ((m_desc.usage & ResourceUsage::DepthStencil) == ResourceUsage::DepthStencil) {
        if (m_desc.format == Format::D24_UNORM_S8_UINT) {
            return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        } else {
            return VK_IMAGE_ASPECT_DEPTH_BIT;
        }
    }
    
    return VK_IMAGE_ASPECT_COLOR_BIT;
}

// イメージレイアウトの遷移 (サブリソース範囲指定)
void VulkanTexture::TransitionLayout(
    VkCommandBuffer cmdBuffer, 
    VkImageLayout newLayout, 
    VkImageSubresourceRange subresourceRange)
{
    // 同じレイアウトへの遷移の場合は何もしない
    if (m_currentLayout == newLayout) {
        return;
    }
    
    // バリア情報の設定
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = m_currentLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_image;
    barrier.subresourceRange = subresourceRange;
    
    // アクセスマスクとパイプラインステージの決定
    VkPipelineStageFlags srcStage;
    VkPipelineStageFlags dstStage;
    
    if (m_currentLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (m_currentLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else if (m_currentLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    }
    else if (m_currentLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }
    else {
        // デフォルトの遷移（一般的な使用の場合）
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = 0;
        
        srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }
    
    // バリアコマンドを発行
    vkCmdPipelineBarrier(
        cmdBuffer,
        srcStage, dstStage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );
    
    // 現在のレイアウトを更新
    m_currentLayout = newLayout;
}

// イメージレイアウトの遷移 (全サブリソース)
void VulkanTexture::TransitionLayout(
    VkCommandBuffer cmdBuffer, 
    VkImageLayout newLayout)
{
    // 全サブリソースを対象とした範囲を設定
    VkImageSubresourceRange subresourceRange{};
    subresourceRange.aspectMask = GetImageAspect();
    subresourceRange.baseMipLevel = 0;
    subresourceRange.levelCount = m_desc.mipLevels;
    subresourceRange.baseArrayLayer = 0;
    subresourceRange.layerCount = m_desc.arraySize;
    
    // レイアウト遷移
    TransitionLayout(cmdBuffer, newLayout, subresourceRange);
}

} // namespace NorvesLib::RHI::Vulkan