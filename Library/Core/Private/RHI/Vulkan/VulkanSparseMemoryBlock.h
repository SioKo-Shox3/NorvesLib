#pragma once

#include "RHI/ITexture.h"
#define VULKAN_HPP_NO_CONSTRUCTORS
#include <vulkan/vulkan.hpp>

namespace NorvesLib::RHI::Vulkan
{
    /**
     * @brief sparse テクスチャへ結ぶ物理メモリの塊の Vulkan 実装（DeviceLocal の VkDeviceMemory 1つ）
     *
     * 破棄はデバイスより先に行う（VulkanTexture などの RHI 資源と同じ規則）。
     */
    class VulkanSparseMemoryBlock final : public ISparseMemoryBlock
    {
    public:
        /**
         * @param device 論理デバイス
         * @param memory 確保済みのメモリ（所有権を受け取る）
         * @param sizeBytes メモリの大きさ
         * @param memoryTypeIndex メモリタイプの番号
         * @param owner 作成した VulkanDevice（別のデバイスの塊を結ぼうとする誤りの検出に使う）
         */
        VulkanSparseMemoryBlock(vk::Device device, vk::DeviceMemory memory, uint64_t sizeBytes,
                                uint32_t memoryTypeIndex, const void *owner)
            : m_device(device),
              m_memory(memory),
              m_sizeBytes(sizeBytes),
              m_memoryTypeIndex(memoryTypeIndex),
              m_owner(owner)
        {
        }

        ~VulkanSparseMemoryBlock() override
        {
            if (m_memory)
            {
                m_device.freeMemory(m_memory);
            }
        }

        VulkanSparseMemoryBlock(const VulkanSparseMemoryBlock &) = delete;
        VulkanSparseMemoryBlock &operator=(const VulkanSparseMemoryBlock &) = delete;

        uint64_t GetSizeBytes() const override { return m_sizeBytes; }
        vk::DeviceMemory GetMemory() const { return m_memory; }
        uint32_t GetMemoryTypeIndex() const { return m_memoryTypeIndex; }
        const void *GetOwner() const { return m_owner; }

    private:
        vk::Device m_device;
        vk::DeviceMemory m_memory;
        uint64_t m_sizeBytes = 0;
        uint32_t m_memoryTypeIndex = 0;
        const void *m_owner = nullptr;
    };

} // namespace NorvesLib::RHI::Vulkan
