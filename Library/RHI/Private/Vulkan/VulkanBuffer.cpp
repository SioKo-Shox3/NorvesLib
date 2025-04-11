#include "VulkanBuffer.h"
#include "VulkanDevice.h"
#include <stdexcept>
#include <cstring>

namespace NorvesLib::RHI::Vulkan
{

// コンストラクタ
VulkanBuffer::VulkanBuffer(std::shared_ptr<VulkanDevice> device, const BufferDesc& desc)
    : m_device(device)
    , m_desc(desc)
{
    // Vulkanバッファ使用法フラグを取得
    VkBufferUsageFlags usage = GetVkBufferUsage();
    
    // メモリプロパティを設定
    VkMemoryPropertyFlags memProps = 0;
    
    if (desc.hostVisible) {
        // CPUからアクセス可能なメモリ
        memProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    } else {
        // デバイスローカルメモリ
        memProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    }
    
    // バッファとメモリを作成
    CreateBuffer(usage, memProps);
}

// デストラクタ
VulkanBuffer::~VulkanBuffer()
{
    // マッピングされている場合はアンマップ
    if (m_isMapped) {
        Unmap();
    }
    
    // バッファとメモリを破棄
    if (m_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_device->GetVkDevice(), m_buffer, nullptr);
    }
    
    if (m_deviceMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device->GetVkDevice(), m_deviceMemory, nullptr);
    }
}

// IBufferインターフェース実装: Map
void* VulkanBuffer::Map(uint64_t offset, uint64_t size)
{
    if (!m_desc.hostVisible) {
        throw std::runtime_error("ホスト可視でないバッファはマッピングできません");
    }
    
    if (m_isMapped) {
        throw std::runtime_error("バッファは既にマッピングされています");
    }
    
    // サイズが0の場合は全体をマップ
    if (size == 0) {
        size = m_desc.size - offset;
    }
    
    // オフセットとサイズの範囲チェック
    if (offset + size > m_desc.size) {
        throw std::runtime_error("マッピング範囲がバッファサイズを超えています");
    }
    
    // メモリのマッピング
    void* data = nullptr;
    VkResult result = vkMapMemory(
        m_device->GetVkDevice(), 
        m_deviceMemory, 
        offset, 
        size, 
        0, // フラグは現在未使用
        &data);
    
    if (result != VK_SUCCESS) {
        throw std::runtime_error("バッファメモリのマッピングに失敗しました");
    }
    
    m_isMapped = true;
    m_mappedData = data;
    
    return data;
}

// IBufferインターフェース実装: Unmap
void VulkanBuffer::Unmap()
{
    if (!m_isMapped) {
        return;
    }
    
    vkUnmapMemory(m_device->GetVkDevice(), m_deviceMemory);
    m_isMapped = false;
    m_mappedData = nullptr;
}

// IBufferインターフェース実装: Update
void VulkanBuffer::Update(const void* data, uint64_t size, uint64_t offset)
{
    if (offset + size > m_desc.size) {
        throw std::runtime_error("更新範囲がバッファサイズを超えています");
    }
    
    // ホスト可視の場合は直接マッピングして更新
    if (m_desc.hostVisible) {
        void* mappedData = Map(offset, size);
        std::memcpy(mappedData, data, size);
        Unmap();
    } 
    // ホスト不可視の場合は一時的なステージングバッファを使用
    else {
        // ステージングバッファの作成
        BufferDesc stagingDesc;
        stagingDesc.size = size;
        stagingDesc.usage = ResourceUsage::TransferSrc;
        stagingDesc.hostVisible = true;
        
        auto stagingBuffer = std::make_shared<VulkanBuffer>(m_device, stagingDesc);
        
        // データをステージングバッファに書き込み
        void* mappedStaging = stagingBuffer->Map(0, size);
        std::memcpy(mappedStaging, data, size);
        stagingBuffer->Unmap();
        
        // コマンドバッファの作成と記録
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = m_device->GetCommandPool();
        allocInfo.commandBufferCount = 1;
        
        VkCommandBuffer commandBuffer;
        vkAllocateCommandBuffers(m_device->GetVkDevice(), &allocInfo, &commandBuffer);
        
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        
        vkBeginCommandBuffer(commandBuffer, &beginInfo);
        
        // コピーコマンドの実行
        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = offset;
        copyRegion.size = size;
        
        vkCmdCopyBuffer(
            commandBuffer, 
            stagingBuffer->GetVkBuffer(), 
            m_buffer, 
            1, 
            &copyRegion);
        
        vkEndCommandBuffer(commandBuffer);
        
        // コマンドの提出と完了を待機
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        
        VkFence fence;
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        
        vkCreateFence(m_device->GetVkDevice(), &fenceInfo, nullptr, &fence);
        
        vkQueueSubmit(m_device->GetGraphicsQueue(), 1, &submitInfo, fence);
        vkWaitForFences(m_device->GetVkDevice(), 1, &fence, VK_TRUE, UINT64_MAX);
        
        // リソース解放
        vkDestroyFence(m_device->GetVkDevice(), fence, nullptr);
        vkFreeCommandBuffers(m_device->GetVkDevice(), m_device->GetCommandPool(), 1, &commandBuffer);
        
        // ステージングバッファは自動的に解放される（スマートポインタによる管理）
    }
}

// バッファとメモリの作成
void VulkanBuffer::CreateBuffer(VkBufferUsageFlags usage, VkMemoryPropertyFlags properties)
{
    // バッファ作成情報
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = m_desc.size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE; // 単一キュー専用

    // バッファの作成
    if (vkCreateBuffer(m_device->GetVkDevice(), &bufferInfo, nullptr, &m_buffer) != VK_SUCCESS) {
        throw std::runtime_error("Vulkanバッファの作成に失敗しました");
    }

    // メモリ要件の取得
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_device->GetVkDevice(), m_buffer, &memRequirements);

    // メモリタイプのインデックスを取得
    uint32_t memoryTypeIndex = m_device->FindMemoryType(memRequirements.memoryTypeBits, properties);

    // メモリ割り当て情報
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = memoryTypeIndex;

    // メモリの割り当て
    if (vkAllocateMemory(m_device->GetVkDevice(), &allocInfo, nullptr, &m_deviceMemory) != VK_SUCCESS) {
        throw std::runtime_error("バッファメモリの割り当てに失敗しました");
    }

    // メモリをバッファにバインド
    vkBindBufferMemory(m_device->GetVkDevice(), m_buffer, m_deviceMemory, 0);
}

// Vulkanバッファ使用法フラグに変換
VkBufferUsageFlags VulkanBuffer::GetVkBufferUsage() const
{
    VkBufferUsageFlags usage = 0;
    
    // 使用法フラグの変換
    if ((m_desc.usage & ResourceUsage::VertexBuffer) == ResourceUsage::VertexBuffer) {
        usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    }
    
    if ((m_desc.usage & ResourceUsage::IndexBuffer) == ResourceUsage::IndexBuffer) {
        usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    }
    
    if ((m_desc.usage & ResourceUsage::ConstantBuffer) == ResourceUsage::ConstantBuffer) {
        usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    }
    
    if ((m_desc.usage & ResourceUsage::ShaderRead) == ResourceUsage::ShaderRead) {
        usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    }
    
    if ((m_desc.usage & ResourceUsage::ShaderWrite) == ResourceUsage::ShaderWrite) {
        usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    }
    
    if ((m_desc.usage & ResourceUsage::TransferSrc) == ResourceUsage::TransferSrc) {
        usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    }
    
    if ((m_desc.usage & ResourceUsage::TransferDst) == ResourceUsage::TransferDst) {
        usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }
    
    // デフォルトでトランスファー先として使用可能にする
    if (!m_desc.hostVisible) {
        usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }
    
    return usage;
}

} // namespace NorvesLib::RHI::Vulkan