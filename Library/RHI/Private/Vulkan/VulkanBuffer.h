#pragma once

#include "RHI/Public/IBuffer.h"
#include <vulkan/vulkan.h>
#include <memory>

namespace NorvesLib::RHI::Vulkan
{

class VulkanDevice;

/**
 * @brief Vulkanバッファ実装クラス
 */
class VulkanBuffer : public IBuffer
{
public:
    /**
     * @brief VulkanBufferのコンストラクタ
     * @param device Vulkanデバイス
     * @param desc バッファ記述子
     */
    VulkanBuffer(std::shared_ptr<VulkanDevice> device, const BufferDesc& desc);
    
    /**
     * @brief デストラクタ
     */
    ~VulkanBuffer() override;

    // IBufferインターフェース実装
    uint64_t GetSize() const override { return m_desc.size; }
    void* Map(uint64_t offset = 0, uint64_t size = 0) override;
    void Unmap() override;
    void Update(const void* data, uint64_t size, uint64_t offset = 0) override;
    ResourceUsage GetUsage() const override { return m_desc.usage; }

    // Vulkan固有のメソッド
    VkBuffer GetVkBuffer() const { return m_buffer; }
    VkDeviceMemory GetVkDeviceMemory() const { return m_deviceMemory; }
    bool IsHostVisible() const { return m_desc.hostVisible; }

private:
    std::shared_ptr<VulkanDevice> m_device;
    BufferDesc m_desc;
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VkDeviceMemory m_deviceMemory = VK_NULL_HANDLE;
    
    bool m_isMapped = false;
    void* m_mappedData = nullptr;

    // バッファとメモリの作成
    void CreateBuffer(VkBufferUsageFlags usage, VkMemoryPropertyFlags properties);

    // Vulkanバッファ使用法フラグに変換
    VkBufferUsageFlags GetVkBufferUsage() const;
};

} // namespace NorvesLib::RHI::Vulkan