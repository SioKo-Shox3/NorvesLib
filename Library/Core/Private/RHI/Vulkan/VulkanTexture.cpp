#include "VulkanTexture.h"
#include "VulkanDevice.h"
#include <stdexcept>
#include <algorithm>
#include <cstring>

namespace NorvesLib::RHI::Vulkan
{

    /**
     * @brief RHI FormatをVulkanフォーマットに変換
     */
    vk::Format ConvertToVkFormat(Format format)
    {
        switch (format)
        {
        case Format::R8_UNORM:
            return vk::Format::eR8Unorm;
        case Format::R8G8_UNORM:
            return vk::Format::eR8G8Unorm;
        case Format::R8G8B8A8_UNORM:
            return vk::Format::eR8G8B8A8Unorm;
        case Format::R8G8B8A8_SRGB:
            return vk::Format::eR8G8B8A8Srgb;
        case Format::B8G8R8A8_UNORM:
            return vk::Format::eB8G8R8A8Unorm;
        case Format::B8G8R8A8_SRGB:
            return vk::Format::eB8G8R8A8Srgb;
        case Format::R16_FLOAT:
            return vk::Format::eR16Sfloat;
        case Format::R16G16_FLOAT:
            return vk::Format::eR16G16Sfloat;
        case Format::R16G16B16A16_FLOAT:
            return vk::Format::eR16G16B16A16Sfloat;
        case Format::R32_FLOAT:
            return vk::Format::eR32Sfloat;
        case Format::R32G32_FLOAT:
            return vk::Format::eR32G32Sfloat;
        case Format::R32G32B32_FLOAT:
            return vk::Format::eR32G32B32Sfloat;
        case Format::R32G32B32A32_FLOAT:
            return vk::Format::eR32G32B32A32Sfloat;
        case Format::D16_UNORM:
            return vk::Format::eD16Unorm;
        case Format::D24_UNORM_S8_UINT:
            return vk::Format::eD24UnormS8Uint;
        case Format::D32_FLOAT:
            return vk::Format::eD32Sfloat;
        default:
            return vk::Format::eUndefined;
        }
    }

    /**
     * @brief ResourceUsageからvk::ImageUsageFlagsに変換
     */
    vk::ImageUsageFlags ConvertToVkImageUsageFlags(ResourceUsage usage)
    {
        vk::ImageUsageFlags result;

        if ((usage & ResourceUsage::ShaderResource) != ResourceUsage::None)
        {
            result |= vk::ImageUsageFlagBits::eSampled;
        }

        if ((usage & ResourceUsage::RenderTarget) != ResourceUsage::None)
        {
            result |= vk::ImageUsageFlagBits::eColorAttachment;
        }

        if ((usage & ResourceUsage::DepthStencil) != ResourceUsage::None)
        {
            result |= vk::ImageUsageFlagBits::eDepthStencilAttachment;
        }

        if ((usage & ResourceUsage::UnorderedAccess) != ResourceUsage::None)
        {
            result |= vk::ImageUsageFlagBits::eStorage;
        }

        // 常に転送操作を許可
        result |= vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst;

        return result;
    }

    /**
     * @brief サブリソースレイアウトを計算
     */
    void GetSubresourceLayout(const TextureDesc &desc, uint32_t mipLevel, uint32_t /*arrayIndex*/,
                              uint32_t &width, uint32_t &height, uint32_t &depth)
    {
        width = std::max(1u, desc.Width >> mipLevel);
        height = std::max(1u, desc.Height >> mipLevel);
        depth = (desc.Dimension == TextureDimension::Texture3D) ? std::max(1u, desc.Depth >> mipLevel) : 1;
    }

    VulkanTexture::VulkanTexture(TSharedPtr<VulkanDevice> device, const TextureDesc &desc)
        : m_device(device), m_desc(desc), m_bOwnsImage(true)
    {
        CreateTexture();
        CreateImageView();
    }

    VulkanTexture::VulkanTexture(TSharedPtr<VulkanDevice> device, const TextureDesc &desc, vk::Image image)
        : m_device(device), m_desc(desc), m_image(image), m_bOwnsImage(false)
    {
        CreateImageView();
    }

    VulkanTexture::~VulkanTexture()
    {
        vk::Device vkDevice = m_device->GetVkDevice();

        if (m_imageView)
        {
            vkDevice.destroyImageView(m_imageView);
            m_imageView = nullptr;
        }

        if (m_bOwnsImage && m_image)
        {
            vkDevice.destroyImage(m_image);
            m_image = nullptr;
        }

        if (m_memory)
        {
            vkDevice.freeMemory(m_memory);
            m_memory = nullptr;
        }
    }

    void VulkanTexture::CreateTexture()
    {
        vk::ImageCreateInfo imageInfo;

        // イメージタイプの設定
        switch (m_desc.Dimension)
        {
        case TextureDimension::Texture1D:
            imageInfo.imageType = vk::ImageType::e1D;
            break;
        case TextureDimension::Texture2D:
            imageInfo.imageType = vk::ImageType::e2D;
            break;
        case TextureDimension::Texture3D:
            imageInfo.imageType = vk::ImageType::e3D;
            break;
        default:
            throw std::runtime_error("未サポートのテクスチャ次元です");
        }

        // キューブマップの場合は追加フラグを設定
        if (m_desc.IsCubemap)
        {
            imageInfo.flags = vk::ImageCreateFlagBits::eCubeCompatible;
        }

        // 基本パラメータの設定
        imageInfo.format = ConvertToVkFormat(m_desc.TextureFormat);
        imageInfo.extent.width = m_desc.Width;
        imageInfo.extent.height = m_desc.Height;
        imageInfo.extent.depth = (m_desc.Dimension == TextureDimension::Texture3D) ? m_desc.Depth : 1;
        imageInfo.mipLevels = m_desc.MipLevels;
        imageInfo.arrayLayers = m_desc.ArraySize * (m_desc.IsCubemap ? 6 : 1);
        imageInfo.samples = vk::SampleCountFlagBits::e1;
        imageInfo.usage = ConvertToVkImageUsageFlags(m_desc.Usage);
        imageInfo.tiling = vk::ImageTiling::eOptimal;
        imageInfo.sharingMode = vk::SharingMode::eExclusive;
        imageInfo.initialLayout = vk::ImageLayout::eUndefined;

        // イメージの作成
        vk::Device vkDevice = m_device->GetVkDevice();
        auto createResult = vkDevice.createImage(imageInfo);
        if (createResult.result != vk::Result::eSuccess)
        {
            throw std::runtime_error("イメージの作成に失敗しました");
        }
        m_image = createResult.value;

        // メモリ要件の取得
        vk::MemoryRequirements memRequirements = vkDevice.getImageMemoryRequirements(m_image);

        // メモリタイプの決定
        uint32_t memoryTypeIndex = m_device->FindMemoryType(
            memRequirements.memoryTypeBits,
            vk::MemoryPropertyFlagBits::eDeviceLocal);

        // メモリの割り当て
        vk::MemoryAllocateInfo allocInfo;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = memoryTypeIndex;

        auto allocResult = vkDevice.allocateMemory(allocInfo);
        if (allocResult.result != vk::Result::eSuccess)
        {
            throw std::runtime_error("イメージメモリの割り当てに失敗しました");
        }
        m_memory = allocResult.value;

        // メモリとイメージをバインド
        auto bindResult = vkDevice.bindImageMemory(m_image, m_memory, 0);
        if (bindResult != vk::Result::eSuccess)
        {
            throw std::runtime_error("イメージメモリのバインドに失敗しました");
        }

        m_currentLayout = vk::ImageLayout::eUndefined;
    }

    void VulkanTexture::CreateImageView()
    {
        vk::ImageViewCreateInfo viewInfo;
        viewInfo.image = m_image;

        // ビュータイプの設定
        switch (m_desc.Dimension)
        {
        case TextureDimension::Texture1D:
            viewInfo.viewType = (m_desc.ArraySize > 1) ? vk::ImageViewType::e1DArray : vk::ImageViewType::e1D;
            break;
        case TextureDimension::Texture2D:
            if (m_desc.IsCubemap)
            {
                viewInfo.viewType = (m_desc.ArraySize > 1) ? vk::ImageViewType::eCubeArray : vk::ImageViewType::eCube;
            }
            else
            {
                viewInfo.viewType = (m_desc.ArraySize > 1) ? vk::ImageViewType::e2DArray : vk::ImageViewType::e2D;
            }
            break;
        case TextureDimension::Texture3D:
            viewInfo.viewType = vk::ImageViewType::e3D;
            break;
        default:
            throw std::runtime_error("未サポートのテクスチャ次元です");
        }

        // フォーマットとコンポーネントマッピングの設定
        viewInfo.format = ConvertToVkFormat(m_desc.TextureFormat);
        viewInfo.components.r = vk::ComponentSwizzle::eIdentity;
        viewInfo.components.g = vk::ComponentSwizzle::eIdentity;
        viewInfo.components.b = vk::ComponentSwizzle::eIdentity;
        viewInfo.components.a = vk::ComponentSwizzle::eIdentity;

        // アスペクトフラグの設定
        if ((m_desc.Usage & ResourceUsage::DepthStencil) != ResourceUsage::None)
        {
            if (m_desc.TextureFormat == Format::D24_UNORM_S8_UINT)
            {
                viewInfo.subresourceRange.aspectMask =
                    vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
            }
            else
            {
                viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
            }
        }
        else
        {
            viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        }

        // サブリソース範囲の設定
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = m_desc.MipLevels;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = m_desc.ArraySize * (m_desc.IsCubemap ? 6 : 1);

        // イメージビューの作成
        vk::Device vkDevice = m_device->GetVkDevice();
        auto createResult = vkDevice.createImageView(viewInfo);
        if (createResult.result != vk::Result::eSuccess)
        {
            throw std::runtime_error("イメージビューの作成に失敗しました");
        }
        m_imageView = createResult.value;
    }

    void VulkanTexture::Update(const void *data, uint32_t rowPitch, uint32_t slicePitch,
                               uint32_t mipLevel, uint32_t arrayIndex)
    {
        if (!data)
        {
            throw std::runtime_error("更新データがnullです");
        }

        if (mipLevel >= m_desc.MipLevels || arrayIndex >= m_desc.ArraySize)
        {
            throw std::runtime_error("無効なミップレベルまたは配列インデックスです");
        }

        // ステージングバッファの作成
        uint32_t width, height, depth;
        GetSubresourceLayout(m_desc, mipLevel, arrayIndex, width, height, depth);

        vk::DeviceSize bufferSize = slicePitch * depth;
        vk::Device vkDevice = m_device->GetVkDevice();

        // ステージングバッファを作成
        vk::BufferCreateInfo bufferInfo;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
        bufferInfo.sharingMode = vk::SharingMode::eExclusive;

        auto bufferResult = vkDevice.createBuffer(bufferInfo);
        if (bufferResult.result != vk::Result::eSuccess)
        {
            throw std::runtime_error("ステージングバッファの作成に失敗しました");
        }
        vk::Buffer stagingBuffer = bufferResult.value;

        // メモリ要件の取得
        vk::MemoryRequirements memRequirements = vkDevice.getBufferMemoryRequirements(stagingBuffer);

        // ステージングバッファ用のメモリタイプ
        uint32_t stagingMemoryTypeIndex = m_device->FindMemoryType(
            memRequirements.memoryTypeBits,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

        // メモリの割り当て
        vk::MemoryAllocateInfo allocInfo;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = stagingMemoryTypeIndex;

        auto memResult = vkDevice.allocateMemory(allocInfo);
        if (memResult.result != vk::Result::eSuccess)
        {
            vkDevice.destroyBuffer(stagingBuffer);
            throw std::runtime_error("ステージングメモリの割り当てに失敗しました");
        }
        vk::DeviceMemory stagingMemory = memResult.value;

        // メモリとバッファをバインド
        auto bindResult = vkDevice.bindBufferMemory(stagingBuffer, stagingMemory, 0);
        if (bindResult != vk::Result::eSuccess)
        {
            vkDevice.destroyBuffer(stagingBuffer);
            vkDevice.freeMemory(stagingMemory);
            throw std::runtime_error("ステージングバッファのメモリバインドに失敗しました");
        }

        // データのコピー
        auto mapResult = vkDevice.mapMemory(stagingMemory, 0, bufferSize, {});
        if (mapResult.result != vk::Result::eSuccess)
        {
            vkDevice.destroyBuffer(stagingBuffer);
            vkDevice.freeMemory(stagingMemory);
            throw std::runtime_error("メモリのマッピングに失敗しました");
        }
        void *mapped = mapResult.value;

        std::memcpy(mapped, data, bufferSize);
        vkDevice.unmapMemory(stagingMemory);

        // 一時的なコマンドバッファの作成
        vk::CommandBuffer commandBuffer = m_device->BeginSingleTimeCommands();

        // レイアウト変更とコピー
        vk::ImageSubresourceRange subresourceRange;
        if ((m_desc.Usage & ResourceUsage::DepthStencil) != ResourceUsage::None)
        {
            subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
            if (m_desc.TextureFormat == Format::D24_UNORM_S8_UINT)
            {
                subresourceRange.aspectMask |= vk::ImageAspectFlagBits::eStencil;
            }
        }
        else
        {
            subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        }
        subresourceRange.baseMipLevel = mipLevel;
        subresourceRange.levelCount = 1;
        subresourceRange.baseArrayLayer = arrayIndex * (m_desc.IsCubemap ? 6 : 1);
        subresourceRange.layerCount = m_desc.IsCubemap ? 6 : 1;

        // 転送先レイアウトに変更
        TransitionLayout(commandBuffer, vk::ImageLayout::eTransferDstOptimal, subresourceRange);

        // バッファからイメージへのコピー
        vk::BufferImageCopy region;
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = subresourceRange.aspectMask;
        region.imageSubresource.mipLevel = mipLevel;
        region.imageSubresource.baseArrayLayer = subresourceRange.baseArrayLayer;
        region.imageSubresource.layerCount = subresourceRange.layerCount;
        region.imageOffset = vk::Offset3D{0, 0, 0};
        region.imageExtent = vk::Extent3D{width, height, depth};

        commandBuffer.copyBufferToImage(
            stagingBuffer,
            m_image,
            vk::ImageLayout::eTransferDstOptimal,
            1,
            &region);

        // 適切なレイアウトに戻す
        vk::ImageLayout targetLayout;
        if ((m_desc.Usage & ResourceUsage::ShaderResource) != ResourceUsage::None)
        {
            targetLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        }
        else if ((m_desc.Usage & ResourceUsage::RenderTarget) != ResourceUsage::None)
        {
            targetLayout = vk::ImageLayout::eColorAttachmentOptimal;
        }
        else if ((m_desc.Usage & ResourceUsage::DepthStencil) != ResourceUsage::None)
        {
            targetLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        }
        else if ((m_desc.Usage & ResourceUsage::UnorderedAccess) != ResourceUsage::None)
        {
            targetLayout = vk::ImageLayout::eGeneral;
        }
        else
        {
            targetLayout = vk::ImageLayout::eGeneral;
        }

        TransitionLayout(commandBuffer, targetLayout, subresourceRange);

        // コマンドバッファの実行と解放
        m_device->EndSingleTimeCommands(commandBuffer);

        // ステージングリソースの解放
        vkDevice.destroyBuffer(stagingBuffer);
        vkDevice.freeMemory(stagingMemory);
    }

    void VulkanTexture::TransitionLayout(
        vk::CommandBuffer cmdBuffer,
        vk::ImageLayout newLayout,
        vk::ImageSubresourceRange subresourceRange)
    {
        if (m_currentLayout == newLayout)
        {
            return;
        }

        vk::ImageMemoryBarrier barrier;
        barrier.oldLayout = m_currentLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = m_image;
        barrier.subresourceRange = subresourceRange;

        vk::PipelineStageFlags sourceStage;
        vk::PipelineStageFlags destinationStage;

        // レイアウト遷移に応じてアクセスマスクとステージを設定
        if (m_currentLayout == vk::ImageLayout::eUndefined &&
            newLayout == vk::ImageLayout::eTransferDstOptimal)
        {
            barrier.srcAccessMask = {};
            barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
            sourceStage = vk::PipelineStageFlagBits::eTopOfPipe;
            destinationStage = vk::PipelineStageFlagBits::eTransfer;
        }
        else if (m_currentLayout == vk::ImageLayout::eTransferDstOptimal &&
                 newLayout == vk::ImageLayout::eShaderReadOnlyOptimal)
        {
            barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
            barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
            sourceStage = vk::PipelineStageFlagBits::eTransfer;
            destinationStage = vk::PipelineStageFlagBits::eFragmentShader;
        }
        else if (m_currentLayout == vk::ImageLayout::eUndefined &&
                 newLayout == vk::ImageLayout::eDepthStencilAttachmentOptimal)
        {
            barrier.srcAccessMask = {};
            barrier.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentRead |
                                    vk::AccessFlagBits::eDepthStencilAttachmentWrite;
            sourceStage = vk::PipelineStageFlagBits::eTopOfPipe;
            destinationStage = vk::PipelineStageFlagBits::eEarlyFragmentTests;
        }
        else if (m_currentLayout == vk::ImageLayout::eUndefined &&
                 newLayout == vk::ImageLayout::eColorAttachmentOptimal)
        {
            barrier.srcAccessMask = {};
            barrier.dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead |
                                    vk::AccessFlagBits::eColorAttachmentWrite;
            sourceStage = vk::PipelineStageFlagBits::eTopOfPipe;
            destinationStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        }
        else if (m_currentLayout == vk::ImageLayout::eUndefined &&
                 newLayout == vk::ImageLayout::eGeneral)
        {
            barrier.srcAccessMask = {};
            barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
            sourceStage = vk::PipelineStageFlagBits::eTopOfPipe;
            destinationStage = vk::PipelineStageFlagBits::eComputeShader;
        }
        else
        {
            // 一般的な遷移（最もパフォーマンスが低いが安全）
            barrier.srcAccessMask = vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite;
            barrier.dstAccessMask = vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite;
            sourceStage = vk::PipelineStageFlagBits::eAllCommands;
            destinationStage = vk::PipelineStageFlagBits::eAllCommands;
        }

        cmdBuffer.pipelineBarrier(
            sourceStage,
            destinationStage,
            {},
            0, nullptr,
            0, nullptr,
            1, &barrier);

        m_currentLayout = newLayout;
    }

    void VulkanTexture::TransitionLayout(
        vk::CommandBuffer cmdBuffer,
        vk::ImageLayout newLayout)
    {
        vk::ImageSubresourceRange subresourceRange;
        if ((m_desc.Usage & ResourceUsage::DepthStencil) != ResourceUsage::None)
        {
            subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
            if (m_desc.TextureFormat == Format::D24_UNORM_S8_UINT)
            {
                subresourceRange.aspectMask |= vk::ImageAspectFlagBits::eStencil;
            }
        }
        else
        {
            subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        }
        subresourceRange.baseMipLevel = 0;
        subresourceRange.levelCount = m_desc.MipLevels;
        subresourceRange.baseArrayLayer = 0;
        subresourceRange.layerCount = m_desc.ArraySize * (m_desc.IsCubemap ? 6 : 1);

        TransitionLayout(cmdBuffer, newLayout, subresourceRange);
    }

} // namespace NorvesLib::RHI::Vulkan
