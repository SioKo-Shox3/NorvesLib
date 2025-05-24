#include "VulkanTexture.h"
#include "VulkanDevice.h"
#include "VulkanCommandList.h"
#include <stdexcept>
#include <algorithm>

namespace NorvesLib::RHI::Vulkan
{

// Vulkanフォーマット変換ヘルパー関数（VulkanUtilsに移動すべき）
VkFormat ConvertToVkFormat(Format format)
{
    switch (format)
    {
        case Format::R8G8B8A8_UNORM:     return VK_FORMAT_R8G8B8A8_UNORM;
        case Format::B8G8R8A8_UNORM:     return VK_FORMAT_B8G8R8A8_UNORM;
        case Format::R32G32B32A32_FLOAT: return VK_FORMAT_R32G32B32A32_SFLOAT;
        case Format::R32G32B32_FLOAT:    return VK_FORMAT_R32G32B32_SFLOAT;
        case Format::R32G32_FLOAT:       return VK_FORMAT_R32G32_SFLOAT;
        case Format::R32_FLOAT:          return VK_FORMAT_R32_SFLOAT;
        case Format::D24_UNORM_S8_UINT:  return VK_FORMAT_D24_UNORM_S8_UINT;
        case Format::D32_FLOAT:          return VK_FORMAT_D32_SFLOAT;
        default:                          return VK_FORMAT_UNDEFINED;
    }
}

// イメージ使用法フラグ変換ヘルパー関数
VkImageUsageFlags ConvertToVkUsageFlags(ResourceUsage usage)
{
    VkImageUsageFlags result = 0;
    
    if ((usage & ResourceUsage::ShaderResource) != ResourceUsage::None)
        result |= VK_IMAGE_USAGE_SAMPLED_BIT;
    
    if ((usage & ResourceUsage::RenderTarget) != ResourceUsage::None)
        result |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    
    if ((usage & ResourceUsage::DepthStencil) != ResourceUsage::None)
        result |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    
    if ((usage & ResourceUsage::UnorderedAccess) != ResourceUsage::None)
        result |= VK_IMAGE_USAGE_STORAGE_BIT;
    
    // 常に転送操作を許可
    result |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    
    return result;
}

// サブリソースレイアウトを計算するヘルパー関数
void GetSubresourceLayout(const TextureDesc& desc, uint32_t mipLevel, uint32_t arrayIndex, 
                          uint32_t& width, uint32_t& height, uint32_t& depth)
{
    width = std::max(1u, desc.width >> mipLevel);
    height = std::max(1u, desc.height >> mipLevel);
    depth = (desc.dimension == TextureDimension::Texture3D) ? std::max(1u, desc.depth >> mipLevel) : 1;
}

// コンストラクタ（新規テクスチャ）
VulkanTexture::VulkanTexture(std::shared_ptr<VulkanDevice> device, const TextureDesc& desc)
    : m_device(device)
    , m_desc(desc)
    , m_ownsImage(true)
{
    CreateTexture();
    CreateImageView();
}

// コンストラクタ（既存のVkImageから）
VulkanTexture::VulkanTexture(std::shared_ptr<VulkanDevice> device, const TextureDesc& desc, VkImage image)
    : m_device(device)
    , m_desc(desc)
    , m_image(image)
    , m_ownsImage(false)
{
    CreateImageView();
}

// デストラクタ
VulkanTexture::~VulkanTexture()
{
    if (m_imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device->GetVkDevice(), m_imageView, nullptr);
        m_imageView = VK_NULL_HANDLE;
    }
    
    if (m_ownsImage && m_image != VK_NULL_HANDLE) {
        vkDestroyImage(m_device->GetVkDevice(), m_image, nullptr);
        m_image = VK_NULL_HANDLE;
    }
    
    if (m_memory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device->GetVkDevice(), m_memory, nullptr);
        m_memory = VK_NULL_HANDLE;
    }
}

// テクスチャの作成
void VulkanTexture::CreateTexture()
{
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    
    // イメージタイプの設定
    switch (m_desc.dimension) {
        case TextureDimension::Texture1D:
            imageInfo.imageType = VK_IMAGE_TYPE_1D;
            break;
        case TextureDimension::Texture2D:
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            break;
        case TextureDimension::Texture3D:
            imageInfo.imageType = VK_IMAGE_TYPE_3D;
            break;
        default:
            throw std::runtime_error("未サポートのテクスチャ次元です");
    }
    
    // キューブマップの場合は追加フラグを設定
    if (m_desc.isCubemap) {
        imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    }
    
    // 基本パラメータの設定
    imageInfo.format = ConvertToVkFormat(m_desc.format);
    imageInfo.extent.width = m_desc.width;
    imageInfo.extent.height = m_desc.height;
    imageInfo.extent.depth = (m_desc.dimension == TextureDimension::Texture3D) ? m_desc.depth : 1;
    imageInfo.mipLevels = m_desc.mipLevels;
    imageInfo.arrayLayers = m_desc.arraySize * (m_desc.isCubemap ? 6 : 1); // キューブマップの場合は6面分
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT; // 現在はマルチサンプリングをサポートしない
    
    // 使用法フラグの設定
    imageInfo.usage = ConvertToVkUsageFlags(m_desc.usage);
    
    // メモリレイアウトと共有モードの設定
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    
    // イメージの作成
    if (vkCreateImage(m_device->GetVkDevice(), &imageInfo, nullptr, &m_image) != VK_SUCCESS) {
        throw std::runtime_error("イメージの作成に失敗しました");
    }
    
    // メモリ要件の取得
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_device->GetVkDevice(), m_image, &memRequirements);
    
    // メモリタイプの決定
    uint32_t memoryTypeIndex = m_device->FindMemoryType(
        memRequirements.memoryTypeBits, 
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    
    // メモリの割り当て
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = memoryTypeIndex;
    
    if (vkAllocateMemory(m_device->GetVkDevice(), &allocInfo, nullptr, &m_memory) != VK_SUCCESS) {
        throw std::runtime_error("イメージメモリの割り当てに失敗しました");
    }
    
    // メモリとイメージをバインド
    if (vkBindImageMemory(m_device->GetVkDevice(), m_image, m_memory, 0) != VK_SUCCESS) {
        throw std::runtime_error("イメージメモリのバインドに失敗しました");
    }
    
    // 初期レイアウトの設定
    m_currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
}

// イメージビューの作成
void VulkanTexture::CreateImageView()
{
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_image;
    
    // ビュータイプの設定
    switch (m_desc.dimension) {
        case TextureDimension::Texture1D:
            viewInfo.viewType = (m_desc.arraySize > 1) ? VK_IMAGE_VIEW_TYPE_1D_ARRAY : VK_IMAGE_VIEW_TYPE_1D;
            break;
        case TextureDimension::Texture2D:
            if (m_desc.isCubemap) {
                viewInfo.viewType = (m_desc.arraySize > 1) ? VK_IMAGE_VIEW_TYPE_CUBE_ARRAY : VK_IMAGE_VIEW_TYPE_CUBE;
            } else {
                viewInfo.viewType = (m_desc.arraySize > 1) ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
            }
            break;
        case TextureDimension::Texture3D:
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;
            break;
        default:
            throw std::runtime_error("未サポートのテクスチャ次元です");
    }
    
    // フォーマットとコンポーネントマッピングの設定
    viewInfo.format = ConvertToVkFormat(m_desc.format);
    viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    
    // アスペクトフラグの設定
    if ((m_desc.usage & ResourceUsage::DepthStencil) != ResourceUsage::None) {
        if (m_desc.format == Format::D24_UNORM_S8_UINT) {
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        } else {
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        }
    } else {
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    }
    
    // サブリソース範囲の設定
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = m_desc.mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = m_desc.arraySize * (m_desc.isCubemap ? 6 : 1);
    
    // イメージビューの作成
    if (vkCreateImageView(m_device->GetVkDevice(), &viewInfo, nullptr, &m_imageView) != VK_SUCCESS) {
        throw std::runtime_error("イメージビューの作成に失敗しました");
    }
}

// テクスチャデータの更新
void VulkanTexture::Update(const void* data, uint32_t rowPitch, uint32_t slicePitch, 
                          uint32_t mipLevel, uint32_t arrayIndex)
{
    if (!data) {
        throw std::runtime_error("更新データがnullです");
    }
    
    if (mipLevel >= m_desc.mipLevels || arrayIndex >= m_desc.arraySize) {
        throw std::runtime_error("無効なミップレベルまたは配列インデックスです");
    }
    
    // ステージングバッファの作成
    uint32_t width, height, depth;
    GetSubresourceLayout(m_desc, mipLevel, arrayIndex, width, height, depth);
    
    VkDeviceSize bufferSize = slicePitch * depth;
    
    // ステージングバッファを作成して更新データをコピー
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateBuffer(m_device->GetVkDevice(), &bufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS) {
        throw std::runtime_error("ステージングバッファの作成に失敗しました");
    }
    
    // メモリ要件の取得
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_device->GetVkDevice(), stagingBuffer, &memRequirements);
    
    // ステージングバッファ用のメモリタイプ
    uint32_t stagingMemoryTypeIndex = m_device->FindMemoryType(
        memRequirements.memoryTypeBits, 
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    
    // メモリの割り当て
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = stagingMemoryTypeIndex;
    
    if (vkAllocateMemory(m_device->GetVkDevice(), &allocInfo, nullptr, &stagingMemory) != VK_SUCCESS) {
        vkDestroyBuffer(m_device->GetVkDevice(), stagingBuffer, nullptr);
        throw std::runtime_error("ステージングメモリの割り当てに失敗しました");
    }
    
    // メモリとバッファをバインド
    if (vkBindBufferMemory(m_device->GetVkDevice(), stagingBuffer, stagingMemory, 0) != VK_SUCCESS) {
        vkDestroyBuffer(m_device->GetVkDevice(), stagingBuffer, nullptr);
        vkFreeMemory(m_device->GetVkDevice(), stagingMemory, nullptr);
        throw std::runtime_error("ステージングバッファのメモリバインドに失敗しました");
    }
    
    // データのコピー
    void* mapped;
    if (vkMapMemory(m_device->GetVkDevice(), stagingMemory, 0, bufferSize, 0, &mapped) != VK_SUCCESS) {
        vkDestroyBuffer(m_device->GetVkDevice(), stagingBuffer, nullptr);
        vkFreeMemory(m_device->GetVkDevice(), stagingMemory, nullptr);
        throw std::runtime_error("メモリのマッピングに失敗しました");
    }
    
    memcpy(mapped, data, bufferSize);
    vkUnmapMemory(m_device->GetVkDevice(), stagingMemory);
    
    // 一時的なコマンドバッファの作成
    VkCommandBuffer commandBuffer = m_device->BeginSingleTimeCommands();
    
    // レイアウト変更とコピー
    VkImageSubresourceRange subresourceRange{};
    if ((m_desc.usage & ResourceUsage::DepthStencil) != ResourceUsage::None) {
        subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (m_desc.format == Format::D24_UNORM_S8_UINT) {
            subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
    } else {
        subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    }
    subresourceRange.baseMipLevel = mipLevel;
    subresourceRange.levelCount = 1;
    subresourceRange.baseArrayLayer = arrayIndex * (m_desc.isCubemap ? 6 : 1);
    subresourceRange.layerCount = m_desc.isCubemap ? 6 : 1;
    
    // 転送先レイアウトに変更
    TransitionLayout(commandBuffer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, subresourceRange);
    
    // バッファからイメージへのコピー
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;  // 0は密にパックされたことを意味する
    region.bufferImageHeight = 0; // 0は密にパックされたことを意味する
    region.imageSubresource.aspectMask = subresourceRange.aspectMask;
    region.imageSubresource.mipLevel = mipLevel;
    region.imageSubresource.baseArrayLayer = subresourceRange.baseArrayLayer;
    region.imageSubresource.layerCount = subresourceRange.layerCount;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, depth};
    
    vkCmdCopyBufferToImage(
        commandBuffer,
        stagingBuffer,
        m_image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region
    );
    
    // 適切なレイアウトに戻す
    VkImageLayout targetLayout;
    if ((m_desc.usage & ResourceUsage::ShaderResource) != ResourceUsage::None) {
        targetLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    } else if ((m_desc.usage & ResourceUsage::RenderTarget) != ResourceUsage::None) {
        targetLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    } else if ((m_desc.usage & ResourceUsage::DepthStencil) != ResourceUsage::None) {
        targetLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    } else if ((m_desc.usage & ResourceUsage::UnorderedAccess) != ResourceUsage::None) {
        targetLayout = VK_IMAGE_LAYOUT_GENERAL;
    } else {
        targetLayout = VK_IMAGE_LAYOUT_GENERAL;
    }
    
    TransitionLayout(commandBuffer, targetLayout, subresourceRange);
    
    // コマンドバッファの実行と解放
    m_device->EndSingleTimeCommands(commandBuffer);
    
    // ステージングリソースの解放
    vkDestroyBuffer(m_device->GetVkDevice(), stagingBuffer, nullptr);
    vkFreeMemory(m_device->GetVkDevice(), stagingMemory, nullptr);
}

// イメージレイアウト遷移（サブリソース範囲指定）
void VulkanTexture::TransitionLayout(
    VkCommandBuffer cmdBuffer, 
    VkImageLayout newLayout, 
    VkImageSubresourceRange subresourceRange)
{
    if (m_currentLayout == newLayout) {
        return; // レイアウトが同じなら何もしない
    }
    
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = m_currentLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_image;
    barrier.subresourceRange = subresourceRange;
    
    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;
    
    // レイアウト遷移に応じてアクセスマスクとステージを設定
    if (m_currentLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } 
    else if (m_currentLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && 
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else if (m_currentLayout == VK_IMAGE_LAYOUT_UNDEFINED && 
             newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | 
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    }
    else if (m_currentLayout == VK_IMAGE_LAYOUT_UNDEFINED && 
             newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }
    else if (m_currentLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
             newLayout == VK_IMAGE_LAYOUT_GENERAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    }
    else {
        // 一般的な遷移（最もパフォーマンスが低いが安全）
        barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }
    
    vkCmdPipelineBarrier(
        cmdBuffer,
        sourceStage, destinationStage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );
    
    // 現在のレイアウトを更新（特定のサブリソースのみの更新の場合は複雑になるが、簡略化のため全体を更新）
    m_currentLayout = newLayout;
}

// イメージレイアウト遷移（全サブリソース）
void VulkanTexture::TransitionLayout(
    VkCommandBuffer cmdBuffer, 
    VkImageLayout newLayout)
{
    VkImageSubresourceRange subresourceRange{};
    if ((m_desc.usage & ResourceUsage::DepthStencil) != ResourceUsage::None) {
        subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (m_desc.format == Format::D24_UNORM_S8_UINT) {
            subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
    } else {
        subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    }
    subresourceRange.baseMipLevel = 0;
    subresourceRange.levelCount = m_desc.mipLevels;
    subresourceRange.baseArrayLayer = 0;
    subresourceRange.layerCount = m_desc.arraySize * (m_desc.isCubemap ? 6 : 1);
    
    TransitionLayout(cmdBuffer, newLayout, subresourceRange);
}

} // namespace NorvesLib::RHI::Vulkan