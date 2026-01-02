#pragma once

#include "RHI/Public/IBuffer.h"
#define VULKAN_HPP_NO_CONSTRUCTORS
#include <vulkan/vulkan.hpp>
#include "Core/Public/Container/Containers.h"

namespace NorvesLib::RHI::Vulkan
{

class VulkanDevice;

/**
 * @brief Vulkanバッファ実装クラス (vulkan.hpp使用)
 */
class VulkanBuffer : public IBuffer
{
public:
    /**
     * @brief VulkanBufferのコンストラクタ
     * @param device Vulkanデバイス
     * @param desc バッファ記述子
     */
    VulkanBuffer(TSharedPtr<VulkanDevice> device, const BufferDesc& desc);
    
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

    // Vulkan固有のメソッド (vulkan.hpp型)
    vk::Buffer GetVkBuffer() const { return m_buffer; }
    vk::DeviceMemory GetVkDeviceMemory() const { return m_deviceMemory; }
    bool IsHostVisible() const { return m_desc.hostVisible; }

private:
    TSharedPtr<VulkanDevice> m_device;
    BufferDesc m_desc;
    vk::Buffer m_buffer;
    vk::DeviceMemory m_deviceMemory;
    
    bool m_bIsMapped = false;
    void* m_mappedData = nullptr;

    // バッファとメモリの作成
    void CreateBuffer(vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties);

    // Vulkanバッファ使用法フラグに変換
    vk::BufferUsageFlags GetVkBufferUsage() const;
};

} // namespace NorvesLib::RHI::Vulkan